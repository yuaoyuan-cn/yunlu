@echo off
REM FFmpeg管道架构原型测试脚本 (Windows)

echo === FFmpeg管道架构原型测试 ===
echo.

REM 检查依赖
echo 检查依赖...
where ffmpeg >nul 2>&1
if %errorlevel% neq 0 (
    echo 警告: ffmpeg未安装
)

pkg-config --exists libavformat libavcodec libavutil 2>nul
if %errorlevel% neq 0 (
    echo 错误: FFmpeg开发库未安装
    echo 请安装FFmpeg开发包
    pause
    exit /b 1
)

echo 依赖检查通过
echo.

REM 编译
echo 编译程序...
make clean
make

if %errorlevel% neq 0 (
    echo 编译失败
    pause
    exit /b 1
)

echo 编译成功
echo.

REM 检查设备
echo 检查视频设备...
if not exist /dev/video0 (
    echo 警告: /dev/video0 不存在
    echo 请连接HDMI信号源
    echo.
    echo 可用的视频设备:
    dir /dev/video* 2>nul || echo 无
    pause
    exit /b 1
)

echo 找到设备: /dev/video0
echo.

REM 运行测试
echo 开始测试...
echo 按Ctrl+C停止测试
echo.

REM 运行简化测试（10秒）
timeout 10 simple_preview /dev/video0

echo.
echo 测试完成
echo.

REM 运行完整测试（5秒）
echo 运行完整采集测试（5秒）...
timeout 5 ffmpeg_capture_test /dev/video0 test_output.mp4

echo.
echo 检查输出文件...
if exist test_output.mp4 (
    dir test_output.mp4
    echo.
    echo 文件信息:
    ffprobe test_output.mp4 2>&1 | findstr "Duration Stream Video"
) else (
    echo 未生成输出文件
)

echo.
echo === 测试完成 ===
pause
