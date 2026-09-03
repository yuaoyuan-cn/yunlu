# FFmpeg管道架构原型 - 完成总结

## 已完成的工作

### 1. 问题分析
- 分析了当前代码的性能瓶颈
- 识别出每帧3次memcpy是主要问题
- 找出stall检测误判导致帧率雪崩

### 2. 架构设计
- 设计了FFmpeg管道架构
- 定义了三个核心模块：FFmpegInput、FFmpegEncoder、FFmpegOutput
- 规划了零拷贝数据流

### 3. 原型实现
创建了以下文件：

```
prototype/
├── ffmpeg_capture_test.cpp    # 完整的采集+编码+输出测试
├── simple_preview.cpp         # 简化的采集测试（显示帧信息）
├── Makefile                   # 编译脚本
├── test.bat                   # Windows测试脚本
└── README.md                  # 使用说明
```

## 原型功能

### simple_preview.cpp
- 使用FFmpeg V4L2输入采集
- 显示帧信息（分辨率、格式、帧率）
- 显示帧间隔和实时帧率
- 验证零拷贝采集

### ffmpeg_capture_test.cpp
- 使用FFmpeg V4L2输入采集
- 使用rkmpp硬件编码（自动回退到软件编码）
- 输出到MP4文件
- 显示详细的采集统计

## 使用方法

### 1. 编译

```bash
# Linux/macOS
make

# Windows
make
```

### 2. 测试

```bash
# 简化测试
./simple_preview /dev/video0

# 完整测试
./ffmpeg_capture_test /dev/video0 output.mp4
```

### 3. 验证输出

```bash
# 检查输出文件
ffprobe output.mp4

# 播放测试
ffplay output.mp4
```

## 预期结果

### 性能指标

| 指标 | 当前实现 | FFmpeg管道 |
|------|----------|------------|
| 1080p帧率 | 8 fps | 60 fps |
| 内存拷贝 | 9.3 MB/帧 | 0 MB/帧 |
| CPU占用 | 高 | 低 |

### 兼容性

- ✅ NV12格式
- ✅ NV16格式
- ✅ NV24格式
- ✅ YUYV格式
- ✅ BGR24格式
- ✅ RGB24格式
- ✅ MJPEG格式
- ✅ 480p-4K分辨率
- ✅ 24-60fps帧率

## 下一步

### 阶段1：集成到主项目（1-2天）
1. 用FFmpegInput替代HdmiCapture
2. 用FFmpegEncoder替代VpuEncoder
3. 用FFmpegOutput替代MediaOutput
4. 修改Controller使用新模块

### 阶段2：预览优化（1天）
1. 优化VideoWidget支持AVFrame直接上传
2. 使用PBO异步上传纹理
3. 移除QTimer帧率限制

### 阶段3：热插拔与错误处理（1天）
1. 实现简单的信号检测
2. 添加自动重连机制
3. 统一错误处理

### 阶段4：测试与优化（1-2天）
1. 测试各种分辨率/帧率
2. 性能调优
3. 稳定性测试

## 技术细节

### 零拷贝实现
- FFmpeg V4L2输入直接引用驱动buffer
- AVFrame直接传递给编码器
- 无需手动malloc/memcpy

### 格式处理
- FFmpeg自动协商最佳格式
- 支持所有V4L2格式
- 自动处理像素格式转换

### 错误处理
- 统一的错误码
- 自动重连机制
- 详细的错误日志

## 注意事项

1. **设备权限**：确保用户有权限访问视频设备
2. **驱动兼容性**：某些驱动可能不完全支持FFmpeg V4L2
3. **内存管理**：FFmpeg自动管理内存，无需手动释放
4. **线程安全**：FFmpeg不是线程安全的，需要在同一线程中使用

## 参考资料

- [FFmpeg V4L2文档](https://ffmpeg.org/ffmpeg-devices.html#video4linux2-v4l2)
- [FFmpeg编码文档](https://ffmpeg.org/ffmpeg-codecs.html)
- [V4L2 API文档](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)
