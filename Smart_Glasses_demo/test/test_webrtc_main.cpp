#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// 信令和WebRTC模块
#include "app/protocol/webrtc/signaling.hpp"
#include "app/protocol/webrtc/webrtc.hpp"
#include "app/tool/log/log.hpp"
#include "app/media/audio/audio.hpp"
#include "app/media/camera/camera.hpp"
#include "app/media/sync.hpp"

using namespace app::tool::log;

#define LOG_TAG "MAIN"

const std::string device_id  = "glasses_123456";
const std::string server_url = "ws://192.168.50.70:8000";

int main()
{
    // 日志系统初始化
    Logger::getInstance().initialize(LogConfig());

    LOG_INFO(LOG_TAG, "开始运行应用程序");

    // 创建音频配置
    app::media::audio::AudioConfig audio_config;
    audio_config.sample_rate       = 48000; // 采样率
    audio_config.channels          = 1;     // 单声道
    audio_config.frame_duration_ms = 20;    // 帧长

    // 创建相机配置
    app::media::camera::VideoConfig video_config;
    video_config.width  = 1920; // 分辨率宽度
    video_config.height = 1080; // 分辨率高度

    // 创建信令配置
    app::protocol::webrtc::SignalingConfig sig_config;
    sig_config.device_id  = device_id;
    sig_config.server_url = server_url;

    // 创建WebRTC配置
    app::protocol::webrtc::WebRTCConfig webrtc_config;
    webrtc_config.ice.stun_servers = {"stun:stun.l.google.com:19302"};

    // 创建信令实例
    auto signaling = std::make_shared<app::protocol::webrtc::Signaling>(sig_config);
    // 创建WebRTC系统实例
    auto webrtc = std::make_shared<app::protocol::webrtc::WebRTCSystem>(webrtc_config);
    // 创建音频系统实例
    auto audio_system = std::make_shared<app::media::audio::AudioSystem>(audio_config);
    // 创建相机系统实例
    auto video_system = std::make_shared<app::media::camera::VideoSystem>(video_config);
    // 创建同步上下文
    auto sync_ctx = std::make_shared<sync_context_t>();
    sync_init(sync_ctx.get());

    // 初始化音频系统
    if (audio_system->initialize(sync_ctx) != app::media::audio::AudioError::NONE)
    {
        LOG_ERROR(LOG_TAG, "音频系统初始化失败");
        return 1;
    }

    // 启动音频播放
    if (audio_system->startStream(app::media::audio::StreamDirection::OUTPUT) !=
        app::media::audio::AudioError::NONE)
    {
        LOG_ERROR(LOG_TAG, "音频播放启动失败");
        return 1;
    }

    // 初始化视频系统
    if (video_system->initialize(sync_ctx) != app::media::camera::VideoError::NONE)
    {
        LOG_ERROR(LOG_TAG, "视频系统初始化失败");
        return 1;
    }

    // 设置webrtc的音频回调
    audio_system->setWebRTCAudioCallback(
        [webrtc](app::media::audio::AudioFramePtr opus_frame)
        {
            if (!opus_frame || opus_frame->size == 0)
            {
                return;
            }
            if (!webrtc->isConnected())
            {
                return;
            }
            webrtc->sendAudioData(opus_frame->data, opus_frame->size, opus_frame->timestamp);
        });

    // 设置webrtc的视频回调
    video_system->setWebRTCVideoCallback(
        [webrtc](app::media::camera::VideoFramePtr video_frame)
        {
            if (!video_frame || video_frame->size == 0)
            {
                return;
            }
            if (!webrtc->isConnected())
            {
                return;
            }
            webrtc->sendVideoData(video_frame->data, video_frame->size, video_frame->timestamp,
                                  video_frame->is_keyframe);
        });

    // 设置WebRTC状态变化回调
    webrtc->onStateChanged(
        [audio_system, video_system](app::protocol::webrtc::WebRTCState state)
        {
            LOG_INFO(LOG_TAG, "WebRTC 状态: %s",
                     app::protocol::webrtc::WebRTCSystem::stateToString(state));

            if (state == app::protocol::webrtc::WebRTCState::CONNECTED)
            {
                if (audio_system->startWebRTCMode() != app::media::audio::AudioError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "启动 WebRTC 音频模式失败");
                }

                if (video_system->startWebRTCMode() != app::media::camera::VideoError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "启动 WebRTC 视频模式失败");
                }
            }
            else if (state == app::protocol::webrtc::WebRTCState::FAILED ||
                     state == app::protocol::webrtc::WebRTCState::DISCONNECTED)
            {
                audio_system->stopWebRTCMode();
                video_system->stopWebRTCMode();
            }
        });

    webrtc->onAudioData(
        [audio_system](const uint8_t* data, size_t size)
        {
            if (!data || size == 0)
            {
                return;
            }
            // 打印接收到的音频数据大小
            // LOG_INFO(LOG_TAG, "收到音频数据: %zu 字节", size);

            auto pcm_frame = audio_system->decodeOpus(data, size);
            if (pcm_frame)
            {
                audio_system->pushPlaybackFrame(pcm_frame);
            }
        });

    // 设置信令状态变化回调
    signaling->onStatusChanged(
        [](app::protocol::webrtc::SignalingStatus status)
        {
            LOG_INFO(LOG_TAG, "信令状态: %s",
                     app::protocol::webrtc::Signaling::statusToString(status).c_str());
            // switch (status) {
            //     case app::protocol::webrtc::SignalingStatus::DISCONNECTED:
            //         LOG_INFO(LOG_TAG, "信令状态: DISCONNECTED (未连接)");
            //         break;
            //     case app::protocol::webrtc::SignalingStatus::CONNECTING:
            //         LOG_INFO(LOG_TAG, "信令状态: CONNECTING (连接中...)");
            //         break;
            //     case app::protocol::webrtc::SignalingStatus::CONNECTED:
            //         LOG_INFO(LOG_TAG, "信令状态: CONNECTED (已连接)");
            //         break;
            //     case app::protocol::webrtc::SignalingStatus::JOINED:
            //         LOG_INFO(LOG_TAG, "信令状态: JOINED (已加入房间，等待配对...)");
            //         break;
            //     case app::protocol::webrtc::SignalingStatus::PAIRED:
            //         LOG_INFO(LOG_TAG, "信令状态: PAIRED (已配对，可以开始WebRTC连接)");
            //         break;
            // }
        });

    // 设置错误回调
    signaling->onError(
        [](app::protocol::webrtc::SignalingError /* error */, const std::string& message)
        { LOG_ERROR(LOG_TAG, "错误: %s", message.c_str()); });

    // 设置房间信息变化回调
    signaling->onRoomInfoChanged(
        [](const app::protocol::webrtc::RoomInfo& room_info)
        {
            LOG_INFO(LOG_TAG, "房间信息变化: 房间ID=%s, 人数=%d", room_info.room_id.c_str(),
                     room_info.num);
        });

    // 连接信令服务器
    if (!signaling->connect())
    {
        LOG_ERROR(LOG_TAG, "连接信令服务器失败");
        return 1;
    }

    // 等待连接成功
    LOG_INFO(LOG_TAG, "等待连接建立...");
    int wait_count = 0;
    while (signaling->getStatus() != app::protocol::webrtc::SignalingStatus::CONNECTED)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
        if (wait_count > 100)
        { // 10秒超时
            LOG_ERROR(LOG_TAG, "连接超时，请检查服务器地址和网络连接");
            return 1;
        }
    }

    // 初始化WebRTC系统
    if (webrtc->initialize(signaling) != app::protocol::webrtc::WebRTCError::NONE)
    {
        LOG_ERROR(LOG_TAG, "WebRTC系统初始化失败");
        return 1;
    }
    LOG_INFO(LOG_TAG, "WebRTC系统初始化成功");

    // 主循环
    LOG_INFO(LOG_TAG, "输入 'q' 退出, 输入 'j' 加入房间, 输入 'l' 离开房间, 输入 'c' 发送连接请求");
    std::string input;
    while (std::cin >> input)
    {
        if (input == "q" || input == "Q")
        {
            break;
        }
        else if (input == "j" || input == "J")
        {
            // 加入房间
            if (!signaling->joinRoom())
            {
                LOG_ERROR(LOG_TAG, "加入房间失败");
            }
            else
            {
                LOG_INFO(LOG_TAG, "已发送加入房间请求");
            }
        }
        else if (input == "l" || input == "L")
        {
            // 离开房间
            if (!signaling->leaveRoom())
            {
                LOG_ERROR(LOG_TAG, "离开房间失败");
            }
            else
            {
                LOG_INFO(LOG_TAG, "已发送离开房间请求");
            }
        }
        else if (input == "c" || input == "C")
        {
            // 发送连接请求
            std::string peer_id = signaling->getPeerDeviceId();
            if (peer_id.empty())
            {
                LOG_ERROR(LOG_TAG, "未配对，无法获取对端设备ID");
            }
            else
            {
                if (!webrtc->sendConnectionRequest(peer_id, true, true, true))
                {
                    LOG_ERROR(LOG_TAG, "发送连接请求失败");
                }
                else
                {
                    LOG_INFO(LOG_TAG, "连接请求已发送到: %s", peer_id.c_str());
                }
            }
        }
    }

    audio_system->stopWebRTCMode();                                       // 停掉采集/编码
    audio_system->stopStream(app::media::audio::StreamDirection::OUTPUT); // 停掉播放
    audio_system->shutdown(); // 释放 PortAudio、内存池等

    video_system->stopWebRTCMode(); // 停止视频推流
    video_system->stopRecord();     // 停止录像
    video_system->shutdown();       // 释放RKMPI资源

    sync_deinit(sync_ctx.get()); // 关闭时间同步

    webrtc->shutdown();      // 关闭 PeerConnection/任务队列
    signaling->disconnect(); // 断开 WebSocket

    return 0;
}