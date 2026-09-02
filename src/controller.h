#ifndef YUNLU_CONTROLLER_H
#define YUNLU_CONTROLLER_H

#include <string>
#include <atomic>
#include <memory>
#include <mutex>

#include "capture/hdmi_capture.h"
#include "encoder/vpu_encoder.h"
#include "output/media_output.h"
#include "common.h"

class VideoWidget;

namespace yunlu {

// 串起采集 -> 编码 -> (录制 + 推流 + 预览) 的控制器
class Controller {
public:
    Controller();
    ~Controller();

    void setPreview(VideoWidget* w) { preview_ = w; }

    bool startCapture(const std::string& device, int w, int h, int fps);
    void stopVideo();

    bool startRecording(const std::string& path);
    bool startStreaming(const std::string& url);
    void stopRecording();
    void stopStreaming();

    bool isCapturing() const;
    bool isRecording() const;
    bool isStreaming() const;

    bool hasSignal() const;
    int captureWidth() const;
    int captureHeight() const;

    uint64_t droppedFrames() const;
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void onFrame(const VideoFrame& f);
    void onPacket(const EncodedPacket& p);

    std::unique_ptr<HdmiCapture> capture_;
    std::unique_ptr<VpuEncoder>  encoder_;
    std::unique_ptr<MediaOutput> output_;
    VideoWidget* preview_ = nullptr;

    int width_ = 1920, height_ = 1080, fps_ = 30;
    int bitrate_ = 8 * 1000 * 1000;

    // 采集状态（由采集后台线程更新，UI 轮询读取）
    mutable std::mutex stateMutex_;
    bool hasSignal_ = false;
    int capW_ = 0, capH_ = 0;
};

} // namespace yunlu

#endif // YUNLU_CONTROLLER_H
