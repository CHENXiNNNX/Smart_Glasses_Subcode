/* test_uvc.cpp - USB UVC：V4L2 单平面采集（MJPEG / YUYV），多帧、参数可配
 *
 * opencv-mobile（rk_aiq）默认走 MIPI，本程序专用于 UVC 节点。
 *
 * 用法:
 *   test_uvc [设备路径] [选项...]
 *   设备默认 /dev/video0；选项与顺序无关。
 *
 * 选项:
 *   -o <前缀>        输出文件前缀（无扩展名）；单帧: 前缀.mjpg|ppm；多帧: 前缀_0001.ext …
 *   -W <宽> -H <高>  分辨率，默认 640x480
 *   -f <mjpg|yuyv>   像素格式，默认 mjpg
 *   -n <帧数>        采集帧数，默认 1
 *   -r <fps>         VIDIOC_S_PARM 帧率，默认 30（失败则忽略）
 *   -t <ms>          STREAMON 后首帧前延时毫秒，默认 300
 *   -l               列出设备支持的像素格式（ENUM_FMT）后退出
 *   -I               打印 QUERYCAP / 当前 VIDIOC_G_FMT 后退出
 *   -v               采集过程简要日志
 *   --help           帮助
 *
 * 示例:
 *   test_uvc -l
 *   test_uvc /dev/video0 -f mjpg -W 1280 -H 720 -n 5 -o /tmp/uvc
 *   test_uvc -f yuyv -n 1 -o cap
 */

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

struct Options
{
    const char* dev      = "/dev/video0";
    const char* out_pref = "uvc_cap";
    unsigned    width    = 640;
    unsigned    height   = 480;
    unsigned    fourcc   = V4L2_PIX_FMT_MJPEG; /* 或 V4L2_PIX_FMT_YUYV */
    int         frames   = 1;
    int         fps      = 30;
    int         warmup_ms = 300;
    bool        list_fmt = false;
    bool        info     = false;
    bool        verbose  = false;
};

static int xioctl(int fd, unsigned long req, void* arg)
{
    int r;
    do
    {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int clamp8(int x)
{
    if (x < 0)
        return 0;
    if (x > 255)
        return 255;
    return x;
}

static void yuyv_row_to_rgb(const uint8_t* row, unsigned width, uint8_t* rgb)
{
    for (unsigned x = 0; x < width; x += 2)
    {
        int y0 = (int)row[0] - 16;
        int u  = (int)row[1] - 128;
        int y1 = (int)row[2] - 16;
        int v  = (int)row[3] - 128;
        row += 4;

        rgb[0] = (uint8_t)clamp8((298 * y0 + 409 * v + 128) >> 8);
        rgb[1] = (uint8_t)clamp8((298 * y0 - 100 * u - 208 * v + 128) >> 8);
        rgb[2] = (uint8_t)clamp8((298 * y0 + 516 * u + 128) >> 8);
        rgb += 3;

        rgb[0] = (uint8_t)clamp8((298 * y1 + 409 * v + 128) >> 8);
        rgb[1] = (uint8_t)clamp8((298 * y1 - 100 * u - 208 * v + 128) >> 8);
        rgb[2] = (uint8_t)clamp8((298 * y1 + 516 * u + 128) >> 8);
        rgb += 3;
    }
}

static bool write_yuyv_as_ppm(const char* path, const uint8_t* buf, unsigned width, unsigned height,
                              unsigned bytesused, bool verbose)
{
    const unsigned expect = width * height * 2;
    if (bytesused < expect)
    {
        fprintf(stderr, "YUYV short frame: got %u need >= %u\n", bytesused, expect);
        return false;
    }

    FILE* fp = fopen(path, "wb");
    if (!fp)
    {
        perror(path);
        return false;
    }
    fprintf(fp, "P6\n%u %u\n255\n", width, height);

    uint8_t* row_rgb = (uint8_t*)malloc(width * 3);
    if (!row_rgb)
    {
        fprintf(stderr, "malloc row_rgb\n");
        fclose(fp);
        return false;
    }

    const unsigned linebytes = width * 2;
    bool           ok        = true;
    for (unsigned y = 0; y < height; y++)
    {
        yuyv_row_to_rgb(buf + y * linebytes, width, row_rgb);
        if (fwrite(row_rgb, 1, width * 3, fp) != width * 3)
        {
            perror("fwrite ppm");
            ok = false;
            break;
        }
    }
    free(row_rgb);
    fclose(fp);
    if (verbose && ok)
        fprintf(stderr, "wrote %s (YUYV->PPM)\n", path);
    return ok;
}

static void fourcc_to_str(unsigned c, char out[5])
{
    out[0] = (char)(c & 0xff);
    out[1] = (char)((c >> 8) & 0xff);
    out[2] = (char)((c >> 16) & 0xff);
    out[3] = (char)((c >> 24) & 0xff);
    out[4] = '\0';
}

static void print_usage(const char* prog)
{
    fprintf(stderr,
            "Usage: %s [device] [options]\n"
            "  -o <prefix>   output basename (no ext.)\n"
            "  -W -H         width height (default 640 480)\n"
            "  -f mjpg|yuyv  pixel format (default mjpg)\n"
            "  -n <n>        frame count (default 1)\n"
            "  -r <fps>      S_PARM fps (default 30)\n"
            "  -t <ms>       delay after STREAMON (default 300)\n"
            "  -l            list formats\n"
            "  -I            device info + current format\n"
            "  -v            verbose\n"
            "  --help\n",
            prog);
}

static bool parse_fmt(const char* s, unsigned* out_fourcc)
{
    if (strcasecmp(s, "mjpg") == 0 || strcasecmp(s, "mjpeg") == 0)
    {
        *out_fourcc = V4L2_PIX_FMT_MJPEG;
        return true;
    }
    if (strcasecmp(s, "yuyv") == 0 || strcasecmp(s, "yuy2") == 0)
    {
        *out_fourcc = V4L2_PIX_FMT_YUYV;
        return true;
    }
    return false;
}

static void print_list_formats(int fd)
{
    fprintf(stderr, "Pixel formats (VIDIOC_ENUM_FMT):\n");
    for (unsigned idx = 0;; idx++)
    {
        v4l2_fmtdesc d{};
        d.index = idx;
        d.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd, VIDIOC_ENUM_FMT, &d) < 0)
            break;
        char tag[5];
        fourcc_to_str(d.pixelformat, tag);
        fprintf(stderr, "  [%u] %.32s  '%s' / 0x%08x\n", idx, d.description, tag, d.pixelformat);
    }
}

static void print_info(int fd)
{
    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
    {
        fprintf(stderr, "card: %s\n", cap.card);
        fprintf(stderr, "driver: %s bus: %s version %u.%u.%u\n", cap.driver, cap.bus_info, (cap.version >> 16) & 0xff,
                (cap.version >> 8) & 0xff, cap.version & 0xff);
    }

    v4l2_format g{};
    g.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_FMT, &g) == 0)
    {
        char tag[5];
        fourcc_to_str(g.fmt.pix.pixelformat, tag);
        fprintf(stderr, "current G_FMT: %u x %u '%s'\n", g.fmt.pix.width, g.fmt.pix.height, tag);
    }
}

/* 解析命令行：返回 false 表示应退出且已打印用法 */
static bool parse_args(int argc, char** argv, Options* o)
{
    int i = 1;
    while (i < argc)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return false;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            o->out_pref = argv[++i];
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-W") == 0 && i + 1 < argc)
        {
            o->width = (unsigned)atoi(argv[++i]);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-H") == 0 && i + 1 < argc)
        {
            o->height = (unsigned)atoi(argv[++i]);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
        {
            const char* fs = argv[++i];
            if (!parse_fmt(fs, &o->fourcc))
            {
                fprintf(stderr, "unknown -f %s (use mjpg|yuyv)\n", fs);
                return false;
            }
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
        {
            o->frames = atoi(argv[++i]);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
        {
            o->fps = atoi(argv[++i]);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
        {
            o->warmup_ms = atoi(argv[++i]);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-l") == 0)
        {
            o->list_fmt = true;
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-I") == 0)
        {
            o->info = true;
            ++i;
            continue;
        }
        if (strcmp(argv[i], "-v") == 0)
        {
            o->verbose = true;
            ++i;
            continue;
        }

        if (argv[i][0] == '-')
        {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return false;
        }

        o->dev = argv[i];
        ++i;
    }

    if (o->width == 0 || o->height == 0 || o->frames < 1 || o->fps < 1)
    {
        fprintf(stderr, "invalid -W/-H/-n/-r\n");
        return false;
    }
    return true;
}

static void build_out_path(char* path, size_t pathsz, const Options* o, int frame_idx, const char* ext)
{
    if (o->frames == 1)
        snprintf(path, pathsz, "%s.%s", o->out_pref, ext);
    else
        snprintf(path, pathsz, "%s_%04d.%s", o->out_pref, frame_idx + 1, ext);
}

static bool capture_run(const Options* o)
{
    const char* ext = (o->fourcc == V4L2_PIX_FMT_MJPEG) ? "mjpg" : "ppm";

    int fd = open(o->dev, O_RDWR);
    if (fd < 0)
    {
        perror(o->dev);
        return false;
    }

    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr, "%s: not video capture\n", o->dev);
        close(fd);
        return false;
    }

    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = o->width;
    fmt.fmt.pix.height      = o->height;
    fmt.fmt.pix.pixelformat = o->fourcc;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        perror("VIDIOC_S_FMT");
        close(fd);
        return false;
    }

    char ftag[5];
    fourcc_to_str(fmt.fmt.pix.pixelformat, ftag);
    fprintf(stderr, "negotiated: %u x %u '%s'\n", fmt.fmt.pix.width, fmt.fmt.pix.height, ftag);

    v4l2_streamparm sp{};
    sp.type                                  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sp.parm.capture.timeperframe.numerator   = 1;
    sp.parm.capture.timeperframe.denominator = (unsigned)o->fps;
    if (xioctl(fd, VIDIOC_S_PARM, &sp) < 0)
    {
        if (o->verbose)
            fprintf(stderr, "VIDIOC_S_PARM skipped: %s\n", strerror(errno));
    }

    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0)
    {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return false;
    }

    struct MBuf
    {
        void*  start{};
        size_t len{};
    };
    MBuf* bufs = new MBuf[req.count]{};

    for (unsigned j = 0; j < req.count; j++)
    {
        v4l2_buffer b{};
        b.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index  = j;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0)
        {
            perror("VIDIOC_QUERYBUF");
            delete[] bufs;
            close(fd);
            return false;
        }
        void* p = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        if (p == MAP_FAILED)
        {
            perror("mmap");
            delete[] bufs;
            close(fd);
            return false;
        }
        bufs[j].start = p;
        bufs[j].len   = b.length;
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0)
        {
            perror("VIDIOC_QBUF");
            delete[] bufs;
            close(fd);
            return false;
        }
    }

    int btype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &btype) < 0)
    {
        perror("VIDIOC_STREAMON");
        delete[] bufs;
        close(fd);
        return false;
    }

    if (o->warmup_ms > 0)
        usleep((useconds_t)(o->warmup_ms * 1000));

    bool ok = true;
    for (int captured = 0; captured < o->frames && ok; captured++)
    {
        v4l2_buffer dq{};
        dq.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dq.memory = V4L2_MEMORY_MMAP;

        bool got = false;
        for (int attempt = 0; attempt < 150 && !got; attempt++)
        {
            pollfd pfd{fd, POLLIN, 0};
            int    pr = poll(&pfd, 1, 200);
            if (pr < 0)
            {
                perror("poll");
                ok = false;
                break;
            }
            if (pr == 0)
                continue;
            if (pfd.revents & (POLLERR | POLLHUP))
            {
                fprintf(stderr, "poll: error revents=0x%x\n", pfd.revents);
                ok = false;
                break;
            }
            if (!(pfd.revents & POLLIN))
                continue;

            if (xioctl(fd, VIDIOC_DQBUF, &dq) == 0)
            {
                got = true;
                break;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            perror("VIDIOC_DQBUF");
            ok = false;
            break;
        }

        if (!ok)
            break;
        if (!got)
        {
            fprintf(stderr, "no frame (timeout)\n");
            ok = false;
            break;
        }

        if (dq.bytesused == 0)
        {
            fprintf(stderr, "skip empty buffer\n");
            if (xioctl(fd, VIDIOC_QBUF, &dq) < 0)
                perror("VIDIOC_QBUF");
            captured--;
            continue;
        }

        char outpath[512];
        build_out_path(outpath, sizeof outpath, o, captured, ext);

        if (o->fourcc == V4L2_PIX_FMT_MJPEG)
        {
            FILE* fp = fopen(outpath, "wb");
            if (!fp)
            {
                perror(outpath);
                ok = false;
            }
            else
            {
                size_t n = fwrite(bufs[dq.index].start, 1, dq.bytesused, fp);
                fclose(fp);
                if (o->verbose || o->frames == 1)
                    fprintf(stderr, "wrote %zu bytes -> %s\n", n, outpath);
                if (n != dq.bytesused)
                    ok = false;
            }
        }
        else
        {
            if (!write_yuyv_as_ppm(outpath, (const uint8_t*)bufs[dq.index].start, fmt.fmt.pix.width,
                                   fmt.fmt.pix.height, dq.bytesused, o->verbose || o->frames == 1))
                ok = false;
        }

        if (xioctl(fd, VIDIOC_QBUF, &dq) < 0)
        {
            perror("VIDIOC_QBUF");
            ok = false;
            break;
        }
    }

    int off = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &off);

    for (unsigned j = 0; j < req.count; j++)
    {
        if (bufs[j].start)
            munmap(bufs[j].start, bufs[j].len);
    }
    delete[] bufs;
    close(fd);
    return ok;
}

int main(int argc, char** argv)
{
    Options opt;
    if (!parse_args(argc, argv, &opt))
        return 1;

    /* 只列格式时需先打开设备：设备路径可为第一个参数 */
    if (opt.list_fmt || opt.info)
    {
        int fd = open(opt.dev, O_RDWR);
        if (fd < 0)
        {
            perror(opt.dev);
            return 1;
        }
        v4l2_capability cap{};
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 || !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
        {
            fprintf(stderr, "%s: not a capture device\n", opt.dev);
            close(fd);
            return 1;
        }
        if (opt.list_fmt)
            print_list_formats(fd);
        if (opt.info)
            print_info(fd);
        close(fd);
        return 0;
    }

    return capture_run(&opt) ? 0 : 1;
}
