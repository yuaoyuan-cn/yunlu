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

bool Controller::startCapture(const std::string& device, int w, int h, int fps, int bitrate) {
    if (isCapturing()) return true;
    width_ = w; height_ = h; fps_ = fps;
    if (bitrate > 0) bitrate_ = bitrate;

    // 编码器不在此处打开：等采集到第一帧后，按"实际信号分辨率"打开。
    // settings 里的宽高只作兜底，HDMI 真实分辨率由信号决定。
    bool ok = capture_->start(device,
        [this](const VideoFrame& f){ onFrame(f); },
        [this](bool hasSignal, int sw, int sh){
            // 状态回调（后台线程），仅记录当前信号状态供 UI 轮询
            std::lock_guard<std::mutex> lk(stateMutex_);
            hasSignal_ = hasSignal;
            if (hasSignal) { capW_ = sw; capH_ = sh; }
        });
    return ok;
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

    // 按实际帧尺寸打开/重开编码器。全部在采集线程内执行，
    // 与编码器内部线程串行，避免此前跨线程 close/open 导致的崩溃。
    if (f.width > 0 && f.height > 0 &&
        (!encoder_->isOpen() || f.width != width_ || f.height != height_)) {
        encoder_->close();
        if (encoder_->open(f.width, f.height, fps_, bitrate_, "h264_rkmpp")) {
            encoder_->setCallback([this](const EncodedPacket& p){ onPacket(p); });
            width_ = f.width; height_ = f.height;
            resolutionChanged_ = true;
            fprintf(stderr, "[controller] encoder opened %dx%d\n", f.width, f.height);
        } else {
            fprintf(stderr, "[controller] encoder open failed %dx%d\n", f.width, f.height);
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

bool Controller::rotateRecording(const std::string& path) {
    if (!encoder_->isOpen()) return false;
    return output_->rotateRecorder(path, encoder_->codecCtx());
}

bool Controller::startStreaming(const std::string& url) {
    if (isStreaming() || !encoder_->isOpen()) return false;
    return output_->openStream(url, encoder_->codecCtx());
}

void Controller::stopRecording() {
    output_->closeRecorder();   // 只停录制，不影响推流
}

void Controller::stopStreaming() {
    output_->closeStream();     // 只停推流，不影响录制
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

bool Controller::consumeResolutionChanged() {
    return resolutionChanged_.exchange(false);
}

} // namespace yunlu
