#!/usr/bin/env bash
# Build AnkiNX.nro (run inside WSL). Needs switch/dist/libanki_switch_core.a
# (switch/build_core.sh) and app/library (tools/fetch_frontend_deps.sh).
# Output: app/dist/AnkiNX.nro
set -euo pipefail
source "$(dirname "$0")/../tools/env.sh"
bash "$ANKISWITCH_SRC/tools/sync.sh"
[[ -f "$ANKISWITCH_WORK/switch/dist/libanki_switch_core.a" ]] ||
    { echo "error: run switch/build_core.sh first" >&2; exit 1; }

cd "$ANKISWITCH_WORK/app"
cmake -B build_switch -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}"
make -C build_switch -j"${JOBS:-3}" AnkiNX.nro

mkdir -p "$ANKISWITCH_SRC/app/dist"
cp build_switch/AnkiNX.nro "$ANKISWITCH_SRC/app/dist/"
ls -l "$ANKISWITCH_SRC/app/dist/AnkiNX.nro"
