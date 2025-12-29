/**
 * @file main.cpp
 * @brief RTSP推流示例程序
 * @details 使用camera模块实现RTSP推流功能
 */

#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/tool/log/log.hpp"
#include <signal.h>
#include <unistd.h>

using namespace app::media::camera;
using namespace app::tool::log;

namespace {
    constexpr const char* LOG_TAG = "MAIN";
    constexpr int STATS_INTERVAL_SEC = 5;
    std::atomic<bool> g_running{true};
    VideoSystem* g_video_system = nullptr;
}

static void signal_handler(int sig)
{
    LOG_INFO(LOG_TAG, "收到信号 %d，准备退出...", sig);
    g_running.store(false);
    if (g_video_system)
    {
        g_video_system->stopRTSPMode();
        g_video_system->stopStream();
        g_video_system->shutdown();
    }
    exit(0);
}

int main(int argc, char* argv[])
{
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    LOG_INFO(LOG_TAG, "=== RTSP推流示例程序 ===");

    // 创建视频配置
    VideoConfig config;
    config.width  = CAMERA_WIDTH;
    config.height = CAMERA_HEIGHT;
    config.fps    = CAMERA_FPS;
    config.format = EncodeFormat::H264;
    config.bitrate = H264_Default_Bitrate;
    config.gop     = H264_Default_Gop;

    // 创建视频系统
    VideoSystem video_system(config);
    g_video_system = &video_system;

    // 初始化视频系统
    LOG_INFO(LOG_TAG, "初始化视频系统...");
    VideoError err = video_system.initialize();
    if (err != VideoError::NONE)
    {
        LOG_ERROR(LOG_TAG, "视频系统初始化失败: %d", static_cast<int>(err));
        return -1;
    }
    LOG_INFO(LOG_TAG, "视频系统初始化成功");

    // 启动RTSP推流
    int rtsp_port = RTSP_PORT;
    std::string rtsp_path = RTSP_PATH;

    // 可以从命令行参数读取端口和路径
    if (argc > 1)
    {
        rtsp_port = std::atoi(argv[1]);
    }
    if (argc > 2)
    {
        rtsp_path = argv[2];
    }

    LOG_INFO(LOG_TAG, "启动RTSP推流: rtsp://<ip>:%d%s", rtsp_port, rtsp_path.c_str());
    err = video_system.startRTSPMode(rtsp_port, rtsp_path);
    if (err != VideoError::NONE)
    {
        LOG_ERROR(LOG_TAG, "启动RTSP推流失败: %d", static_cast<int>(err));
        video_system.shutdown();
        return -1;
    }

    LOG_INFO(LOG_TAG, "RTSP推流已启动，按Ctrl+C停止");

    // 主循环 - 定期输出统计信息
    while (g_running.load())
    {
        sleep(STATS_INTERVAL_SEC);

        if (video_system.isRTSPStreaming())
        {
            float fps = video_system.getCurrentFPS();
            VideoSystem::Stats stats;
            video_system.getStats(stats);

            LOG_INFO(LOG_TAG, "RTSP推流中 - FPS: %.2f, 总帧数: %zu, 丢弃帧数: %zu",
                     static_cast<double>(fps), stats.frames_captured.load(), stats.frames_dropped.load());
        }
        else
        {
            LOG_WARN(LOG_TAG, "RTSP推流已停止");
            break;
        }
    }

    // 清理资源
    LOG_INFO(LOG_TAG, "正在停止RTSP推流...");
    video_system.stopRTSPMode();
    video_system.stopStream();
    video_system.shutdown();

    LOG_INFO(LOG_TAG, "程序退出");
    return 0;
}

