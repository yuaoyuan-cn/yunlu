#!/usr/bin/env bash
# 创建 FFmpeg 动态库的无版本号符号链接 (libavcodec.so 等)
# 鲁班猫固件自带 FFmpeg 6.1 头文件 + libavcodec.so.60 等，但缺链接用 .so 符号链接
# 用法: sudo scripts/link_ffmpeg.sh
set -e

LIB=/usr/lib/aarch64-linux-gnu

# 目标库名: 实际版本文件.base
#   avcodec.so.60  avformat.so.60  avutil.so.58  avfilter.so.9  swscale.so.7  swresample.so.4
mkdir -p "$LIB"

link_best() {
    local base="$1"      # 例如 avcodec
    local best="$2"      # 最高可用的 .so 文件（不含 .so 前缀）
    # 用 /usr/bin/ffmpeg 同源的 6.1；若找不到 6.1 回退到 4.4
    if [ -e "$LIB/lib${base}.so.$best" ]; then
        sudo ln -sf "lib${base}.so.$best" "$LIB/lib${base}.so"
        echo "-> lib${base}.so -> lib${base}.so.$best"
    else
        # 兜底：取目录里最大的版本化 .so.X
        local f
        f=$(ls -1 "$LIB"/lib${base}.so.[0-9]* 2>/dev/null | sort -V | tail -1)
        if [ -n "$f" ]; then
            sudo ln -sf "$(basename "$f")" "$LIB/lib${base}.so"
            echo "-> lib${base}.so -> $(basename "$f")"
        else
            echo "!! 未找到 lib${base}.so.*"
        fi
    fi
}

link_best avcodec     60
link_best avformat    60
link_best avutil      58
link_best avfilter    9
link_best swscale     7
link_best swresample  4

sudo ldconfig
echo "==> 完成。验证: ls -l $LIB/libavcodec.so"
