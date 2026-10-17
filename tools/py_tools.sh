#!/usr/bin/env bash
# Python virtualenv for build-time helper scripts (icon generation).
# Usage: bash tools/py_tools.sh <script.py> [args...]
set -euo pipefail
source "$(dirname "$0")/env.sh"
VENV="$ANKISWITCH_WORK/.venv-tools"
if [[ ! -x "$VENV/bin/python" ]]; then
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q pillow
fi
exec "$VENV/bin/python" "$@"
