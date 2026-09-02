#ifndef YUNLU_VPU_ENCODER_H
#define YUNLU_VPU_ENCODER_H

#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace yunlu {

struct EncodedPacket {
    const uint8_t* data = nullptr;
    int size = 0;
    int64_t pts_us = 0;
    int64_t dts_us = 0;
    bool keyframe = false;
    AVCodecContext* ctx = nullptr; // 供 muxer 获取 extradata/编码信息
};

using EncodedCallback = std::function<void(const EncodedPacket&)>;

class VpuEncoder {
public:
    VpuEncoder();
    ~VpuEncoder();

    // 打开 rkmpp 硬编。width/height/fps/bitrate/codec("h264"/"hevc")
    bool open(int width, int height, int fps, int bitrate, const char* codec);
    void close();

    // 送入一帧 NV12（sw frame），内部异步编码。按引用拷贝由调用方保证存活。
    void pushFrame(const uint8_t* y, const uint8_t* uv,
                   int linesizeY, int linesizeUV,
                   int width, int height, int64_t ptsUs);

    bool isOpen() const { return open_.load(); }

    // 回调输出编码后的包（线程内）
    void setCallback(EncodedCallback cb) { onPacket_ = std::move(cb); }

    // 供 muxer 获取编码器信息（extradata / timebase / codecpar）
    const AVCodecContext* codecCtx() const { return ctx_; }

    static const char* supportedName() { return "rkmpp"; }

private:
    void encodeLoop();
    void drain();

    AVCodecContext* ctx_ = nullptr;
    AVFrame* enc_frame_ = nullptr;
    AVPacket* av_pkt_   = nullptr;

    std::thread thread_;
    std::atomic<bool> open_{false};
    std::atomic<bool> drain_{false};
    std::function<void(const EncodedPacket&)> onPacket_;

    // 帧队列（简单环形缓冲，单生产者单消费者）
    struct QItem {
        uint8_t* planar = nullptr; // y 与 uv 拷贝到连续平面
        int linesizeY = 0, linesizeUV = 0;
        int width = 0, height = 0;
        int64_t ptsUs = 0;
    };
    static constexpr int kQueue = 8;
    QItem queue_[kQueue];
    int head_ = 0, tail_ = 0;      // 仅在各自线程访问
    std::mutex mtx_;
    std::condition_variable cv_;
    void enqueue(const QItem&);
};

} // namespace yunlu

#endif // YUNLU_VPU_ENCODER_H
