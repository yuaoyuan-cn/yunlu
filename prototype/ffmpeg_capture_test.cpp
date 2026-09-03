/**
 * FFmpeg管道架构原型测试
 * 
 * 功能：
 * 1. 使用FFmpeg V4L2输入采集
 * 2. 使用rkmpp硬件编码（自动回退到软件编码）
 * 3. 输出到MP4文件
 * 
 * 编译：
 * g++ -o ffmpeg_capture_test ffmpeg_capture_test.cpp -lavformat -lavcodec -lavutil -lswscale
 * 
 * 运行：
 * ./ffmpeg_capture_test /dev/video0 output.mp4
 */

#include <cstdio>
#include <cstring>
#include <csignal>
#include <unistd.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

static volatile bool running = true;

void signal_handler(int sig) {
    running = false;
}

// 打开V4L2设备
AVFormatContext* open_v4l2_device(const char* device, int& video_stream_idx) {
    AVFormatContext* ctx = avformat_alloc_context();
    if (!ctx) {
        fprintf(stderr, "无法分配格式上下文\n");
        return nullptr;
    }
    
    // V4L2输入选项
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", "1920x1080", 0);
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "input_format", "nv12", 0);
    
    // 打开设备
    int ret = avformat_open_input(&ctx, device, nullptr, &options);
    av_dict_free(&options);
    
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        fprintf(stderr, "无法打开设备 %s: %s\n", device, err);
        avformat_free_context(ctx);
        return nullptr;
    }
    
    // 获取流信息
    ret = avformat_find_stream_info(ctx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "无法获取流信息\n");
        avformat_close_input(&ctx);
        return nullptr;
    }
    
    // 查找视频流
    video_stream_idx = -1;
    for (unsigned i = 0; i < ctx->nb_streams; i++) {
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    
    if (video_stream_idx < 0) {
        fprintf(stderr, "未找到视频流\n");
        avformat_close_input(&ctx);
        return nullptr;
    }
    
    AVCodecParameters* par = ctx->streams[video_stream_idx]->codecpar;
    fprintf(stderr, "输入: %dx%d, 格式=%d, 帧率=%f\n",
            par->width, par->height, par->format,
            av_q2d(ctx->streams[video_stream_idx]->r_frame_rate));
    
    return ctx;
}

// 打开编码器
AVCodecContext* open_encoder(int width, int height, int fps, int bitrate) {
    // 尝试rkmpp编码器
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_rkmpp");
    if (!codec) {
        fprintf(stderr, "rkmpp不可用，使用软件编码器\n");
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!codec) {
        fprintf(stderr, "未找到H264编码器\n");
        return nullptr;
    }
    
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        fprintf(stderr, "无法分配编码器上下文\n");
        return nullptr;
    }
    
    ctx->width = width;
    ctx->height = height;
    ctx->bit_rate = bitrate;
    ctx->time_base = (AVRational){1, fps};
    ctx->framerate = (AVRational){fps, 1};
    ctx->gop_size = fps * 2;
    ctx->max_b_frames = 0;
    ctx->pix_fmt = AV_PIX_FMT_NV12;
    
    // rkmpp特定选项
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(ctx->priv_data, "profile", "high", 0);
    }
    
    int ret = avcodec_open2(ctx, codec, nullptr);
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        fprintf(stderr, "无法打开编码器: %s\n", err);
        avcodec_free_context(&ctx);
        return nullptr;
    }
    
    fprintf(stderr, "编码器: %s, %dx%d, fps=%d, bitrate=%d\n",
            codec->name, width, height, fps, bitrate);
    
    return ctx;
}

// 打开输出文件
AVFormatContext* open_output(const char* filename, AVCodecContext* enc_ctx, AVStream* in_stream) {
    AVFormatContext* ctx = nullptr;
    int ret = avformat_alloc_output_context2(&ctx, nullptr, nullptr, filename);
    if (ret < 0 || !ctx) {
        fprintf(stderr, "无法创建输出上下文\n");
        return nullptr;
    }
    
    // 创建输出流
    AVStream* out_stream = avformat_new_stream(ctx, nullptr);
    if (!out_stream) {
        fprintf(stderr, "无法创建输出流\n");
        avformat_free_context(ctx);
        return nullptr;
    }
    
    // 复制编码器参数
    ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    if (ret < 0) {
        fprintf(stderr, "无法复制编码器参数\n");
        avformat_free_context(ctx);
        return nullptr;
    }
    
    out_stream->time_base = enc_ctx->time_base;
    
    // 打开输出文件
    if (!(ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "无法打开输出文件 %s: %s\n", filename, err);
            avformat_free_context(ctx);
            return nullptr;
        }
    }
    
    // 写入文件头
    ret = avformat_write_header(ctx, nullptr);
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        fprintf(stderr, "无法写入文件头: %s\n", err);
        if (ctx->pb) avio_closep(&ctx->pb);
        avformat_free_context(ctx);
        return nullptr;
    }
    
    fprintf(stderr, "输出: %s\n", filename);
    return ctx;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "用法: %s <视频设备> <输出文件>\n", argv[0]);
        fprintf(stderr, "示例: %s /dev/video0 output.mp4\n", argv[0]);
        return 1;
    }
    
    const char* device = argv[1];
    const char* output_file = argv[2];
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化FFmpeg
    avformat_network_init();
    
    // 打开V4L2设备
    int video_stream_idx = -1;
    AVFormatContext* input_ctx = open_v4l2_device(device, video_stream_idx);
    if (!input_ctx) {
        return 1;
    }
    
    AVStream* in_stream = input_ctx->streams[video_stream_idx];
    AVCodecParameters* in_par = in_stream->codecpar;
    
    // 打开编码器
    int fps = 30;
    if (in_stream->r_frame_rate.num > 0) {
        fps = in_stream->r_frame_rate.num / in_stream->r_frame_rate.den;
    }
    AVCodecContext* enc_ctx = open_encoder(in_par->width, in_par->height, fps, 8000000);
    if (!enc_ctx) {
        avformat_close_input(&input_ctx);
        return 1;
    }
    
    // 打开输出
    AVFormatContext* output_ctx = open_output(output_file, enc_ctx, in_stream);
    if (!output_ctx) {
        avcodec_free_context(&enc_ctx);
        avformat_close_input(&input_ctx);
        return 1;
    }
    
    // 分配帧和包
    AVFrame* frame = av_frame_alloc();
    frame->format = enc_ctx->pix_fmt;
    frame->width = enc_ctx->width;
    frame->height = enc_ctx->height;
    av_frame_get_buffer(frame, 0);
    
    AVPacket* pkt = av_packet_alloc();
    
    // 主循环
    int64_t frame_count = 0;
    int64_t start_time = av_gettime();
    
    fprintf(stderr, "开始采集... (按Ctrl+C停止)\n");
    
    while (running) {
        // 读取一帧
        int ret = av_read_frame(input_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                continue;
            }
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "读取帧错误: %s\n", err);
            break;
        }
        
        // 只处理视频流
        if (pkt->stream_index != video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }
        
        // 发送到编码器
        ret = avcodec_send_packet(enc_ctx, pkt);
        av_packet_unref(pkt);
        
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "发送到编码器错误: %s\n", err);
            continue;
        }
        
        // 接收编码后的帧
        while (true) {
            ret = avcodec_receive_packet(enc_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                fprintf(stderr, "接收编码帧错误: %s\n", err);
                break;
            }
            
            // 写入输出文件
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc_ctx->time_base, output_ctx->streams[0]->time_base);
            
            ret = av_interleaved_write_frame(output_ctx, pkt);
            if (ret < 0) {
                char err[256];
                av_strerror(ret, err, sizeof(err));
                fprintf(stderr, "写入帧错误: %s\n", err);
            }
            
            av_packet_unref(pkt);
            frame_count++;
            
            // 每100帧打印一次状态
            if (frame_count % 100 == 0) {
                int64_t elapsed = (av_gettime() - start_time) / 1000000;
                fprintf(stderr, "\r已处理: %lld 帧, %lld 秒, %.2f fps",
                        (long long)frame_count, (long long)elapsed,
                        elapsed > 0 ? (double)frame_count / elapsed : 0);
                fflush(stderr);
            }
        }
    }
    
    fprintf(stderr, "\n采集结束\n");
    
    // 清理
    av_frame_free(&frame);
    av_packet_free(&pkt);
    
    // 写入文件尾
    av_write_trailer(output_ctx);
    
    // 关闭输出
    if (output_ctx->pb) avio_closep(&output_ctx->pb);
    avformat_free_context(output_ctx);
    
    // 关闭编码器
    avcodec_free_context(&enc_ctx);
    
    // 关闭输入
    avformat_close_input(&input_ctx);
    
    avformat_network_deinit();
    
    fprintf(stderr, "共处理 %lld 帧\n", (long long)frame_count);
    
    return 0;
}
