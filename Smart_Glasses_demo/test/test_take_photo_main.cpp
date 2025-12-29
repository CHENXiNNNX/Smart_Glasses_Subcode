#include "app/media/camera/camera.hpp"
#include "app/tool/log/log.hpp"
#include "app/media/sync.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace app::media::camera;
using namespace app::tool::log;

int main()
{
    // 初始化日志
    LogConfig log_config;
    log_config.enable_console = true;
    log_config.min_level      = LogLevel::INFO;
    Logger::getInstance().initialize(log_config);

    // 创建同步上下文
    auto sync_ctx = std::make_shared<sync_context_t>();
    if (sync_init(sync_ctx.get()) != 0)
    {
        std::cerr << "同步上下文初始化失败\n";
        return -1;
    }

    // 配置视频系统
    VideoConfig config;
    config.width       = 1920;
    config.height      = 1080;
    config.fps         = 30;
    config.format      = EncodeFormat::H264;
    config.bitrate     = 6 * 1024;
    config.photo_path  = "/root/test_photos/";
    config.record_path = "/root/test_videos/";

    // 初始化并启动视频系统
    VideoSystem video(config);
    if (video.initialize(sync_ctx) != VideoError::NONE)
    {
        std::cerr << "视频系统初始化失败\n";
        sync_deinit(sync_ctx.get());
        return -1;
    }

    if (video.startStream() != VideoError::NONE)
    {
        std::cerr << "启动视频流失败\n";
        video.shutdown();
        sync_deinit(sync_ctx.get());
        return -1;
    }

    // 等待流稳定
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 拍照
    video.setMainState(VideoMainState::PHOTO);

    if (video.takePhoto() == VideoError::NONE)
    {
        // 等待拍照完成
        while (video.isPhotoCapturing())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "拍照成功！\n";

        // 恢复H264编码器
        video.restoreH264Encoder();
    }
    else
    {
        std::cerr << "拍照失败\n";
    }

    // 清理资源
    video.stopStream();
    video.shutdown();
    sync_deinit(sync_ctx.get());

    return 0;
}
