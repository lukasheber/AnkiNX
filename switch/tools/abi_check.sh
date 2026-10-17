#!/usr/bin/env bash
# Verify switch/vendor/libc against devkitA64 newlib + libnx headers/libraries.
# Output: $ANKISWITCH_WORK/abi/report.txt (plus Rust-side assert failures appended).
set -euo pipefail
source "$(dirname "$0")/../../tools/env.sh"
bash "$ANKISWITCH_SRC/tools/sync.sh"

T="$ANKISWITCH_WORK/switch/targets/aarch64-nintendo-switch-newlib.json"
OUT="$ANKISWITCH_WORK/abi"
TGT_DIR="$ANKISWITCH_WORK/target-abi"
mkdir -p "$OUT"

echo "==> rustdoc JSON for libc"
(cd "$ANKISWITCH_WORK/switch/vendor/libc" &&
    cargo +"$RUST_NIGHTLY" rustdoc --lib -q --target "$T" --target-dir "$TGT_DIR" \
        -Zbuild-std=core -Zjson-target-spec -- -Zunstable-options --output-format json)
JSON="$(find "$TGT_DIR" -name libc.json -path '*doc*' | head -1)"

echo "==> C probe + compare"
python3 "$ANKISWITCH_WORK/switch/tools/abi_check.py" "$JSON" "$OUT"

echo "==> Rust-side layout asserts"
CRATE="$OUT/check_crate"
mkdir -p "$CRATE/src"
cat > "$CRATE/Cargo.toml" <<EOF
[package]
name = "libc_abi_check"
version = "0.0.0"
edition = "2021"
[lib]
crate-type = ["rlib"]
[dependencies]
libc = { path = "$ANKISWITCH_WORK/switch/vendor/libc", default-features = false }
EOF
{ echo '#![no_std]'; cat "$OUT/rust_checks.rs"; } > "$CRATE/src/lib.rs"
(cd "$CRATE" && cargo +"$RUST_NIGHTLY" build -q --target "$T" --target-dir "$TGT_DIR" \
    -Zbuild-std=core -Zjson-target-spec 2>&1 || true) \
    | grep -oE '(evaluation panicked|assertion failed)[^`]*: [a-z]+ [^"]+' | sed 's/^[^:]*: /RUST    /' \
    | sort -u > "$OUT/rust_failures.txt" || true
{ echo; echo "== Rust layout/value assert failures =="; cat "$OUT/rust_failures.txt"; } >> "$OUT/report.txt"
echo "Report: $OUT/report.txt ($(wc -l < "$OUT/rust_failures.txt") rust-side failures)"
