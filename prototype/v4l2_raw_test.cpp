/**
 * 原始V4L2 API测试
 * 直接使用V4L2系统调用，绕过FFmpeg的V4L2封装
 */

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

int main(int argc, char* argv[]) {
    const char* device = (argc > 1) ? argv[1] : "/dev/video0";
    
    fprintf(stderr, "=== 原始V4L2 API测试 ===\n");
    fprintf(stderr, "设备: %s\n\n", device);
    
    // 1. 打开设备
    fprintf(stderr, "1. 打开设备...\n");
    int fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "   失败: %s\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "   成功 (fd=%d)\n", fd);
    
    // 2. 查询设备能力
    fprintf(stderr, "\n2. 查询设备能力...\n");
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        fprintf(stderr, "   驱动: %s\n", cap.driver);
        fprintf(stderr, "   设备: %s\n", cap.card);
        fprintf(stderr, "   总线: %s\n", cap.bus_info);
        fprintf(stderr, "   版本: %u.%u.%u\n",
                (cap.version >> 16) & 0xFF,
                (cap.version >> 8) & 0xFF,
                cap.version & 0xFF);
    } else {
        fprintf(stderr, "   VIDIOC_QUERYCAP 失败: %s\n", strerror(errno));
    }
    
    // 3. 枚举支持的格式
    fprintf(stderr, "\n3. 支持的像素格式:\n");
    struct v4l2_fmtdesc fmt_desc;
    memset(&fmt_desc, 0, sizeof(fmt_desc));
    fmt_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
        fprintf(stderr, "   [%d] %c%c%c%c - %s\n",
                fmt_desc.index,
                fmt_desc.pixelformat & 0xFF,
                (fmt_desc.pixelformat >> 8) & 0xFF,
                (fmt_desc.pixelformat >> 16) & 0xFF,
                (fmt_desc.pixelformat >> 24) & 0xFF,
                fmt_desc.description);
        fmt_desc.index++;
    }
    
    // 4. 尝试设置格式
    fprintf(stderr, "\n4. 尝试设置格式...\n");
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = 1920;
    fmt.fmt.pix_mp.height = 1080;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;
    
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) == 0) {
        fprintf(stderr, "   设置成功: %dx%d, 格式=0x%08x\n",
                fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
                fmt.fmt.pix_mp.pixelformat);
    } else {
        fprintf(stderr, "   设置失败: %s\n", strerror(errno));
        
        // 尝试读取当前格式
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (xioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
            fprintf(stderr, "   当前格式: %dx%d, 格式=0x%08x\n",
                    fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
                    fmt.fmt.pix_mp.pixelformat);
        }
    }
    
    // 5. 查询DV timings（HDMI信号）
    fprintf(stderr, "\n5. 查询HDMI信号...\n");
    struct v4l2_dv_timings timings;
    memset(&timings, 0, sizeof(timings));
    if (xioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &timings) == 0) {
        fprintf(stderr, "   信号: %dx%d @ %.2f Hz\n",
                timings.bt.width, timings.bt.height,
                (double)timings.bt.pixelclock / 
                (timings.bt.width + timings.bt.hfrontporch + timings.bt.hsync + timings.bt.hbackporch) /
                (timings.bt.height + timings.bt.vfrontporch + timings.bt.vsync + timings.bt.vbackporch));
    } else {
        fprintf(stderr, "   无信号或查询失败: %s\n", strerror(errno));
    }
    
    // 6. 请求缓冲区
    fprintf(stderr, "\n6. 请求缓冲区...\n");
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (xioctl(fd, VIDIOC_REQBUFS, &req) == 0) {
        fprintf(stderr, "   成功: %u 个缓冲区\n", req.count);
    } else {
        fprintf(stderr, "   失败: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    
    // 7. 查询并映射缓冲区
    fprintf(stderr, "\n7. 映射缓冲区...\n");
    void** buffers = new void*[req.count];
    size_t* lengths = new size_t[req.count];
    
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;
        
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) != 0) {
            fprintf(stderr, "   缓冲区 %u 查询失败: %s\n", i, strerror(errno));
            continue;
        }
        
        void* m = mmap(nullptr, planes[0].length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, planes[0].m.mem_offset);
        if (m == MAP_FAILED) {
            fprintf(stderr, "   缓冲区 %u 映射失败: %s\n", i, strerror(errno));
            buffers[i] = nullptr;
        } else {
            buffers[i] = m;
            lengths[i] = planes[0].length;
            fprintf(stderr, "   缓冲区 %u: 地址=%p, 大小=%zu\n", i, m, planes[0].length);
        }
    }
    
    // 8. 开始流
    fprintf(stderr, "\n8. 开始视频流...\n");
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) == 0) {
        fprintf(stderr, "   流已启动\n");
    } else {
        fprintf(stderr, "   启动失败: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    
    // 9. 入队所有缓冲区
    fprintf(stderr, "\n9. 入队缓冲区...\n");
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;
        planes[0].length = lengths[i];
        
        if (xioctl(fd, VIDIOC_QBUF, &buf) != 0) {
            fprintf(stderr, "   入队 %u 失败: %s\n", i, strerror(errno));
        }
    }
    
    // 10. 读取几帧
    fprintf(stderr, "\n10. 读取帧...\n");
    for (int frame = 0; frame < 5; frame++) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        int r = select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0) {
            fprintf(stderr, "   帧 %d: 超时\n", frame);
            continue;
        }
        
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.length = 1;
        
        if (xioctl(fd, VIDIOC_DQBUF, &buf) != 0) {
            fprintf(stderr, "   帧 %d: DQBUF 失败: %s\n", frame, strerror(errno));
            continue;
        }
        
        fprintf(stderr, "   帧 %d: 索引=%u, 大小=%u, 字节已用=%u\n",
                frame, buf.index, planes[0].length, planes[0].bytesused);
        
        // 重新入队
        xioctl(fd, VIDIOC_QBUF, &buf);
    }
    
    // 11. 停止流
    fprintf(stderr, "\n11. 停止视频流...\n");
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    
    // 12. 清理
    fprintf(stderr, "\n12. 清理资源...\n");
    for (unsigned i = 0; i < req.count; i++) {
        if (buffers[i]) munmap(buffers[i], lengths[i]);
    }
    delete[] buffers;
    delete[] lengths;
    
    // 释放缓冲区
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    xioctl(fd, VIDIOC_REQBUFS, &req);
    
    close(fd);
    
    fprintf(stderr, "\n=== 测试完成 ===\n");
    return 0;
}
