# Anki for Nintendo Switch: native port

Phase 2 output: Anki's Rust backend (`rslib`, Anki 26.09.3) cross-compiled for
Horizon OS as a C static library, `dist/libanki_switch_core.a`, with
`dist/anki_switch_core.h`.

## Build

Inside WSL Ubuntu (Phase 1 environment, `tools/provision_phase1.sh`):

```sh
bash /mnt/c/ankiswitch/tools/fetch_ftl.sh          # once: Anki translation submodules
bash /mnt/c/ankiswitch/switch/build_core.sh        # -> switch/dist/
bash /mnt/c/ankiswitch/switch/tools/check_core.sh  # verify: 0 MISSING symbols
bash /mnt/c/ankiswitch/switch/probes/link_test/build.sh   # link a test .nro
```

Sources live on `C:\ankiswitch`; `tools/sync.sh` mirrors them to `~/ankiswitch`
(same layout), where cargo runs, because building on `/mnt/c` is very slow.

Link the frontend with: `-lanki_switch_core -lnx -lm` (see
`dist/native-static-libs.txt`), using `-specs=switch.specs`.

## Layout

| Path | What |
| --- | --- |
| `anki_switch_core/` | Static library crate: C ABI over `anki::backend` (`src/lib.rs`, `include/`), POSIX shim (`shim/nx_posix.c`) |
| `targets/aarch64-nintendo-switch-newlib.json` | Target spec used instead of the built-in one (see below) |
| `vendor.lst`, `patches/`, `vendor/` | Upstream crates carried with Switch patches. `vendor/` is generated from crates.io + `patches/` |
| `tools/vendor.sh` | `apply` (regenerate `vendor/`), `save` (refresh patches after editing `vendor/`), `port` (new upstream version) |
| `tools/abi_check.sh` | Verifies the libc port against devkitA64/libnx headers (values, sizes, field offsets) |
| `tools/check_core.sh` | Lists symbols the archive needs that libnx/newlib/libgcc don't provide |
| `tools/c_has.sh` | Asks the Switch C headers whether a macro/type/function exists |
| `probes/` | Minimal crates/programs used to validate each layer |

## How the port works, and where it departs from the guide

**Target.** The built-in `aarch64-nintendo-switch-freestanding` target is
`no_std` by design (no `env`, not `unix`). The guide's patch to std's
`build.rs` (`tools/patch_rust_std.sh`) is applied as requested, but on that
target it only switches std into its Horizon (3DS) code, which then fails with
39 errors because libc has no newlib types for it. So the build uses
`targets/aarch64-nintendo-switch-newlib.json`: the same CPU, layout and
`os = "horizon"`, plus `env = "newlib"` and `target-family = ["unix"]`, which
is what devkitA64 actually provides. std is built from source (`-Zbuild-std`) on
the pinned nightly (`nightly-2026-09-15`). With `env = "newlib"`, std's
existing Nintendo+newlib clause already applies, so the guide's patch is
redundant for this target but harmless.

`tools/patch_rust_std.sh` also points std's own `libc` dependency (via
`rust-src/library/Cargo.toml` and its lockfile) at the vendored fork, because
`-Zbuild-std` ignores the application's `[patch]` table. `--revert` undoes it.

**Patched crates** (all keyed on `target_os = "horizon"` + aarch64, `cfg(libnx)`):

- **libc**: real Switch ABI in `src/unix/newlib/libnx/` plus `libnx` branches in
  the shared newlib module. libc's Horizon support described the 3DS (libctru);
  the Switch differs in `stat`, pthread types, BSD sockaddrs, `pollfd`, and 30+
  socket/netdb constants. 600 constants, 65 types and 55 structs are verified by
  `tools/abi_check.sh`.
- **mio**: a loopback-UDP **waker**. On Horizon, mio used a flag-only waker, so
  a thread blocked in `poll()` could not be woken, which stalls tokio's
  multi-thread runtime. libnx has no `pipe`/`eventfd`. Also: sockets are put in
  non-blocking mode (mio skipped this on Horizon), `sin_len`, no `FD_CLOEXEC`.
- **tokio**: no signal driver on Horizon (it needs `sigaction` and an AF_UNIX
  socketpair, so runtime creation would panic). `signal::ctrl_c()` returns
  `Unsupported`; Anki only uses it in the standalone sync server.
- **getrandom 0.2/0.3/0.4**: use the newlib `__errno` + `libc::getrandom` path
  that was limited to the 3DS.
- **ring**: use its ELF AArch64 assembly (`linux64`) on Horizon.

**C shim** (`anki_switch_core/shim/nx_posix.c`, compiled into the archive):
`getrandom` (libnx `randomGet`, ChaCha20 seeded from kernel entropy),
`sysconf` (cores from the process core mask, page size), `pread`/`pwrite`
(lseek-emulated, **not atomic**), `*at()` calls for `AT_FDCWD`/absolute paths,
and `ENOSYS` for ownership, `chroot`, FIFOs, `pipe`, `fstatvfs`. It also
**replaces newlib's `stat`, `lstat`, `read`, `write` and `rename`** with POSIX
behaviour on top of Horizon's filesystem (`shim/horizon_fixes.h`):
- `stat`/`lstat`: libnx opens the file to read its size, and Horizon refuses to
  open a file that is open for writing (on device: `SQLITE_IOERR_FSTAT`, 1802).
  Falls back to the entry type (size 0, inode 0).
- `read`: Horizon fails reads at or past the end of file instead of returning 0
  (on device: `SQLITE_IOERR_CORRUPTFS`, 8458).
- `write`: past the end of file, the file is extended first.
- `rename`: Horizon won't rename onto an existing file; Anki's full download
  needs that. The old file is deleted first, so it's not atomic.

**Testing these on a PC:** `shim/horizon_emu.c` is an `LD_PRELOAD` library that
makes Linux behave like Horizon for these calls and layers the same fixes on top.
`switch/test_core_host.sh` runs the API tests plainly, under emulation with the
fixes (must pass), and without them (must fail; it reproduces 8458).

For the same reason, backups are made by `Collection::export_colpkg` after the
collection is closed, not by `maybe_backup()` (which reads the open database).

**SQLite** (bundled, via `LIBSQLITE3_FLAGS` in `build_core.sh`):
`SQLITE_OMIT_WAL`, `SQLITE_MAX_MMAP_SIZE=0` (Horizon has no `mmap`), so Anki's
`journal_mode=wal` falls back to a rollback journal, which is the guide's
recommendation for the SD card anyway. `SQLITE_TEMP_STORE=3` (no `/tmp`), no
loadable extensions. The default VFS is `ankinx` (`shim/sqlite_vfs.c`): the
lock-free `unix-none` VFS (libnx has no POSIX file locks and only this process
opens the collection), plus one fix-up. A build without WAL rejects files whose
header says WAL (bytes 18 and 19 = 2) as "file is not a database" (error 26).
That is how AnkiWeb sends the collection on a full download, and how Anki
desktop leaves collection.anki2. So reads of the main database report those
bytes as 1 (rollback journal), which is what `journal_mode=delete` would write.
This is safe because a WAL file left beside such a collection is never read:
Anki checkpoints on close, and downloads come without one.

**Files open for writing can't be opened again on Horizon** ("target
locked"), not even for reading or deleting. No shim can fix that, so code must
close a file before opening it again. Anki's `Collection::full_download` breaks
this rule: it writes the download through a second handle to a temp file it
keeps open. So `api.rs` has its own `full_download` that opens and closes the
file at each step. `horizon_emu.c` applies this rule in both of its modes.

**Threads and memory.** libnx gives threads 128 KiB of stack by default, and
borealis' task thread (`brls::async`) uses that default for the API calls it
makes. `examples/sync_probe.rs` makes its calls from a thread that size, and
login, sync and full download fit. Tokio workers and the media sync thread use
Rust's 2 MiB default. Memory is tight in applet mode (launched from the Album),
and a failed allocation aborts the app, so avoid fixed reservations: a build
that ran every call on its own 8 MiB-stack thread crashed at startup on
hardware.

**Thread-locals.** rustc can't use native TLS on this target, because devkitA64
reads the thread pointer its own way (`has-thread-local: false`). So every Rust
`thread_local!` is a pthread key. libnx has about 27 TLS slots, shared with C
code, and Anki plus tokio and the other crates need more than that. On
hardware, std aborted with "out of TLS keys" when loading a real collection and
when syncing (crash reports showed `abort` under `sync`). The libc fork maps
Rust's `pthread_key_*` to `nxshim_pthread_key_*` in `shim/nx_posix.c`: one
libnx slot holding a growable per-thread table. C code keeps libnx's own keys.

**Rebuilding std after libc fork changes.** `-Zbuild-std` treats std's
dependencies like registry crates and doesn't notice edits to the fork, so std
kept a libc compiled hours earlier. `build_core.sh` therefore hashes the fork
and, when the hash changes, deletes libc's build units, which forces libc, std
and everything above them to recompile. Check with `aarch64-none-elf-nm`: no
Rust object may reference `pthread_getspecific` directly.

**Panics** abort on the Switch (`panic=abort`) and only show "The software was
closed because an error occurred". Once `init` has run, the panic message is
first appended to `crash.log` in the data folder (`/switch/ankinx/crash.log`).

**Live sync check:** `examples/sync_probe.rs` runs login, sync and full
download against a real AnkiWeb account, from a thread with a 128 KiB stack as
on the Switch. Credentials come from `ANKIWEB_USER`/`ANKIWEB_PASS`; run it with
`LD_PRELOAD=horizon_emu.so` to emulate the Switch's filesystem.

**TLS.** rustls with the bundled Mozilla root store (`webpki-roots`) instead of
the guide's curl/mbedtls: the backend's HTTP stack (reqwest) is pure Rust, and
Horizon exposes no OS certificate store to homebrew. switch-curl and
switch-mbedtls remain installed for the frontend if it needs them.

**Translations.** `tools/fetch_ftl.sh` fetches `ftl/core-repo` and `ftl/qt-repo`
at the commits pinned by the Anki 26.09.3 tag; source archives ship them empty.

## JSON API for the frontend (Phase 3)

`ankinx_call(cmd, json)` / `ankinx_free` (`anki_switch_core/src/api.rs`) is what
the AnkiNX app uses: the library owns the `Collection` and mirrors Anki's own
backend (reviewer queue, `backend/sync.rs`). Commands: `init`, `status`, `open`,
`close` (backs up, then closes), `deck_tree`, `set_collapsed`, `select_deck`,
`next_card` (rendered HTML/CSS, counts, interval labels, sound tags), `answer`,
`undo`, `login`, `logout`, `sync`, `full_sync`, `media_sync_status`, `shutdown`.
`bash switch/test_core_host.sh` runs `tests/api.rs` on the Linux host (study,
answer, undo, collapse, errors, backup on close). The older `anki_backend_*`
protobuf functions are still exported.

## Status and known risks

Verified: std and the full dependency graph compile; the libc ABI matches the
headers; the archive has 0 unresolved symbols against libnx/newlib/libgcc;
`probes/link_test` links with libnx into an `.nro` (`dist/anki_link_test.nro`).

**Nothing has run on hardware or an emulator yet.** To try the link test:
copy `dist/anki_link_test.nro` to `sdmc:/switch/` and launch it through title
override (hold R on a game) from hbmenu. Open points for Phases 3–5:

- The frontend must initialise sockets **before** any backend call that starts
  the async runtime (the mio waker binds a loopback UDP socket). AnkiNX gets this
  from borealis' `switch_wrapper.c`; the link test was confirmed on hardware.
- Unverified on device: libnx `fcntl(F_SETFL, O_NONBLOCK)` on sockets, `poll()`
  wake-ups through the UDP waker, and thread limits under tokio + rayon.
- A collection left in WAL mode (header version 2) can't be opened without WAL
  support. Anki normally downgrades to DELETE mode on close; collections from
  AnkiWeb downloads are fine.
- `pread`/`pwrite` are not atomic (no current caller depends on that).
