#include "controller.h"

#include <cstdio>
#include <ctime>
#include <string>

#include "display/video_widget.h"

namespace yunlu {

Controller::Controller()
    : capture_(std::make_unique<HdmiCapture>()),
      encoder_(std::make_unique<VpuEncoder>()),
      output_(std::make_unique<MediaOutput>()) {}

Controller::~Controller() {
    stopVideo();
}

bool Controller::startCapture(const std::string& device, int w, int h, int fps) {
    if (isCapturing()) return true;
    width_ = w; height_ = h; fps_ = fps;

    // vpu 编码器输入格式即 NV12（与采集一致），此处直接开硬编
    if (!encoder_->open(w, h, fps, bitrate_, "h264_rkmpp")) {
        fprintf(stderr, "[controller] open encoder failed\n");
        return false;
    }

    encoder_->setCallback([this](const EncodedPacket& p){ onPacket(p); });

    // 采集支持 HDMI 热插拔自动恢复：只要设备能打开 start 即成功，
    // 无信号时后台自动等待，接入后自动采集。
    bool ok = capture_->start(device,
        [this](const VideoFrame& f){ onFrame(f); },
        [this](bool hasSignal, int sw, int sh){
            // 状态回调（后台线程），仅记录当前信号状态供 UI 轮询
            std::lock_guard<std::mutex> lk(stateMutex_);
            hasSignal_ = hasSignal;
            if (hasSignal) { capW_ = sw; capH_ = sh; }
        });
    if (!ok) {
        encoder_->close();
        return false;
    }
    return true;
}

void Controller::stopVideo() {
    if (capture_) capture_->stop();
    stopRecording();
    stopStreaming();
    if (encoder_) encoder_->close();
}

void Controller::onFrame(const VideoFrame& f) {
    // FPS 计算
    fpsFrameCount_++;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t nowUs = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    if (fpsLastTimeUs_ == 0) fpsLastTimeUs_ = nowUs;
    if (nowUs - fpsLastTimeUs_ >= 1000000) {
        captureFps_ = fpsFrameCount_ * 1000000.0 / (nowUs - fpsLastTimeUs_);
        fpsFrameCount_ = 0;
        fpsLastTimeUs_ = nowUs;
    }

    // 分辨率变化：仅记录日志，不重配编码器（避免线程竞态崩溃）
    if (f.width != width_ || f.height != height_) {
        static bool logged = false;
        if (!logged) {
            fprintf(stderr, "[controller] resolution mismatch: %dx%d vs encoder %dx%d, skipping encode\n",
                    f.width, f.height, width_, height_);
            logged = true;
        }
    }

    // 1) 编码（仅当帧尺寸与编码器一致）
    if (encoder_->isOpen() && f.width == width_ && f.height == height_) {
        encoder_->pushFrame(f.y, f.uv, f.linesize_y, f.linesize_uv,
                            f.width, f.height, f.pts_us);
    }
    // 2) 预览
    if (preview_)
        preview_->submitFrame(f);
}

void Controller::onPacket(const EncodedPacket& p) {
    output_->writePacket(p.data, p.size, p.pts_us, p.dts_us, p.keyframe, p.ctx);
}

bool Controller::startRecording(const std::string& path) {
    if (isRecording() || !encoder_->isOpen()) return false;
    return output_->openRecorder(path, encoder_->codecCtx());
}

bool Controller::startStreaming(const std::string& url) {
    if (isStreaming() || !encoder_->isOpen()) return false;
    return output_->openStream(url, encoder_->codecCtx());
}

void Controller::stopRecording() {
    output_->closeAll();   // 录制+推流一起写尾
}

void Controller::stopStreaming() {
    output_->closeAll();
}

bool Controller::isCapturing() const { return capture_->isCapturing(); }
bool Controller::hasSignal() const {
    std::lock_guard<std::mutex> lk(stateMutex_);
    return hasSignal_;
}
int Controller::captureWidth() const {
    int w = width_;
    { std::lock_guard<std::mutex> lk(stateMutex_); if (hasSignal_ && capW_ > 0) w = capW_; }
    return w;
}
int Controller::captureHeight() const {
    int h = height_;
    { std::lock_guard<std::mutex> lk(stateMutex_); if (hasSignal_ && capH_ > 0) h = capH_; }
    return h;
}
bool Controller::isRecording() const { return output_->recording(); }
bool Controller::isStreaming() const { return output_->streaming(); }
uint64_t Controller::droppedFrames() const { return capture_->droppedFrames(); }

} // namespace yunlu
