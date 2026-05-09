/* test_uvc_mosquito.cpp - UVC 摄像头实时蚊子检测
 *
 * V4L2 采集 MJPEG 帧 → OpenCV 解码 → letterbox 640×640 → RKNN 推理 → 输出云台坐标
 *
 * 用法:
 *   test_uvc_mosquito [选项...]
 *
 * 选项:
 *   -d <设备>     UVC 设备路径（默认 /dev/video0）
 *   -m <模型>     RKNN 模型路径
 *   -W <宽>       采集宽度（默认 640）
 *   -H <高>       采集高度（默认 480）
 *   -r <fps>      帧率（默认 30）
 *   -c <值>       置信度阈值（默认 0.25）
 *   -n <值>       NMS 阈值（默认 0.45）
 *   -v            详细日志
 *   -h            帮助
 *
 * 示例:
 *   test_uvc_mosquito -d /dev/video0 -W 640 -H 480
 *   test_uvc_mosquito -d /dev/video1 -W 1280 -H 720 -c 0.3
 */

#include "app/rknn/rknn.hpp"
#include "app/tool/log/log.hpp"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace app::rknn;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "UVC_MOSQUITO";

    /* ================================================================== */
    /*  YOLOv8 检测相关（与 test_mosquito.cpp 一致）                        */
    /* ================================================================== */

    constexpr int   MODEL_SIZE   = 640;
    constexpr int   REG_MAX      = 16;
    constexpr float DEF_CONF_THR = 0.50f;
    constexpr float DEF_NMS_THR  = 0.45f;
    constexpr int   DEF_MIN_HIT  = 3;     // 连续命中 N 帧才确认目标
    constexpr float SMOOTH_ALPHA = 0.4f;  // EMA 平滑系数 (越小越平稳)

    struct ScaleInfo { int grid_h, grid_w, stride, dfl_idx, cls_idx; };
    constexpr ScaleInfo SCALES[3] = {
        {80, 80, 8,  0, 1},
        {40, 40, 16, 3, 4},
        {20, 20, 32, 6, 7},
    };

    struct MosquitoBox { float x1, y1, x2, y2, cx, cy, confidence; };
    struct LetterboxInfo { float scale; int pad_x, pad_y; };

    inline float dequant(int8_t v, int32_t zp, float sc)
    {
        return (static_cast<float>(v) - static_cast<float>(zp)) * sc;
    }

    float compute_iou(const MosquitoBox& a, const MosquitoBox& b)
    {
        float ix1 = std::max(a.x1, b.x1), iy1 = std::max(a.y1, b.y1);
        float ix2 = std::min(a.x2, b.x2), iy2 = std::min(a.y2, b.y2);
        float inter = std::max(0.f, ix2 - ix1) * std::max(0.f, iy2 - iy1);
        float ua = (a.x2 - a.x1) * (a.y2 - a.y1);
        float ub = (b.x2 - b.x1) * (b.y2 - b.y1);
        return inter / (ua + ub - inter + 1e-6f);
    }

    std::vector<MosquitoBox> nms(std::vector<MosquitoBox>& boxes, float thr)
    {
        std::sort(boxes.begin(), boxes.end(),
                  [](const MosquitoBox& a, const MosquitoBox& b)
                  { return a.confidence > b.confidence; });
        std::vector<bool> sup(boxes.size(), false);
        std::vector<MosquitoBox> out;
        for (size_t i = 0; i < boxes.size(); i++)
        {
            if (sup[i]) continue;
            out.push_back(boxes[i]);
            for (size_t j = i + 1; j < boxes.size(); j++)
                if (!sup[j] && compute_iou(boxes[i], boxes[j]) > thr)
                    sup[j] = true;
        }
        return out;
    }

    float dfl_decode(const float vals[REG_MAX])
    {
        float mx = vals[0];
        for (int i = 1; i < REG_MAX; i++)
            if (vals[i] > mx) mx = vals[i];
        float sum = 0;
        float ev[REG_MAX];
        for (int i = 0; i < REG_MAX; i++) { ev[i] = std::exp(vals[i] - mx); sum += ev[i]; }
        float r = 0;
        for (int i = 0; i < REG_MAX; i++) r += i * (ev[i] / sum);
        return r;
    }

    cv::Mat letterbox(const cv::Mat& src, int target, LetterboxInfo& info)
    {
        float s = std::min(static_cast<float>(target) / src.cols,
                           static_cast<float>(target) / src.rows);
        int nw = static_cast<int>(src.cols * s);
        int nh = static_cast<int>(src.rows * s);
        info.scale = s; info.pad_x = (target - nw) / 2; info.pad_y = (target - nh) / 2;
        cv::Mat resized;
        cv::resize(src, resized, cv::Size(nw, nh));
        cv::Mat padded(target, target, CV_8UC3, cv::Scalar(128, 128, 128));
        resized.copyTo(padded(cv::Rect(info.pad_x, info.pad_y, nw, nh)));
        return padded;
    }

    void process_scale(const ScaleInfo& si, RKNNModel& model, float conf_thr,
                       const LetterboxInfo& lb, int ow, int oh,
                       std::vector<MosquitoBox>& cands)
    {
        const auto* dfl_raw = static_cast<const int8_t*>(model.getOutput(si.dfl_idx));
        const auto* cls_raw = static_cast<const int8_t*>(model.getOutput(si.cls_idx));
        int32_t dfl_zp = 0; float dfl_sc = 0;
        int32_t cls_zp = 0; float cls_sc = 0;
        model.getOutputQuantParams(si.dfl_idx, dfl_zp, dfl_sc);
        model.getOutputQuantParams(si.cls_idx, cls_zp, cls_sc);
        const int dfl_c = 64;

        for (int gy = 0; gy < si.grid_h; gy++)
        {
            for (int gx = 0; gx < si.grid_w; gx++)
            {
                float conf = dequant(cls_raw[gy * si.grid_w + gx], cls_zp, cls_sc);
                if (conf < conf_thr) continue;

                int dfl_base = gy * si.grid_w * dfl_c + gx * dfl_c;
                float dist[4];
                for (int d = 0; d < 4; d++)
                {
                    float vals[REG_MAX];
                    for (int k = 0; k < REG_MAX; k++)
                        vals[k] = dequant(dfl_raw[dfl_base + d * REG_MAX + k], dfl_zp, dfl_sc);
                    dist[d] = dfl_decode(vals);
                }

                float ax = (gx + 0.5f) * si.stride, ay = (gy + 0.5f) * si.stride;
                float x1 = ax - dist[0] * si.stride, y1 = ay - dist[1] * si.stride;
                float x2 = ax + dist[2] * si.stride, y2 = ay + dist[3] * si.stride;
                x1 = (x1 - lb.pad_x) / lb.scale; y1 = (y1 - lb.pad_y) / lb.scale;
                x2 = (x2 - lb.pad_x) / lb.scale; y2 = (y2 - lb.pad_y) / lb.scale;
                x1 = std::max(0.f, std::min(x1, static_cast<float>(ow)));
                y1 = std::max(0.f, std::min(y1, static_cast<float>(oh)));
                x2 = std::max(0.f, std::min(x2, static_cast<float>(ow)));
                y2 = std::max(0.f, std::min(y2, static_cast<float>(oh)));
                if (x2 - x1 < 1.f || y2 - y1 < 1.f) continue;

                MosquitoBox b;
                b.x1 = x1; b.y1 = y1; b.x2 = x2; b.y2 = y2;
                b.cx = (x1 + x2) * 0.5f; b.cy = (y1 + y2) * 0.5f;
                b.confidence = conf;
                cands.push_back(b);
            }
        }
    }

    std::vector<MosquitoBox> detect(RKNNModel& model, const cv::Mat& rgb,
                                     const LetterboxInfo& lb, int ow, int oh,
                                     float conf_thr, float nms_thr)
    {
        std::vector<MosquitoBox> cands;
        cands.reserve(256);
        for (int s = 0; s < 3; s++)
            process_scale(SCALES[s], model, conf_thr, lb, ow, oh, cands);

        auto results = nms(cands, nms_thr);

        /* 过滤不合理尺寸的框（蚊子在正常距离下的像素大小范围） */
        results.erase(
            std::remove_if(results.begin(), results.end(),
                           [ow, oh](const MosquitoBox& b)
                           {
                               float bw = b.x2 - b.x1;
                               float bh = b.y2 - b.y1;
                               float ratio = std::max(bw, bh) / (std::min(bw, bh) + 1e-6f);
                               float area  = bw * bh;
                               float img_area = static_cast<float>(ow * oh);

                               if (area < 400.f)  return true;  // 太小 (<20×20)
                               if (area > img_area * 0.25f) return true;  // 太大 (>25% 画面)
                               if (ratio > 5.0f)  return true;  // 宽高比异常
                               return false;
                           }),
            results.end());

        return results;
    }

    /* ================================================================== */
    /*  V4L2 UVC 采集                                                      */
    /* ================================================================== */

    static int xioctl(int fd, unsigned long req, void* arg)
    {
        int r;
        do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
        return r;
    }

    struct MmapBuf { void* start{}; size_t len{}; };

    /* ================================================================== */
    /*  全局信号控制                                                        */
    /* ================================================================== */

    std::atomic<bool> g_running{true};
    void sig_handler(int) { g_running.store(false); }

    /* ================================================================== */
    /*  命令行                                                              */
    /* ================================================================== */

    /* ================================================================== */
    /*  轻量 MJPEG AVI 写入器（不依赖 cv::VideoWriter）                    */
    /* ================================================================== */

    class MjpegAviWriter
    {
    public:
        bool open(const std::string& path, int w, int h, int fps)
        {
            fp_ = fopen(path.c_str(), "wb");
            if (!fp_) return false;
            w_ = w; h_ = h; fps_ = fps; frame_count_ = 0;
            idx_entries_.clear();
            write_placeholder_headers();
            movi_start_ = static_cast<uint32_t>(ftell(fp_));
            write_tag("LIST");
            write_u32(0); // placeholder for movi size
            write_tag("movi");
            return true;
        }

        void write(const cv::Mat& bgr)
        {
            if (!fp_) return;
            std::vector<uint8_t> buf;
            cv::imencode(".jpg", bgr, buf);
            uint32_t chunk_offset = static_cast<uint32_t>(ftell(fp_)) - movi_start_ - 4;
            uint32_t data_size    = static_cast<uint32_t>(buf.size());
            uint32_t padded       = (data_size + 1) & ~1u;

            write_tag("00dc");
            write_u32(data_size);
            fwrite(buf.data(), 1, data_size, fp_);
            if (padded > data_size) { uint8_t z = 0; fwrite(&z, 1, 1, fp_); }

            IdxEntry e; e.offset = chunk_offset; e.size = data_size;
            idx_entries_.push_back(e);
            frame_count_++;
        }

        bool isOpened() const { return fp_ != nullptr; }

        void release()
        {
            if (!fp_) return;
            uint32_t movi_end = static_cast<uint32_t>(ftell(fp_));
            /* patch movi LIST size */
            fseek(fp_, static_cast<long>(movi_start_ + 4), SEEK_SET);
            uint32_t movi_size = movi_end - movi_start_ - 8;
            write_u32(movi_size);
            fseek(fp_, 0, SEEK_END);

            /* write idx1 */
            write_tag("idx1");
            write_u32(static_cast<uint32_t>(idx_entries_.size() * 16));
            for (const auto& e : idx_entries_)
            {
                write_tag("00dc");
                write_u32(0x10); // AVIIF_KEYFRAME
                write_u32(e.offset);
                write_u32(e.size);
            }

            uint32_t file_end = static_cast<uint32_t>(ftell(fp_));
            /* patch RIFF size */
            fseek(fp_, 4, SEEK_SET);
            write_u32(file_end - 8);
            /* patch frame count in avih */
            fseek(fp_, 48, SEEK_SET);
            write_u32(frame_count_);
            /* patch frame count in strh */
            fseek(fp_, 140, SEEK_SET);
            write_u32(frame_count_);

            fclose(fp_);
            fp_ = nullptr;
        }

        ~MjpegAviWriter() { if (fp_) release(); }

    private:
        FILE*    fp_ = nullptr;
        int      w_ = 0, h_ = 0, fps_ = 30;
        uint32_t frame_count_ = 0;
        uint32_t movi_start_  = 0;

        struct IdxEntry { uint32_t offset, size; };
        std::vector<IdxEntry> idx_entries_;

        void write_tag(const char* t) { fwrite(t, 1, 4, fp_); }
        void write_u32(uint32_t v) { fwrite(&v, 4, 1, fp_); }
        void write_u16(uint16_t v) { fwrite(&v, 2, 1, fp_); }

        void write_placeholder_headers()
        {
            uint32_t us_per_frame = (fps_ > 0) ? (1000000u / static_cast<uint32_t>(fps_)) : 33333u;
            /* RIFF AVI */
            write_tag("RIFF"); write_u32(0); write_tag("AVI ");
            /* LIST hdrl */
            write_tag("LIST"); write_u32(192); write_tag("hdrl");
            /* avih (56 bytes) */
            write_tag("avih"); write_u32(56);
            write_u32(us_per_frame);
            write_u32(0); write_u32(0); write_u32(0);
            write_u32(0); // frame count placeholder @offset 48
            write_u32(0); write_u32(1); write_u32(0);
            write_u32(static_cast<uint32_t>(w_));
            write_u32(static_cast<uint32_t>(h_));
            write_u32(0); write_u32(0); write_u32(0); write_u32(0);
            /* LIST strl */
            write_tag("LIST"); write_u32(116); write_tag("strl");
            /* strh (56 bytes) */
            write_tag("strh"); write_u32(56);
            write_tag("vids"); write_tag("MJPG");
            write_u32(0); write_u32(0); write_u32(0);
            write_u32(1); write_u32(static_cast<uint32_t>(fps_));
            write_u32(0); // frame count placeholder @offset 140
            write_u32(0); write_u32(0xFFFF);
            write_u32(0); write_u16(static_cast<uint16_t>(w_));
            write_u16(static_cast<uint16_t>(h_));
            write_u32(0); write_u32(0);
            /* strf (40 bytes = BITMAPINFOHEADER) */
            write_tag("strf"); write_u32(40);
            write_u32(40); // biSize
            write_u32(static_cast<uint32_t>(w_));
            write_u32(static_cast<uint32_t>(h_));
            write_u16(1); write_u16(24); // planes, bpp
            write_tag("MJPG");
            write_u32(static_cast<uint32_t>(w_ * h_ * 3));
            write_u32(0); write_u32(0); write_u32(0); write_u32(0);
        }
    };

    /* ================================================================== */

    void draw_detections(cv::Mat& frame, const std::vector<MosquitoBox>& dets)
    {
        for (const auto& d : dets)
        {
            cv::rectangle(frame,
                          cv::Point(static_cast<int>(d.x1), static_cast<int>(d.y1)),
                          cv::Point(static_cast<int>(d.x2), static_cast<int>(d.y2)),
                          cv::Scalar(0, 0, 255), 2);
            cv::circle(frame,
                       cv::Point(static_cast<int>(d.cx), static_cast<int>(d.cy)),
                       4, cv::Scalar(0, 255, 0), -1);
            char label[64];
            snprintf(label, sizeof(label), "mosquito %.0f%%", d.confidence * 100.f);
            cv::putText(frame, label,
                        cv::Point(static_cast<int>(d.x1), std::max(0, static_cast<int>(d.y1) - 6)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
        }
    }

    /* ================================================================== */
    /*  帧间稳定性跟踪器                                                   */
    /* ================================================================== */

    struct Tracker
    {
        float cx = 0, cy = 0;   // EMA 平滑后的坐标
        float conf = 0;
        int   hit_streak = 0;   // 连续检测到的帧数
        int   miss_streak = 0;  // 连续未检测到的帧数
        bool  confirmed = false;

        void update(const MosquitoBox& det, int min_hit)
        {
            miss_streak = 0;
            hit_streak++;
            if (hit_streak == 1)
            {
                cx = det.cx;
                cy = det.cy;
            }
            else
            {
                cx = SMOOTH_ALPHA * det.cx + (1.0f - SMOOTH_ALPHA) * cx;
                cy = SMOOTH_ALPHA * det.cy + (1.0f - SMOOTH_ALPHA) * cy;
            }
            conf = det.confidence;
            confirmed = (hit_streak >= min_hit);
        }

        void mark_miss()
        {
            miss_streak++;
            hit_streak = 0;
            if (miss_streak >= 5)
                confirmed = false;
        }
    };

    struct Options
    {
        const char* dev       = "/dev/video0";
        std::string model     = "/root/bin/assets/models/mosquito_yolov8_rv1106_int8.rknn";
        std::string video_out;
        unsigned    width     = 640;
        unsigned    height    = 480;
        int         fps       = 30;
        float       conf_thr  = DEF_CONF_THR;
        float       nms_thr   = DEF_NMS_THR;
        int         min_hit   = DEF_MIN_HIT;
        int         warmup_ms = 300;
        bool        verbose   = false;
    };

    bool parse_args(int argc, char** argv, Options& o)
    {
        for (int i = 1; i < argc; i++)
        {
            std::string a = argv[i];
            if (a == "-d" && i + 1 < argc) { o.dev = argv[++i]; }
            else if (a == "-m" && i + 1 < argc) { o.model = argv[++i]; }
            else if (a == "-o" && i + 1 < argc) { o.video_out = argv[++i]; }
            else if (a == "-W" && i + 1 < argc) { o.width = static_cast<unsigned>(atoi(argv[++i])); }
            else if (a == "-H" && i + 1 < argc) { o.height = static_cast<unsigned>(atoi(argv[++i])); }
            else if (a == "-r" && i + 1 < argc) { o.fps = atoi(argv[++i]); }
            else if (a == "-c" && i + 1 < argc) { o.conf_thr = static_cast<float>(atof(argv[++i])); }
            else if (a == "-n" && i + 1 < argc) { o.nms_thr = static_cast<float>(atof(argv[++i])); }
            else if (a == "-v") { o.verbose = true; }
            else if (a == "-h" || a == "--help")
            {
                fprintf(stderr,
                    "用法: %s [选项]\n"
                    "  -d <设备>  UVC 设备（默认 /dev/video0）\n"
                    "  -m <模型>  RKNN 模型路径\n"
                    "  -o <路径>  保存标注录像（.avi，不指定则不录像）\n"
                    "  -W <宽>    采集宽度（默认 640）\n"
                    "  -H <高>    采集高度（默认 480）\n"
                    "  -r <fps>   帧率（默认 30）\n"
                    "  -c <值>    置信度阈值（默认 0.25）\n"
                    "  -n <值>    NMS 阈值（默认 0.45）\n"
                    "  -v         详细日志\n", argv[0]);
                return false;
            }
            else if (a[0] != '-') { o.dev = argv[i]; }
            else { fprintf(stderr, "未知参数: %s\n", argv[i]); return false; }
        }
        return true;
    }

} // namespace

/* ====================================================================== */

int main(int argc, char* argv[])
{
    Logger::inst().init(LogConfig());
    std::signal(SIGINT,  sig_handler);
    std::signal(SIGTERM, sig_handler);

    Options opt;
    if (!parse_args(argc, argv, opt))
        return EXIT_FAILURE;

    /* ---- 1. 加载 RKNN 模型 ---- */
    LOG_INFO(LOG_TAG, "加载模型: %s", opt.model.c_str());
    RKNNModel model;
    if (model.init(opt.model) != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "模型加载失败");
        return EXIT_FAILURE;
    }
    if (model.getOutputNum() != 9)
    {
        LOG_ERROR(LOG_TAG, "需要 9 输出的 RKNN 模型");
        model.deinit();
        return EXIT_FAILURE;
    }
    LOG_INFO(LOG_TAG, "模型就绪 (输入 %dx%d, 9 输出)", model.getModelWidth(), model.getModelHeight());

    /* ---- 2. 打开 UVC 设备 ---- */
    LOG_INFO(LOG_TAG, "打开设备: %s (%ux%u MJPEG @%dfps)", opt.dev, opt.width, opt.height, opt.fps);
    int fd = open(opt.dev, O_RDWR);
    if (fd < 0) { perror(opt.dev); model.deinit(); return EXIT_FAILURE; }

    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 || !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        LOG_ERROR(LOG_TAG, "%s 不是视频采集设备", opt.dev);
        close(fd); model.deinit(); return EXIT_FAILURE;
    }

    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = opt.width;
    fmt.fmt.pix.height      = opt.height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        LOG_ERROR(LOG_TAG, "VIDIOC_S_FMT 失败");
        close(fd); model.deinit(); return EXIT_FAILURE;
    }
    LOG_INFO(LOG_TAG, "实际分辨率: %ux%u", fmt.fmt.pix.width, fmt.fmt.pix.height);

    v4l2_streamparm sp{};
    sp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sp.parm.capture.timeperframe.numerator   = 1;
    sp.parm.capture.timeperframe.denominator = static_cast<unsigned>(opt.fps);
    xioctl(fd, VIDIOC_S_PARM, &sp);

    /* ---- 3. 申请 MMAP 缓冲 ---- */
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0)
    {
        LOG_ERROR(LOG_TAG, "VIDIOC_REQBUFS 失败");
        close(fd); model.deinit(); return EXIT_FAILURE;
    }

    std::vector<MmapBuf> bufs(req.count);
    for (unsigned j = 0; j < req.count; j++)
    {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = j;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("QUERYBUF"); close(fd); model.deinit(); return EXIT_FAILURE; }
        bufs[j].start = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        bufs[j].len = b.length;
        if (bufs[j].start == MAP_FAILED) { perror("mmap"); close(fd); model.deinit(); return EXIT_FAILURE; }
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("QBUF"); close(fd); model.deinit(); return EXIT_FAILURE; }
    }

    /* ---- 4. 开始采集 ---- */
    int btype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &btype) < 0)
    {
        LOG_ERROR(LOG_TAG, "VIDIOC_STREAMON 失败");
        close(fd); model.deinit(); return EXIT_FAILURE;
    }

    if (opt.warmup_ms > 0)
        usleep(static_cast<useconds_t>(opt.warmup_ms * 1000));

    LOG_INFO(LOG_TAG, "开始实时检测 (Ctrl+C 退出)...");

    int    frame_count  = 0;
    int    detect_count = 0;
    auto   start_time   = std::chrono::steady_clock::now();
    Tracker tracker;

    /* 录像 (延迟初始化，等第一帧确定实际分辨率) */
    MjpegAviWriter writer;
    bool recording = !opt.video_out.empty();
    if (recording)
        LOG_INFO(LOG_TAG, "录像将保存到: %s", opt.video_out.c_str());

    /* ---- 5. 主循环: 采集 → 检测 → 标注 → 录像 → 输出坐标 ---- */
    while (g_running.load(std::memory_order_relaxed))
    {
        /* 等待帧就绪 */
        pollfd pfd{fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 500);
        if (pr <= 0) continue;
        if (pfd.revents & (POLLERR | POLLHUP)) break;

        v4l2_buffer dq{};
        dq.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dq.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &dq) < 0)
        {
            if (errno == EAGAIN) continue;
            perror("DQBUF");
            break;
        }

        if (dq.bytesused == 0)
        {
            xioctl(fd, VIDIOC_QBUF, &dq);
            continue;
        }

        frame_count++;
        auto t0 = std::chrono::steady_clock::now();

        /* MJPEG → BGR */
        cv::Mat jpeg_buf(1, static_cast<int>(dq.bytesused), CV_8UC1,
                         bufs[dq.index].start);
        cv::Mat bgr = cv::imdecode(jpeg_buf, cv::IMREAD_COLOR);

        /* 归还缓冲（尽早，减少延迟） */
        xioctl(fd, VIDIOC_QBUF, &dq);

        if (bgr.empty()) continue;

        int ow = bgr.cols, oh = bgr.rows;

        /* 首帧时初始化录像 */
        if (recording && !writer.isOpened())
        {
            if (!writer.open(opt.video_out, ow, oh, opt.fps))
            {
                LOG_WARN(LOG_TAG, "无法创建录像文件，录像功能已禁用");
                recording = false;
            }
            else
            {
                LOG_INFO(LOG_TAG, "录像已开始: %dx%d @%dfps MJPEG AVI", ow, oh, opt.fps);
            }
        }

        /* BGR → RGB → letterbox */
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        LetterboxInfo lb{};
        cv::Mat input_img = letterbox(rgb, MODEL_SIZE, lb);

        /* RKNN 推理 */
        uint32_t in_sz = static_cast<uint32_t>(input_img.total() * input_img.elemSize());
        if (model.setInput(0, input_img.data, in_sz) != RKNNError::NONE) continue;
        if (model.run() != RKNNError::NONE) continue;

        /* 后处理 */
        auto dets = detect(model, rgb, lb, ow, oh, opt.conf_thr, opt.nms_thr);

        auto t1 = std::chrono::steady_clock::now();
        long ms = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

        /* 在 BGR 帧上画检测框 + 信息 */
        draw_detections(bgr, dets);

        char info_text[128];
        snprintf(info_text, sizeof(info_text), "F:%d T:%ldms D:%d",
                 frame_count, ms, static_cast<int>(dets.size()));
        cv::putText(bgr, info_text, cv::Point(5, 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1);

        /* 写入录像 */
        if (recording && writer.isOpened())
            writer.write(bgr);

        /* 跟踪 + 稳定性过滤 */
        if (!dets.empty())
        {
            tracker.update(dets[0], opt.min_hit);
        }
        else
        {
            tracker.mark_miss();
        }

        /* 只有连续命中 min_hit 帧后才输出（过滤偶发误检） */
        if (tracker.confirmed)
        {
            detect_count++;
            float icx = ow * 0.5f, icy = oh * 0.5f;
            float dx = (tracker.cx - icx) / icx;
            float dy = (tracker.cy - icy) / icy;

            LOG_INFO(LOG_TAG,
                     "帧%d [%ldms] 蚊子: (%.0f,%.0f) conf=%.0f%% 偏移=(%.3f,%.3f)",
                     frame_count, ms, tracker.cx, tracker.cy,
                     tracker.conf * 100.f, dx, dy);

            // TODO: 在此处将 dx, dy 发送给云台控制模块
            // gimbal_move(dx, dy);
        }
        else if (opt.verbose)
        {
            LOG_DEBUG(LOG_TAG, "帧%d [%ldms] %s (hit=%d miss=%d)",
                      frame_count, ms,
                      dets.empty() ? "无目标" : "等待确认",
                      tracker.hit_streak, tracker.miss_streak);
        }
    }

    /* ---- 6. 清理 ---- */
    LOG_INFO(LOG_TAG, "停止采集...");

    if (writer.isOpened())
    {
        writer.release();
        LOG_INFO(LOG_TAG, "录像已保存: %s", opt.video_out.c_str());
    }

    int off = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &off);

    for (auto& b : bufs)
        if (b.start && b.start != MAP_FAILED)
            munmap(b.start, b.len);

    close(fd);
    model.deinit();

    auto total_sec = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - start_time).count();
    LOG_INFO(LOG_TAG, "统计: %d 帧, %d 次检测, 运行 %ld 秒",
             frame_count, detect_count, static_cast<long>(total_sec));

    return EXIT_SUCCESS;
}
