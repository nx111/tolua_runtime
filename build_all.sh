#!/bin/sh

curdir="$(pwd)"
cd "$(dirname $0)"

./build_x86.sh && ./build_arm.sh && ./build_arm64.sh && ./build_ubuntu.sh && ./build_win32.sh && ./build_win64.sh

echo "构建完成！"
cd "$curdir"