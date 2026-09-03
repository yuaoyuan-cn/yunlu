#!/bin/bash

# FFmpeg管道架构原型测试脚本

set -e

echo "=== FFmpeg管道架构原型测试 ==="
echo ""

# 检查依赖
echo "检查依赖..."
if ! command -v ffmpeg &> /dev/null; then
    echo "警告: ffmpeg未安装"
fi

if ! pkg-config --exists libavformat libavcodec libavutil; then
    echo "错误: FFmpeg开发库未安装"
    echo "请运行: sudo apt-get install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev"
    exit 1
fi

echo "依赖检查通过"
echo ""

# 编译
echo "编译程序..."
make clean
make

if [ $? -ne 0 ]; then
    echo "编译失败"
    exit 1
fi

echo "编译成功"
echo ""

# 检查设备
echo "检查视频设备..."
if [ ! -e /dev/video0 ]; then
    echo "警告: /dev/video0 不存在"
    echo "请连接HDMI信号源"
    echo ""
    echo "可用的视频设备:"
    ls -la /dev/video* 2>/dev/null || echo "无"
    exit 1
fi

echo "找到设备: /dev/video0"
echo ""

# 显示设备信息
echo "设备信息:"
if command -v v4l2-ctl &> /dev/null; then
    v4l2-ctl --list-formats-ext -d /dev/video0
else
    echo "v4l2-ctl未安装，跳过设备信息显示"
fi
echo ""

# 运行测试
echo "开始测试..."
echo "按Ctrl+C停止测试"
echo ""

# 运行简化测试（10秒）
timeout 10 ./simple_preview /dev/video0 || true

echo ""
echo "测试完成"
echo ""

# 运行完整测试（5秒）
echo "运行完整采集测试（5秒）..."
timeout 5 ./ffmpeg_capture_test /dev/video0 test_output.mp4 || true

echo ""
echo "检查输出文件..."
if [ -f test_output.mp4 ]; then
    ls -lh test_output.mp4
    echo ""
    echo "文件信息:"
    ffprobe test_output.mp4 2>&1 | grep -E "(Duration|Stream|Video)"
else
    echo "未生成输出文件"
fi

echo ""
echo "=== 测试完成 ==="
