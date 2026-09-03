# FFmpeg管道架构原型测试

## 目的

验证使用FFmpeg管道架构替代当前V4L2手动采集的可行性，解决以下问题：
1. 每帧多次memcpy导致的性能问题
2. stall检测误判导致的帧率雪崩
3. 格式兼容性问题

## 文件说明

- `ffmpeg_capture_test.cpp` - 完整的采集+编码+输出测试
- `simple_preview.cpp` - 简化的采集测试（显示帧信息）
- `Makefile` - 编译脚本

## 编译

### 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev

# 或使用Makefile
make install-deps
```

### 编译程序

```bash
make
```

## 测试

### 1. 检查设备

```bash
# 查看可用的视频设备
ls -la /dev/video*

# 使用v4l2-ctl测试
v4l2-ctl --list-formats-ext -d /dev/video0
```

### 2. 运行简化测试

```bash
# 测试采集
./simple_preview /dev/video0

# 预期输出：
# === 输入信息 ===
# 分辨率: 1920x1080
# 像素格式: 0 (NV12)
# 帧率: 30.000000 fps
# ...
# 开始采集... (按Ctrl+C停止)
# 帧间隔: 33.33 ms (30.0 fps)
# 已处理: 100 帧, 3 秒, 33.33 fps
```

### 3. 运行完整测试

```bash
# 采集并编码到MP4
./ffmpeg_capture_test /dev/video0 output.mp4

# 按Ctrl+C停止后，检查输出文件
ls -lh output.mp4
ffprobe output.mp4
```

## 预期结果

### 性能对比

| 指标 | 当前实现 | FFmpeg管道 |
|------|----------|------------|
| 1080p帧率 | 8 fps | 60 fps |
| 内存拷贝 | 9.3 MB/帧 | 0 MB/帧 |
| CPU占用 | 高 | 低 |

### 兼容性

- 支持所有V4L2格式（NV12, NV16, YUYV, BGR24等）
- 支持所有分辨率（480p, 720p, 1080p, 4K）
- 支持所有帧率（24, 25, 30, 50, 60 fps）

## 故障排除

### 问题1：无法打开设备

```
无法打开设备 /dev/video0: Permission denied
```

解决：
```bash
# 添加用户到video组
sudo usermod -a -G video $USER
# 重新登录

# 或临时修改权限
sudo chmod 666 /dev/video0
```

### 问题2：rkmpp不可用

```
rkmpp不可用，使用软件编码器
```

这是正常的，程序会自动回退到软件编码器。在Rockchip平台上，确保安装了rkmpp库。

### 问题3：帧率低

```
帧间隔: 100.00 ms (10.0 fps)
```

可能原因：
1. HDMI信号源帧率低
2. V4L2驱动配置问题
3. 系统负载高

解决：
```bash
# 检查系统负载
top

# 检查V4L2驱动
dmesg | grep -i video

# 尝试不同的输入格式
v4l2-ctl --set-fmt-video=pixelformat=NV12 -d /dev/video0
```

## 下一步

1. 验证原型性能
2. 集成到主项目
3. 添加预览功能（SDL2或Qt）
4. 优化热插拔处理
5. 添加错误恢复机制

## 参考资料

- [FFmpeg V4L2文档](https://ffmpeg.org/ffmpeg-devices.html#video4linux2-v4l2)
- [FFmpeg编码文档](https://ffmpeg.org/ffmpeg-codecs.html)
- [V4L2 API文档](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)
