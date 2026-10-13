#ifndef YUNLU_SETTINGS_H
#define YUNLU_SETTINGS_H

// 编译期默认配置，可在设备上按需修改
namespace yunlu::settings {

// HDMI-In 采集
inline const char* kCaptureDevice = "/dev/video0";
inline const int   kCaptureWidth  = 1920;
inline const int   kCaptureHeight = 1080;
inline const int   kCaptureFps    = 30;

// 编码
inline const char* kCodec   = "h264_rkmpp";   // 或 hevc_rkmpp
inline const int   kBitrate = 8 * 1000 * 1000; // bps

// 录制目录（需存在且可写）
inline const char* kRecordDir = "/home/cat/recordings";

// 录制容器：mkv(Matroska) 或 mp4。
// mkv 为纯追加式容器，不依赖写尾即可播放，抗断电/被杀/卡顿，强烈推荐。
inline const char* kContainer = "mkv";

// 单个录制文件达到该大小(MB)后自动分段，规避 MP4 4GB 上限并便于管理
inline const int   kMaxFileMB = 2000;

// 磁盘可用空间低于该值(MB)时自动停止录制，避免写满导致 MP4 索引写不下(文件损坏)
inline const int   kMinFreeMB = 500;

// 默认 RTMP 地址（运行时配置文件优先）
inline const char* kDefaultRtmpUrl =
    "rtmp://58.87.69.226/live/yunlu1?secret=18d44630c086480783f62f33dbb7e09a";

// 运行时配置文件（INI）。不存在时程序会自动生成默认内容，可直接编辑。
inline const char* kConfigFile = "/home/cat/yunlu.conf";

} // namespace yunlu::settings

#endif // YUNLU_SETTINGS_H
