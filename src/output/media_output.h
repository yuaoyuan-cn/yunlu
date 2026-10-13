#ifndef YUNLU_MEDIA_OUTPUT_H
#define YUNLU_MEDIA_OUTPUT_H

#include <string>
#include <atomic>
#include <mutex>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace yunlu {

// 将编码后的 H.264 流同时写入 MP4 文件和 RTMP 推流
class MediaOutput {
public:
    MediaOutput();
    ~MediaOutput();

    // 从一个已打开的编码器(AVCodecContext)初始化输出
    bool openRecorder(const std::string& path, const AVCodecContext* encCtx);
    bool openStream(const std::string& rtmpUrl, const AVCodecContext* encCtx);

    void writePacket(const uint8_t* data, int size,
                     int64_t ptsUs, int64_t dtsUs, bool keyframe,
                     const AVCodecContext* encCtx);
    void closeRecorder();
    void closeStream();
    void closeAll();

    // 录制分段：关闭当前录制文件并按新路径重开（用于单文件过大时自动切分）
    bool rotateRecorder(const std::string& path, const AVCodecContext* encCtx);

    bool recording() const { return rec_.open.load(); }
    bool streaming() const { return str_.open.load(); }
    int64_t recorderBytes() const { return rec_.bytes.load(); }

private:
    struct Sink {
        AVFormatContext* fmt = nullptr;
        AVStream* st = nullptr;
        int64_t startUs = 0;
        std::mutex mtx;
        std::atomic<bool> open{false};
        std::atomic<int64_t> bytes{0};
        std::string path;
    };

    Sink rec_, str_;
    std::atomic<bool> anyOpen_{false};

    static bool openSink(Sink& s, const std::string& url,
                         const AVCodecContext* encCtx);
    static void closeSink(Sink& s);
    void pushToSink(Sink& s, const uint8_t* data, int size,
                    int64_t ptsUs, int64_t dtsUs, bool keyframe,
                    const AVCodecContext* encCtx);
};

} // namespace yunlu

#endif // YUNLU_MEDIA_OUTPUT_H
