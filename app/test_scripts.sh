#!/usr/bin/env bash
# Build the desktop preview (which includes the host tests) and run the card
# script, right-to-left text and romaji-to-kana tests (app/tests). Run inside WSL.
set -euo pipefail
source "$(dirname "$0")/../tools/env.sh"
bash "$ANKISWITCH_SRC/app/build_desktop.sh" > /dev/null
cd "$ANKISWITCH_WORK/app"
make -C build_desktop -j"${JOBS:-3}" script_test rtl_test kana_test > /dev/null
./build_desktop/script_test resources/script/runtime.js
./build_desktop/rtl_test
./build_desktop/kana_test
