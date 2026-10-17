#!/usr/bin/env bash
# Build the desktop preview of AnkiNX (Linux/WSLg, GLFW + X11): same UI code and
# Rust backend as the Switch build, for checking layouts without a console.
# Needs switch/dist-host (switch/build_core_host.sh). Audio is disabled.
set -euo pipefail
source "$(dirname "$0")/../tools/env.sh"
bash "$ANKISWITCH_SRC/tools/sync.sh"
bash "$ANKISWITCH_SRC/tools/desktop_sysroot.sh"
ROOT="$ANKISWITCH_WORK/sysroot"
export CPATH="$ROOT/usr/include"
export LIBRARY_PATH="$ROOT/usr/lib/x86_64-linux-gnu"
export PKG_CONFIG_PATH="$ROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$ROOT/usr/share/pkgconfig"

cd "$ANKISWITCH_WORK/app"
cmake -B build_desktop -DPLATFORM_DESKTOP=ON -DANKINX_AUDIO=OFF -DCMAKE_BUILD_TYPE=Release \
    -DANKI_CORE_DIR="$ANKISWITCH_WORK/switch/dist-host" \
    -DGLFW_BUILD_WAYLAND=OFF -DCMAKE_PREFIX_PATH="$ROOT/usr" \
    -DDBUS_INCLUDE_DIR="$ROOT/usr/include/dbus-1.0" \
    -DDBUS_ARCH_INCLUDE_DIR="$ROOT/usr/lib/x86_64-linux-gnu/dbus-1.0/include"
make -C build_desktop -j"${JOBS:-3}" AnkiNX
ls -l build_desktop/AnkiNX
