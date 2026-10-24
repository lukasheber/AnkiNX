#!/usr/bin/env bash
# Mirror the project's Switch sources (and the Anki source tree) from the
# Windows drive into the Linux work tree, where builds run (cargo on /mnt/c is
# very slow). The layout is identical, so relative paths work in both trees.
set -euo pipefail
source "$(dirname "$0")/env.sh"
mkdir -p "$ANKISWITCH_WORK/source_code"
cp "$ANKISWITCH_SRC/VERSION" "$ANKISWITCH_WORK/VERSION"

# Our own code, edited while builds run: changed files are found by content
# and stamped with the copy time (no -t), so make and cargo always see them as
# newer than what they built. With -a a file kept its edit time, which could
# be older than the objects of a build that synced earlier and was still
# compiling, and the edit was silently skipped. Everything else (third-party
# trees, assets) is compared by size and time, which is much faster.
APP_OWN=(src include js/src tests patches)
SWITCH_OWN=(anki_switch_core/src anki_switch_core/examples anki_switch_core/tests)
own() { # root, folders...
    local root=$1 dir
    shift
    for dir in "$@"; do
        [[ -d "$ANKISWITCH_SRC/$root/$dir" ]] || continue
        mkdir -p "$ANKISWITCH_WORK/$root/$dir"
        rsync -rlpD --checksum --delete "$ANKISWITCH_SRC/$root/$dir/" "$ANKISWITCH_WORK/$root/$dir/"
    done
}
excludes() { local dir; for dir in "$@"; do printf -- '--exclude=/%s/\n' "$dir"; done; }

mapfile -t skip < <(excludes "${SWITCH_OWN[@]}")
rsync -a --delete --exclude target/ --exclude '*.orig' --exclude Cargo.lock "${skip[@]}" \
    "$ANKISWITCH_SRC/switch/" "$ANKISWITCH_WORK/switch/"
own switch "${SWITCH_OWN[@]}"
rsync -a --delete --exclude .git/ --exclude node_modules/ --exclude out/ --exclude target/ \
    "$ANKISWITCH_SRC/source_code/anki-26.09.3/" "$ANKISWITCH_WORK/source_code/anki-26.09.3/"
if [[ -d "$ANKISWITCH_SRC/app" ]]; then
    mapfile -t skip < <(excludes "${APP_OWN[@]}")
    rsync -a --delete --exclude 'build*/' "${skip[@]}" "$ANKISWITCH_SRC/app/" "$ANKISWITCH_WORK/app/"
    own app "${APP_OWN[@]}"
fi
