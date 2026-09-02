#ifndef YUNLU_HDMI_CAPTURE_H
#define YUNLU_HDMI_CAPTURE_H

#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>

#include <linux/videodev2.h>

#include "../common.h"

extern "C" {
#include <libswscale/swscale.h>
}

namespace yunlu {

// 帧回调：y/uv 指针 + 尺寸。指针是采集层内部 scratch（本帧有效），调用方应即时拷贝。
using CaptureCallback = std::function<void(const VideoFrame&)>;
using CaptureStateCallback = std::function<void(bool hasSignal, int w, int h)>;

// 支持 HDMI-in 热插拔自动恢复的采集器。
// start() 只要设备能打开即返回 true（无信号也能启动），后台线程：
//   - 周期 VIDIOC_QUERY_DV_TIMINGS 检测接入/断开/分辨率变化
//   - 接入 -> 自动以驱动当前支持的格式(S_FMT/G_FMT)开流抓帧
//   - 用 libswscale 统一转为 NV12 后回调（BGR3/NV12 源均可正确输出）
//   - 拔出 -> 自动停流释放，等待下次接入
class HdmiCapture {
public:
    HdmiCapture();
    ~HdmiCapture();

    // device="/dev/video0"。onState 可选状态回调（可在任意线程触发，勿重活）。
    bool start(const std::string& device,
               CaptureCallback onFrame, CaptureStateCallback onState = nullptr);
    void stop();

    bool isRunning() const { return running_.load(); }
    bool isCapturing() const { return streaming_.load(); }
    int width()  const { return width_.load();  }
    int height() const { return height_.load(); }

private:
    void monitorRun();

    bool startCapStream();
    void closeCapStream();
    bool openDevice();
    void closeDevice();
    bool initSwsConversion();
    void grabFrames(int timeoutMs);
    void maybeLogFlow();

    int fd_ = -1;
    std::string device_;

    // 信号尺寸与源格式
    std::atomic<int> width_{0};
    std::atomic<int> height_{0};
    uint32_t srcFmt_ = 0;            // 驱动实际像素格式(BGR3/NV12/NV24/NV16)
    int srcBytesPerLine_ = 0;

    // mmap 缓冲（驱动全部为单平面）：plane_map_[buf]
    void**  plane_map_ = nullptr;
    size_t* plane_len_ = nullptr;
    size_t  bufLen_    = 0;
    unsigned int buf_count_ = 0;

    // libswscale: 源(单平面) -> NV12 供编码/预览
    SwsContext* sws_ = nullptr;
    uint8_t* swsYuv_ = nullptr;          // 输出 NV12 连续缓冲
    int convW_ = 0, convH_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> streaming_{false};
    std::thread thread_;
    CaptureCallback onFrame_;
    CaptureStateCallback onState_;

    int64_t frameCounter_ = 0;
    int64_t basePtsUs_    = 0;

    // 调试：跟踪重连后的取帧是否恢复（用于排查"重连后预览冻结"）
    int64_t lastFlowLogUs_  = 0;
    int64_t framesThisWindow_ = 0;
    int     reopenSeq_ = 0;
    int     signalOkStreak_ = 0;   // 连续 QUERY_DV_TIMINGS 成功次数（用于等信号稳定）
    uint64_t lastFrameUs_ = 0;     // 最近一次成功取到帧的时间（用于检测"流卡死不帧"）
    uint64_t streamStartedUs_ = 0; // 最近一次开流成功的时间
    int     zeroFrameWindows_ = 0; // 连续"有信号但一帧未取到"的 flow 窗口数
    int     rebuilds_ = 0;         // 连续重建次数（用于打印/限速）

    std::atomic<uint64_t> dropped_{0};
public:
    uint64_t droppedFrames() const { return dropped_.load(); }
};

} // namespace yunlu

#endif
