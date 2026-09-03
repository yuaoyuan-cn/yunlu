# 云录 FFmpeg管道架构重构方案

## 一、当前问题分析

### 1.1 性能瓶颈
| 问题 | 影响 | 当前状态 |
|------|------|----------|
| 每帧3次memcpy | 1080p: 9.3MB/帧, 30fps=280MB/s | 严重 |
| stall检测误判 | 频繁重建，帧率雪崩 | 严重 |
| DQBUF错误 | 驱动拒绝请求，帧丢失 | 中等 |
| 预览帧率限制 | QTimer 33ms = 30fps上限 | 低 |

### 1.2 代码架构问题
- V4L2采集层过于复杂（热插拔、信号检测、格式转换）
- 编码器与采集器紧耦合
- 内存管理分散，多处malloc/memcpy
- 缺少统一的错误处理和恢复机制

## 二、FFmpeg管道架构设计

### 2.1 架构图
```
┌─────────────────────────────────────────────────────────┐
│                    main.cpp                              │
│  QApplication + Qt5 OpenGL ES 3.2 + Controller + UI    │
└──────────────────────┬──────────────────────────────────┘
                       │
          ┌────────────▼────────────┐
          │     Controller          │  串联各模块，管理生命周期
          │  (controller.cpp)       │
          └──┬─────────┬────────┬──┘
             │         │        │
    ┌────────▼──┐ ┌────▼─────┐ ┌▼──────────┐
    │FFmpegInput│ │FFmpegEnc │ │FFmpegOutput│
    │(V4L2/SDL) │ │(rkmpp)   │ │(MP4+RTMP) │
    └────┬──────┘ └────┬─────┘ └───────────┘
         │             │
         │  AVFrame    │  AVPacket
         ├─────────────┤────────────────▶ FFmpegOutput.writePacket()
         │             │
         └──────▶ VideoWidget.submitFrame()
                      │
                 ┌────▼──────────┐
                 │VideoRenderer  │
                 │(GL NV12→RGB)  │
                 └───────────────┘
```

### 2.2 核心模块设计

#### 模块1: FFmpegInput (替代HdmiCapture)
```cpp
class FFmpegInput {
public:
    bool open(const std::string& device);
    void close();
    
    // 获取下一帧（零拷贝）
    AVFrame* grabFrame(int timeoutMs);
    
    // 信号检测
    bool hasSignal() const;
    int width() const;
    int height() const;
    
private:
    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* dec_ctx_ = nullptr;
    int video_stream_idx_ = -1;
};
```

**优势**：
- FFmpeg内置V4L2支持，自动处理格式协商
- 零拷贝：AVFrame直接引用驱动buffer
- 自动处理热插拔（通过avformat_close_input/reopen）
- 支持所有V4L2格式

#### 模块2: FFmpegEncoder (替代VpuEncoder)
```cpp
class FFmpegEncoder {
public:
    bool open(int width, int height, int fps, int bitrate, const char* codec);
    void close();
    
    // 编码一帧（零拷贝）
    AVPacket* encode(AVFrame* frame);
    
private:
    AVCodecContext* ctx_ = nullptr;
};
```

**优势**：
- 直接接收AVFrame，无需memcpy
- rkmpp硬件编码
- 自动处理帧率、比特率

#### 模块3: FFmpegOutput (替代MediaOutput)
```cpp
class FFmpegOutput {
public:
    bool openRecorder(const std::string& path, AVCodecContext* enc_ctx);
    bool openStream(const std::string& url, AVCodecContext* enc_ctx);
    void close();
    
    // 写入编码包
    bool writePacket(AVPacket* pkt);
    
private:
    AVFormatContext* mp4_ctx_ = nullptr;
    AVFormatContext* rtmp_ctx_ = nullptr;
};
```

### 2.3 数据流设计

```
V4L2驱动
    │
    ▼ (mmap buffer)
FFmpegInput::grabFrame()
    │
    ▼ (AVFrame, 零拷贝引用)
Controller::onFrame()
    │
    ├───────────────────┐
    ▼                   ▼
FFmpegEncoder::encode()  VideoWidget::submitFrame()
    │                   │
    ▼                   ▼
AVPacket               GPU纹理上传
    │
    ▼
FFmpegOutput::writePacket()
    │
    ├───────────┐
    ▼           ▼
MP4文件      RTMP流
```

## 三、关键改进点

### 3.1 零拷贝内存管理
- **当前**：每帧3次memcpy (采集→编码器, 采集→预览, 编码器内部)
- **改进**：AVFrame直接引用驱动buffer，编码器直接处理AVFrame

### 3.2 智能格式处理
- **当前**：手动检测格式，使用swscale转换
- **改进**：FFmpeg自动协商最佳格式，支持所有V4L2格式

### 3.3 简化的热插拔处理
- **当前**：复杂的monitorRun线程，stall检测，频繁重建
- **改进**：FFmpeg自动检测信号丢失，简单的重连逻辑

### 3.4 统一的错误处理
- **当前**：分散的错误处理，容易遗漏
- **改进**：统一的错误码和恢复机制

## 四、实现步骤

### 阶段1: 基础框架 (1-2天)
1. 创建FFmpegInput类，实现V4L2采集
2. 创建FFmpegEncoder类，实现rkmpp编码
3. 创建FFmpegOutput类，实现MP4/RTMP输出
4. 修改Controller，使用新模块

### 阶段2: 预览优化 (1天)
1. 优化VideoWidget，支持AVFrame直接上传
2. 使用PBO异步上传纹理
3. 移除QTimer帧率限制

### 阶段3: 热插拔与错误处理 (1天)
1. 实现简单的信号检测
2. 添加自动重连机制
3. 统一错误处理

### 阶段4: 测试与优化 (1-2天)
1. 测试各种分辨率/帧率
2. 性能调优
3. 稳定性测试

## 五、兼容性设计

### 5.1 支持的输入格式
- NV12 (V4L2_PIX_FMT_NV12)
- NV16 (V4L2_PIX_FMT_NV16)
- NV24 (V4L2_PIX_FMT_NV24)
- YUYV (V4L2_PIX_FMT_YUYV)
- BGR24 (V4L2_PIX_FMT_BGR24)
- RGB24 (V4L2_PIX_FMT_RGB24)
- MJPEG (V4L2_PIX_FMT_MJPEG)

### 5.2 支持的分辨率
- 480p, 720p, 1080p, 4K
- 自动检测输入分辨率
- 动态调整编码参数

### 5.3 支持的帧率
- 24fps, 25fps, 30fps, 50fps, 60fps
- 自动检测输入帧率
- 动态调整编码帧率

## 六、性能预期

| 指标 | 当前 | 改进后 |
|------|------|--------|
| 1080p帧率 | 8fps | 60fps |
| 内存拷贝 | 9.3MB/帧 | 0MB/帧 |
| 启动时间 | ~2s | ~0.5s |
| 热插拔恢复 | ~500ms | ~100ms |
| CPU占用 | 高 | 低 |

## 七、风险评估

### 7.1 技术风险
- FFmpeg V4L2支持可能不完整
- rkmpp编码器可能有兼容性问题
- 零拷贝可能需要特殊处理

### 7.2 缓解措施
- 保留手动V4L2采集作为后备
- 使用FFmpeg的软件编码器作为备选
- 充分测试各种硬件平台

## 八、总结

FFmpeg管道架构将显著简化代码，提高性能和兼容性。主要优势：
1. 零拷贝内存管理
2. 自动格式协商
3. 简化的热插拔处理
4. 统一的错误处理
5. 更好的可维护性

预计2-3周完成重构，性能提升显著。
