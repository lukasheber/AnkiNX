#!/usr/bin/env bash
# Node.js for bundling the card script runtime (app/js, see app/js/README.md).
# Development only: the app embeds the bundled runtime.js, never Node. Installs
# a pinned Node into ~/.local (no sudo) and prints the bin folder to add to PATH.
set -euo pipefail
NODE_VERSION="${NODE_VERSION:-v22.20.0}"
DEST="$HOME/.local/node-$NODE_VERSION"
if [[ ! -x "$DEST/bin/node" ]]; then
    tmp="$(mktemp -d)"
    curl -fsSL "https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-linux-x64.tar.xz" -o "$tmp/node.tar.xz"
    curl -fsSL "https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt" -o "$tmp/SHASUMS256.txt"
    (cd "$tmp" && grep " node-$NODE_VERSION-linux-x64.tar.xz\$" SHASUMS256.txt | sed 's/node-.*$/node.tar.xz/' | sha256sum -c -)
    mkdir -p "$DEST"
    tar -xJf "$tmp/node.tar.xz" -C "$DEST" --strip-components=1
    rm -rf "$tmp"
fi
echo "$DEST/bin"
