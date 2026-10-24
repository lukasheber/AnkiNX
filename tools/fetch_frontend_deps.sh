#!/usr/bin/env bash
# Fetch the frontend's third-party libraries into app/library at pinned commits.
# The first run (with no pins file) records the resolved commits in
# app/library/PINS so later runs reproduce the same sources.
set -euo pipefail
source "$(dirname "$0")/env.sh"
LIB="$ANKISWITCH_SRC/app/library"
PINS="$LIB/PINS"
mkdir -p "$LIB"

# name  url  ref(branch or tag; used only when not pinned yet)
DEPS=(
    "borealis https://github.com/dragonflylee/borealis.git switchfin"
    "litehtml https://github.com/litehtml/litehtml.git master"
    # SVG card images (incl. Anki's LaTeX SVGs, which need <use>); Switchfin's fork
    "lunasvg https://github.com/dragonflylee/lunasvg.git master"
    # JavaScript in card templates (app/src/view/card_script.cpp)
    "quickjs https://github.com/quickjs-ng/quickjs.git v0.17.0"
    # Text shaping for scripts NanoVG can't draw letter by letter (app/src/view/shaping.cpp)
    "harfbuzz https://github.com/harfbuzz/harfbuzz.git 12.3.2"
    # The forest behind the deck list (app/forest): Memory Forest's scene scripts
    "memory-forest https://github.com/baraklevy20/memory-forest.git main"
)

pinned() { [[ -f "$PINS" ]] && awk -v n="$1" '$1 == n {print $2}' "$PINS"; }
# Our fixes to a library: app/patches/<name>/*.patch, applied in order with
# `patch -p1` after extraction. The stamp changes when they do.
PATCHES="$ANKISWITCH_SRC/app/patches"
patch_stamp() { { cat "$PATCHES/$1"/*.patch 2>/dev/null || true; } | sha1sum | cut -c1-12; }

for dep in "${DEPS[@]}"; do
    read -r name url ref <<< "$dep"
    sha=$(pinned "$name" || true)
    if [[ -z "$sha" ]]; then
        sha=$(git ls-remote "$url" "refs/heads/$ref" "refs/tags/$ref" | head -1 | cut -f1)
        [[ -n "$sha" ]] || { echo "error: $url has no branch/tag '$ref'" >&2; exit 1; }
        echo "$name $sha $url" >> "$PINS"
    fi
    dest="$LIB/$name"
    stamp="$sha $(patch_stamp "$name")"
    if [[ -f "$dest/.pinned-sha" && $(cat "$dest/.pinned-sha") == "$stamp" ]]; then
        echo "$name already at $sha"; continue
    fi
    echo "$name -> $url @ $sha"
    tmp=$(mktemp -d)
    git -C "$tmp" init -q
    git -C "$tmp" fetch -q --depth 1 "$url" "$sha"
    git -C "$tmp" checkout -q FETCH_HEAD
    # borealis vendors some of its dependencies as submodules
    git -C "$tmp" submodule update -q --init --depth 1 --recursive 2>/dev/null || true
    rm -rf "$dest" && mkdir -p "$dest"
    (cd "$tmp" && tar --exclude=.git -cf - .) | tar -xf - -C "$dest"
    for p in "$PATCHES/$name"/*.patch; do
        [[ -f "$p" ]] || continue
        echo "  patch $(basename "$p")"
        patch -s -p1 -d "$dest" < "$p"
    done
    echo "$stamp" > "$dest/.pinned-sha"
    rm -rf "$tmp"
done
# The desktop preview needs the Switch system fonts, which borealis ships.
# They are Nintendo's, so they are copied here and not kept in our repository.
# (The .nro drops them and uses the console's shared fonts.)
FONTS="$ANKISWITCH_SRC/app/resources/font"
mkdir -p "$FONTS"
cp "$LIB/borealis/resources/font/switch_font.ttf" "$LIB/borealis/resources/font/switch_icons.ttf" "$FONTS/"
cat "$PINS"
