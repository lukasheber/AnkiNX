#!/usr/bin/env bash
# Build the desktop preview (which includes script_test) and run the card
# script tests (app/tests/script_test.cpp). Run inside WSL.
set -euo pipefail
source "$(dirname "$0")/../tools/env.sh"
bash "$ANKISWITCH_SRC/app/build_desktop.sh" > /dev/null
cd "$ANKISWITCH_WORK/app"
make -C build_desktop -j"${JOBS:-3}" script_test > /dev/null
./build_desktop/script_test resources/script/prelude.js
