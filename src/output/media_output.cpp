#include "media_output.h"

#include <cstdio>
#include <cstring>
#include <cerrno>

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
    else if (url.size() > 4 && url.compare(url.size() - 4, 4, ".mkv") == 0)
        fmtName = "matroska";
    else if (url.size() > 4 && url.compare(url.size() - 4, 4, ".mp4") == 0)
        fmtName = "mp4";
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

    {
        std::lock_guard<std::mutex> lock(s.mtx);
        s.fmt = fmt;
        s.st  = st;
        s.startUs = 0;
        s.bytes = 0;
        s.path = url;
        s.tsBased_  = false;
        s.firstPtsUs = 0;
        s.lastPtsUs  = -1;
        s.lastDtsUs  = -1;
        s.open = true;
    }
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
    if (keyframe) pkt->flags |= AV_PKT_FLAG_KEY;

    {
        std::lock_guard<std::mutex> lock(s.mtx);
        // 注意：必须在锁内重新校验 s.st / s.fmt。
        // U 盘可能在锁外被拔出，主线程 closeSink() 会把它们置空，
        // 锁外解引用会造成空指针崩溃。
        if (!s.open.load() || !s.fmt || !s.st) {
            av_packet_free(&pkt);
            return;
        }
        pkt->stream_index = s.st->index;

        // 时间戳：以本文件/直播写入的第一个包为 0 点重新基准化。
        // 采集侧的时间轴从 HDMI 信号锁定起算，直接写文件会让时长包含
        // "录制开始前"的那段时间，导致进度条前段空白、时长比实际长。
        if (!s.tsBased_) { s.firstPtsUs = ptsUs; s.tsBased_ = true; }
        int64_t outPts = ptsUs - s.firstPtsUs;
        int64_t outDts = dtsUs - s.firstPtsUs;
        if (outPts < 0) outPts = 0;
        if (outDts < 0) outDts = 0;
        // 保证单调不回退（信号抖动/重锁时采集侧 PTS 可能倒退）
        if (s.lastPtsUs >= 0 && outPts < s.lastPtsUs) outPts = s.lastPtsUs;
        if (s.lastDtsUs >= 0 && outDts < s.lastDtsUs) outDts = s.lastDtsUs;
        // dts 不得大于 pts
        if (outDts > outPts) outDts = outPts;
        s.lastPtsUs = outPts;
        s.lastDtsUs = outDts;

        AVRational srcTb = encCtx ? encCtx->time_base : (AVRational){1, 1000000};
        if (srcTb.num <= 0 || srcTb.den <= 0) srcTb = (AVRational){1, 1000000};
        pkt->pts = outPts;
        pkt->dts = outDts;
        av_packet_rescale_ts(pkt, srcTb, s.st->time_base);

        int ret = av_interleaved_write_frame(s.fmt, pkt);
        if (ret < 0) {
            char eb[128];
            fprintf(stderr, "[output] interleave write failed: %s (已写 %.2f MB)\n",
                    errstr(ret, eb, sizeof(eb)), s.bytes.load() / (1024.0 * 1024.0));
            // 网络断开/存储拔出时关闭对应 sink 但保留另一个。
            // 注意：此时不应再调用 av_write_trailer（设备可能已失效），
            // 直接关闭文件句柄即可；MKV 追加式容器不受影响，MP4 会缺索引。
            av_write_trailer(s.fmt);
            if (s.fmt->pb) avio_closep(&s.fmt->pb);
            avformat_free_context(s.fmt);
            s.fmt = nullptr;
            s.st  = nullptr;
        } else {
            s.bytes.fetch_add(pkt->size);
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
    s.bytes = 0;
}

void MediaOutput::closeRecorder() {
    closeSink(rec_);
}

bool MediaOutput::rotateRecorder(const std::string& path, const AVCodecContext* encCtx) {
    if (recording()) {
        closeSink(rec_);
    }
    if (!openSink(rec_, path, encCtx)) return false;
    anyOpen_ = true;
    return true;
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
