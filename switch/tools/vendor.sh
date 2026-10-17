#!/usr/bin/env bash
# Manage the patched crates in switch/vendor (see switch/vendor.lst).
#
#   vendor.sh apply [dir...]  Recreate vendor dirs from pristine crates.io sources
#                             + switch/patches/<dir>.patch (destroys local edits).
#   vendor.sh save  [dir...]  Regenerate switch/patches/<dir>.patch from the
#                             current vendor dir (run after editing vendor code).
#   vendor.sh port <dir> <new-version>
#                             Re-apply <dir>'s patch onto a new upstream version.
set -euo pipefail
source "$(dirname "$0")/../../tools/env.sh"
SW="$ANKISWITCH_SRC/switch"
PRISTINE="$ANKISWITCH_WORK/pristine"
mkdir -p "$PRISTINE" "$SW/patches"

entries() { grep -vE '^\s*(#|$)' "$SW/vendor.lst"; }

pristine() { # name version -> path of unpacked upstream source
    local name=$1 ver=$2 dst="$PRISTINE/$1-$2"
    if [[ ! -d "$dst" ]]; then
        curl -sSfL "https://static.crates.io/crates/$name/$name-$ver.crate" | tar -xz -C "$PRISTINE"
    fi
    echo "$dst"
}

selected() { # filter manifest by dir names given on the command line
    if [[ $# -eq 0 ]]; then entries; else
        for d in "$@"; do entries | awk -v d="$d" '$3 == d'; done
    fi
}

save_one() {
    local name=$1 ver=$2 dir=$3 src out
    src=$(pristine "$name" "$ver")
    out="$SW/patches/$dir.patch"
    # Paths relative to the crate root; ignore cargo metadata files.
    (cd "$PRISTINE" && diff -ruN --exclude=.cargo-ok --exclude=.cargo_vcs_info.json \
        "$name-$ver" "$SW/vendor/$dir" |
        sed -E -e "s|^--- $name-$ver/([^\t]*).*|--- a/\1|" \
               -e "s|^\+\+\+ $SW/vendor/$dir/([^\t]*).*|+++ b/\1|" \
               -e "s|^diff -ruN .*$name-$ver/(.*) $SW/vendor/$dir/.*|diff -ruN a/\1 b/\1|") > "$out" || true
    echo "saved $out ($(grep -c '^@@' "$out") hunks)"
}

apply_one() {
    local name=$1 ver=$2 dir=$3 src
    src=$(pristine "$name" "$ver")
    rm -rf "$SW/vendor/$dir"
    cp -r "$src" "$SW/vendor/$dir"
    if [[ -s "$SW/patches/$dir.patch" ]]; then
        patch -s -p1 -d "$SW/vendor/$dir" < "$SW/patches/$dir.patch"
    fi
    echo "applied $dir ($name $ver)"
}

cmd=${1:-}; shift || true
case "$cmd" in
    apply) selected "$@" | while read -r n v d; do apply_one "$n" "$v" "$d"; done ;;
    save)  selected "$@" | while read -r n v d; do save_one "$n" "$v" "$d"; done ;;
    port)
        d=$1 newver=$2
        read -r n _ _ < <(entries | awk -v d="$d" '$3 == d')
        sed -i "s/^\($n\s\+\)[0-9.]\+\(\s\+$d\)$/\1$newver\2/" "$SW/vendor.lst"
        apply_one "$n" "$newver" "$d" ;;
    *) sed -n '2,10p' "$0"; exit 1 ;;
esac
