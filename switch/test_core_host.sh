#!/usr/bin/env bash
# Build anki_switch_core for the Linux host and run its tests three ways:
#   1. plain Linux;
#   2. under the Horizon filesystem emulator with the Switch shim's fixes
#      (shim/horizon_emu.c + horizon_fixes.h): must pass;
#   3. under the emulator without fixes (HORIZON_EMU_RAW=1): must FAIL, which
#      shows the emulator reproduces the errors seen on hardware.
set -euo pipefail
source "$(dirname "$0")/../tools/env.sh"
bash "$ANKISWITCH_SRC/tools/sync.sh"
CRATE="$ANKISWITCH_WORK/switch/anki_switch_core"
LOCK="$ANKISWITCH_SRC/switch/anki_switch_core/Cargo.lock"
[[ -f "$LOCK" ]] && cp "$LOCK" "$CRATE/Cargo.lock"
export PROTOC=/usr/bin/protoc
export CARGO_TARGET_DIR="$ANKISWITCH_WORK/target-host"
export DESCRIPTORS_BIN="$CARGO_TARGET_DIR/anki_descriptors.bin"
cd "$CRATE"

EMU="$CARGO_TARGET_DIR/horizon_emu.so"
gcc -shared -fPIC -O2 -Wall -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -o "$EMU" shim/horizon_emu.c -ldl

echo "== 1. Linux"
cargo +"$RUST_NIGHTLY" test --release -j "${JOBS:-2}" "$@"
cp "$CRATE/Cargo.lock" "$LOCK"

# Only the test binaries run under the emulator, not cargo itself.
RUNNER_VAR=CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_RUNNER
echo "== 2. Horizon emulation + fixes (must pass)"
env "$RUNNER_VAR=env LD_PRELOAD=$EMU" \
    cargo +"$RUST_NIGHTLY" test --release -j "${JOBS:-2}" --test api "$@"

echo "== 3. Horizon emulation without fixes (must fail)"
if env "$RUNNER_VAR=timeout 120 env LD_PRELOAD=$EMU HORIZON_EMU_RAW=1" \
    cargo +"$RUST_NIGHTLY" test --release -j "${JOBS:-2}" --test api "$@" > "$CARGO_TARGET_DIR/raw.log" 2>&1; then
    echo "error: the emulator no longer reproduces the Horizon failures" >&2
    exit 1
fi
grep -oE 'extended_code: [0-9]+|disk I/O error|panicked at [^ ]+' "$CARGO_TARGET_DIR/raw.log" | sort -u
echo "ok: failed as expected without the fixes"
