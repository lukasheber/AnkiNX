#!/usr/bin/env bash
# Fetch Anki's translation submodules (ftl/core-repo, ftl/qt-repo) at the exact
# commits the Anki source pins, since source archives and shallow clones ship
# them empty. Reads the pins from source_code/$ANKI_DIR's own git tree when it
# is a checkout (tools/fetch_anki.sh), else from ANKI_REPO at the given ref.
# Usage: bash tools/fetch_ftl.sh [ref]   (default: ANKI_COMMIT, see tools/env.sh)
set -euo pipefail
source "$(dirname "$0")/env.sh"
REF=${1:-$ANKI_COMMIT}
ANKI="$ANKISWITCH_SRC/source_code/$ANKI_DIR"
SLUG=$(sed -E 's|^https://github.com/||; s|\.git$||' <<< "$ANKI_REPO")

pin() { # submodule path -> "sha url"
    local sub=$1 info
    if [[ -d "$ANKI/.git" ]]; then
        local sha url key
        # WSL's git distrusts checkouts owned by the Windows user.
        sha=$(git -c safe.directory='*' -C "$ANKI" ls-tree "$REF" "$sub" | awk '{print $3}')
        key=$(git config -f "$ANKI/.gitmodules" --get-regexp '^submodule\..*\.path$' |
            awk -v p="$sub" '$2 == p { sub(/\.path$/, ".url", $1); print $1 }')
        url=$(git config -f "$ANKI/.gitmodules" --get "$key")
        [[ -n $sha && -n $url ]] && { echo "$sha $url"; return; }
    fi
    info=$(curl -sSf -H 'Accept: application/vnd.github+json' \
        "https://api.github.com/repos/$SLUG/contents/$sub?ref=$REF")
    python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["sha"], d["submodule_git_url"])' <<< "$info"
}

for sub in ftl/core-repo ftl/qt-repo; do
    read -r sha url < <(pin "$sub")
    dest="$ANKI/$sub"
    if [[ -f "$dest/.pinned-sha" && $(cat "$dest/.pinned-sha") == "$sha" ]]; then
        echo "$sub already at $sha"; continue
    fi
    echo "$sub -> $url @ $sha"
    tmp=$(mktemp -d)
    git -C "$tmp" init -q
    git -C "$tmp" fetch -q --depth 1 "$url" "$sha"
    git -C "$tmp" checkout -q FETCH_HEAD
    rm -rf "$dest" && mkdir -p "$dest"
    (cd "$tmp" && git archive HEAD) | tar -x -C "$dest"
    echo "$sha" > "$dest/.pinned-sha"
    rm -rf "$tmp"
done
