#include "vpu_encoder.h"

#include <cstring>
#include <cstdio>
#include <mutex>
#include <condition_variable>
#include <algorithm>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

namespace yunlu {

VpuEncoder::VpuEncoder() = default;
VpuEncoder::~VpuEncoder() { close(); }

bool VpuEncoder::open(int width, int height, int fps, int bitrate, const char* codec) {
    if (open_.load()) return false;

    const AVCodec* c = avcodec_find_encoder_by_name(codec ? codec : "h264");
    if (!c) {
        // 回退到 rkmpp
        const char* base = (codec && strcmp(codec, "hevc") == 0) ? "hevc" : "h264";
        char name[32];
        snprintf(name, sizeof(name), "%s_rkmpp", base);
        c = avcodec_find_encoder_by_name(name);
        if (!c) {
            fprintf(stderr, "[encoder] no rkmpp encoder found\n");
            return false;
        }
    }

    ctx_ = avcodec_alloc_context3(c);
    if (!ctx_) { fprintf(stderr, "[encoder] alloc ctx failed\n"); return false; }

    ctx_->width  = width;
    ctx_->height = height;
    ctx_->bit_rate = bitrate;
    // 时间基：包时间戳以微秒为单位，与采集 PTS 对齐
    ctx_->time_base = (AVRational){1, 1000000};
    ctx_->framerate = (AVRational){fps, 1};
    ctx_->gop_size  = fps * 2;                 // 2s key interval
    ctx_->pix_fmt   = AV_PIX_FMT_NV12;
    ctx_->max_b_frames = 0;
    ctx_->thread_count = 1;

    // rkmpp 常用参数：延迟/lossless 关闭、CRF 或 CBR
    av_opt_set(ctx_->priv_data, "profile", "high", 0);
    // rkmpp 编码器固定偏 CBR，gop/bitrate 已设

    if (avcodec_open2(ctx_, c, nullptr) < 0) {
        fprintf(stderr, "[encoder] open rkmpp failed\n");
        avcodec_free_context(&ctx_);
        return false;
    }

    enc_frame_ = av_frame_alloc();
    enc_frame_->format = AV_PIX_FMT_NV12;
    enc_frame_->width  = width;
    enc_frame_->height = height;
    if (av_frame_get_buffer(enc_frame_, 0) < 0) {
        fprintf(stderr, "[encoder] frame buffer alloc failed\n");
        close();
        return false;
    }

    av_pkt_ = av_packet_alloc();
    head_ = tail_ = 0;

    open_ = true;
    thread_ = std::thread(&VpuEncoder::encodeLoop, this);
    fprintf(stderr, "[encoder] opened %s_rkmpp %dx%d fps=%d bitrate=%d\n",
            codec ? codec : "h264", width, height, fps, bitrate);
    return true;
}

void VpuEncoder::enqueue(const QItem& it) {
    std::unique_lock<std::mutex> lock(mtx_);
    // 满则丢最旧
    while (((tail_ + 1) % kQueue) == head_) {
        QItem& old = queue_[head_];
        if (old.planar) { free(old.planar); old.planar = nullptr; }
        head_ = (head_ + 1) % kQueue;
    }
    queue_[tail_] = it;
    tail_ = (tail_ + 1) % kQueue;
    cv_.notify_one();
}

void VpuEncoder::pushFrame(const uint8_t* y, const uint8_t* uv,
                           int lsy, int lsuv, int width, int height, int64_t ptsUs) {
    if (!open_.load()) return;

    QItem it;
    it.width = width; it.height = height; it.ptsUs = ptsUs;
    it.linesizeY = lsy; it.linesizeUV = lsuv;
    // 编码器输入固定为 NV12 (4:2:0)：UV 平面为 lsuv * (height/2)
    size_t uvSize = (size_t)lsuv * (height / 2);
    it.planar = (uint8_t*)malloc((size_t)lsy * height + uvSize);
    if (!it.planar) return;
    memcpy(it.planar, y, (size_t)lsy * height);
    memcpy(it.planar + (size_t)lsy * height, uv, uvSize);
    enqueue(it);
}

void VpuEncoder::encodeLoop() {
    bool finished = false;
    while (!finished) {
        QItem cur;
        bool have = false;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [&] {
                bool dataAvail = head_ != tail_;
                bool quit = drain_.load() && !open_.load();
                return dataAvail || quit;
            });
            // 关闭中且队列已空 -> 退出循环
            if (head_ == tail_ && drain_.load() && !open_.load()) {
                finished = true;
                continue;
            }
            if (head_ != tail_) {
                cur = queue_[head_];
                head_ = (head_ + 1) % kQueue;
                have = true;
            }
        }
        if (have) {
            enc_frame_->pts = cur.ptsUs;
            enc_frame_->data[0] = cur.planar;
            enc_frame_->data[1] = cur.planar + (size_t)cur.linesizeY * cur.height;
            enc_frame_->linesize[0] = cur.linesizeY;
            enc_frame_->linesize[1] = cur.linesizeUV;

            int ret = avcodec_send_frame(ctx_, enc_frame_);
            if (ret < 0) {
                char err[128]; av_strerror(ret, err, sizeof(err));
                fprintf(stderr, "[encoder] send_frame: %s\n", err);
            }
            free(cur.planar);
        }

        // 尽量收包
        for (;;) {
            int r = avcodec_receive_packet(ctx_, av_pkt_);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) {
                char err[128]; av_strerror(r, err, sizeof(err));
                fprintf(stderr, "[encoder] receive_packet: %s\n", err);
                break;
            }

            if (onPacket_) {
                EncodedPacket p;
                p.data = av_pkt_->data;
                p.size = av_pkt_->size;
                p.pts_us = av_pkt_->pts;
                p.dts_us = av_pkt_->dts;
                p.keyframe = av_pkt_->flags & AV_PKT_FLAG_KEY;
                p.ctx = ctx_;
                onPacket_(p);
            }
            av_packet_unref(av_pkt_);
        }
    }
}

void VpuEncoder::drain() {
    std::lock_guard<std::mutex> lock(mtx_);
    drain_ = true;
    cv_.notify_one();
}

void VpuEncoder::close() {
    if (!open_.load()) return;

    // 先标记关闭让 encodeLoop 能在排空后退出，再通知
    open_ = false;
    drain();

    if (thread_.joinable()) thread_.join();

    if (ctx_ && avcodec_is_open(ctx_)) {
        avcodec_send_frame(ctx_, nullptr);   // flush
        for (;;) {
            int r = avcodec_receive_packet(ctx_, av_pkt_);
            if (r < 0) break;
            if (onPacket_) {
                EncodedPacket p;
                p.data = av_pkt_->data; p.size = av_pkt_->size;
                p.pts_us = av_pkt_->pts; p.dts_us = av_pkt_->dts;
                p.keyframe = av_pkt_->flags & AV_PKT_FLAG_KEY;
                p.ctx = ctx_;
                onPacket_(p);
            }
            av_packet_unref(av_pkt_);
        }
    }

    drain_ = false;

    // 释放队列残留
    for (int i = 0; i < kQueue; i++) {
        if (queue_[i].planar) { free(queue_[i].planar); queue_[i].planar = nullptr; }
    }

    if (av_pkt_) av_packet_free(&av_pkt_);
    if (enc_frame_) av_frame_free(&enc_frame_);
    if (ctx_) avcodec_free_context(&ctx_);
}

} // namespace yunlu
