#ifndef YUNLU_COMMON_H
#define YUNLU_COMMON_H

#include <cstdint>

namespace yunlu {

// 由一个线程持有的 NV12 视频帧（引用计数由调用方管理）
struct VideoFrame {
    uint8_t* y   = nullptr;
    uint8_t* uv  = nullptr;
    int width   = 0;
    int height  = 0;
    int linesize_y  = 0;   // Y 行字节数
    int linesize_uv = 0;   // UV 平面行字节数
    int64_t pts_us = 0;    // 微秒时间戳
    bool yuv420sp = true;  // 默认 NV12 (YUV420SP)

    int sizeY()  const { return linesize_y * height; }
    int sizeUV() const { return linesize_uv * (height / 2); }
};

struct StreamConfig {
    int width        = 1920;
    int height       = 1080;
    int fps          = 30;
    int bitrate      = 8 * 1000 * 1000;   // bps
    const char* codec = "h264";           // h264 / hevc

    // 输出
    std::string record_dir  = "/home/cat/recordings";
    std::string rtmp_url    = "";         // 空 = 不推流
    std::string record_path = "";         // 空 = 自动按时间生成
};

} // namespace yunlu

#endif // YUNLU_COMMON_H
