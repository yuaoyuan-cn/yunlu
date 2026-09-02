#!/usr/bin/env bash
# 云录 设备侧编译脚本
# 用法: 在设备上 mkdir build && cd build && ../scripts/build.sh
set -e

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"

echo "==> 检查依赖"
command -v cmake >/dev/null || { echo "缺少 cmake"; exit 1; }
command -v qmake >/dev/null || { echo "缺少 qmake (需安装 qtbase5-dev)"; exit 1; }
pkg-config --exists libavcodec || echo "警告: 未找到 ffmpeg pkg-config(libavcodec-dev)"

echo "==> 配置"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> 编译"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> 完成: $BUILD_DIR/yunlu"
