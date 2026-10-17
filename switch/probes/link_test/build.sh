#!/usr/bin/env bash
# Link main.c against switch/dist/libanki_switch_core.a + libnx into an .nro.
set -euo pipefail
source "$(dirname "$0")/../../../tools/env.sh"
HERE="$ANKISWITCH_SRC/switch/probes/link_test"
DIST="$ANKISWITCH_SRC/switch/dist"
OUT="$ANKISWITCH_WORK/link_test"
mkdir -p "$OUT"

ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
aarch64-none-elf-gcc $ARCH -O2 -Wall -D__SWITCH__ -I"$DEVKITPRO/libnx/include" -I"$DIST" \
    -c "$HERE/main.c" -o "$OUT/main.o"
aarch64-none-elf-gcc $ARCH -specs="$DEVKITPRO/libnx/switch.specs" -Wl,-Map,"$OUT/anki_link_test.map" \
    "$OUT/main.o" -L"$DIST" -lanki_switch_core -L"$DEVKITPRO/libnx/lib" -lnx -lm \
    -o "$OUT/anki_link_test.elf"
elf2nro "$OUT/anki_link_test.elf" "$OUT/anki_link_test.nro"
ls -l "$OUT"/anki_link_test.{elf,nro}
