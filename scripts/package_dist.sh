#!/bin/bash
# WebPackager distribution packaging script
# Usage: bash scripts/package_dist.sh
# Expects: build/bin/web_packager.exe and lib/ with pre-built libraries

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$REPO_DIR/build"
DIST_DIR="$REPO_DIR/dist"

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/lib/webview/include"
mkdir -p "$DIST_DIR/lib/webview2/include"
mkdir -p "$DIST_DIR/lib/webview2/lib/x64"

cp "$BUILD_DIR/bin/web_packager.exe" "$DIST_DIR/"
if [ -d "$REPO_DIR/lib" ]; then
  cp -r "$REPO_DIR/lib/"* "$DIST_DIR/lib/"
fi

echo "Distribution packaged at: $DIST_DIR"
echo "Total size: $(du -sh "$DIST_DIR" | cut -f1)"
