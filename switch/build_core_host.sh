#!/usr/bin/env bash
# Build anki_switch_core for the Linux host (desktop preview of the app).
# Output: switch/dist-host/libanki_switch_core.a
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
cargo +"$RUST_NIGHTLY" build --release -j "${JOBS:-2}" --lib --examples
DIST="$ANKISWITCH_SRC/switch/dist-host"
mkdir -p "$DIST"
cp "$CARGO_TARGET_DIR/release/libanki_switch_core.a" "$CRATE/include/anki_switch_core.h" "$DIST/"
cp "$CARGO_TARGET_DIR/release/examples/make_sample" "$DIST/"
ls -l "$DIST"
