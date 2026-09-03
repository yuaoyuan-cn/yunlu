/**
 * 最简单的FFmpeg V4L2测试
 * 只测试能否打开设备并读取一帧
 */

#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "用法: %s <视频设备>\n", argv[0]);
        return 1;
    }
    
    const char* device = argv[1];
    fprintf(stderr, "尝试打开设备: %s\n", device);
    
    // 方法1：基本打开
    fprintf(stderr, "\n方法1：基本打开...\n");
    {
        AVFormatContext* ctx = avformat_alloc_context();
        int ret = avformat_open_input(&ctx, device, nullptr, nullptr);
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "失败: %s\n", err);
        } else {
            fprintf(stderr, "成功\n");
            avformat_close_input(&ctx);
        }
    }
    
    // 方法2：带input_format选项
    fprintf(stderr, "\n方法2：带input_format选项...\n");
    {
        AVFormatContext* ctx = avformat_alloc_context();
        AVDictionary* options = nullptr;
        av_dict_set(&options, "input_format", "nv12", 0);
        
        int ret = avformat_open_input(&ctx, device, nullptr, &options);
        av_dict_free(&options);
        
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "失败: %s\n", err);
        } else {
            fprintf(stderr, "成功\n");
            avformat_close_input(&ctx);
        }
    }
    
    // 方法3：带video_size选项
    fprintf(stderr, "\n方法3：带video_size选项...\n");
    {
        AVFormatContext* ctx = avformat_alloc_context();
        AVDictionary* options = nullptr;
        av_dict_set(&options, "video_size", "1920x1080", 0);
        
        int ret = avformat_open_input(&ctx, device, nullptr, &options);
        av_dict_free(&options);
        
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "失败: %s\n", err);
        } else {
            fprintf(stderr, "成功\n");
            avformat_close_input(&ctx);
        }
    }
    
    // 方法4：带所有选项
    fprintf(stderr, "\n方法4：带所有选项...\n");
    {
        AVFormatContext* ctx = avformat_alloc_context();
        AVDictionary* options = nullptr;
        av_dict_set(&options, "video_size", "1920x1080", 0);
        av_dict_set(&options, "framerate", "30", 0);
        av_dict_set(&options, "input_format", "nv12", 0);
        
        int ret = avformat_open_input(&ctx, device, nullptr, &options);
        av_dict_free(&options);
        
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "失败: %s\n", err);
        } else {
            fprintf(stderr, "成功\n");
            avformat_close_input(&ctx);
        }
    }
    
    // 方法5：尝试读取一帧
    fprintf(stderr, "\n方法5：尝试读取一帧...\n");
    {
        AVFormatContext* ctx = avformat_alloc_context();
        AVDictionary* options = nullptr;
        av_dict_set(&options, "video_size", "1920x1080", 0);
        av_dict_set(&options, "framerate", "30", 0);
        
        int ret = avformat_open_input(&ctx, device, nullptr, &options);
        av_dict_free(&options);
        
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            fprintf(stderr, "打开失败: %s\n", err);
        } else {
            ret = avformat_find_stream_info(ctx, nullptr);
            if (ret < 0) {
                fprintf(stderr, "获取流信息失败\n");
            } else {
                fprintf(stderr, "成功打开并获取流信息\n");
                
                // 查找视频流
                for (unsigned i = 0; i < ctx->nb_streams; i++) {
                    if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                        AVCodecParameters* par = ctx->streams[i]->codecpar;
                        fprintf(stderr, "视频流 %d: %dx%d, 格式=%d\n",
                                i, par->width, par->height, par->format);
                        break;
                    }
                }
                
                // 尝试读取一帧
                AVPacket* pkt = av_packet_alloc();
                ret = av_read_frame(ctx, pkt);
                if (ret < 0) {
                    char err[256];
                    av_strerror(ret, err, sizeof(err));
                    fprintf(stderr, "读取帧失败: %s\n", err);
                } else {
                    fprintf(stderr, "成功读取一帧: size=%d\n", pkt->size);
                    av_packet_unref(pkt);
                }
                av_packet_free(&pkt);
            }
            avformat_close_input(&ctx);
        }
    }
    
    return 0;
}
