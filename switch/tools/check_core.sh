#!/usr/bin/env bash
# Inspect switch/dist/libanki_switch_core.a: architecture, exported C API, and
# external symbols that the Switch link libraries do not provide (these must be
# supplied by the port's C shim, or the final link will fail).
set -euo pipefail
source "$(dirname "$0")/../../tools/env.sh"
LIB="${1:-$ANKISWITCH_SRC/switch/dist/libanki_switch_core.a}"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

echo "== archive"
aarch64-none-elf-ar t "$LIB" > "$TMP/members"
echo "  members: $(wc -l < "$TMP/members")"
first=$(grep -m1 '\.o$' "$TMP/members")
(cd "$TMP" && aarch64-none-elf-ar x "$LIB" "$first" && aarch64-none-elf-readelf -h "$first" | grep -E 'Class|Machine|Type:')

echo "== exported C API"
aarch64-none-elf-nm -g --defined-only "$LIB" 2>/dev/null | awk '$3 ~ /^anki(nx)?_/ {print "  " $2, $3}' | sort -u

echo "== external symbols"
aarch64-none-elf-nm -g --defined-only "$LIB" 2>/dev/null | awk 'NF==3 {print $3}' | sort -u > "$TMP/defined"
# Strong references only ("U"); weak ones ("w") may stay unresolved.
aarch64-none-elf-nm -g --undefined-only "$LIB" 2>/dev/null | awk '$1 == "U" {print $2}' | sort -u > "$TMP/undef"
comm -23 "$TMP/undef" "$TMP/defined" > "$TMP/external"
GCCLIB=$(dirname "$(aarch64-none-elf-gcc -march=armv8-a -fPIE -print-libgcc-file-name)")
for lib in "$DEVKITPRO/libnx/lib/libnx.a" \
           "$DEVKITA64/aarch64-none-elf/lib/pic/libc.a" \
           "$DEVKITA64/aarch64-none-elf/lib/pic/libm.a" \
           "$DEVKITA64/aarch64-none-elf/lib/pic/libsysbase.a" \
           "$GCCLIB/libgcc.a"; do
    [[ -f "$lib" ]] && aarch64-none-elf-nm -g --defined-only "$lib" 2>/dev/null | awk 'NF==3 {print $3}'
done | sort -u > "$TMP/provided"
comm -23 "$TMP/external" "$TMP/provided" > "$TMP/missing"
echo "  needed from outside the archive: $(wc -l < "$TMP/external")"
echo "  provided by libnx/newlib/libgcc: $(comm -12 "$TMP/external" "$TMP/provided" | wc -l)"
echo "  MISSING (need shim):            $(wc -l < "$TMP/missing")"
sed 's/^/    /' "$TMP/missing"
