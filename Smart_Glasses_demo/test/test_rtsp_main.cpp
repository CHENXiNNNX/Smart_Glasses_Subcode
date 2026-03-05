/* test_rtsp_main.cpp - RTSP 推流测试 */

#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/tool/log/log.hpp"

#include <atomic>
#include <csignal>
#include <unistd.h>

using namespace app::media::camera;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG   = "RTSP";
    constexpr int         STATS_SEC = 5;
    std::atomic<bool>     g_running{true};
    CameraDrv*            g_cam = nullptr;
} // namespace

static void signal_handler(int sig)
{
    (void)sig;
    LOG_INFO(LOG_TAG, "收到信号，退出");
    g_running.store(false);
    if (g_cam)
    {
        g_cam->stop();
        g_cam->rtsp().stop();
        g_cam->deinit();
    }
    exit(0);
}

int main(int argc, char* argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    Logger::inst().init(LogConfig());
    LOG_INFO(LOG_TAG, "RTSP 推流测试");

    int         port = RTSP_PORT;
    std::string path = RTSP_PATH;
    if (argc > 1)
        port = std::atoi(argv[1]);
    if (argc > 2)
        path = argv[2];

    CameraCfg cfg;
    cfg.h264.width   = CAMERA_WIDTH;
    cfg.h264.height  = CAMERA_HEIGHT;
    cfg.h264.fps     = CAMERA_FPS;
    cfg.h264.bitrate = H264_Default_Bitrate;
    cfg.h264.gop     = H264_Default_Gop;
    cfg.iq_file_dir  = ISP_PATH;
    cfg.enable_h264  = true;
    cfg.enable_jpeg  = false;

    CameraDrv cam;
    g_cam = &cam;

    if (cam.init(cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(LOG_TAG, "摄像头初始化失败");
        return -1;
    }

    if (cam.rtsp().start(static_cast<uint16_t>(port), path) != Error::OK)
    {
        LOG_ERROR(LOG_TAG, "RTSP 启动失败");
        cam.deinit();
        return -1;
    }

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(LOG_TAG, "摄像头启动失败");
        cam.rtsp().stop();
        cam.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "RTSP 已启动 rtsp://IP:%d%s", port, path.c_str());
    LOG_INFO(LOG_TAG, "Ctrl+C 停止");

    while (g_running.load())
    {
        sleep(STATS_SEC);
        if (!cam.is_running())
            break;

        auto s = cam.stats();
        LOG_INFO(LOG_TAG, "推流 帧=%u 丢=%u fps=%.1f", s.h264_frames, s.h264_drops,
                 static_cast<double>(s.h264_fps));
    }

    cam.stop();
    cam.rtsp().stop();
    cam.deinit();
    g_cam = nullptr;

    LOG_INFO(LOG_TAG, "退出");
    return 0;
}
