#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

struct Buffer {
    void*  start{};
    size_t length{};
};

static int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

int main(int argc, char** argv) {
    const char* dev = (argc > 1) ? argv[1] : "/dev/video12"; // rkisp selfpath
    int width  = (argc > 2) ? atoi(argv[2]) : 1280;
    int height = (argc > 3) ? atoi(argv[3]) : 720;
    int frames = (argc > 4) ? atoi(argv[4]) : 30;           // 采集帧数
    const char* outdir = (argc > 5) ? argv[5] : "/tmp";      // 输出目录

    int fd = open(dev, O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }

    // 1) 设置格式（MPLANE + NV12 优先，失败则退 NV21）
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width  = (unsigned)width;
    fmt.fmt.pix_mp.height = (unsigned)height;
    fmt.fmt.pix_mp.field  = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.num_planes  = 1;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        // try NV21
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width  = (unsigned)width;
        fmt.fmt.pix_mp.height = (unsigned)height;
        fmt.fmt.pix_mp.field  = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV21;
        fmt.fmt.pix_mp.num_planes  = 1;
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("VIDIOC_S_FMT"); return 1; }
        fprintf(stderr, "Fallback to NV21\n");
    } else {
        fprintf(stderr, "Using NV12\n");
    }

    // 2) 申请缓冲
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) { perror("VIDIOC_REQBUFS"); return 1; }
    if (req.count < 2) { fprintf(stderr, "Insufficient buffers\n"); return 1; }

    Buffer* bufs = new Buffer[req.count]{};
    for (unsigned i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        v4l2_plane  planes[VIDEO_MAX_PLANES]{};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.length   = 1;
        buf.m.planes = planes;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("VIDIOC_QUERYBUF"); return 1; }
        void* start = mmap(nullptr, buf.m.planes[0].length, PROT_READ|PROT_WRITE, MAP_SHARED,
                           fd, buf.m.planes[0].m.mem_offset);
        if (start == MAP_FAILED) { perror("mmap"); return 1; }
        bufs[i].start  = start;
        bufs[i].length = buf.m.planes[0].length;

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) { perror("VIDIOC_QBUF"); return 1; }
    }

    // 3) 启动
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("VIDIOC_STREAMON"); return 1; }

    // 4) 采集循环（poll + DQBUF/QBUF）
    int captured = 0;
    while (captured < frames) {
        pollfd pfd{fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 2000); // 2s 超时
        if (pr <= 0) { perror("poll"); break; }

        v4l2_buffer buf{};
        v4l2_plane  planes[VIDEO_MAX_PLANES]{};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.length   = 1;
        buf.m.planes = planes;

        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            perror("VIDIOC_DQBUF"); break;
        }

        // 输出（落盘）
        char path[256];
        snprintf(path, sizeof(path), "%s/frame_%06d.%s", outdir, captured,
                 (fmt.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV12) ? "nv12" : "nv21");
        FILE* fp = fopen(path, "wb");
        if (fp) {
            fwrite(bufs[buf.index].start, 1, buf.m.planes[0].bytesused, fp);
            fclose(fp);
            fprintf(stderr, "Saved %s (%u bytes)\n", path, buf.m.planes[0].bytesused);
        }

        // 回队
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) { perror("VIDIOC_QBUF"); break; }
        ++captured;
    }

    // 5) 停止 & 释放
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) perror("VIDIOC_STREAMOFF");
    for (unsigned i = 0; i < req.count; ++i) {
        if (bufs[i].start && bufs[i].length) munmap(bufs[i].start, bufs[i].length);
    }
    delete[] bufs;
    close(fd);
    return 0;
}
