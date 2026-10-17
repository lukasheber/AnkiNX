#!/usr/bin/env bash
# Phase 2: cross-compile Anki's Rust backend into a static library for the
# Nintendo Switch (aarch64, Horizon OS, devkitA64 newlib + libnx).
#
# Usage: bash switch/build_core.sh [--debug]
# Output: switch/dist/libanki_switch_core.a + include/anki_switch_core.h
set -euo pipefail
source "$(dirname "$0")/../tools/env.sh"

PROFILE=release; [[ "${1:-}" == "--debug" ]] && PROFILE=dev
TARGET_NAME=aarch64-nintendo-switch-newlib
TARGET="$ANKISWITCH_WORK/switch/targets/$TARGET_NAME.json"
CRATE="$ANKISWITCH_WORK/switch/anki_switch_core"
TARGET_DIR="$ANKISWITCH_WORK/target"

bash "$ANKISWITCH_SRC/tools/sync.sh"
bash "$ANKISWITCH_SRC/tools/patch_rust_std.sh" >/dev/null

# Start from Anki's own lockfile so dependency versions match upstream, except
# for the crates we carry Switch patches for (switch/vendor), which must resolve
# to the vendored versions or cargo silently ignores the [patch] entries.
LOCK="$ANKISWITCH_SRC/switch/anki_switch_core/Cargo.lock"
if [[ -f "$LOCK" ]]; then
    cp "$LOCK" "$CRATE/Cargo.lock"
else
    cp "$ANKISWITCH_WORK/source_code/anki-26.09.3/Cargo.lock" "$CRATE/Cargo.lock"
fi
# Move every crates.io copy of a vendored crate to the vendored version, so the
# [patch] applies (cargo otherwise parks it under [[patch.unused]]). Idempotent.
registry_versions() { # name -> versions of that crate sourced from crates.io
    awk -v n="$1" 'BEGIN { RS = ""; FS = "\n" }
        $0 ~ "\nname = \"" n "\"\n" && /source = "registry/ {
            for (i = 1; i <= NF; i++) if ($i ~ /^version = /) { gsub(/version = |"/, "", $i); print $i } }' \
        "$CRATE/Cargo.lock"
}
semver_series() { # 0.3.3 -> 0.3, 1.53.3 -> 1
    local IFS=.; set -- $1
    [[ $1 == 0 ]] && echo "0.$2" || echo "$1"
}
for dir in "$ANKISWITCH_WORK"/switch/vendor/*/; do
    name=$(sed -n 's/^name = "\(.*\)"/\1/p' "$dir/Cargo.toml" | head -1)
    ver=$(sed -n 's/^version = "\(.*\)"/\1/p' "$dir/Cargo.toml" | head -1)
    for old in $(registry_versions "$name"); do
        [[ $(semver_series "$old") == $(semver_series "$ver") ]] || continue
        echo "pinning $name $old -> $ver (vendored)"
        (cd "$CRATE" && cargo +"$RUST_NIGHTLY" update -q -p "$name@$old" --precise "$ver")
    done
done

# C code built by crates (bundled SQLite, zstd, ring) uses devkitA64.
T_ENV=${TARGET_NAME//-/_}
export "CC_$T_ENV=aarch64-none-elf-gcc"
export "AR_$T_ENV=aarch64-none-elf-ar"
export "CFLAGS_$T_ENV=-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE \
-ffunction-sections -fdata-sections -D__SWITCH__ \
-I$DEVKITPRO/libnx/include -I$DEVKITPRO/portlibs/switch/include"
export PROTOC=/usr/bin/protoc
# Shared location for the protobuf descriptors that anki_proto's build script
# writes and rslib's reads (Anki's .cargo/config.toml does the same). Without it
# they derive the path from OUT_DIR, which nightly cargo's build-dir layout breaks.
export DESCRIPTORS_BIN="$TARGET_DIR/anki_descriptors.bin"

# Bundled SQLite: LIBSQLITE3_FLAGS from tools/env.sh (no WAL/mmap, no /tmp, no
# dlopen). The rollback journal is also the safer choice on the SD card's FAT/exFAT.
export CARGO_TARGET_DIR="$TARGET_DIR"

# One project version (VERSION, see CHANGELOG.md): the crate must match.
APP_VERSION=$(tr -d '[:space:]' < "$ANKISWITCH_SRC/VERSION")
CRATE_VERSION=$(sed -n 's/^version = "\([^"]*\)".*/\1/p' "$CRATE/Cargo.toml" | head -1)
if [[ "$APP_VERSION" != "$CRATE_VERSION" ]]; then
    echo "error: VERSION is $APP_VERSION but anki_switch_core is $CRATE_VERSION" >&2
    exit 1
fi

# -Zbuild-std fingerprints std's dependencies like registry crates: it does not
# notice when a path dependency of std changes. std's libc is our fork, so when
# the fork changes, drop libc's fingerprints to force libc, std and everything
# above them to rebuild. (Without this, std kept a libc from hours earlier.)
LIBC_FORK="$ANKISWITCH_WORK/switch/vendor/libc"
LIBC_STAMP="$TARGET_DIR/libc-fork.sha256"
LIBC_SUM=$(cd "$LIBC_FORK" && find . -type f \( -name '*.rs' -o -name Cargo.toml \) -print0 \
    | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
if [[ "$(cat "$LIBC_STAMP" 2>/dev/null)" != "$LIBC_SUM" ]]; then
    echo "libc fork changed: rebuilding std"
    # Classic layout (.fingerprint/libc-*) and nightly's build-dir layout
    # (build/libc/<hash>/fingerprint): remove libc's units entirely.
    rm -rf "$TARGET_DIR"/*/*/.fingerprint/libc-* "$TARGET_DIR"/*/.fingerprint/libc-* \
        "$TARGET_DIR"/*/*/build/libc "$TARGET_DIR"/*/build/libc
    mkdir -p "$TARGET_DIR"
    echo "$LIBC_SUM" > "$LIBC_STAMP"
fi

cd "$CRATE"
if cargo +"$RUST_NIGHTLY" tree -q --target "$TARGET" -Zjson-target-spec -i mio 2>&1 | grep -q 'patch.*not used'; then
    echo "error: a [patch] in anki_switch_core/Cargo.toml is not being used" >&2; exit 1
fi
cargo +"$RUST_NIGHTLY" rustc --profile "$PROFILE" -j "${JOBS:-2}" \
    -Zbuild-std=std,panic_abort -Zjson-target-spec --target "$TARGET" \
    -- --print native-static-libs 2> >(tee "$TARGET_DIR/rustc-stderr.log" >&2)
# Record the native libraries the frontend must link after this archive.
grep -o 'native-static-libs: .*' "$TARGET_DIR/rustc-stderr.log" | tail -1 \
    | sed 's/native-static-libs: //' > "$TARGET_DIR/native-static-libs.txt" || true

OUT_SUBDIR=$([[ $PROFILE == dev ]] && echo debug || echo release)
DIST="$ANKISWITCH_SRC/switch/dist"
mkdir -p "$DIST"
cp "$TARGET_DIR/$TARGET_NAME/$OUT_SUBDIR/libanki_switch_core.a" "$DIST/"
cp "$CRATE/include/anki_switch_core.h" "$DIST/"
cp "$TARGET_DIR/native-static-libs.txt" "$DIST/"
# Keep the resolved lockfile in the project so builds are reproducible.
cp "$CRATE/Cargo.lock" "$ANKISWITCH_SRC/switch/anki_switch_core/Cargo.lock"
ls -l "$DIST"
