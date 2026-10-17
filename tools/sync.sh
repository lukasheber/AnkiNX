#!/usr/bin/env bash
# Mirror the project's Switch sources (and the Anki source tree) from the
# Windows drive into the Linux work tree, where builds run (cargo on /mnt/c is
# very slow). The layout is identical, so relative paths work in both trees.
set -euo pipefail
source "$(dirname "$0")/env.sh"
mkdir -p "$ANKISWITCH_WORK/source_code"
cp "$ANKISWITCH_SRC/VERSION" "$ANKISWITCH_WORK/VERSION"
rsync -a --delete --exclude target/ --exclude '*.orig' --exclude Cargo.lock \
    "$ANKISWITCH_SRC/switch/" "$ANKISWITCH_WORK/switch/"
rsync -a --delete --exclude .git/ --exclude node_modules/ --exclude out/ --exclude target/ \
    "$ANKISWITCH_SRC/source_code/anki-26.09.3/" "$ANKISWITCH_WORK/source_code/anki-26.09.3/"
if [[ -d "$ANKISWITCH_SRC/app" ]]; then
    rsync -a --delete --exclude 'build*/' "$ANKISWITCH_SRC/app/" "$ANKISWITCH_WORK/app/"
fi
