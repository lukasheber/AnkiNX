#!/usr/bin/env python3
"""Check the vendored libc's Switch (libnx) definitions against the real C ABI.

Pipeline:
  1. Read rustdoc JSON of libc built for the Switch target: every constant,
     type alias, struct/union (and its public fields) and extern fn that libc
     exposes for this cfg.
  2. Generate a C probe that evaluates each item with devkitA64's gcc against
     newlib + libnx headers. Items gcc rejects are dropped iteratively (they
     don't exist in the C headers), with `struct X` -> `X` fallback for names.
  3. Compare constants directly (rustdoc gives literal values); emit Rust
     compile-time asserts for sizes, alignments, signedness and field offsets.
  4. Check every extern fn against the symbols defined by the link libraries.

Usage: abi_check.py <libc.json> <outdir>
Writes: <outdir>/report.txt, <outdir>/rust_checks.rs, <outdir>/c_values.json
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

DKP = os.environ.get("DEVKITPRO", "/opt/devkitpro")
CC = f"{DKP}/devkitA64/bin/aarch64-none-elf-gcc"
NM = f"{DKP}/devkitA64/bin/aarch64-none-elf-nm"
CFLAGS = [
    "-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIE",
    "-D__SWITCH__", f"-I{DKP}/libnx/include", "-std=gnu11", "-O0", "-w", "-S",
]
HEADERS = """
sys/types.h sys/stat.h sys/time.h sys/select.h sys/socket.h sys/ioctl.h sys/poll.h
poll.h sys/statvfs.h sys/resource.h sys/wait.h sys/times.h sys/utime.h sys/unistd.h sys/uio.h
sys/un.h sys/mman.h netinet/in.h netinet/tcp.h arpa/inet.h netdb.h net/if.h errno.h
fcntl.h unistd.h dirent.h pthread.h sched.h signal.h time.h stdio.h stdlib.h
string.h strings.h limits.h locale.h termios.h pwd.h grp.h semaphore.h stddef.h
stdint.h utime.h ifaddrs.h dlfcn.h langinfo.h
""".split()
LINK_LIBS = [
    f"{DKP}/libnx/lib/libnx.a",
    f"{DKP}/devkitA64/aarch64-none-elf/lib/pic/libc.a",
    f"{DKP}/devkitA64/aarch64-none-elf/lib/pic/libm.a",
    f"{DKP}/devkitA64/aarch64-none-elf/lib/pic/libsysbase.a",
    f"{DKP}/devkitA64/lib/gcc/aarch64-none-elf/15.2.0/pic/libgcc.a",
]

INT_PRIMS = {"i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32", "u64",
             "u128", "usize"}
# Opaque in libc by design (only ever used behind pointers): layout not comparable.
OPAQUE = {"DIR", "FILE", "fpos_t"}
# Integer aliases that live outside libc's own index (core::ffi re-exports).
EXTERNAL_INT_ALIASES = {"c_char", "c_schar", "c_uchar", "c_short", "c_ushort", "c_int",
                        "c_uint", "c_long", "c_ulong", "c_longlong", "c_ulonglong",
                        "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t"}


class Doc:
    def __init__(self, path: Path):
        self.j = json.loads(path.read_text())
        self.index = self.j["index"]
        self.root_crate = self.index[str(self.j["root"])]["crate_id"]

    def own_items(self):
        for item in self.index.values():
            if item.get("crate_id") == self.root_crate and item.get("name"):
                yield item

    def item(self, id_):
        return self.index.get(str(id_))

    def alias_target(self, ty) -> dict | None:
        """Follow type aliases until a primitive/non-alias type."""
        seen = 0
        while isinstance(ty, dict) and "resolved_path" in ty and seen < 20:
            rp = ty["resolved_path"]
            it = self.item(rp["id"])
            if not it or "type_alias" not in it["inner"]:
                last = rp.get("path", "").split("::")[-1]
                if last in EXTERNAL_INT_ALIASES:
                    return {"primitive": "i64"}  # only integer-ness matters here
                return ty
            ty = it["inner"]["type_alias"]["type"]
            seen += 1
        return ty

    def is_int(self, ty) -> bool:
        t = self.alias_target(ty)
        return isinstance(t, dict) and t.get("primitive") in INT_PRIMS

    def is_pointer(self, ty) -> bool:
        t = self.alias_target(ty)
        return isinstance(t, dict) and ("raw_pointer" in t or "function_pointer" in t)


def c_ident(s: str) -> str:
    return re.sub(r"\W", "_", s)


def collect(doc: Doc):
    consts, aliases, records, fns = {}, {}, {}, set()
    for it in doc.own_items():
        name, inner = it["name"], it["inner"]
        if "constant" in inner:
            c = inner["constant"]
            if doc.is_int(c["type"]):
                consts[name] = c["const"].get("value") or c["const"].get("expr")
        elif "type_alias" in inner:
            aliases[name] = inner["type_alias"]["type"]
        elif "struct" in inner or "union" in inner:
            kind = "struct" if "struct" in inner else "union"
            body = inner[kind]
            if kind == "struct":
                k = body["kind"]
                field_ids = k.get("plain", {}).get("fields", []) if isinstance(k, dict) else []
            else:
                field_ids = body.get("fields", [])
            fields = []
            for fid in field_ids:
                f = doc.item(fid)
                if f and f.get("name") and f.get("visibility") == "public":
                    fields.append(f["name"])
            records[name] = (kind, fields)
        elif "function" in inner and it.get("visibility") == "public":
            if not inner["function"].get("has_body", True):
                fns.add(_link_name(it))
    return consts, aliases, records, fns


def _link_name(it) -> str:
    for a in it.get("attrs", []):
        s = a if isinstance(a, str) else json.dumps(a)
        m = re.search(r'link_name\s*=\s*\\?"([^"\\]+)', s)
        if m:
            return m.group(1)
    return it["name"]


class Probe:
    """C probe with per-line items and iterative removal of failing lines."""

    def __init__(self, workdir: Path):
        self.work = workdir
        self.items: list[dict] = []  # {key, variants:[str templates], vi}
        self.headers = [h for h in HEADERS if self._header_ok(h)]

    def _header_ok(self, h: str) -> bool:
        # Some newlib headers exist but include files devkitA64 doesn't ship.
        src = self.work / "hdr.c"
        src.write_text(f"#include <{h}>\n")
        r = subprocess.run([CC, *CFLAGS, str(src), "-o", os.devnull], capture_output=True)
        return r.returncode == 0

    def add(self, key: str, variants: list[str]):
        self.items.append({"key": key, "variants": variants, "vi": 0})

    def render(self) -> tuple[str, dict[int, int]]:
        lines = ["#include <stddef.h>"]
        lines += [f"#include <{h}>" for h in self.headers]
        text = "\n".join(lines).split("\n")
        line_map = {}
        for idx, it in enumerate(self.items):
            if it["vi"] >= len(it["variants"]):
                continue
            text.append(it["variants"][it["vi"]])
            line_map[len(text)] = idx
        return "\n".join(text) + "\n", line_map

    def run(self) -> str:
        src = self.work / "probe.c"
        asm = self.work / "probe.s"
        for _ in range(40):
            code, line_map = self.render()
            src.write_text(code)
            r = subprocess.run([CC, *CFLAGS, str(src), "-o", str(asm)],
                               capture_output=True, text=True)
            if r.returncode == 0:
                return asm.read_text()
            bad = {int(m.group(1)) for m in re.finditer(r"probe\.c:(\d+):\d+: error", r.stderr)}
            hit = {line_map[b] for b in bad if b in line_map}
            if not hit:
                raise SystemExit("probe failed outside item lines:\n" + r.stderr[-4000:])
            for i in hit:
                self.items[i]["vi"] += 1
        raise SystemExit("probe did not converge")


def parse_asm(asm: str) -> dict[str, int]:
    vals, cur = {}, None
    for line in asm.splitlines():
        m = re.match(r"^(\w+):$", line.strip())
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"^\s*\.(xword|dword|quad|8byte)\s+(-?\d+)", line)
        if m and cur:
            vals[cur] = int(m.group(2))
            cur = None
        elif cur and re.match(r"^\s*\.zero\s+8\b", line):  # gcc emits 0 as `.zero 8`
            vals[cur] = 0
            cur = None
    return vals


def to_int(v: str | None) -> int | None:
    if v is None:
        return None
    s = v.replace("_", "").strip()
    s = re.sub(r"(i|u)(8|16|32|64|128|size)$", "", s)
    try:
        return int(s, 0) if not s.startswith("0o") else int(s[2:], 8)
    except ValueError:
        return None


def main():
    json_path, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    doc = Doc(json_path)
    consts, aliases, records, fns = collect(doc)

    probe = Probe(out)
    V = "__attribute__((used)) const long long"
    for n in sorted(consts):
        probe.add(f"k_{n}", [f"{V} k_{c_ident(n)} = (long long)({n});"])
    for n in sorted(aliases):
        sz = f"{V} z_{c_ident(n)} = sizeof({n});"
        if doc.is_int(aliases[n]):
            probe.add(f"z_{n}", [f"{sz} {V} s_{c_ident(n)} = (({n})-1) < 0;"])
        else:
            probe.add(f"z_{n}", [sz])
    for n, (kind, fields) in sorted(records.items()):
        probe.add(f"r_{n}", [
            f"{V} z_{c_ident(n)} = sizeof({cn}); {V} a_{c_ident(n)} = _Alignof({cn});"
            for cn in (f"{kind} {n}", n)
        ])
        for f in fields:
            probe.add(f"o_{n}.{f}", [
                f"{V} o_{c_ident(n)}__{f} = offsetof({cn}, {f}); "
                f"{V} fz_{c_ident(n)}__{f} = sizeof((({cn}*)0)->{f});"
                for cn in (f"{kind} {n}", n)
            ])
    cvals = parse_asm(probe.run())
    (out / "c_values.json").write_text(json.dumps(cvals, indent=1, sort_keys=True))

    report, rust = [], ["// Generated by abi_check.py: compile-time ABI checks vs devkitA64/libnx.",
                        "#![allow(non_upper_case_globals, deprecated)]", "use core::mem::*;", ""]

    # Constants: compare rustdoc literal values directly, else assert in Rust.
    missing_c = []
    for n, rv in sorted(consts.items()):
        cv = cvals.get(f"k_{c_ident(n)}")
        if cv is None:
            missing_c.append(n)
            continue
        r = to_int(rv)
        if r is None:
            rust.append(f'const _: () = assert!(libc::{n} as i128 == {cv}i128, "const {n} != C {cv}");')
        # Same bit pattern seen through a signed C `int` vs unsigned Rust type is fine.
        elif r != cv and (r & 0xFFFFFFFF) != (cv & 0xFFFFFFFF):
            report.append(f"CONST   {n}: rust={rv} C={cv}")

    for n, ty in sorted(aliases.items()):
        z = cvals.get(f"z_{c_ident(n)}")
        if z is None:
            missing_c.append(n)
            continue
        rust.append(f'const _: () = assert!(size_of::<libc::{n}>() == {z}, "size {n} != C {z}");')
        s = cvals.get(f"s_{c_ident(n)}")
        if s is not None:
            rust.append(f'const _: () = assert!(((0 as libc::{n}).wrapping_sub(1) < (0 as libc::{n})) == {"true" if s else "false"}, "sign {n} != C signed={s}");')

    for n, (kind, fields) in sorted(records.items()):
        if n in OPAQUE:
            continue
        z = cvals.get(f"z_{c_ident(n)}")
        if z is None:
            missing_c.append(f"{kind} {n}")
            continue
        a = cvals.get(f"a_{c_ident(n)}")
        rust.append(f'const _: () = assert!(size_of::<libc::{n}>() == {z}, "size {kind} {n} != C {z}");')
        rust.append(f'const _: () = assert!(align_of::<libc::{n}>() == {a}, "align {kind} {n} != C {a}");')
        for f in fields:
            o = cvals.get(f"o_{c_ident(n)}__{f}")
            if o is None:
                report.append(f"FIELD   {n}.{f}: not in C struct")
                continue
            fz = cvals[f"fz_{c_ident(n)}__{f}"]
            rust.append(f'const _: () = assert!(offset_of!(libc::{n}, {f}) == {o}, "offset {n}.{f} != C {o}");')
            rust.append(f'const _: () = {{ let u = core::mem::MaybeUninit::<libc::{n}>::uninit(); '
                        f'let p = unsafe {{ core::ptr::addr_of!((*u.as_ptr()).{f}) }}; '
                        f'assert!(size_of_val_raw_sz(p) == {fz}, "fieldsize {n}.{f} != C {fz}"); }};')

    rust.append("const fn size_of_val_raw_sz<T>(_: *const T) -> usize { size_of::<T>() }")

    # Extern functions vs symbols defined by the link libraries.
    defined = set()
    for lib in LINK_LIBS:
        if Path(lib).exists():
            r = subprocess.run([NM, "-g", "--defined-only", lib], capture_output=True, text=True)
            defined |= {l.split()[-1] for l in r.stdout.splitlines() if len(l.split()) >= 3}
    undefined_fns = sorted(f for f in fns if f not in defined)

    (out / "rust_checks.rs").write_text("\n".join(rust) + "\n")
    with open(out / "report.txt", "w") as fh:
        fh.write(f"consts={len(consts)} aliases={len(aliases)} records={len(records)} fns={len(fns)}\n\n")
        fh.write("== Value mismatches (rust literal vs C) ==\n" + "\n".join(report) + "\n\n")
        fh.write("== Declared in Rust but absent from C headers ==\n" + " ".join(sorted(missing_c)) + "\n\n")
        fh.write("== extern fns with no definition in libnx/newlib ==\n" + " ".join(undefined_fns) + "\n")
    print((out / "report.txt").read_text()[:200])


if __name__ == "__main__":
    main()
