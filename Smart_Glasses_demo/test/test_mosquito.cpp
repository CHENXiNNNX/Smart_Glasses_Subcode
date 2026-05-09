/* test_mosquito.cpp - 蚊子检测测试 (9 输出 RKNN YOLOv8 模型)
 *
 * 读取 JPG → letterbox 640×640 → RKNN 推理 → DFL 解码 + 置信度 → NMS → 云台坐标
 *
 * 模型输出结构 (由 head.py RKNN 分支导出):
 *   每个 scale 3 个 tensor, 共 9 个输出:
 *     output[0]: [1, 64, 80, 80]  DFL bbox (stride 8)
 *     output[1]: [1,  1, 80, 80]  sigmoid class score
 *     output[2]: [1,  1, 80, 80]  cls_sum (nc=1 时与 output[1] 相同)
 *     output[3]: [1, 64, 40, 40]  DFL bbox (stride 16)
 *     output[4]: [1,  1, 40, 40]  sigmoid class score
 *     output[5]: [1,  1, 40, 40]  cls_sum
 *     output[6]: [1, 64, 20, 20]  DFL bbox (stride 32)
 *     output[7]: [1,  1, 20, 20]  sigmoid class score
 *     output[8]: [1,  1, 20, 20]  cls_sum
 *
 * 用法:
 *   test_mosquito <图片路径> [选项...]
 *   test_mosquito -i ./000027.jpg -o result.jpg
 */

#include "app/rknn/rknn.hpp"
#include "app/tool/log/log.hpp"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace app::rknn;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "MOSQUITO";

    constexpr int   MODEL_SIZE   = 640;
    constexpr int   REG_MAX      = 16;
    constexpr float DEF_CONF_THR = 0.25f;
    constexpr float DEF_NMS_THR  = 0.45f;

    struct ScaleInfo
    {
        int grid_h, grid_w;
        int stride;
        int dfl_idx; // model output index for DFL [1,64,H,W]
        int cls_idx; // model output index for cls [1,1,H,W]
    };

    constexpr ScaleInfo SCALES[3] = {
        {80, 80, 8,  0, 1},
        {40, 40, 16, 3, 4},
        {20, 20, 32, 6, 7},
    };

    /* ------------------------------------------------------------------ */

    struct MosquitoBox
    {
        float x1, y1, x2, y2;
        float cx, cy;
        float confidence;
    };

    struct LetterboxInfo
    {
        float scale;
        int   pad_x, pad_y;
    };

    /* ------------------------------------------------------------------ */
    /*  预处理                                                             */
    /* ------------------------------------------------------------------ */

    cv::Mat letterbox(const cv::Mat& src, int target, LetterboxInfo& info)
    {
        float s = std::min(static_cast<float>(target) / src.cols,
                           static_cast<float>(target) / src.rows);
        int nw = static_cast<int>(src.cols * s);
        int nh = static_cast<int>(src.rows * s);
        info.scale = s;
        info.pad_x = (target - nw) / 2;
        info.pad_y = (target - nh) / 2;

        cv::Mat resized;
        cv::resize(src, resized, cv::Size(nw, nh));
        cv::Mat padded(target, target, CV_8UC3, cv::Scalar(128, 128, 128));
        resized.copyTo(padded(cv::Rect(info.pad_x, info.pad_y, nw, nh)));
        return padded;
    }

    /* ------------------------------------------------------------------ */
    /*  工具                                                               */
    /* ------------------------------------------------------------------ */

    inline float dequant(int8_t v, int32_t zp, float sc)
    {
        return (static_cast<float>(v) - static_cast<float>(zp)) * sc;
    }

    float compute_iou(const MosquitoBox& a, const MosquitoBox& b)
    {
        float ix1   = std::max(a.x1, b.x1);
        float iy1   = std::max(a.y1, b.y1);
        float ix2   = std::min(a.x2, b.x2);
        float iy2   = std::min(a.y2, b.y2);
        float inter = std::max(0.f, ix2 - ix1) * std::max(0.f, iy2 - iy1);
        float ua    = (a.x2 - a.x1) * (a.y2 - a.y1);
        float ub    = (b.x2 - b.x1) * (b.y2 - b.y1);
        return inter / (ua + ub - inter + 1e-6f);
    }

    std::vector<MosquitoBox> nms(std::vector<MosquitoBox>& boxes, float thr)
    {
        std::sort(boxes.begin(), boxes.end(),
                  [](const MosquitoBox& a, const MosquitoBox& b)
                  { return a.confidence > b.confidence; });
        std::vector<bool>        sup(boxes.size(), false);
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

    /* ------------------------------------------------------------------ */
    /*  DFL 解码: softmax + 加权求和                                       */
    /* ------------------------------------------------------------------ */

    float dfl_decode(const float vals[REG_MAX])
    {
        float mx = vals[0];
        for (int i = 1; i < REG_MAX; i++)
            if (vals[i] > mx) mx = vals[i];

        float sum = 0;
        float ev[REG_MAX];
        for (int i = 0; i < REG_MAX; i++)
        {
            ev[i] = std::exp(vals[i] - mx);
            sum += ev[i];
        }

        float result = 0;
        for (int i = 0; i < REG_MAX; i++)
            result += i * (ev[i] / sum);
        return result;
    }

    /* ------------------------------------------------------------------ */
    /*  后处理: 逐 scale 解码 DFL + 置信度                                  */
    /* ------------------------------------------------------------------ */

    void process_scale(const ScaleInfo&         si,
                       RKNNModel&               model,
                       float                    conf_thr,
                       const LetterboxInfo&     lb,
                       int orig_w, int orig_h,
                       std::vector<MosquitoBox>& cands)
    {
        const auto* dfl_raw = static_cast<const int8_t*>(model.getOutput(si.dfl_idx));
        const auto* cls_raw = static_cast<const int8_t*>(model.getOutput(si.cls_idx));

        int32_t dfl_zp = 0; float dfl_sc = 0;
        int32_t cls_zp = 0; float cls_sc = 0;
        model.getOutputQuantParams(si.dfl_idx, dfl_zp, dfl_sc);
        model.getOutputQuantParams(si.cls_idx, cls_zp, cls_sc);

        /* DFL 输出 NHWC [1,H,W,64]: data[h*W*64 + w*64 + c]
         * cls 输出连续存放 [1,H,W,1]: data[h*W + w]（size_with_stride 仅为内存对齐） */
        const int dfl_c = 64;

        for (int gy = 0; gy < si.grid_h; gy++)
        {
            for (int gx = 0; gx < si.grid_w; gx++)
            {
                /* 置信度（已 sigmoid，连续排列） */
                float conf = dequant(cls_raw[gy * si.grid_w + gx], cls_zp, cls_sc);
                if (conf < conf_thr)
                    continue;

                /* DFL 解码 — NHWC: data[h*W*64 + w*64 + c] */
                int dfl_base = gy * si.grid_w * dfl_c + gx * dfl_c;
                float dist[4];
                for (int d = 0; d < 4; d++)
                {
                    float vals[REG_MAX];
                    for (int k = 0; k < REG_MAX; k++)
                        vals[k] = dequant(dfl_raw[dfl_base + d * REG_MAX + k],
                                          dfl_zp, dfl_sc);
                    dist[d] = dfl_decode(vals);
                }

                float ax = (gx + 0.5f) * si.stride;
                float ay = (gy + 0.5f) * si.stride;

                float x1 = ax - dist[0] * si.stride;
                float y1 = ay - dist[1] * si.stride;
                float x2 = ax + dist[2] * si.stride;
                float y2 = ay + dist[3] * si.stride;

                /* letterbox → 原图 */
                x1 = (x1 - lb.pad_x) / lb.scale;
                y1 = (y1 - lb.pad_y) / lb.scale;
                x2 = (x2 - lb.pad_x) / lb.scale;
                y2 = (y2 - lb.pad_y) / lb.scale;

                x1 = std::max(0.f, std::min(x1, static_cast<float>(orig_w)));
                y1 = std::max(0.f, std::min(y1, static_cast<float>(orig_h)));
                x2 = std::max(0.f, std::min(x2, static_cast<float>(orig_w)));
                y2 = std::max(0.f, std::min(y2, static_cast<float>(orig_h)));

                if (x2 - x1 < 1.f || y2 - y1 < 1.f)
                    continue;

                MosquitoBox b;
                b.x1 = x1; b.y1 = y1; b.x2 = x2; b.y2 = y2;
                b.cx = (x1 + x2) * 0.5f;
                b.cy = (y1 + y2) * 0.5f;
                b.confidence = conf;
                cands.push_back(b);
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /*  可视化                                                             */
    /* ------------------------------------------------------------------ */

    void draw_results(cv::Mat& img, const std::vector<MosquitoBox>& dets)
    {
        for (const auto& d : dets)
        {
            cv::rectangle(img, cv::Point(static_cast<int>(d.x1), static_cast<int>(d.y1)),
                          cv::Point(static_cast<int>(d.x2), static_cast<int>(d.y2)),
                          cv::Scalar(0, 0, 255), 2);
            cv::circle(img, cv::Point(static_cast<int>(d.cx), static_cast<int>(d.cy)), 4,
                       cv::Scalar(0, 255, 0), -1);
            char label[64];
            snprintf(label, sizeof(label), "mosquito %.0f%%", d.confidence * 100.f);
            cv::putText(img, label,
                        cv::Point(static_cast<int>(d.x1), std::max(0, static_cast<int>(d.y1) - 6)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
        }
    }

    /* ------------------------------------------------------------------ */
    /*  命令行                                                             */
    /* ------------------------------------------------------------------ */

    struct Options
    {
        std::string model_path = "/root/bin/assets/models/mosquito_yolov8_rv1106_int8.rknn";
        std::string image_path;
        std::string output_path;
        float       conf_thr = DEF_CONF_THR;
        float       nms_thr  = DEF_NMS_THR;
    };

    bool parse_args(int argc, char** argv, Options& opt)
    {
        for (int i = 1; i < argc; i++)
        {
            std::string a = argv[i];
            if ((a == "-i" || a == "--image") && i + 1 < argc)
                opt.image_path = argv[++i];
            else if ((a == "-m" || a == "--model") && i + 1 < argc)
                opt.model_path = argv[++i];
            else if ((a == "-o" || a == "--output") && i + 1 < argc)
                opt.output_path = argv[++i];
            else if ((a == "-c" || a == "--conf") && i + 1 < argc)
                opt.conf_thr = static_cast<float>(std::atof(argv[++i]));
            else if ((a == "-n" || a == "--nms") && i + 1 < argc)
                opt.nms_thr = static_cast<float>(std::atof(argv[++i]));
            else if (a == "-h" || a == "--help")
            {
                fprintf(stderr,
                        "用法: %s -i <图片> [选项]\n"
                        "  -i  输入图片    -m  模型路径\n"
                        "  -o  输出图片    -c  置信度阈值 (默认 0.25)\n"
                        "  -n  NMS 阈值    -h  帮助\n",
                        argv[0]);
                return false;
            }
            else if (opt.image_path.empty() && a[0] != '-')
                opt.image_path = a;
            else
            {
                fprintf(stderr, "未知参数: %s\n", argv[i]);
                return false;
            }
        }
        return true;
    }

} // namespace

/* ====================================================================== */

int main(int argc, char* argv[])
{
    Logger::inst().init(LogConfig());

    Options opt;
    if (!parse_args(argc, argv, opt))
        return EXIT_FAILURE;
    if (opt.image_path.empty())
    {
        LOG_ERROR(LOG_TAG, "请指定图片: %s -i <图片路径>", argv[0]);
        return EXIT_FAILURE;
    }

    /* 1. 读取 & 预处理 */
    LOG_INFO(LOG_TAG, "读取图片: %s", opt.image_path.c_str());
    cv::Mat bgr = cv::imread(opt.image_path);
    if (bgr.empty())
    {
        LOG_ERROR(LOG_TAG, "无法读取: %s", opt.image_path.c_str());
        return EXIT_FAILURE;
    }
    int orig_w = bgr.cols, orig_h = bgr.rows;
    LOG_INFO(LOG_TAG, "尺寸: %dx%d", orig_w, orig_h);

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    LetterboxInfo lb{};
    cv::Mat input_img = letterbox(rgb, MODEL_SIZE, lb);

    /* 2. 加载模型 */
    LOG_INFO(LOG_TAG, "加载模型: %s", opt.model_path.c_str());
    RKNNModel model;
    RKNNError err = model.init(opt.model_path);
    if (err != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "模型加载失败 (%d)", static_cast<int>(err));
        return EXIT_FAILURE;
    }

    uint32_t n_out = model.getOutputNum();
    LOG_INFO(LOG_TAG, "模型输出数: %u", n_out);

    if (n_out != 9)
    {
        LOG_ERROR(LOG_TAG, "期望 9 输出 (3scale×3), 实际 %u — 请使用 RKNN 分支导出的模型", n_out);
        model.deinit();
        return EXIT_FAILURE;
    }

    /* 3. 推理 */
    uint32_t in_sz = static_cast<uint32_t>(input_img.total() * input_img.elemSize());
    err = model.setInput(0, input_img.data, in_sz);
    if (err != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "设置输入失败");
        model.deinit();
        return EXIT_FAILURE;
    }

    LOG_INFO(LOG_TAG, "推理中...");
    auto t0 = std::chrono::steady_clock::now();
    err     = model.run();
    auto t1 = std::chrono::steady_clock::now();
    if (err != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "推理失败");
        model.deinit();
        return EXIT_FAILURE;
    }
    LOG_INFO(LOG_TAG, "推理耗时: %ld ms",
             static_cast<long>(
                 std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()));

    /* 打印各输出量化参数 */
    for (uint32_t i = 0; i < n_out; i++)
    {
        int32_t zp = 0; float sc = 0;
        model.getOutputQuantParams(i, zp, sc);
        LOG_DEBUG(LOG_TAG, "  output[%u] size=%u zp=%d scale=%.6f",
                  i, model.getOutputSize(i), zp, sc);
    }

    /* 4. 后处理: 逐 scale DFL 解码 + 置信度过滤 */
    std::vector<MosquitoBox> candidates;
    candidates.reserve(512);

    for (int s = 0; s < 3; s++)
        process_scale(SCALES[s], model, opt.conf_thr, lb, orig_w, orig_h, candidates);

    LOG_INFO(LOG_TAG, "过滤后候选框: %d", static_cast<int>(candidates.size()));

    std::vector<MosquitoBox> detections = nms(candidates, opt.nms_thr);

    /* 5. 输出 */
    LOG_INFO(LOG_TAG, "=== 检测到 %d 个蚊子 ===", static_cast<int>(detections.size()));
    for (int i = 0; i < static_cast<int>(detections.size()); i++)
    {
        const auto& d = detections[i];
        LOG_INFO(LOG_TAG, "蚊子[%d]: 中心=(%.1f,%.1f) 框=(%d,%d,%d,%d) 置信=%.1f%%",
                 i, d.cx, d.cy,
                 static_cast<int>(d.x1), static_cast<int>(d.y1),
                 static_cast<int>(d.x2), static_cast<int>(d.y2),
                 d.confidence * 100.f);
    }

    if (!detections.empty())
    {
        const auto& best = detections[0];
        float icx = orig_w * 0.5f, icy = orig_h * 0.5f;
        LOG_INFO(LOG_TAG, "");
        LOG_INFO(LOG_TAG, "=== 云台目标 ===");
        LOG_INFO(LOG_TAG, "目标中心: (%.1f, %.1f)", best.cx, best.cy);
        LOG_INFO(LOG_TAG, "偏移像素: dx=%.1f dy=%.1f", best.cx - icx, best.cy - icy);
        LOG_INFO(LOG_TAG, "归一化:   dx=%.4f dy=%.4f",
                 (best.cx - icx) / icx, (best.cy - icy) / icy);
    }
    else
    {
        LOG_WARN(LOG_TAG, "未检测到蚊子");
    }

    /* 6. 可视化 */
    if (!opt.output_path.empty())
    {
        draw_results(bgr, detections);
        if (cv::imwrite(opt.output_path, bgr))
            LOG_INFO(LOG_TAG, "结果图: %s", opt.output_path.c_str());
        else
            LOG_ERROR(LOG_TAG, "保存失败: %s", opt.output_path.c_str());
    }

    model.deinit();
    LOG_INFO(LOG_TAG, "完成");
    return detections.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
}
