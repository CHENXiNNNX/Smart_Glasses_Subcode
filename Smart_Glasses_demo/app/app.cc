#include "app.hpp"
#include "tool/log/log.hpp"
#include "network/wifi/wifi.hpp"
#include "media/audio/audio.hpp"
#include "media/camera/camera.hpp"
#include "protocol/webrtc/signaling.hpp"
#include "protocol/webrtc/webrtc.hpp"
#include "chatbot/chatbot.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace app::tool::log;
namespace wifi    = app::network::wifi;
namespace audio   = app::media::audio;
namespace video   = app::media::camera;
namespace chatbot = app::chatbot;

namespace
{
    constexpr const char* LOG_TAG = "APP";
} // namespace

namespace app
{

    App::App() {}

    App::~App()
    {
        deinitChatbot();
        deinitWebrtc();
        deinitSignaling();
        deinitVideo();
        deinitAudio();
        deinitSync();
        deinitNetwork();
        deinitLog();
    }

    // 初始化日志
    bool App::initLog()
    {
        bool log_err = Logger::getInstance().initialize(LogConfig());
        if (!log_err)
        {
            LOG_ERROR(LOG_TAG, "初始化日志失败");
            return false;
        }
        return true;
    }

    // 释放日志系统
    bool App::deinitLog()
    {
        bool log_err = Logger::getInstance().shutdown();
        if (!log_err)
        {
            LOG_ERROR(LOG_TAG, "释放日志失败");
            return false;
        }
        return true;
    }

    // 初始化网络
    bool App::initNetwork()
    {
        // 初始化WiFi系统
        if (!initWiFi())
        {
            return false;
        }

        // 检查WiFi连接状态
        bool err = checkNetwork();
        if (err)
        {
            // 已连接，直接返回成功
            return true;
        }

        // 未连接，进入配网流程（阻塞直到成功）
        LOG_INFO(LOG_TAG, "进入配网流程...");

        // 阻塞循环，直到连接成功
        while (true)
        {
            err = connectNetwork();
            if (err)
            {
                // 连接成功
                return true;
            }

            // 连接失败，等待后重试
            LOG_WARN(LOG_TAG, "WiFi连接失败，10秒后重试...");
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }

    // 释放网络
    bool App::deinitNetwork()
    {
        return deinitWiFi();
    }

    // 初始化WiFi
    bool App::initWiFi()
    {
        // 创建WiFi配置
        wifi::WifiConfig wifi_config;
        wifi_config.auto_connect_on_init = true; // 自动连接已保存的WiFi

        // 创建WiFi管理器
        wifi_manager_ = std::make_unique<wifi::WifiManager>(wifi_config);

        // 初始化WiFi管理器
        wifi::WifiError wifi_err = wifi_manager_->initialize();
        if (wifi_err != wifi::WifiError::NONE)
        {
            LOG_ERROR(LOG_TAG, "  WiFi管理器初始化失败");
            wifi_manager_.reset();
            return false;
        }
        LOG_INFO(LOG_TAG, "  WiFi管理器初始化成功");
        return true;
    }

    // 释放WiFi
    bool App::deinitWiFi()
    {
        if (wifi_manager_)
        {
            wifi_manager_->shutdown();
            wifi_manager_.reset();
        }
        return true;
    }

    // 初始化蓝牙
    bool App::initBluetooth()
    {
        // TODO: 初始化蓝牙
        return true;
    }

    // 释放蓝牙
    bool App::deinitBluetooth()
    {
        // TODO: 释放蓝牙
        return true;
    }

    // 检查网络连接状态
    bool App::checkNetwork()
    {
        if (!wifi_manager_)
        {
            LOG_ERROR(LOG_TAG, "WiFi管理器未初始化");
            return false;
        }

        // 检查WiFi是否已连接
        if (wifi_manager_->isConnected())
        {
            std::string ssid = wifi_manager_->getCurrentSSID();
            std::string ip   = wifi_manager_->getIPAddress();
            LOG_INFO(LOG_TAG, "WiFi已连接: %s (IP: %s)", ssid.c_str(), ip.c_str());
            return true;
        }

        LOG_INFO(LOG_TAG, "WiFi未连接");
        return false;
    }

    // 连接网络
    bool App::connectNetwork()
    {
        if (!wifi_manager_)
        {
            LOG_ERROR(LOG_TAG, "WiFi管理器未初始化");
            return false;
        }

        // 先尝试连接已保存的WiFi
        LOG_INFO(LOG_TAG, "尝试连接已保存的WiFi...");
        wifi::WifiError wifi_err = wifi_manager_->connectSavedNetwork();
        if (wifi_err == wifi::WifiError::NONE)
        {
            std::string ssid = wifi_manager_->getCurrentSSID();
            std::string ip   = wifi_manager_->getIPAddress();
            LOG_INFO(LOG_TAG, "  已保存WiFi连接成功: %s (IP: %s)", ssid.c_str(), ip.c_str());
            return true;
        }

        // 连接已保存WiFi失败，返回错误
        LOG_WARN(LOG_TAG, "已保存WiFi连接失败");
        return false;
    }

    // 断开网络
    bool App::disconnectNetwork()
    {
        if (!wifi_manager_)
        {
            LOG_ERROR(LOG_TAG, "WiFi管理器未初始化");
            return false;
        }

        if (!wifi_manager_->isConnected())
        {
            LOG_INFO(LOG_TAG, "WiFi未连接");
            return true;
        }

        wifi::WifiError wifi_err = wifi_manager_->disconnect();
        if (wifi_err != wifi::WifiError::NONE)
        {
            LOG_ERROR(LOG_TAG, "WiFi断开失败");
            return false;
        }

        LOG_INFO(LOG_TAG, "WiFi已断开");
        return true;
    }

    // 搜索已保存的网络
    bool App::searchSavedNetwork()
    {
        if (!wifi_manager_)
        {
            LOG_ERROR(LOG_TAG, "WiFi管理器未初始化");
            return false;
        }

        // 获取已保存的网络列表
        std::vector<wifi::SavedNetworkInfo> saved = wifi_manager_->getSavedNetworks();

        if (saved.empty())
        {
            LOG_INFO(LOG_TAG, "没有已保存的WiFi网络");
            return true;
        }

        LOG_INFO(LOG_TAG, "找到 %zu 个已保存的WiFi网络:", saved.size());

        // 输出每个网络的详细信息
        for (size_t i = 0; i < saved.size(); ++i)
        {
            const auto& net = saved[i];
            LOG_INFO(LOG_TAG, "  [%zu] SSID: %s", i + 1, net.ssid.c_str());
            LOG_INFO(LOG_TAG, "       网络ID: %d", net.network_id);

            if (net.is_current)
            {
                LOG_INFO(LOG_TAG, "       状态: 当前连接");
            }
            else
            {
                LOG_INFO(LOG_TAG, "       状态: 未连接");
            }

            if (net.is_enabled_auto)
            {
                LOG_INFO(LOG_TAG, "       自动连接: 启用");
            }
            else
            {
                LOG_INFO(LOG_TAG, "       自动连接: 禁用");
            }

            if (net.priority > 0)
            {
                LOG_INFO(LOG_TAG, "       优先级: %d", net.priority);
            }
        }

        return true;
    }

    // 忘记网络
    bool App::forgetNetwork(const std::string& ssid)
    {
        if (!wifi_manager_)
        {
            LOG_ERROR(LOG_TAG, "WiFi管理器未初始化");
            return false;
        }

        if (ssid.empty())
        {
            LOG_ERROR(LOG_TAG, "SSID不能为空");
            return false;
        }

        // 检查网络是否已保存
        if (!wifi_manager_->isNetworkSaved(ssid))
        {
            LOG_WARN(LOG_TAG, "WiFi \"%s\" 未在已保存列表中", ssid.c_str());
            return false;
        }

        // 执行删除
        wifi::WifiError err = wifi_manager_->forgetNetwork(ssid);
        if (err != wifi::WifiError::NONE)
        {
            LOG_ERROR(LOG_TAG, "删除WiFi \"%s\" 失败: %d", ssid.c_str(), static_cast<int>(err));
            return false;
        }

        LOG_INFO(LOG_TAG, "WiFi \"%s\" 已删除", ssid.c_str());
        return true;
    }

    // 初始化时间同步上下文
    bool App::initSync()
    {
        // 创建同步上下文
        sync_ctx_ = std::make_shared<sync_context_t>();

        // 初始化时间同步
        int sync_err = sync_init(sync_ctx_.get());
        if (sync_err != 0)
        {
            LOG_ERROR(LOG_TAG, "  同步系统初始化失败");
            sync_ctx_.reset();
            return false;
        }

        LOG_INFO(LOG_TAG, "  同步系统初始化成功");
        return true;
    }

    // 释放时间同步上下文
    bool App::deinitSync()
    {
        if (!sync_ctx_)
        {
            return true;
        }

        // 释放时间同步资源
        sync_deinit(sync_ctx_.get());
        sync_ctx_.reset();

        LOG_INFO(LOG_TAG, "  同步系统已释放");
        return true;
    }

    // 初始化音频系统
    bool App::initAudio(int sample_rate, int channels, int frame_duration_ms)
    {
        // 创建音频配置
        audio::AudioConfig audio_config;
        audio_config.sample_rate       = sample_rate; // 采样率
        audio_config.channels          = channels;     // 单声道
        audio_config.frame_duration_ms = frame_duration_ms;    // 帧时长（毫秒）

        // 创建音频系统
        audio_system_ = std::make_unique<audio::AudioSystem>(audio_config);

        // 初始化音频系统并传入同步上下文
        audio::AudioError audio_err = audio_system_->initialize(sync_ctx_);
        if (audio_err != audio::AudioError::NONE)
        {
            LOG_ERROR(LOG_TAG, "  音频系统初始化失败");
            audio_system_.reset();
            return false;
        }
        LOG_INFO(LOG_TAG, "  音频系统初始化成功");

        // 启动录音
        audio_err = audio_system_->startStream(app::media::audio::StreamDirection::INPUT);
        if (audio_err != audio::AudioError::NONE)
        {
            LOG_ERROR(LOG_TAG, "启动录音失败");
            return false;
        }

        return true;
    }

    // 释放音频系统
    bool App::deinitAudio()
    {
        if (!audio_system_)
        {
            return true;
        }

        // 停止录音（如果正在录音）
        if (audio_system_->isStreamRunning(app::media::audio::StreamDirection::INPUT))
        {
            audio_system_->stopStream(app::media::audio::StreamDirection::INPUT);
        }

        // 关闭音频系统
        audio_system_->shutdown();
        audio_system_.reset();

        return true;
    }

    // 初始化视频系统
    bool App::initVideo(int width, int height)
    {
        // 创建视频配置
        video::VideoConfig video_config;
        video_config.width  = width;  // 分辨率宽度
        video_config.height = height; // 分辨率高度

        // 创建视频系统
        video_system_ = std::make_unique<video::VideoSystem>(video_config);

        // 初始化视频系统并传入同步上下文
        video::VideoError video_err = video_system_->initialize(sync_ctx_);
        if (video_err != video::VideoError::NONE)
        {
            LOG_ERROR(LOG_TAG, "  视频系统初始化失败");
            video_system_.reset();
            return false;
        }
        LOG_INFO(LOG_TAG, "  视频系统初始化成功");
        return true;
    }

    // 释放视频系统
    bool App::deinitVideo()
    {
        if (!video_system_)
        {
            return true;
        }

        video_system_->shutdown();
        video_system_.reset();
        return true;
    }

    // 初始化信令
    bool App::initSignaling(std::string device_id, std::string server_url)
    {
        // 创建信令配置
        app::protocol::webrtc::SignalingConfig sig_config;
        sig_config.device_id = device_id;
        sig_config.server_url = server_url;

        // 创建信令实例
        signaling_ = std::make_shared<app::protocol::webrtc::Signaling>(sig_config);

        // 设置错误回调
        signaling_->onError(
            [](app::protocol::webrtc::SignalingError error, const std::string& message) {
                (void)error; // 当前仅记录文本，防止未使用告警
                LOG_ERROR(LOG_TAG, "信令错误: %s", message.c_str());
            });

        // 设置状态变化回调
        signaling_->onStatusChanged([](app::protocol::webrtc::SignalingStatus status) {
            LOG_INFO(LOG_TAG, "信令状态变化: %s", 
                     app::protocol::webrtc::Signaling::statusToString(status).c_str());
        });

        // 连接信令服务器
        if (!signaling_->connect())
        {
            LOG_ERROR(LOG_TAG, "  信令服务器连接失败");
            signaling_.reset();
            return false;
        }

        LOG_INFO(LOG_TAG, "  信令系统初始化成功");
        return true;
    }

    // 释放信令
    bool App::deinitSignaling()
    {
        if (!signaling_)
        {
            return true;
        }

        // 断开信令连接
        signaling_->disconnect();
        signaling_.reset();

        LOG_INFO(LOG_TAG, "  信令系统已释放");
        return true;
    }

    // 初始化Webrtc
    bool App::initWebrtc()
    {
        // if (webrtc_)
        // {
        //     LOG_WARN(LOG_TAG, "WebRTC系统已初始化，跳过");
        //     return true;
        // }

        // if (!signaling_)
        // {
        //     LOG_ERROR(LOG_TAG, "信令系统未初始化，无法初始化WebRTC");
        //     return false;
        // }

        // // 创建WebRTC配置
        // app::protocol::webrtc::WebRTCConfig webrtc_config;
        // webrtc_config.enableAudioSend    = true;
        // webrtc_config.enableAudioReceive = true;
        // webrtc_config.enableVideoSend    = true;
        // webrtc_config.enableVideoReceive = false;
        // webrtc_config.enableDataChannel  = true;
        // webrtc_config.ice.stunServers    = {"stun:stun.l.google.com:19302"};

        // // 创建WebRTC实例
        // webrtc_ = std::make_shared<app::protocol::webrtc::WebRTCSystem>(webrtc_config);

        // // 打开WebRTC系统
        // app::protocol::webrtc::WebRTCError webrtc_err = webrtc_->open(signaling_);
        // if (webrtc_err != app::protocol::webrtc::WebRTCError::NONE)
        // {
        //     LOG_ERROR(LOG_TAG, "  WebRTC系统初始化失败: %d", static_cast<int>(webrtc_err));
        //     webrtc_.reset();
        //     return false;
        // }

        // std::weak_ptr<app::protocol::webrtc::WebRTCSystem> webrtc_wp = webrtc_;

        // if (audio_system_)
        // {
        //     audio_system_->setWebRTCAudioCallback(
        //         [webrtc_wp](app::media::audio::AudioFramePtr opus_frame)
        //         {
        //             auto webrtc = webrtc_wp.lock();
        //             if (!webrtc || !opus_frame || opus_frame->size == 0 || !webrtc->isConnected())
        //             {
        //                 return;
        //             }
        //             webrtc->sendAudioData(opus_frame->data, opus_frame->size,
        //                                   opus_frame->timestamp);
        //         });

        //     webrtc_->onAudioData(
        //         [this](const uint8_t* data, size_t size)
        //         {
        //             if (!audio_system_ || !data || size == 0)
        //             {
        //                 return;
        //             }

        //             if (!audio_system_->isStreamRunning(app::media::audio::StreamDirection::OUTPUT))
        //             {
        //                 auto play_err = audio_system_->startStream(app::media::audio::StreamDirection::OUTPUT);
        //                 if (play_err != app::media::audio::AudioError::NONE &&
        //                     play_err != app::media::audio::AudioError::ALREADY_RUNNING)
        //                 {
        //                     LOG_ERROR(LOG_TAG, "启动WebRTC播放流失败: %d", static_cast<int>(play_err));
        //                     return;
        //                 }
        //             }

        //             auto pcm_frame = audio_system_->decodeOpus(data, size);
        //             if (pcm_frame)
        //             {
        //                 audio_system_->pushPlaybackFrame(pcm_frame);
        //             }
        //         });
        // }

        // if (video_system_)
        // {
        //     video_system_->setWebRTCVideoCallback(
        //         [webrtc_wp](app::media::camera::VideoFramePtr video_frame)
        //         {
        //             auto webrtc = webrtc_wp.lock();
        //             if (!webrtc || !video_frame || video_frame->size == 0 || !webrtc->isConnected())
        //             {
        //                 return;
        //             }

        //             webrtc->sendVideoData(video_frame->data, video_frame->size,
        //                                   video_frame->timestamp, video_frame->is_keyframe);
        //         });
        // }

        // // AI音频 -> WebRTC 音频
        // auto prepare_audio_for_webrtc = [this]()
        // {
        //     if (!audio_system_)
        //     {
        //         return;
        //     }

        //     auto stop_ai_err = audio_system_->stopAIMode();
        //     if (stop_ai_err != app::media::audio::AudioError::NONE &&
        //         stop_ai_err != app::media::audio::AudioError::NOT_INITIALIZED)
        //     {
        //         LOG_WARN(LOG_TAG, "停止AI模式失败: %d", static_cast<int>(stop_ai_err));
        //     }

        //     if (audio_system_->isStreamRunning(app::media::audio::StreamDirection::OUTPUT))
        //     {
        //         audio_system_->stopStream(app::media::audio::StreamDirection::OUTPUT);
        //     }

        //     const int max_wait_ms       = 500;
        //     const int check_interval_ms = 50;
        //     int       waited_ms         = 0;

        //     while (audio_system_->getMainState() != app::media::audio::AudioMainState::NONE &&
        //            waited_ms < max_wait_ms)
        //     {
        //         std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        //         waited_ms += check_interval_ms;
        //     }

        //     if (audio_system_->getMainState() != app::media::audio::AudioMainState::NONE)
        //     {
        //         LOG_WARN(LOG_TAG, "音频主状态仍未退出AI模式，强制切换");
        //     }
        // };

        // // 状态变化时切换音视频系统模式
        // webrtc_->onStateChanged(
        //     [this, prepare_audio_for_webrtc](app::protocol::webrtc::WebRTCState state)
        //     {
        //         LOG_INFO(LOG_TAG, "WebRTC 状态变化: %d", static_cast<int>(state));

        //         if (state == app::protocol::webrtc::WebRTCState::CONNECTED)
        //         {
        //             if (audio_system_)
        //             {
        //                 prepare_audio_for_webrtc();
        //                 auto err = audio_system_->startWebRTCMode();
        //                 if (err != app::media::audio::AudioError::NONE &&
        //                     err != app::media::audio::AudioError::ALREADY_RUNNING)
        //                 {
        //                     LOG_ERROR(LOG_TAG, "启动WebRTC音频模式失败: %d", static_cast<int>(err));
        //                 }
        //             }

        //             if (video_system_)
        //             {
        //                 auto err = video_system_->startWebRTCMode();
        //                 if (err != app::media::camera::VideoError::NONE &&
        //                     err != app::media::camera::VideoError::ALREADY_STARTED)
        //                 {
        //                     LOG_ERROR(LOG_TAG, "启动WebRTC视频模式失败: %d", static_cast<int>(err));
        //                 }
        //             }
        //         }
        //         else if (state == app::protocol::webrtc::WebRTCState::FAILED ||
        //                  state == app::protocol::webrtc::WebRTCState::DISCONNECTED)
        //         {
        //             if (audio_system_)
        //             {
        //                 audio_system_->stopWebRTCMode();
        //             }
        //             if (video_system_)
        //             {
        //                 video_system_->stopWebRTCMode();
        //             }
        //         }
        //     });

        // LOG_INFO(LOG_TAG, "  WebRTC系统初始化成功");
        return true;
    }

    // 释放Webrtc
    bool App::deinitWebrtc()
    {
        // if (!webrtc_)
        // {
        //     return true;
        // }

        // if (audio_system_)
        // {
        //     audio_system_->stopWebRTCMode();
        //     audio_system_->setWebRTCAudioCallback(nullptr);
        // }

        // if (video_system_)
        // {
        //     video_system_->stopWebRTCMode();
        //     video_system_->setWebRTCVideoCallback(nullptr);
        // }

        // webrtc_->close();
        // webrtc_.reset();

        // LOG_INFO(LOG_TAG, "  WebRTC系统已释放");
        return true;
    }

    // 初始化聊天机器人
    bool App::initChatbot()
    {
        // 确保音频系统已初始化
        if (!audio_system_)
        {
            LOG_ERROR(LOG_TAG, "音频系统未初始化，无法初始化聊天机器人");
            return false;
        }

        // 创建聊天机器人配置
        chatbot::ChatbotConfig chatbot_config;

        // 创建聊天机器人系统
        chatbot_system_ = std::make_unique<chatbot::ChatbotSystem>(chatbot_config);

        // 注入音频系统（必须在 open() 之前）
        chatbot_system_->setAudioSystem(audio_system_.get());

        // 注入视频系统（必须在 open() 之前）
        if (video_system_)
        {
            chatbot_system_->setVideoSystem(video_system_.get());
        }

        // 注入WiFi管理器（必须在 open() 之前）
        if (wifi_manager_)
        {
            chatbot_system_->setWifiManager(wifi_manager_.get());
        }

        // 注入信令系统（必须在 open() 之前）
        if (signaling_)
        {
            chatbot_system_->setSignaling(signaling_.get());
        }

        // 注入WebRTC系统（必须在 open() 之前）
        if (webrtc_)
        {
            chatbot_system_->setWebRTCSystem(webrtc_.get());
        }

        // 打开聊天机器人系统
        chatbot::ChatbotError chatbot_err = chatbot_system_->open();
        if (chatbot_err != chatbot::ChatbotError::NONE)
        {
            LOG_ERROR(LOG_TAG, "  聊天机器人系统初始化失败: %s", chatbot::errorToString(chatbot_err));
            chatbot_system_.reset();
            return false;
        }

        LOG_INFO(LOG_TAG, "  聊天机器人系统初始化成功");
        return true;
    }

    // 释放聊天机器人
    bool App::deinitChatbot()
    {
        if (!chatbot_system_)
        {
            return true;
        }

        // 关闭聊天机器人系统
        chatbot_system_->close();
        chatbot_system_.reset();

        LOG_INFO(LOG_TAG, "  聊天机器人系统已释放");
        return true;
    }

    // 初始化
    void App::init()
    {
        // 初始化日志
        if (!initLog())
        {
            return;
        }

        // 初始化同步系统
        if (!initSync())
        {
            return;
        }

        // 初始化音频系统
        if (!initAudio(48000, 1, 20))
        {
            return;
        }

        // 初始化视频系统
        if (!initVideo(1920, 1080))
        {
            return;
        }

        // 初始化网络
        if (!initNetwork())
        {
            return;
        }

        // 初始化信令
        if (!initSignaling("glasses_123456", "ws://10.93.1.49:8000"))
        {
            return;
        }

        // 初始化Webrtc
        if (!initWebrtc())
        {
            return;
        }

        // 初始化聊天机器人
        if (!initChatbot())
        {
            return;
        }

        // 初始化完成
        LOG_INFO(LOG_TAG, "初始化完成");
    }

    // 运行
    void App::run()
    {
        init();

        // 主循环保持程序运行，等待用户输入退出命令
        LOG_INFO(LOG_TAG, "========================================");
        LOG_INFO(LOG_TAG, "  系统运行中，等待唤醒词...");
        LOG_INFO(LOG_TAG, "  输入 'q' 或 'Q' 退出程序");
        LOG_INFO(LOG_TAG, "========================================");

        std::string input;
        while (std::cin >> input)
        {
            if (input == "q" || input == "Q")
            {
                LOG_INFO(LOG_TAG, "收到退出指令，正在关闭系统...");
                break;
            }
            LOG_INFO(LOG_TAG, "未知命令: %s ", input.c_str());
        }
    }

} // namespace app