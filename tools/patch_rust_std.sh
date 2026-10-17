#!/usr/bin/env bash
# Phase 2: patch the pinned nightly's std/build.rs so the Switch (Nintendo +
# Horizon OS) is treated as a full-std target instead of `restricted_std`.
#
# std/build.rs already accepts `target_vendor == "nintendo" && target_env == "newlib"`
# (3DS); the built-in aarch64-nintendo-switch-freestanding target has no env,
# so it needs the extra clause below.
#
# Usage: bash tools/patch_rust_std.sh [--revert]
# Safe to re-run.

set -euo pipefail

RUST_NIGHTLY="${RUST_NIGHTLY:-nightly-2026-09-15}"
source "$HOME/.cargo/env"

SYSROOT="$(rustc +"$RUST_NIGHTLY" --print sysroot)"
BUILD_RS="$SYSROOT/lib/rustlib/src/rust/library/std/build.rs"
BACKUP="$BUILD_RS.orig"
CLAUSE='|| (target_vendor == "nintendo" \&\& target_os == "horizon")'
MARKER='target_vendor == "nintendo" && target_os == "horizon"'

[[ -f "$BUILD_RS" ]] || { echo "rust-src missing for $RUST_NIGHTLY: $BUILD_RS" >&2; exit 1; }

if [[ "${1:-}" == "--revert" ]]; then
    [[ -f "$BACKUP" ]] && cp "$BACKUP" "$BUILD_RS" && echo "Reverted $BUILD_RS"
    LIB_TOML="$SYSROOT/lib/rustlib/src/rust/library/Cargo.toml"
    [[ -f "$LIB_TOML.orig" ]] && cp "$LIB_TOML.orig" "$LIB_TOML" && echo "Reverted $LIB_TOML"
    LIB_LOCK="$SYSROOT/lib/rustlib/src/rust/library/Cargo.lock"
    [[ -f "$LIB_LOCK.orig" ]] && cp "$LIB_LOCK.orig" "$LIB_LOCK" && echo "Reverted $LIB_LOCK"
    exit 0
fi

if grep -qF "$MARKER" "$BUILD_RS"; then
    echo "Already patched: $BUILD_RS"
else
    [[ -f "$BACKUP" ]] || cp "$BUILD_RS" "$BACKUP"
    # Insert right after the existing 3DS (nintendo + newlib) clause.
    sed -i "/target_vendor == \"nintendo\" && target_env == \"newlib\")/a\\        $CLAUSE" "$BUILD_RS"
    grep -qF "$MARKER" "$BUILD_RS" || { echo "Patch anchor not found in $BUILD_RS" >&2; exit 1; }
    echo "Patched: $BUILD_RS"
fi
diff -u "$BACKUP" "$BUILD_RS" || true

# -Zbuild-std resolves std's dependencies through library/Cargo.toml and ignores
# the application's [patch] table, so std's libc must be redirected here to the
# vendored Switch-aware fork (switch/vendor/libc).
source "$(dirname "$0")/env.sh"
LIB_TOML="$SYSROOT/lib/rustlib/src/rust/library/Cargo.toml"
LIBC_FORK="$ANKISWITCH_WORK/switch/vendor/libc"
[[ -f "$LIB_TOML.orig" ]] || cp "$LIB_TOML" "$LIB_TOML.orig"
if grep -q '^libc = { path' "$LIB_TOML"; then
    sed -i "s|^libc = { path = .*|libc = { path = '$LIBC_FORK' }|" "$LIB_TOML"
else
    sed -i "/^\[patch.crates-io\]/a libc = { path = '$LIBC_FORK' }" "$LIB_TOML"
fi
# library/Cargo.lock pins crates.io libc at an exact version, which keeps the
# path patch from matching; drop that entry so cargo re-resolves libc to the fork.
LIB_LOCK="$SYSROOT/lib/rustlib/src/rust/library/Cargo.lock"
[[ -f "$LIB_LOCK.orig" ]] || cp "$LIB_LOCK" "$LIB_LOCK.orig"
awk 'BEGIN { RS = ""; ORS = "\n\n" }
     !($0 ~ /^\[\[package\]\]\nname = "libc"\n/ && $0 ~ /source = "registry/)' \
    "$LIB_LOCK" > "$LIB_LOCK.tmp" && mv "$LIB_LOCK.tmp" "$LIB_LOCK"
echo "std's libc -> $LIBC_FORK"
