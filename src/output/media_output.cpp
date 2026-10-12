#include "media_output.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
}

namespace yunlu {

namespace {
// 将 av 错误码格式化为字符串（避免 av_err2str 临时数组问题）
const char* errstr(int err, char* buf, size_t sz) {
    av_strerror(err, buf, sz);
    return buf;
}
}

MediaOutput::MediaOutput() = default;
MediaOutput::~MediaOutput() { closeAll(); }

bool MediaOutput::openSink(Sink& s, const std::string& url,
                           const AVCodecContext* encCtx) {
    if (!encCtx || !encCtx->codec || !encCtx->codec_id) return false;

    AVFormatContext* fmt = nullptr;
    // RTMP(S) 地址带查询串(?secret=...)时 FFmpeg 无法自动猜出封装格式，需显式指定 flv
    const char* fmtName = nullptr;
    if (url.compare(0, 7, "rtmp://") == 0 || url.compare(0, 8, "rtmps://") == 0)
        fmtName = "flv";
    int ret = avformat_alloc_output_context2(&fmt, nullptr, fmtName, url.c_str());
    if (ret < 0 || !fmt) {
        char eb[128];
        fprintf(stderr, "[output] alloc output ctx failed: %s\n", errstr(ret, eb, sizeof(eb)));
        return false;
    }

    AVStream* st = avformat_new_stream(fmt, nullptr);
    if (!st) {
        fprintf(stderr, "[output] new stream failed\n");
        avformat_free_context(fmt);
        return false;
    }

    // 从编码器上下文生成流参数（含 extradata：SPS/PPS）
    avcodec_parameters_from_context(st->codecpar, encCtx);
    st->codecpar->codec_tag = 0;

    // 时间基：用编码器 time_base，包时间戳按此缩放
    st->time_base = (AVRational){1, 1000000};

    if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt->pb, url.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char eb[128];
            fprintf(stderr, "[output] open '%s' failed: %s\n", url.c_str(), errstr(ret, eb, sizeof(eb)));
            avformat_free_context(fmt);
            return false;
        }
        // 本地文件：faststart 将 moov 前移，提升 Windows/mobile 播放器兼容性
        av_opt_set(fmt->priv_data, "movflags", "+faststart", 0);
    }

    ret = avformat_write_header(fmt, nullptr);
    if (ret < 0) {
        char eb[128];
        fprintf(stderr, "[output] write header failed: %s\n", errstr(ret, eb, sizeof(eb)));
        if (fmt->pb) avio_closep(&fmt->pb);
        avformat_free_context(fmt);
        return false;
    }

    s.fmt = fmt;
    s.st  = st;
    s.startUs = 0;
    s.open = true;
    fprintf(stderr, "[output] opened %s (stream time_base=%d/%d)\n",
            url.c_str(), st->time_base.num, st->time_base.den);
    return true;
}

bool MediaOutput::openRecorder(const std::string& path, const AVCodecContext* encCtx) {
    if (rec_.open.load()) return false;
    bool ok = openSink(rec_, path, encCtx);
    if (ok) anyOpen_ = true;
    return ok;
}

bool MediaOutput::openStream(const std::string& rtmpUrl, const AVCodecContext* encCtx) {
    if (str_.open.load() || rtmpUrl.empty()) return false;
    bool ok = openSink(str_, rtmpUrl, encCtx);
    if (ok) anyOpen_ = true;
    return ok;
}

void MediaOutput::pushToSink(Sink& s, const uint8_t* data, int size,
                             int64_t ptsUs, int64_t dtsUs, bool keyframe,
                             const AVCodecContext* encCtx) {
    if (!s.open.load()) return;

    AVPacket* pkt = av_packet_alloc();
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = size;
    pkt->stream_index = s.st->index;
    if (keyframe) pkt->flags |= AV_PKT_FLAG_KEY;

    // 时间戳：从编码器时间基(通常 1/1000000) 缩放到 muxer 实际的流 time_base。
    // 注意：muxer 在 avformat_write_header 时可能改写 st->time_base（FLV/RTMP 为
    // 1/1000），若不重缩放，微秒会被当成毫秒写入 -> 时间戳放大 1000 倍，
    // 表现为录制时长异常（几秒显示几万秒）、播放器反复重载。
    AVRational srcTb = encCtx ? encCtx->time_base : (AVRational){1, 1000000};
    if (srcTb.num <= 0 || srcTb.den <= 0) srcTb = (AVRational){1, 1000000};
    pkt->pts = ptsUs;
    pkt->dts = dtsUs;
    av_packet_rescale_ts(pkt, srcTb, s.st->time_base);

    {
        std::lock_guard<std::mutex> lock(s.mtx);
        if (!s.open.load() || !s.fmt) {
            av_packet_free(&pkt);
            return;
        }
        int ret = av_interleaved_write_frame(s.fmt, pkt);
        if (ret < 0) {
            char eb[128];
            fprintf(stderr, "[output] interleave write failed: %s\n", errstr(ret, eb, sizeof(eb)));
            // 网络断开时关闭对应 sink 但保留另一个
            if (s.fmt) {
                av_write_trailer(s.fmt);
                if (s.fmt->pb) avio_closep(&s.fmt->pb);
                avformat_free_context(s.fmt);
                s.fmt = nullptr;
                s.open = false;
            }
        }
    }
    av_packet_free(&pkt);
}

void MediaOutput::writePacket(const uint8_t* data, int size,
                              int64_t ptsUs, int64_t dtsUs, bool keyframe,
                              const AVCodecContext* encCtx) {
    if (rec_.open.load()) pushToSink(rec_, data, size, ptsUs, dtsUs, keyframe, encCtx);
    if (str_.open.load()) pushToSink(str_, data, size, ptsUs, dtsUs, keyframe, encCtx);
}

void MediaOutput::closeSink(Sink& s) {
    if (s.fmt) {
        std::lock_guard<std::mutex> lock(s.mtx);
        av_write_trailer(s.fmt);
        if (s.fmt->pb) avio_closep(&s.fmt->pb);
        avformat_free_context(s.fmt);
        s.fmt = nullptr;
        s.st  = nullptr;
    }
    s.open = false;
}

void MediaOutput::closeRecorder() {
    closeSink(rec_);
}

void MediaOutput::closeStream() {
    closeSink(str_);
}

void MediaOutput::closeAll() {
    closeSink(rec_);
    closeSink(str_);
    anyOpen_ = false;
}

} // namespace yunlu
