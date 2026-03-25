#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

#include "app/protocol/webrtc/signaling.hpp"
#include "app/protocol/webrtc/webrtc.hpp"
#include "app/tool/log/log.hpp"
#include "app/media/audio/audio.hpp"
#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/media/sync.hpp"

using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG    = "MAIN";
    const std::string     device_id  = "glasses_123456";
    const std::string     server_url = "ws://192.168.50.68:8000";
} // namespace

int main()
{
    Logger::inst().init(LogConfig());
    LOG_INFO(LOG_TAG, "WebRTC 测试启动");

    auto sync_ctx = std::make_shared<sync_context_t>();
    if (sync_init(sync_ctx.get()) != 0)
    {
        LOG_WARN(LOG_TAG, "同步初始化失败，继续");
    }

    app::protocol::webrtc::SignalingConfig sig_config;
    sig_config.device_id  = device_id;
    sig_config.server_url = server_url;

    app::protocol::webrtc::WebRTCConfig webrtc_config;
    webrtc_config.ice.stun_servers = {
        "stun:stun.l.google.com:19302",
        "stun:stun.miwifi.com:3478",
        "stun:stun.chat.bilibili.com:3478",
        "stun:stun.12voip.com:3478",
        "stun:stun.aa.net.uk:3478",
        "stun:stun.actionvoip.com:3478"
    };
    webrtc_config.ice.turn_servers = {
        "turn:turnuser1:turnpass123@116.62.24.66:3478",
        "turn:turnuser1:turnpass123@116.62.24.66:3478?transport=tcp",
        "turn:user:mypwd@43.138.235.180:9002"
    };
    webrtc_config.enable_video_pacing      = true;
    webrtc_config.video_pacing_bps         = 0;
    webrtc_config.video_pacing_interval_ms = 10;

    auto signaling = std::make_shared<app::protocol::webrtc::Signaling>(sig_config);
    auto webrtc    = std::make_shared<app::protocol::webrtc::WebRTCSystem>(webrtc_config);

    app::media::audio::AudioCfg audio_cfg;
    audio_cfg.capture.rate      = AUDIO_SAMPLE_RATE;
    audio_cfg.capture.channels  = AUDIO_CHANNELS;
    audio_cfg.capture.frame_ms  = AUDIO_FRAME_DURATION_MS;
    audio_cfg.playback.rate     = AUDIO_SAMPLE_RATE;
    audio_cfg.playback.channels = AUDIO_CHANNELS;
    audio_cfg.playback.frame_ms = AUDIO_FRAME_DURATION_MS;
    audio_cfg.playback.volume   = 50;
    audio_cfg.opus.rate         = AUDIO_SAMPLE_RATE;
    audio_cfg.opus.channels     = AUDIO_CHANNELS;
    audio_cfg.opus.bitrate      = AUDIO_BIT_RATE;
    audio_cfg.opus.vbr          = true;
    audio_cfg.enable_capture    = true;
    audio_cfg.enable_playback   = true;
    audio_cfg.enable_opus       = true;
    audio_cfg.enable_proc       = true;

    auto audio_drv = std::make_unique<app::media::audio::AudioDrv>();
    if (audio_drv->init(audio_cfg) != app::media::audio::Error::OK)
    {
        LOG_ERROR(LOG_TAG, "音频初始化失败");
        return 1;
    }

    audio_drv->set_capture_cb(
        [webrtc, audio_drv = audio_drv.get()](const app::media::audio::FramePtr& pcm)
        {
            if (!pcm || pcm->size == 0 || !webrtc->isConnected())
                return;
            auto opus_frame = audio_drv->opus().encode(pcm);
            if (opus_frame && opus_frame->size > 0)
                webrtc->sendAudioData(opus_frame->data, opus_frame->size, opus_frame->timestamp);
        });

    if (audio_drv->start() != app::media::audio::Error::OK)
    {
        LOG_ERROR(LOG_TAG, "音频启动失败");
        return 1;
    }
    LOG_INFO(LOG_TAG, "音频就绪");

    std::unique_ptr<app::media::camera::CameraDrv> camera_drv;
    app::media::camera::CameraCfg                  camera_cfg;
    camera_cfg.h264.width   = CAMERA_WIDTH;
    camera_cfg.h264.height  = CAMERA_HEIGHT;
    camera_cfg.h264.fps     = CAMERA_FPS;
    camera_cfg.h264.bitrate = H264_Default_Bitrate;
    camera_cfg.iq_file_dir  = ISP_PATH;

    camera_drv = std::make_unique<app::media::camera::CameraDrv>();
    if (camera_drv->init(camera_cfg, sync_ctx) == app::media::camera::Error::OK &&
        camera_drv->start() == app::media::camera::Error::OK)
    {
        camera_drv->set_webrtc_sink(
            [webrtc, sync_ctx](const uint8_t* data, size_t size, uint64_t pts, bool keyframe)
            {
                if (!data || size == 0 || !webrtc->isConnected())
                    return;
                uint64_t ts = sync_ctx ? sync_get_timestamp(sync_ctx.get(), pts, false) : pts;
                webrtc->sendVideoData(data, size, ts, keyframe);
            });
        LOG_INFO(LOG_TAG, "视频就绪");
    }
    else
    {
        LOG_WARN(LOG_TAG, "摄像头初始化失败，仅音频模式");
        camera_drv.reset();
    }

    webrtc->onStateChanged(
        [](app::protocol::webrtc::WebRTCState state)
        {
            LOG_INFO(LOG_TAG, "WebRTC 状态: %s",
                     app::protocol::webrtc::WebRTCSystem::stateToString(state));
        });

    webrtc->onAudioData(
        [audio_drv = audio_drv.get()](const uint8_t* data, size_t size)
        {
            if (!data || size == 0 || !audio_drv)
                return;
            auto pcm = audio_drv->opus().decode(data, size);
            if (pcm)
                audio_drv->playback().push(pcm);
        });

    signaling->onStatusChanged(
        [](app::protocol::webrtc::SignalingStatus status)
        {
            LOG_INFO(LOG_TAG, "信令: %s",
                     app::protocol::webrtc::Signaling::statusToString(status).c_str());
        });
    signaling->onError([](app::protocol::webrtc::SignalingError /* e */, const std::string& msg)
                       { LOG_ERROR(LOG_TAG, "信令错误: %s", msg.c_str()); });
    signaling->onRoomInfoChanged(
        [](const app::protocol::webrtc::RoomInfo& info)
        { LOG_INFO(LOG_TAG, "房间 %s 人数=%d", info.room_id.c_str(), info.num); });

    if (!signaling->connect())
    {
        LOG_ERROR(LOG_TAG, "信令连接失败");
        return 1;
    }

    int wait_cnt = 0;
    while (signaling->getStatus() != app::protocol::webrtc::SignalingStatus::CONNECTED)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (++wait_cnt > 100)
        {
            LOG_ERROR(LOG_TAG, "信令连接超时");
            return 1;
        }
    }
    LOG_INFO(LOG_TAG, "信令已连接");

    if (webrtc->init(signaling) != app::protocol::webrtc::WebRTCError::NONE)
    {
        LOG_ERROR(LOG_TAG, "WebRTC 初始化失败");
        return 1;
    }
    LOG_INFO(LOG_TAG, "WebRTC 就绪");

    if (camera_drv)
    {
        app::media::camera::CameraDrv* cam = camera_drv.get();
        webrtc->setVideoNetworkCallbacks(
            [cam](unsigned int bps)
            {
                if (!cam)
                    return;
                unsigned kbps = std::max(200u, std::min(bps / 1000u, 50000u));
                static unsigned                     s_last_kbps   = 0;
                static bool                         s_have        = false;
                static std::chrono::steady_clock::time_point s_last_small_chg{};
                if (s_have && kbps == s_last_kbps)
                    return;
                auto now = std::chrono::steady_clock::now();
                constexpr unsigned kMinStepKbps = 48;
                if (s_have && std::abs(static_cast<int>(kbps) - static_cast<int>(s_last_kbps)) <
                        static_cast<int>(kMinStepKbps) &&
                    now - s_last_small_chg < std::chrono::seconds(4))
                    return;
                s_last_kbps      = kbps;
                s_last_small_chg = now;
                s_have           = true;
                if (cam->h264().set_bitrate(static_cast<uint16_t>(kbps)) ==
                    app::media::camera::Error::OK)
                    LOG_DEBUG(LOG_TAG, "REMB %ukbps", kbps);
            },
            [cam]()
            {
                if (!cam)
                    return;
                if (cam->h264().request_idr() == app::media::camera::Error::OK)
                    LOG_DEBUG(LOG_TAG, "RequestIDR ok");
            });
    }

    LOG_INFO(LOG_TAG, "输入 q 退出, j 加入房间, l 离开, c 发送连接请求");
    std::string input;
    while (std::cin >> input)
    {
        if (input == "q" || input == "Q")
            break;
        if (input == "j" || input == "J")
        {
            if (signaling->joinRoom())
                LOG_INFO(LOG_TAG, "已加入房间");
            else
                LOG_ERROR(LOG_TAG, "加入房间失败");
        }
        else if (input == "l" || input == "L")
        {
            if (signaling->leaveRoom())
                LOG_INFO(LOG_TAG, "已离开房间");
            else
                LOG_ERROR(LOG_TAG, "离开房间失败");
        }
        else if (input == "c" || input == "C")
        {
            std::string peer = signaling->getPeerDeviceId();
            if (peer.empty())
                LOG_ERROR(LOG_TAG, "未配对");
            else if (webrtc->sendConnectionRequest(peer, true, true, true))
                LOG_INFO(LOG_TAG, "连接请求已发往 %s", peer.c_str());
            else
                LOG_ERROR(LOG_TAG, "发送连接请求失败");
        }
    }

    webrtc->deinit();
    signaling->disconnect();
    if (audio_drv)
    {
        audio_drv->stop();
        audio_drv->deinit();
    }
    if (camera_drv)
    {
        camera_drv->stop();
        camera_drv->deinit();
    }
    if (sync_ctx)
        sync_deinit(sync_ctx.get());

    LOG_INFO(LOG_TAG, "退出");
    return 0;
}
