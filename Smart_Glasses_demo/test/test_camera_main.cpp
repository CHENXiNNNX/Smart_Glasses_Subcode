/* test_camera_main.cpp - 摄像头驱动测试 */

#include "app/media/camera/camera.hpp"
#include "app/tool/log/log.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
    constexpr const char* TAG = "CamTest";
} // namespace

using namespace app::media::camera;
using namespace app::tool::log;

static std::atomic<bool> g_running{true};

static void signal_handler(int sig)
{
    (void)sig;
    g_running = false;
}

static const char* error_str(Error err)
{
    switch (err)
    {
    case Error::OK:
        return "OK";
    case Error::NOT_INIT:
        return "未初始化";
    case Error::ALREADY_INIT:
        return "已初始化";
    case Error::INVALID_PARAM:
        return "参数错误";
    case Error::DEVICE_ERROR:
        return "设备错误";
    case Error::ENCODER_ERROR:
        return "编码错误";
    case Error::MEMORY_ERROR:
        return "内存错误";
    case Error::TIMEOUT:
        return "超时";
    case Error::BUSY:
        return "忙";
    case Error::NOT_SUPPORTED:
        return "不支持";
    default:
        return "未知错误";
    }
}

static void print_menu()
{
    printf("\n--- 摄像头测试 ---\n");
    printf("1 H264 2 JPEG 3 拍照 4 连拍 5 录像 6 边录边拍\n");
    printf("7 曝光 8 白平衡 9 图像 10 码率 11 质量 12 分辨率 13 RTSP\n");
    printf("0 退出 选择: ");
    fflush(stdout);
}

static void test_h264_stream()
{
    LOG_INFO(TAG, "H264 视频流测试 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.h264.width   = 1920;
    cfg.h264.height  = 1080;
    cfg.h264.fps     = 30;
    cfg.h264.bitrate = 2000;
    cfg.h264.gop     = 30;
    cfg.enable_h264  = true;
    cfg.enable_jpeg  = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    std::atomic<uint32_t> frame_cnt{0};
    std::atomic<uint32_t> key_cnt{0};
    std::atomic<size_t>   total_size{0};

    cam.set_h264_cb(
        [&](const FramePtr& f)
        {
            if (f)
            {
                frame_cnt++;
                total_size += f->size;
                if (f->keyframe)
                    key_cnt++;
            }
        });

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    LOG_INFO(TAG, "H264: %ux%u@%ufps %ukbps GOP=%u", (unsigned)cfg.h264.width,
             (unsigned)cfg.h264.height, (unsigned)cfg.h264.fps, (unsigned)cfg.h264.bitrate,
             (unsigned)cfg.h264.gop);

    g_running          = true;
    auto     last_time = std::chrono::steady_clock::now();
    uint32_t last_cnt  = 0;
    size_t   last_size = 0;

    while (g_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<float>(now - last_time).count();

        uint32_t cnt = frame_cnt.load();
        size_t   sz  = total_size.load();

        float fps  = (cnt - last_cnt) / elapsed;
        float kbps = (sz - last_size) * 8.0f / 1000.0f / elapsed;

        LOG_INFO(TAG, "帧=%u 关键帧=%u FPS=%.1f 码率=%.0fkbps", cnt, key_cnt.load(), fps, kbps);

        last_time = now;
        last_cnt  = cnt;
        last_size = sz;
    }

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_jpeg_stream()
{
    LOG_INFO(TAG, "JPEG 图片流测试 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.jpeg.width   = 640;
    cfg.jpeg.height  = 480;
    cfg.jpeg.quality = 80;
    cfg.enable_h264  = false;
    cfg.enable_jpeg  = true;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    std::atomic<uint32_t> frame_cnt{0};
    std::atomic<size_t>   total_size{0};
    std::atomic<size_t>   last_size{0};

    cam.set_jpeg_cb(
        [&](const FramePtr& f)
        {
            if (f)
            {
                frame_cnt++;
                total_size += f->size;
                last_size = f->size;
            }
        });

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    LOG_INFO(TAG, "JPEG: %ux%u 质量=%u", (unsigned)cfg.jpeg.width, (unsigned)cfg.jpeg.height,
             (unsigned)cfg.jpeg.quality);

    g_running          = true;
    auto     last_time = std::chrono::steady_clock::now();
    uint32_t last_cnt  = 0;

    while (g_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<float>(now - last_time).count();

        uint32_t cnt    = frame_cnt.load();
        float    fps    = (cnt - last_cnt) / elapsed;
        float    avg_kb = (cnt > 0) ? (total_size.load() / cnt / 1024.0f) : 0;

        LOG_INFO(TAG, "帧=%u FPS=%.1f 平均=%.1fKB 最新=%.1fKB", cnt, fps, avg_kb,
                 last_size.load() / 1024.0f);

        last_time = now;
        last_cnt  = cnt;
    }

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_take_photo()
{
    LOG_INFO(TAG, "单张拍照测试");

    const char* path = "/tmp/photo.jpg";

    CameraCfg cfg;
    cfg.jpeg.width   = 1920;
    cfg.jpeg.height  = 1080;
    cfg.jpeg.quality = 90;
    cfg.enable_h264  = false;
    cfg.enable_jpeg  = true;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    /* 等待预热 */
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    LOG_INFO(TAG, "正在拍照...");

    std::atomic<bool> done{false};
    std::atomic<bool> success{false};

    cam.jpeg().save(path,
                    [&](const std::string& p, Error err)
                    {
                        if (err == Error::OK)
                        {
                            LOG_INFO(TAG, "拍照成功: %s", p.c_str());
                            success = true;
                        }
                        else
                        {
                            LOG_ERROR(TAG, "拍照失败: %s", error_str(err));
                        }
                        done = true;
                    });

    /* 等待完成 */
    int timeout = 50; /* 5秒 */
    while (!done && timeout-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (!done)
        LOG_ERROR(TAG, "拍照超时");

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_burst_photo()
{
    LOG_INFO(TAG, "连拍测试 10张");

    CameraCfg cfg;
    cfg.jpeg.width   = 1920;
    cfg.jpeg.height  = 1080;
    cfg.jpeg.quality = 85;
    cfg.enable_h264  = false;
    cfg.enable_jpeg  = true;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::atomic<int> success_cnt{0};
    std::atomic<int> done_cnt{0};

    for (int i = 1; i <= 10 && g_running; ++i)
    {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/photo_%02d.jpg", i);

        LOG_INFO(TAG, "拍照 %d/10 ...", i);

        cam.jpeg().save(path,
                        [&, i](const std::string& p, Error err)
                        {
                            if (err == Error::OK)
                            {
                                LOG_INFO(TAG, "  [%d] 成功: %s", i, p.c_str());
                                success_cnt++;
                            }
                            else
                            {
                                LOG_ERROR(TAG, "  [%d] 失败: %s", i, error_str(err));
                            }
                            done_cnt++;
                        });

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    /* 等待所有完成 */
    int timeout = 50;
    while (done_cnt < 10 && timeout-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "连拍完成: 成功=%d/10", success_cnt.load());
}

static void test_record_video()
{
    LOG_INFO(TAG, "录像测试 10秒");

    const char* path = "/tmp/video.h264";

    CameraCfg cfg;
    cfg.h264.width   = 1920;
    cfg.h264.height  = 1080;
    cfg.h264.fps     = 30;
    cfg.h264.bitrate = 2000;
    cfg.h264.gop     = 30;
    cfg.enable_h264  = true;
    cfg.enable_jpeg  = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    LOG_INFO(TAG, "开始录像: %s", path);

    if (cam.recorder().start(path, 10) != Error::OK)
    {
        LOG_ERROR(TAG, "录像启动失败");
        cam.stop();
        cam.deinit();
        return;
    }

    g_running = true;
    while (g_running && cam.recorder().is_recording())
    {
        LOG_INFO(TAG, "录像中... %u秒 帧数=%u 大小=%.1fMB", (unsigned)cam.recorder().duration_sec(),
                 (unsigned)cam.recorder().frames(), cam.recorder().file_size() / 1024.0f / 1024.0f);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    cam.recorder().stop();
    cam.stop();
    cam.deinit();

    LOG_INFO(TAG, "录像完成: %s", path);
}

static void test_record_and_photo()
{
    LOG_INFO(TAG, "边录边拍测试");
    LOG_INFO(TAG, "录像10秒，每2秒拍一张照片");

    CameraCfg cfg;
    cfg.h264.width   = 1920;
    cfg.h264.height  = 1080;
    cfg.h264.fps     = 30;
    cfg.h264.bitrate = 2000;
    cfg.jpeg.width   = 1920;
    cfg.jpeg.height  = 1080;
    cfg.jpeg.quality = 85;
    cfg.enable_h264  = true;
    cfg.enable_jpeg  = true;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    const char* video_path = "/tmp/video.h264";
    LOG_INFO(TAG, "开始录像: %s", video_path);

    if (cam.recorder().start(video_path, 10) != Error::OK)
    {
        LOG_ERROR(TAG, "录像启动失败");
        cam.stop();
        cam.deinit();
        return;
    }

    g_running     = true;
    int photo_idx = 0;

    while (g_running && cam.recorder().is_recording())
    {
        /* 每2秒拍一张 */
        if (cam.recorder().duration_sec() % 2 == 0 && cam.recorder().duration_sec() > 0)
        {
            photo_idx++;
            char path[64];
            snprintf(path, sizeof(path), "/tmp/rec_photo_%02d.jpg", photo_idx);

            cam.jpeg().save(path,
                            [](const std::string& p, Error err)
                            {
                                if (err == Error::OK)
                                    LOG_INFO(TAG, "  拍照: %s", p.c_str());
                            });

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        LOG_INFO(TAG, "录像: %u秒 拍照: %d张", (unsigned)cam.recorder().duration_sec(), photo_idx);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    cam.recorder().stop();
    cam.stop();
    cam.deinit();

    LOG_INFO(TAG, "完成: 录像=%s 照片=%d张", video_path, photo_idx);
}

static void test_exposure()
{
    LOG_INFO(TAG, "曝光调节测试");
    LOG_INFO(TAG, "循环切换 自动/手动曝光");
    LOG_INFO(TAG, "按 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.h264.width  = 1280;
    cfg.h264.height = 720;
    cfg.h264.fps    = 30;
    cfg.enable_h264 = true;
    cfg.enable_jpeg = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    g_running         = true;
    bool  manual_mode = false;
    float exp_time    = 10.0f; /* ms */
    float exp_gain    = 1.0f;
    int   step        = 0;

    while (g_running)
    {
        if (step % 10 == 0) /* 每10秒切换模式 */
        {
            manual_mode = !manual_mode;
            if (manual_mode)
            {
                cam.isp().set_ae_mode(IspCtrl::AeMode::MANUAL);
                LOG_INFO(TAG, "切换到手动曝光");
            }
            else
            {
                cam.isp().set_ae_mode(IspCtrl::AeMode::AUTO);
                LOG_INFO(TAG, "切换到自动曝光");
            }
        }

        if (manual_mode)
        {
            /* 循环调节曝光时间 */
            exp_time += 2.0f;
            if (exp_time > 33.0f)
                exp_time = 5.0f;

            cam.isp().set_exposure(exp_time, exp_gain);
            LOG_INFO(TAG, "手动曝光: %.1fms gain=%.1f", exp_time, exp_gain);
        }
        else
        {
            LOG_INFO(TAG, "自动曝光运行中...");
        }

        step++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_white_balance()
{
    LOG_INFO(TAG, "白平衡调节测试");
    LOG_INFO(TAG, "循环切换 自动/手动白平衡");
    LOG_INFO(TAG, "按 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.h264.width  = 1280;
    cfg.h264.height = 720;
    cfg.h264.fps    = 30;
    cfg.enable_h264 = true;
    cfg.enable_jpeg = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    g_running         = true;
    bool  manual_mode = false;
    float r_gain      = 1.0f;
    float b_gain      = 1.0f;
    int   step        = 0;

    while (g_running)
    {
        if (step % 10 == 0)
        {
            manual_mode = !manual_mode;
            if (manual_mode)
            {
                cam.isp().set_awb_mode(IspCtrl::AwbMode::MANUAL);
                LOG_INFO(TAG, "切换到手动白平衡");
            }
            else
            {
                cam.isp().set_awb_mode(IspCtrl::AwbMode::AUTO);
                LOG_INFO(TAG, "切换到自动白平衡");
            }
        }

        if (manual_mode)
        {
            /* 循环调节增益 */
            r_gain += 0.1f;
            b_gain -= 0.05f;
            if (r_gain > 2.0f)
                r_gain = 0.5f;
            if (b_gain < 0.5f)
                b_gain = 2.0f;

            cam.isp().set_wb_gain(r_gain, b_gain);
            LOG_INFO(TAG, "手动WB: R=%.2f B=%.2f", r_gain, b_gain);
        }
        else
        {
            LOG_INFO(TAG, "自动白平衡运行中...");
        }

        step++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_image_adjust()
{
    LOG_INFO(TAG, "图像参数调节测试");
    LOG_INFO(TAG, "循环调节 亮度/对比度/饱和度/锐度");
    LOG_INFO(TAG, "按 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.h264.width  = 1280;
    cfg.h264.height = 720;
    cfg.h264.fps    = 30;
    cfg.enable_h264 = true;
    cfg.enable_jpeg = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    g_running           = true;
    int         val     = 128;
    int         dir     = 10;
    int         param   = 0; /* 0=亮度 1=对比度 2=饱和度 3=锐度 */
    const char* names[] = {"亮度", "对比度", "饱和度", "锐度"};

    while (g_running)
    {
        val += dir;
        if (val >= 255)
        {
            val = 255;
            dir = -10;
        }
        if (val <= 0)
        {
            val   = 0;
            dir   = 10;
            param = (param + 1) % 4;
        }

        uint8_t v = static_cast<uint8_t>(val);
        switch (param)
        {
        case 0:
            cam.isp().set_brightness(v);
            break;
        case 1:
            cam.isp().set_contrast(v);
            break;
        case 2:
            cam.isp().set_saturation(v);
            break;
        case 3:
            cam.isp().set_sharpness(v);
            break;
        }

        LOG_INFO(TAG, "%s = %d", names[param], val);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    /* 恢复默认 */
    cam.isp().set_brightness(128);
    cam.isp().set_contrast(128);
    cam.isp().set_saturation(128);
    cam.isp().set_sharpness(128);

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_h264_bitrate()
{
    LOG_INFO(TAG, "H264 码率调节测试");
    LOG_INFO(TAG, "动态调节码率 500kbps->4000kbps->500kbps");
    LOG_INFO(TAG, "按 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.h264.width   = 1920;
    cfg.h264.height  = 1080;
    cfg.h264.fps     = 30;
    cfg.h264.bitrate = 2000;
    cfg.enable_h264  = true;
    cfg.enable_jpeg  = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    std::atomic<size_t> frame_size{0};
    cam.set_h264_cb(
        [&](const FramePtr& f)
        {
            if (f)
                frame_size = f->size;
        });

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    g_running   = true;
    int bitrate = 2000;
    int dir     = 250;

    while (g_running)
    {
        bitrate += dir;
        if (bitrate >= 4000)
        {
            bitrate = 4000;
            dir     = -250;
        }
        if (bitrate <= 500)
        {
            bitrate = 500;
            dir     = 250;
        }

        cam.h264().set_bitrate(static_cast<uint16_t>(bitrate));
        LOG_INFO(TAG, "码率=%dkbps 帧大小=%.1fKB", bitrate, frame_size.load() / 1024.0f);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_jpeg_quality()
{
    LOG_INFO(TAG, "JPEG 质量调节测试");
    LOG_INFO(TAG, "动态调节质量 20->100->20");
    LOG_INFO(TAG, "按 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.jpeg.width   = 1920;
    cfg.jpeg.height  = 1080;
    cfg.jpeg.quality = 80;
    cfg.enable_h264  = false;
    cfg.enable_jpeg  = true;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    std::atomic<size_t> frame_size{0};
    cam.set_jpeg_cb(
        [&](const FramePtr& f)
        {
            if (f)
                frame_size = f->size;
        });

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    g_running   = true;
    int quality = 80;
    int dir     = 5;

    while (g_running)
    {
        quality += dir;
        if (quality >= 100)
        {
            quality = 100;
            dir     = -5;
        }
        if (quality <= 20)
        {
            quality = 20;
            dir     = 5;
        }

        cam.jpeg().set_quality(static_cast<uint8_t>(quality));
        LOG_INFO(TAG, "质量=%d%% 大小=%.1fKB", quality, frame_size.load() / 1024.0f);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_resolution_switch()
{
    LOG_INFO(TAG, "分辨率切换测试");
    LOG_INFO(TAG, "动态切换 1080P->720P->480P->1080P");
    LOG_INFO(TAG, "按 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.h264.width   = 1920;
    cfg.h264.height  = 1080;
    cfg.h264.fps     = 30;
    cfg.h264.bitrate = 2000;
    cfg.enable_h264  = true;
    cfg.enable_jpeg  = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    std::atomic<size_t>   frame_size{0};
    std::atomic<uint32_t> frame_cnt{0};

    cam.set_h264_cb(
        [&](const FramePtr& f)
        {
            if (f)
            {
                frame_size = f->size;
                frame_cnt++;
            }
        });

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "启动失败");
        cam.deinit();
        return;
    }

    /* 分辨率列表 */
    struct Resolution
    {
        uint16_t    w;
        uint16_t    h;
        const char* name;
    };
    Resolution resolutions[] = {
        {1920, 1080, "1080P"},
        {1280, 720, "720P"},
        {640, 480, "480P"},
        {1920, 1080, "1080P"},
    };
    int res_idx   = 0;
    int res_count = static_cast<int>(sizeof(resolutions) / sizeof(resolutions[0]));

    g_running = true;
    int step  = 0;

    while (g_running)
    {
        /* 每5秒切换一次分辨率 */
        if (step > 0 && step % 5 == 0)
        {
            res_idx         = (res_idx + 1) % res_count;
            Resolution& res = resolutions[res_idx];

            LOG_INFO(TAG, "切换分辨率: %s (%dx%d)", res.name, res.w, res.h);

            Error err = cam.h264().set_resolution(res.w, res.h);
            if (err != Error::OK)
            {
                LOG_ERROR(TAG, "切换失败: %s", error_str(err));
            }
            else
            {
                LOG_INFO(TAG, "切换成功");
            }
        }

        LOG_INFO(TAG, "帧=%u 大小=%.1fKB", frame_cnt.load(), frame_size.load() / 1024.0f);

        step++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    cam.stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

static void test_rtsp_stream()
{
    LOG_INFO(TAG, "RTSP 推流测试");
    LOG_INFO(TAG, "按 Ctrl+C 退出");

    CameraCfg cfg;
    cfg.h264.width   = 1920;
    cfg.h264.height  = 1080;
    cfg.h264.fps     = 30;
    cfg.h264.bitrate = 20000;
    cfg.h264.gop     = 30;
    cfg.enable_h264  = true;
    cfg.enable_jpeg  = false;

    CameraDrv cam;
    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(TAG, "初始化失败");
        return;
    }

    /* 启动 RTSP 服务 */
    uint16_t    port = 554;
    const char* path = "/live";

    if (cam.rtsp().start(port, path) != Error::OK)
    {
        LOG_ERROR(TAG, "RTSP启动失败");
        cam.deinit();
        return;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(TAG, "摄像头启动失败");
        cam.rtsp().stop();
        cam.deinit();
        return;
    }

    LOG_INFO(TAG, "RTSP 已启动 rtsp://IP:%u%s %ux%u@%ufps", static_cast<unsigned>(port), path,
             static_cast<unsigned>(cfg.h264.width), static_cast<unsigned>(cfg.h264.height),
             static_cast<unsigned>(cfg.h264.fps));
    LOG_INFO(TAG, "使用VLC打开上述地址即可观看");

    std::atomic<uint32_t> frame_cnt{0};
    std::atomic<size_t>   total_size{0};

    cam.set_h264_cb(
        [&](const FramePtr& f)
        {
            if (f)
            {
                frame_cnt++;
                total_size += f->size;
            }
        });

    g_running          = true;
    auto     last_time = std::chrono::steady_clock::now();
    uint32_t last_cnt  = 0;
    size_t   last_size = 0;

    while (g_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(3));

        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<float>(now - last_time).count();

        uint32_t cnt = frame_cnt.load();
        size_t   sz  = total_size.load();

        float fps  = (cnt - last_cnt) / elapsed;
        float kbps = (sz - last_size) * 8.0f / 1000.0f / elapsed;

        LOG_INFO(TAG, "推流中: 帧=%u FPS=%.1f 码率=%.0fkbps", cnt, fps, kbps);

        last_time = now;
        last_cnt  = cnt;
        last_size = sz;
    }

    cam.stop();
    cam.rtsp().stop();
    cam.deinit();
    LOG_INFO(TAG, "测试结束");
}

int main(int argc, char* argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 日志初始化 */
    LogConfig log_cfg;
    log_cfg.enable_file    = false;
    log_cfg.enable_console = true;
    Logger::inst().init(log_cfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int choice = -1;

    /* 命令行参数 */
    if (argc > 1)
    {
        choice = atoi(argv[1]);
    }

    while (true)
    {
        if (choice < 0)
        {
            print_menu();
            if (scanf("%d", &choice) != 1)
            {
                while (getchar() != '\n')
                    ;
                continue;
            }
        }

        g_running = true;

        switch (choice)
        {
        case 0:
            LOG_INFO(TAG, "退出程序");
            Logger::inst().deinit();
            return 0;

        case 1:
            test_h264_stream();
            break;

        case 2:
            test_jpeg_stream();
            break;

        case 3:
            test_take_photo();
            break;

        case 4:
            test_burst_photo();
            break;

        case 5:
            test_record_video();
            break;

        case 6:
            test_record_and_photo();
            break;

        case 7:
            test_exposure();
            break;

        case 8:
            test_white_balance();
            break;

        case 9:
            test_image_adjust();
            break;

        case 10:
            test_h264_bitrate();
            break;

        case 11:
            test_jpeg_quality();
            break;

        case 12:
            test_resolution_switch();
            break;

        case 13:
            test_rtsp_stream();
            break;

        default:
            LOG_WARN(TAG, "无效选项: %d", choice);
            break;
        }

        choice = -1;
    }

    Logger::inst().deinit();
    return 0;
}
