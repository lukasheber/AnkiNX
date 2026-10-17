#!/usr/bin/env bash
# Development headers for building the AnkiNX desktop preview (borealis + GLFW
# on Linux/WSLg) without root: fetch the -dev packages with `apt-get download`
# and unpack them into $ANKISWITCH_WORK/sysroot. Runtime libraries come from
# the system, so dangling .so symlinks are re-pointed at /usr/lib.
set -euo pipefail
source "$(dirname "$0")/env.sh"
ROOT="$ANKISWITCH_WORK/sysroot"
[[ -f "$ROOT/.done" ]] && { echo "sysroot ready: $ROOT"; exit 0; }

PKGS=(
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev
    libxrender-dev libxfixes-dev x11proto-dev libxau-dev libxdmcp-dev libxcb1-dev
    libgl-dev libglx-dev libegl-dev libopengl-dev libgles-dev
    libdbus-1-dev # borealis' Linux platform (power/network status)
    libwebp-dev libsharpyuv-dev # WebP card images
    x11-apps libxkbfile1 # xwd, for screenshots
    xdotool libxdo3      # simulated key presses / clicks for UI walkthroughs
)
mkdir -p "$ROOT/debs"
(cd "$ROOT/debs" && apt-get download "${PKGS[@]}")
for deb in "$ROOT"/debs/*.deb; do dpkg -x "$deb" "$ROOT"; done

LIBDIR="$ROOT/usr/lib/x86_64-linux-gnu"
find "$LIBDIR" -maxdepth 1 -type l | while read -r link; do
    if [[ ! -e "$link" ]]; then
        target=$(readlink "$link")
        ln -sf "/usr/lib/x86_64-linux-gnu/$(basename "$target")" "$link"
    fi
done
# pkg-config files must point at the sysroot, not /usr.
find "$ROOT" -name '*.pc' -exec sed -i "s|^prefix=/usr|prefix=$ROOT/usr|" {} +
touch "$ROOT/.done"
echo "sysroot ready: $ROOT"
