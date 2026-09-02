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

// 默认 RTMP 地址（界面上可改）
inline const char* kDefaultRtmpUrl = "rtmp://192.168.66.1/live/cam";

} // namespace yunlu::settings

#endif // YUNLU_SETTINGS_H
