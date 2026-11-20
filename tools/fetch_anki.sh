#!/usr/bin/env bash
# Put the Anki source the core is built from (ANKI_REPO at ANKI_COMMIT, see
# tools/env.sh) in source_code/$ANKI_DIR, unmodified, then fetch its
# translation submodules (tools/fetch_ftl.sh). Does nothing to a checkout that
# is already at the pinned commit; refuses to touch one with local changes.
# Usage: bash tools/fetch_anki.sh
set -euo pipefail
source "$(dirname "$0")/env.sh"
DEST="$ANKISWITCH_SRC/source_code/$ANKI_DIR"

if [[ -d "$DEST/.git" ]]; then
    if [[ -n $(git -c safe.directory='*' -C "$DEST" status --porcelain --untracked-files=no) ]]; then
        echo "error: $DEST has local changes; Anki's source must stay unmodified" >&2
        exit 1
    fi
    if [[ $(git -c safe.directory='*' -C "$DEST" rev-parse HEAD) != "$ANKI_COMMIT" ]]; then
        echo "$ANKI_DIR -> $ANKI_COMMIT"
        git -c safe.directory='*' -C "$DEST" fetch -q --depth 1 origin "$ANKI_COMMIT"
        git -c safe.directory='*' -C "$DEST" checkout -q --detach FETCH_HEAD
    else
        echo "$ANKI_DIR already at $ANKI_COMMIT"
    fi
elif [[ -e "$DEST" ]]; then
    echo "error: $DEST exists but is not a git checkout" >&2
    exit 1
else
    echo "$ANKI_REPO @ $ANKI_COMMIT -> source_code/$ANKI_DIR"
    mkdir -p "$DEST"
    git -c safe.directory='*' -C "$DEST" init -q
    git -c safe.directory='*' -C "$DEST" remote add origin "$ANKI_REPO"
    git -c safe.directory='*' -C "$DEST" fetch -q --depth 1 origin "$ANKI_COMMIT"
    git -c safe.directory='*' -C "$DEST" checkout -q --detach FETCH_HEAD
fi

bash "$(dirname "$0")/fetch_ftl.sh"
