#!/usr/bin/env bash
# Fetch Anki's translation submodules (ftl/core-repo, ftl/qt-repo) at the exact
# commits pinned by the Anki release, since source archives ship them empty.
# Usage: bash tools/fetch_ftl.sh [anki-tag]   (default: 26.09.3)
set -euo pipefail
source "$(dirname "$0")/env.sh"
TAG=${1:-26.09.3}
ANKI="$ANKISWITCH_SRC/source_code/anki-$TAG"

for sub in ftl/core-repo ftl/qt-repo; do
    info=$(curl -sSf -H 'Accept: application/vnd.github+json' \
        "https://api.github.com/repos/ankitects/anki/contents/$sub?ref=$TAG")
    sha=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["sha"])' <<< "$info")
    url=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["submodule_git_url"])' <<< "$info")
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
