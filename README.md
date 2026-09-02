# 云录 (yunlu)

鲁班猫 RK3588 (Ubuntu 22.04) 上的 HDMI-In 本地录制 + RTMP 推流 + 全屏预览软件。

## 功能
- V4L2 采集 HDMI-In (`/dev/video0`, NV12)
- RK3588 VPU 硬编 (`h264_rkmpp` / `hevc_rkmpp`)
- H.264 + MP4 本地录制
- H.264 + RTMP 推流
- Qt5 OpenGL 全屏界面，GPU 纹理实时预览 HDMI-In 图像
- systemd 开机自启动

## 环境要求（在设备上一次性安装）
系统已带 FFmpeg(6.1, 含 rkmpp) 与 libmali GPU 驱动。需补装开发包：

```bash
sudo apt-get update
sudo apt-get install -y \
  qtbase5-dev libqt5opengl5-dev \
  libegl1-mesa-dev libgles2-mesa-dev \
  libavcodec-dev libavformat-dev libavfilter-dev libavutil-dev \
  libswscale-dev libswresample-dev libavdevice-dev \
  libdrm-dev cmake
```

> 注意：apt 可能装到 FFmpeg 4.4 (rkmpp PPA) 或保持 6.1 的 dev 包。
> 若编译时 `avcodec` 版本与运行库不一致，会出现 ABI 告警。确保 `pkg-config --modversion libavcodec`
> 对应的版本与 `ffmpeg -version` 一致即可。

## 编译（在设备上）
```bash
cd yunlu
scripts/build.sh
# 或手动:
# cmake -S . -B build && cmake --build build -j4
```

二进制输出: `build/yunlu`

## 配置
编辑 `config/settings.h` 可改采集分辨率/帧率/码率/录制目录/默认RTMP地址。

## 运行（联机调试）
```bash
# 设备本机全屏运行（有屏时，X :0）
export DISPLAY=:0
./build/yunlu

# 或经 SSH X11 转发调试
./build/yunlu
```

- 界面按钮: 录制 / 推流 / 退出；`Esc` 退出
- RTMP 地址在界面输入框填

## 开机自启动
```bash
sudo scripts/install_service.sh
```
取消自启动: `sudo systemctl disable yunlu`

## 架构
```
capture (V4L2 NV12)  ──┬──▶ encoder (rkmpp H.264)
                       │        │
                       └──▶ video_widget (GL 预览)   └─▶ output (MP4 + RTMP)
```
- `src/capture`: V4L2 M-MAP 采集线程
- `src/encoder`: libavcodec rkmpp 硬编线程
- `src/output`: libavformat 写 MP4 / RTMP
- `src/display`: QOpenGLWidget + GL shader (NV12→RGB)
- `src/ui`: 全屏控制界面
- `src/controller`: 线程串联与生命周期

## 已知注意事项
- HDMI-In 无信号时 `v4l2 select` 会超时但程序保持等待不退出。
- 推流网络断开,对应输出会自动停止并写尾,不影响录制。
- 若有同步/性能问题,优先检查 VPU 频率与 DDR 带宽。
