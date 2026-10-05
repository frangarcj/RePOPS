#!/bin/sh
set -eu

VERSION=v21.3
ASSET=ghidra_12.1_PUBLIC_20260520_ghidra-allegrex.zip
SHA256=a72675aa4af37f4319ed587a1bb5d93c7220901309b4de175d1e9ab1e5b6902c
URL="https://github.com/kotcrab/ghidra-allegrex/releases/download/$VERSION/$ASSET"

CACHE=${TMPDIR:-/tmp}/$ASSET
DEST=${GHIDRA_USER_EXTENSIONS:-"$HOME/Library/ghidra/ghidra_12.1_PUBLIC/Extensions"}

if [ -e "$DEST/ghidra-allegrex" ]; then
    echo "Extension already exists at $DEST/ghidra-allegrex; not replacing it."
    exit 0
fi

curl -L --fail --silent --show-error "$URL" -o "$CACHE"
ACTUAL=$(shasum -a 256 "$CACHE" | awk '{print $1}')
if [ "$ACTUAL" != "$SHA256" ]; then
    echo "sha256 mismatch: expected $SHA256, got $ACTUAL" >&2
    exit 1
fi

mkdir -p "$DEST"
unzip -q "$CACHE" -d "$DEST"
echo "installed ghidra-allegrex to $DEST/ghidra-allegrex"
