/**
 * 简化的FFmpeg采集+预览测试
 * 
 * 功能：
 * 1. 使用FFmpeg V4L2输入采集
 * 2. 显示帧信息（分辨率、格式、帧率）
 * 3. 可选：使用SDL2显示预览窗口
 * 
 * 编译：
 * g++ -o simple_preview simple_preview.cpp -lavformat -lavcodec -lavutil -lswscale
 * 
 * 运行：
 * ./simple_preview /dev/video0
 */

#include <cstdio>
#include <cstring>
#include <csignal>
#include <unistd.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

static volatile bool running = true;

void signal_handler(int sig) {
    running = false;
}

// 获取像素格式名称
const char* get_pix_fmt_name(int fmt) {
    switch (fmt) {
        case AV_PIX_FMT_NV12: return "NV12";
        case AV_PIX_FMT_NV16: return "NV16";
        case AV_PIX_FMT_NV24: return "NV24";
        case AV_PIX_FMT_YUYV422: return "YUYV";
        case AV_PIX_FMT_BGR24: return "BGR24";
        case AV_PIX_FMT_RGB24: return "RGB24";
        case AV_PIX_FMT_UYVY422: return "UYVY";
        default: return "未知";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <视频设备>\n", argv[0]);
        fprintf(stderr, "示例: %s /dev/video0\n", argv[0]);
        return 1;
    }
    
    const char* device = argv[1];
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化FFmpeg
    avformat_network_init();
    
    // 打开V4L2设备
    AVFormatContext* input_ctx = avformat_alloc_context();
    if (!input_ctx) {
        fprintf(stderr, "无法分配格式上下文\n");
        return 1;
    }
    
    // V4L2输入选项
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", "1920x1080", 0);
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "input_format", "nv12", 0);
    
    fprintf(stderr, "正在打开设备: %s\n", device);
    
    int ret = avformat_open_input(&input_ctx, device, nullptr, &options);
    av_dict_free(&options);
    
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        fprintf(stderr, "无法打开设备 %s: %s\n", device, err);
        avformat_free_context(input_ctx);
        return 1;
    }
    
    // 获取流信息
    ret = avformat_find_stream_info(input_ctx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "无法获取流信息\n");
        avformat_close_input(&input_ctx);
        return 1;
    }
    
    // 查找视频流
    int video_stream_idx = -1;
    for (unsigned i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    
    if (video_stream_idx < 0) {
        fprintf(stderr, "未找到视频流\n");
        avformat_close_input(&input_ctx);
        return 1;
    }
    
    AVStream* in_stream = input_ctx->streams[video_stream_idx];
    AVCodecParameters* in_par = in_stream->codecpar;
    
    // 打印输入信息
    fprintf(stderr, "\n=== 输入信息 ===\n");
    fprintf(stderr, "分辨率: %dx%d\n", in_par->width, in_par->height);
    fprintf(stderr, "像素格式: %d (%s)\n", in_par->format, get_pix_fmt_name(in_par->format));
    fprintf(stderr, "帧率: %f fps\n", av_q2d(in_stream->r_frame_rate));
    fprintf(stderr, "时间基: %f\n", av_q2d(in_stream->time_base));
    fprintf(stderr, "编码: %d\n", in_par->codec_id);
    
    // 分配帧
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    
    // 主循环
    int64_t frame_count = 0;
    int64_t start_time = av_gettime();
    int64_t last_frame_time = 0;
    
    fprintf(stderr, "\n开始采集... (按Ctrl+C停止)\n\n");
    
    while (running) {
        // 读取一帧
        ret = av_read_frame(input_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                usleep(1000);
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
        
        // 计算帧间隔
        int64_t now = av_gettime();
        if (last_frame_time > 0) {
            int64_t interval = now - last_frame_time;
            // 每10帧打印一次帧间隔
            if (frame_count % 10 == 0) {
                fprintf(stderr, "帧间隔: %.2f ms (%.1f fps)\n",
                        interval / 1000.0, 1000000.0 / interval);
            }
        }
        last_frame_time = now;
        
        frame_count++;
        
        // 每100帧打印一次统计
        if (frame_count % 100 == 0) {
            int64_t elapsed = (now - start_time) / 1000000;
            fprintf(stderr, "已处理: %lld 帧, %lld 秒, %.2f fps\n",
                    (long long)frame_count, (long long)elapsed,
                    elapsed > 0 ? (double)frame_count / elapsed : 0);
        }
        
        // 打印包信息
        if (frame_count <= 5) {
            fprintf(stderr, "帧 %lld: size=%d, pts=%lld, dts=%lld, flags=%d\n",
                    (long long)frame_count, pkt->size,
                    (long long)pkt->pts, (long long)pkt->dts,
                    pkt->flags);
        }
        
        av_packet_unref(pkt);
    }
    
    fprintf(stderr, "\n采集结束\n");
    
    // 清理
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avformat_close_input(&input_ctx);
    avformat_network_deinit();
    
    // 打印最终统计
    int64_t total_time = (av_gettime() - start_time) / 1000000;
    fprintf(stderr, "\n=== 最终统计 ===\n");
    fprintf(stderr, "总帧数: %lld\n", (long long)frame_count);
    fprintf(stderr, "总时间: %lld 秒\n", (long long)total_time);
    fprintf(stderr, "平均帧率: %.2f fps\n",
            total_time > 0 ? (double)frame_count / total_time : 0);
    
    return 0;
}
