#include "webrtc_device_runner.hpp"
#include "signaling.hpp"
#include "webrtc.hpp"
#include "../../media/audio/audio.hpp"
#include "../../media/audio/codec/opus_codec.hpp"
#include "../../media/camera/camera.hpp"
#include "../../media/media_config.hpp"
#include "../../media/sync.hpp"
#include "../../tool/log/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

namespace app::protocol::webrtc
{
    using namespace app::tool::log;

    void apply_default_webrtc_device_options(WebRtcDeviceOptions& opt)
    {
        if (opt.device_id.empty())
            opt.device_id = "glasses_123456";
        if (opt.server_url.empty())
            opt.server_url = "ws://192.168.50.68:8000";
        if (opt.ice_stun.empty())
        {
            opt.ice_stun = {
                "stun:stun.l.google.com:19302",     "stun:stun.miwifi.com:3478",
                "stun:stun.chat.bilibili.com:3478", "stun:stun.12voip.com:3478",
                "stun:stun.aa.net.uk:3478",         "stun:stun.actionvoip.com:3478",
            };
        }
        if (opt.ice_turn.empty())
        {
            opt.ice_turn = {
                "turn:turnuser1:turnpass123@116.62.24.66:3478",
                "turn:turnuser1:turnpass123@116.62.24.66:3478?transport=tcp",
                "turn:user:mypwd@43.138.235.180:9002",
            };
        }
        if (opt.command_hint.empty())
            opt.command_hint = "输入 q 退出, j 加入房间, l 离开, c 发送连接请求";
    }

    namespace
    {
        void attach_camera_weak_net(const std::shared_ptr<WebRTCSystem>& webrtc,
                                    app::media::camera::CameraDrv* cam, const char* tag)
        {
            if (!webrtc || !cam)
                return;
            webrtc->setVideoNetworkCallbacks(
                [cam, tag](unsigned int bps)
                {
                    if (!cam)
                        return;
                    unsigned        kbps        = std::max(200u, std::min(bps / 1000u, 50000u));
                    static unsigned s_last_kbps = 0;
                    static bool     s_have      = false;
                    static std::chrono::steady_clock::time_point s_last_small_chg{};
                    if (s_have && kbps == s_last_kbps)
                        return;
                    auto               now          = std::chrono::steady_clock::now();
                    constexpr unsigned kMinStepKbps = 48;
                    if (s_have &&
                        std::abs(static_cast<int>(kbps) - static_cast<int>(s_last_kbps)) <
                            static_cast<int>(kMinStepKbps) &&
                        now - s_last_small_chg < std::chrono::seconds(4))
                        return;
                    s_last_kbps      = kbps;
                    s_last_small_chg = now;
                    s_have           = true;
                    if (cam->h264().set_bitrate(static_cast<uint16_t>(kbps)) ==
                        app::media::camera::Error::OK)
                        LOG_DEBUG(tag, "REMB %ukbps", kbps);
                },
                [cam, tag]()
                {
                    if (!cam)
                        return;
                    if (cam->h264().request_idr() == app::media::camera::Error::OK)
                        LOG_DEBUG(tag, "RequestIDR ok");
                });
        }
    } // namespace

    int run_webrtc_device_interactive(WebRtcDeviceOptions opt, std::istream& in)
    {
        apply_default_webrtc_device_options(opt);
        const char* tag = opt.log_tag ? opt.log_tag : "MAIN";

        int                                            rc = 0;
        std::shared_ptr<sync_context_t>                sync_ctx;
        std::unique_ptr<app::media::audio::AudioDrv>   audio_drv;
        app::media::audio::SubHandle                   mic_sub = 0;
        std::unique_ptr<app::media::camera::CameraDrv> camera_drv;
        std::shared_ptr<Signaling>                     signaling;
        std::shared_ptr<WebRTCSystem>                  webrtc;

        if (!opt.boot_log_line.empty())
            LOG_INFO(tag, "%s", opt.boot_log_line.c_str());

        sync_ctx = std::make_shared<sync_context_t>();
        if (sync_init(sync_ctx.get()) != 0)
            LOG_WARN(tag, "同步初始化失败，继续");

        {
            SignalingConfig sig;
            sig.device_id  = opt.device_id;
            sig.server_url = opt.server_url;

            WebRTCConfig wc;
            wc.ice.stun_servers         = std::move(opt.ice_stun);
            wc.ice.turn_servers         = std::move(opt.ice_turn);
            wc.enable_video_pacing      = true;
            wc.video_pacing_bps         = 0;
            wc.video_pacing_interval_ms = 10;

            signaling = std::make_shared<Signaling>(sig);
            webrtc    = std::make_shared<WebRTCSystem>(wc);
        }

        app::media::audio::AudioCfg audio_cfg{};
        audio_cfg.capture.rate      = AUDIO_SAMPLE_RATE;
        audio_cfg.capture.channels  = AUDIO_CHANNELS;
        audio_cfg.capture.frame_ms  = AUDIO_FRAME_DURATION_MS;
        audio_cfg.playback.rate     = AUDIO_SAMPLE_RATE;
        audio_cfg.playback.channels = AUDIO_CHANNELS;
        audio_cfg.playback.frame_ms = AUDIO_FRAME_DURATION_MS;
        audio_cfg.playback.volume   = opt.audio_playback_volume;
        audio_cfg.opus.rate         = AUDIO_SAMPLE_RATE;
        audio_cfg.opus.channels     = AUDIO_CHANNELS;
        audio_cfg.opus.bitrate      = AUDIO_BIT_RATE;
        audio_cfg.opus.vbr          = true;
        audio_cfg.enable_capture    = true;
        audio_cfg.enable_playback   = true;
        audio_cfg.enable_opus       = true;
        audio_cfg.enable_proc       = true;

        audio_cfg.capture.rk.enable_vqe = (AUDIO_RK_VQE_ENABLED != 0);
        if (AUDIO_RK_VQE_ENABLED)
        {
            audio_cfg.capture.rk.vqe_config_file   = AUDIO_RK_VQE_CONFIG_PATH;
            audio_cfg.capture.rk.ao_dev_id_for_vqe = AUDIO_RK_VQE_AO_DEV_ID;
            audio_cfg.capture.rk.ao_chn_for_vqe    = AUDIO_RK_VQE_AO_CHN_ID;
            audio_cfg.capture.rk.vqe_gap_ms        = AUDIO_RK_VQE_GAP_MS;
        }

        audio_drv = std::make_unique<app::media::audio::AudioDrv>();
        if (audio_drv->init(audio_cfg) != app::media::audio::Error::OK)
        {
            LOG_ERROR(tag, "音频初始化失败");
            rc = 1;
            goto cleanup;
        }

        mic_sub = audio_drv->subscribe(
            app::media::audio::StreamId::MicProcessed,
            [webrtc, raw = audio_drv.get()](const app::media::audio::FramePtr& pcm)
            {
                if (!pcm || pcm->size == 0 || !webrtc->isConnected())
                    return;
                auto opus_frame = raw->opus().encode(pcm);
                if (opus_frame && opus_frame->size > 0)
                    webrtc->sendAudioData(opus_frame->data, opus_frame->size,
                                          opus_frame->timestamp);
            });

        if (audio_drv->start() != app::media::audio::Error::OK)
        {
            LOG_ERROR(tag, "音频启动失败");
            rc = 1;
            goto cleanup;
        }
        LOG_INFO(tag, "音频就绪");

        {
            app::media::camera::CameraCfg cam_cfg{};
            cam_cfg.h264.width   = CAMERA_WIDTH;
            cam_cfg.h264.height  = CAMERA_HEIGHT;
            cam_cfg.h264.fps     = CAMERA_FPS;
            cam_cfg.h264.bitrate = H264_Default_Bitrate;
            cam_cfg.iq_file_dir  = ISP_PATH;

            camera_drv = std::make_unique<app::media::camera::CameraDrv>();
            if (camera_drv->init(cam_cfg, sync_ctx) == app::media::camera::Error::OK &&
                camera_drv->start() == app::media::camera::Error::OK)
            {
                camera_drv->set_webrtc_sink(
                    [webrtc, sync_ctx](const uint8_t* data, size_t size, uint64_t pts,
                                       bool keyframe)
                    {
                        if (!data || size == 0 || !webrtc->isConnected())
                            return;
                        uint64_t ts =
                            sync_ctx ? sync_get_timestamp(sync_ctx.get(), pts, false) : pts;
                        webrtc->sendVideoData(data, size, ts, keyframe);
                    });
                LOG_INFO(tag, "视频就绪");
            }
            else
            {
                LOG_WARN(tag, "摄像头初始化失败，仅音频模式");
                camera_drv.reset();
            }
        }

        webrtc->onStateChanged(
            [tag](WebRTCState state)
            { LOG_INFO(tag, "WebRTC 状态: %s", WebRTCSystem::stateToString(state)); });
        webrtc->onAudioData(
            [raw = audio_drv.get()](const uint8_t* data, size_t size)
            {
                if (!data || size == 0 || !raw)
                    return;
                raw->pushPlaybackOpus(data, size);
            });

        signaling->onStatusChanged(
            [tag](SignalingStatus status)
            { LOG_INFO(tag, "信令: %s", Signaling::statusToString(status).c_str()); });
        signaling->onError([tag](SignalingError /* e */, const std::string& msg)
                           { LOG_ERROR(tag, "信令错误: %s", msg.c_str()); });
        signaling->onRoomInfoChanged(
            [tag](const RoomInfo& info)
            { LOG_INFO(tag, "房间 %s 人数=%d", info.room_id.c_str(), info.num); });

        if (!signaling->connect())
        {
            LOG_ERROR(tag, "信令连接失败");
            rc = 1;
            goto cleanup;
        }

        {
            int wait_cnt = 0;
            while (signaling->getStatus() != SignalingStatus::CONNECTED)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (++wait_cnt > 100)
                {
                    LOG_ERROR(tag, "信令连接超时");
                    rc = 1;
                    goto cleanup;
                }
            }
        }
        LOG_INFO(tag, "信令已连接");

        if (webrtc->init(signaling) != WebRTCError::NONE)
        {
            LOG_ERROR(tag, "WebRTC 初始化失败");
            rc = 1;
            goto cleanup;
        }
        LOG_INFO(tag, "WebRTC 就绪");

        attach_camera_weak_net(webrtc, camera_drv.get(), tag);

        LOG_INFO(tag, "%s", opt.command_hint.c_str());
        {
            std::string input;
            while (in >> input)
            {
                if (input == "q" || input == "Q")
                    break;
                if (input == "j" || input == "J")
                {
                    if (signaling->joinRoom())
                        LOG_INFO(tag, "已加入房间");
                    else
                        LOG_ERROR(tag, "加入房间失败");
                }
                else if (input == "l" || input == "L")
                {
                    if (signaling->leaveRoom())
                        LOG_INFO(tag, "已离开房间");
                    else
                        LOG_ERROR(tag, "离开房间失败");
                }
                else if (input == "c" || input == "C")
                {
                    std::string peer = signaling->getPeerDeviceId();
                    if (peer.empty())
                        LOG_ERROR(tag, "未配对");
                    else if (webrtc->sendConnectionRequest(peer, true, true, true))
                        LOG_INFO(tag, "连接请求已发往 %s", peer.c_str());
                    else
                        LOG_ERROR(tag, "发送连接请求失败");
                }
            }
        }

    cleanup:
        if (webrtc)
            webrtc->deinit();
        if (signaling)
            signaling->disconnect();
        if (audio_drv)
        {
            if (mic_sub)
                audio_drv->unsubscribe(mic_sub);
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

        if (!opt.end_log_line.empty())
            LOG_INFO(tag, "%s", opt.end_log_line.c_str());
        return rc;
    }

} // namespace app::protocol::webrtc
