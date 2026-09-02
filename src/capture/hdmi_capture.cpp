#include "hdmi_capture.h"

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mem.h>
}

namespace yunlu {

namespace {
int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}
uint64_t nowUs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
// 源像素格式(单平面)-> swscale 源像素格式
AVPixelFormat srcToAvFmt(uint32_t pf) {
    switch (pf) {
        case V4L2_PIX_FMT_BGR24: return AV_PIX_FMT_BGR24;
        case V4L2_PIX_FMT_RGB24: return AV_PIX_FMT_RGB24;
        case V4L2_PIX_FMT_NV12:  return AV_PIX_FMT_NV12;
        case V4L2_PIX_FMT_NV16:  return AV_PIX_FMT_NV16;
        case V4L2_PIX_FMT_NV24:  return AV_PIX_FMT_NV24;
        case V4L2_PIX_FMT_YUYV:  return AV_PIX_FMT_YUYV422;
        default:                 return AV_PIX_FMT_NONE;
    }
}
}

HdmiCapture::HdmiCapture() = default;
HdmiCapture::~HdmiCapture() { stop(); }

bool HdmiCapture::start(const std::string& device,
                        CaptureCallback onFrame, CaptureStateCallback onState) {
    if (running_.load()) return false;

    device_   = device;
    onFrame_  = std::move(onFrame);
    onState_  = std::move(onState);
    frameCounter_ = 0;
    basePtsUs_    = 0;

    running_ = true;
    thread_ = std::thread(&HdmiCapture::monitorRun, this);
    fprintf(stderr, "[capture] started (device %s, waiting for HDMI signal)\n",
            device.c_str());
    return true;
}

bool HdmiCapture::openDevice() {
    if (fd_ >= 0) return true;
    fd_ = ::open(device_.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) {
        fprintf(stderr, "[capture] open %s failed: %s\n", device_.c_str(), strerror(errno));
        return false;
    }
    return true;
}

void HdmiCapture::closeDevice() {
    closeCapStream();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

// 读取/锁定驱动当前格式。rk_hdmirx 由信号自身决定宽高与通常的像素格式
// （BGR3/NV12 等），S_FMT 若被拒(EINVAL)则沿用驱动现有格式(VIDIOC_G_FMT)。
bool HdmiCapture::startCapStream() {
    if (streaming_.load()) return true;

    int w = width_.load(), h = height_.load();
    if (w <= 0 || h <= 0) return false;

    // 尝试 S_FMT 到我们首选(依次)格式；失败则接受驱动 G_FMT 现有格式
    const uint32_t candidates[] = { V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_BGR24 };
    uint32_t chosen = 0;
    for (uint32_t pf : candidates) {
        struct v4l2_format f;
        memset(&f, 0, sizeof(f));
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        f.fmt.pix_mp.pixelformat = pf;
        f.fmt.pix_mp.width  = (unsigned)w;
        f.fmt.pix_mp.height = (unsigned)h;
        f.fmt.pix_mp.field  = V4L2_FIELD_NONE;
        f.fmt.pix_mp.num_planes = 1;
        if (xioctl(fd_, VIDIOC_S_FMT, &f) == 0) {
            chosen = f.fmt.pix_mp.pixelformat;
            break;
        }
    }
    if (chosen == 0) {
        // 驱动拒绝所有尝试 -> 读取驱动当前实际格式
        struct v4l2_format f;
        memset(&f, 0, sizeof(f));
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (xioctl(fd_, VIDIOC_G_FMT, &f) == 0) {
            chosen = f.fmt.pix_mp.pixelformat;
            w = f.fmt.pix_mp.width;
            h = f.fmt.pix_mp.height;
        }
    }
    if (chosen == 0) {
        fprintf(stderr, "[capture] 无法取得采集格式 (信号端未就绪)\n");
        return false;
    }

    srcFmt_ = chosen;
    width_  = w;
    height_ = h;
    fprintf(stderr, "[capture] fmt: 0x%08x %dx%d\n", srcFmt_, w, h);

    // 重新读一次当前格式拿 bytesperline / sizeimage
    {
        struct v4l2_format f;
        memset(&f, 0, sizeof(f));
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (xioctl(fd_, VIDIOC_G_FMT, &f) == 0)
            srcBytesPerLine_ = f.fmt.pix_mp.plane_fmt[0].bytesperline;
    }
    if (srcBytesPerLine_ <= 0) srcBytesPerLine_ = w * 3;  // BGR3 兜底

    if (!initSwsConversion()) return false;

    // 申请 mmap 缓冲（驱动为单平面，每 buffer 一块连续内存）
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) != 0) {
        fprintf(stderr, "[capture] REQBUFS failed: %s\n", strerror(errno));
        return false;
    }
    buf_count_ = req.count;
    plane_map_ = new void*[buf_count_]();   // 零初始化：防止部分失败时 close 误 munmap
    plane_len_ = new size_t[buf_count_]();
    bufLen_ = 0;

    for (unsigned i = 0; i < buf_count_; i++) {
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 1;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) != 0) {
            fprintf(stderr, "[capture] QUERYBUF %u failed: %s\n", i, strerror(errno));
            closeCapStream();
            return false;
        }
        void* m = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd_, planes[0].m.mem_offset);
        if (m == MAP_FAILED) {
            fprintf(stderr, "[capture] mmap buf%u failed: %s\n", i, strerror(errno));
            closeCapStream();
            return false;
        }
        plane_map_[i] = m;
        plane_len_[i] = planes[0].length;
        if (planes[0].length > bufLen_) bufLen_ = planes[0].length;
    }

    // QBUF 全部 + STREAMON
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (unsigned i = 0; i < buf_count_; i++) {
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        buf.m.planes = planes;
        buf.length   = 1;
        planes[0].length = plane_len_[i];
        if (xioctl(fd_, VIDIOC_QBUF, &buf) != 0) {
            fprintf(stderr, "[capture] QBUF %u failed: %s\n", i, strerror(errno));
            closeCapStream();
            return false;
        }
    }
    if (xioctl(fd_, VIDIOC_STREAMON, &type) != 0) {
        fprintf(stderr, "[capture] STREAMON failed: %s\n", strerror(errno));
        closeCapStream();
        return false;
    }

    frameCounter_ = 0;
    basePtsUs_ = 0;
    reopenSeq_++;
    lastFlowLogUs_ = nowUs();
    framesThisWindow_ = 0;
    lastFrameUs_ = 0;   // 新流：等取到第一帧后再启用卡死检测
    streamStartedUs_ = nowUs();
    streaming_ = true;
    fprintf(stderr, "[capture] streaming on %dx%d (swscale->NV12)\n",
            width_.load(), height_.load());
    return true;
}

bool HdmiCapture::initSwsConversion() {
    AVPixelFormat src = srcToAvFmt(srcFmt_);
    if (src == AV_PIX_FMT_NONE) {
        fprintf(stderr, "[capture] 不支持的源格式 0x%08x\n", srcFmt_);
        return false;
    }
    if (sws_) sws_freeContext(sws_);
    sws_ = sws_getContext(width_, height_, src,
                          width_, height_, AV_PIX_FMT_NV12,
                          SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) {
        fprintf(stderr, "[capture] sws_getContext failed\n");
        return false;
    }
    if (swsYuv_) { av_free(swsYuv_); swsYuv_ = nullptr; }
    int ysz = width_ * height_;
    int uvsz = width_ * (height_ / 2);
    swsYuv_ = (uint8_t*)av_malloc((size_t)ysz + uvsz);
    convW_ = width_; convH_ = height_;
    return swsYuv_ != nullptr;
}

void HdmiCapture::closeCapStream() {
    if (fd_ < 0) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);

    if (plane_map_) {
        for (unsigned i = 0; i < buf_count_; i++)
            if (plane_map_[i]) munmap(plane_map_[i], plane_len_ ? plane_len_[i] : bufLen_);
        delete[] plane_map_;
        delete[] plane_len_;
        plane_map_ = nullptr;
        plane_len_ = nullptr;
    }

    // 显式释放驱动缓冲，避免下次 REQBUFS 复用旧状态
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    xioctl(fd_, VIDIOC_REQBUFS, &req);
    buf_count_ = 0;
    srcFmt_ = 0;
    if (streaming_.exchange(false))
        fprintf(stderr, "[capture] streaming stopped (signal lost)\n");
}

void HdmiCapture::grabFrames(int timeoutMs) {
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (r <= 0 || !FD_ISSET(fd_, &fds)) return;

    for (;;) {
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.length   = 1;

        if (xioctl(fd_, VIDIOC_DQBUF, &buf) != 0) {
            if (errno == EAGAIN) return;
            if (errno == EIO) { continue; }
            fprintf(stderr, "[capture] DQBUF error: %s\n", strerror(errno));
            return;
        }

        if (onFrame_ && planes[0].bytesused > 0) {
            const uint8_t* src = (const uint8_t*)plane_map_[buf.index];
            int w = width_.load(), h = height_.load();
            int ysz = w * h;

            uint8_t* dstY  = swsYuv_;
            uint8_t* dstUV = swsYuv_ + ysz;

            if (srcFmt_ == V4L2_PIX_FMT_NV12) {
                // 已是 NV12（连续单缓冲）：Y 在头，UV 紧跟其后
                memcpy(dstY,  src, (size_t)w * h);
                memcpy(dstUV, src + (size_t)w * h, (size_t)w * (h / 2));
            } else {
                // 其它格式统一经 swscale 转 NV12。
                // 用 av_image_fill_linesizes 根据源像像格式得到各平面每行字节数，
                // 并按连续单缓冲的平面偏移自动切分 Y/UV 平面（NV24/NV16 等多平面源）。
                AVPixelFormat avsrc = srcToAvFmt(srcFmt_);
                int  lsz[4] = {0,0,0,0};
                int  nb = av_pix_fmt_count_planes(avsrc);
                if (nb <= 0) { fprintf(stderr, "[capture] 无法确定源平面数\n"); continue; }
                if (av_image_fill_linesizes(lsz, avsrc, w) < 0) {
                    fprintf(stderr, "[capture] fill_linesizes failed\n");
                    continue;
                }
                for (int p = 0; p < nb; p++) if (lsz[p] <= 0) lsz[p] = 1;

                const uint8_t* sdata[4]  = {0};
                int            ss[4]     = {0};
                // 单平面(打包)源：stride 用驱动 bytesperline（无则按 Y 平面大小估）
                if (nb == 1) {
                    sdata[0] = src;
                    int stride = srcBytesPerLine_ > 0 ? srcBytesPerLine_ : lsz[0];
                    ss[0] = stride > 0 ? stride : (w * 3);
                } else {
                    size_t off = 0;
                    for (int p = 0; p < nb; p++) {
                        sdata[p] = src + off;
                        ss[p] = lsz[p];
                        off += (size_t)lsz[p] * h;
                    }
                }

                uint8_t* ddata[2] = { dstY, dstUV };
                int ds[2]         = { w, w };
                sws_scale(sws_, sdata, ss, 0, h, ddata, ds);
            }

            VideoFrame fr;
            fr.y   = dstY;
            fr.uv  = dstUV;
            fr.width  = w;
            fr.height = h;
            fr.linesize_y  = w;
            fr.linesize_uv = w;
            if (frameCounter_ == 0) basePtsUs_ = nowUs();
            fr.pts_us = nowUs() - basePtsUs_;
            frameCounter_++;
            onFrame_(fr);
            framesThisWindow_++;
            lastFrameUs_ = nowUs();
        }

        if (xioctl(fd_, VIDIOC_QBUF, &buf) != 0) {
            fprintf(stderr, "[capture] QBUF error: %s\n", strerror(errno));
            return;
        }
    }
}

void HdmiCapture::maybeLogFlow() {
    if (!streaming_.load()) return;
    uint64_t t = nowUs();
    if (t - lastFlowLogUs_ >= 2000000) {
        fprintf(stderr, "[capture] flow: seq=%d frames_in_2s=%lld streaming_on width=%dx%d\n",
                reopenSeq_, (long long)framesThisWindow_,
                width_.load(), height_.load());
        if (framesThisWindow_ == 0)
            zeroFrameWindows_++;
        else
            zeroFrameWindows_ = 0;
        lastFlowLogUs_ = t;
        framesThisWindow_ = 0;
    }
}

void HdmiCapture::monitorRun() {
    int64_t lastOpenAttemptUs = 0;
    while (running_.load()) {
        uint64_t now = nowUs();

        // 确保设备已打开（拔线后 closeDevice 会置 fd=-1，需重新打开）
        if (fd_ < 0 && !openDevice()) {
            usleep(200 * 1000);
            continue;
        }

        struct v4l2_dv_timings t;
        memset(&t, 0, sizeof(t));
        int r = xioctl(fd_, VIDIOC_QUERY_DV_TIMINGS, &t);

        if (r != 0) {
            // 信号丢失：停流并完全关闭设备；下次接入时重新打开(全新 open)
            if (streaming_.load()) { closeDevice(); if (onState_) onState_(false,0,0); }
            else if (fd_ < 0) { /* 已关 */ }
            usleep(200 * 1000);
            continue;
        }

        int tw = (int)t.bt.width, th = (int)t.bt.height;
        if (tw < 0) tw = 0;
        if (th < 0) th = 0;

        if (!streaming_.load()) {
            // 有信号但未开流。
            // 重插后驱动信号 lock 会有短暂的抖动/无效期(dmesg: timing is invalid 反复)。
            // 这里放慢一点、等信号稍稳定(>=3 个 QUERY 成功)再开流，减少在未 lock 窗口误开。
            signalOkStreak_++;
            if (signalOkStreak_ < 3 ||
                (int64_t)(now - lastOpenAttemptUs) < 150000) {
                if (tw > 0 && th > 0) { width_ = tw; height_ = th; }
                usleep(120 * 1000);
                continue;
            }
            lastOpenAttemptUs = (int64_t)now;
            if (tw > 0 && th > 0) { width_ = tw; height_ = th; }
            if (!startCapStream()) {
                signalOkStreak_ = 0;
                if (onState_) onState_(false, 0, 0);
                usleep(300 * 1000);
                continue;
            }
            if (onState_) onState_(true, width_.load(), height_.load());
            continue;
        }

        // 已开流：抓帧
        grabFrames(100);
        maybeLogFlow();

        // 流卡死自愈：已开流但长时间取不到帧(驱动 DMA 未真正起/抖动后残留)。
        // 与 v4l2-ctl 一致——每次"全新 open + 重建流"都是有效的，反复重建直到出帧。
        // 触发条件：
        //   a) 本次开流 2s 后一帧都没取到（冷启动超时）；或
        //   b) 已 3s 未取到新帧（曾出过帧但中途卡死）。
        bool needRebuild = false;
        int64_t sinceStart = (int64_t)(now - streamStartedUs_);
        if (lastFrameUs_ != 0 && (int64_t)(now - lastFrameUs_) > 3000000ull)
            needRebuild = true;
        else if (lastFrameUs_ == 0 && sinceStart > 2000000ull)
            needRebuild = true;

        if (needRebuild) {
            ++rebuilds_;
            fprintf(stderr, "[capture] stream stalled, full rebuild (#%d)\n", rebuilds_);
            signalOkStreak_ = 0;
            zeroFrameWindows_ = 0;
            closeDevice();              // 完全关闭 fd，等价 v4l2-ctl 退出后重开
            // 重建期间不置 onState(false)：避免状态栏在"等待信号/信号信息"间来回跳。
            // 真正信号丢失才由 QUERY 失败分支置 false。
            usleep(300 * 1000);
            continue;
        }
    }
}

void HdmiCapture::stop() {
    if (!running_.load() && fd_ < 0) return;

    running_ = false;
    if (thread_.joinable()) thread_.join();

    closeDevice();
    if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
    if (swsYuv_) { av_free(swsYuv_); swsYuv_ = nullptr; }

    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

} // namespace yunlu
