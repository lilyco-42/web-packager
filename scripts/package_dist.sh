#!/bin/bash
# WebPackager 独立打包脚本
set -e

BUILD_DIR="D:/CODE/CPP/webview/build3"
EXAMPLES_DIR="D:/CODE/CPP/webview/examples/web_packager"
DIST_DIR="$BUILD_DIR/dist"

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/lib/webview/include"
mkdir -p "$DIST_DIR/lib/webview2/include"
mkdir -p "$DIST_DIR/lib/webview2/lib/x64"

cp "$BUILD_DIR/bin/web_packager.exe" "$DIST_DIR/"
cp "$BUILD_DIR/bin/lib/webview/libwebview.a" "$DIST_DIR/lib/webview/"
cp -r "$BUILD_DIR/bin/lib/webview/include/"* "$DIST_DIR/lib/webview/include/"
cp -r "$BUILD_DIR/bin/lib/webview2/include/"* "$DIST_DIR/lib/webview2/include/"
cp -r "$BUILD_DIR/bin/lib/webview2/lib/"* "$DIST_DIR/lib/webview2/lib/"

echo "打包完成！文件在: $DIST_DIR"
echo "总大小: $(du -sh "$DIST_DIR" | cut -f1)"
echo ""
echo "用户解压后直接运行 web_packager.exe 即可"
