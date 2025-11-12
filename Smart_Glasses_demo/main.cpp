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

// 默认配置
constexpr const char* DEFAULT_DEVICE_ID = "glasses_123456";
constexpr const char* DEFAULT_SERVER_URL = "ws://192.168.50.184:8000";

// 打印使用说明
void printUsage(const char* program_name) {
    std::cout << "用法: " << program_name << " [选项]" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  <服务器地址>           信令服务器地址 (例如: ws://192.168.1.100:8000)" << std::endl;
    std::cout << "  -h, --help            显示此帮助信息" << std::endl;
    std::cout << "  -d, --device <ID>     指定设备ID (默认: glasses_123456)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program_name << "                              # 使用默认配置" << std::endl;
    std::cout << "  " << program_name << " ws://192.168.1.100:8000     # 指定服务器地址" << std::endl;
    std::cout << "  " << program_name << " -d my_glasses ws://localhost:8000  # 指定设备ID和服务器" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string device_id = DEFAULT_DEVICE_ID;
    std::string server_url = DEFAULT_SERVER_URL;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-d" || arg == "--device") {
            if (i + 1 < argc) {
                device_id = argv[++i];
            } else {
                std::cerr << "错误: -d/--device 选项需要指定设备ID" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        } else if (arg.find("ws://") == 0 || arg.find("wss://") == 0) {
            // 识别为服务器地址
            server_url = arg;
        } else {
            std::cerr << "错误: 未知选项 '" << arg << "'" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // 日志系统初始化
    app::tool::log::Logger::getInstance().initialize(app::tool::log::LogConfig());

    LOG_INFO("Main", "开始初始化WebRTC系统...");
    LOG_INFO("Main", "设备ID: %s", device_id.c_str());
    LOG_INFO("Main", "服务器地址: %s", server_url.c_str());
    
    // 创建音频配置
    app::media::audio::AudioConfig audio_config;
    audio_config.sample_rate = 48000;        // 采样率
    audio_config.channels = 1;               // 单声道
    audio_config.frame_duration_ms = 20;     // 帧长

    // 创建相机配置
    app::media::camera::VideoConfig video_config;
    video_config.width = 1920;           // 分辨率宽度
    video_config.height = 1080;           // 分辨率高度

    // 创建信令配置
    app::protocol::webrtc::SignalingConfig sig_config;
    sig_config.deviceId = device_id;
    sig_config.serverUrl = server_url;

    // 创建WebRTC配置
    app::protocol::webrtc::WebRTCConfig webrtc_config;
    webrtc_config.enableAudioSend = true;
    webrtc_config.enableAudioReceive = true;
    webrtc_config.enableVideoSend = true;
    webrtc_config.enableVideoReceive = false;
    webrtc_config.enableDataChannel = true;
    webrtc_config.ice.stunServers = {"stun:stun.l.google.com:19302"};

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
    if (audio_system->initialize(sync_ctx) != app::media::audio::AudioError::NONE) {
        LOG_ERROR("Main", "音频系统初始化失败");
        return 1;
    }

    // 启动音频播放
    if (audio_system->startPlayback() != app::media::audio::AudioError::NONE) {
        LOG_ERROR("Main", "音频播放启动失败");
        return 1;
    }

    // 初始化视频系统
    if (video_system->initialize(sync_ctx) != app::media::camera::VideoError::NONE) {
        LOG_ERROR("Main", "视频系统初始化失败");
        return 1;
    }

    // 设置webrtc的音频回调
    audio_system->setWebRTCAudioCallback(
        [webrtc](app::media::audio::AudioFramePtr opus_frame) {
            if (!opus_frame || opus_frame->size == 0) {
                return;
            }
            if (!webrtc->isConnected()) {
                return;
            }
            webrtc->sendAudioData(opus_frame->data, opus_frame->size, opus_frame->timestamp);
        }
    );

    // 设置webrtc的视频回调
    video_system->setWebRTCVideoCallback(
        [webrtc](app::media::camera::VideoFramePtr video_frame) {
            if (!video_frame || video_frame->size == 0) {
                return;
            }
            if (!webrtc->isConnected()) {
                return;
            }
            webrtc->sendVideoData(
                video_frame->data, 
                video_frame->size, 
                video_frame->timestamp,
                video_frame->is_keyframe
            );
        }
    );

    // 设置WebRTC状态变化回调
    webrtc->onStateChanged([audio_system, video_system](app::protocol::webrtc::WebRTCState state) {
        LOG_INFO("Main", "WebRTC 状态: %d", static_cast<int>(state));

        if (state == app::protocol::webrtc::WebRTCState::CONNECTED) {
            if (audio_system->startWebRTCMode() != app::media::audio::AudioError::NONE) {
                LOG_ERROR("Main", "启动 WebRTC 音频模式失败");
            }

            if (video_system->startWebRTCMode() != app::media::camera::VideoError::NONE) {
                LOG_ERROR("Main", "启动 WebRTC 视频模式失败");
            }
        } else if (state == app::protocol::webrtc::WebRTCState::FAILED || state == app::protocol::webrtc::WebRTCState::DISCONNECTED) {
            audio_system->stopWebRTCMode();
            video_system->stopWebRTCMode();
        }

    });

    webrtc->onAudioData(
        [audio_system](const uint8_t* data, size_t size) {
            if (!data || size == 0) {
                return;
            }
            // 打印接收到的音频数据大小
            LOG_INFO("Main", "📥 收到音频数据: %zu 字节", size);
            
            auto pcm_frame = audio_system->decodeOpus(data, size);
            if (pcm_frame) {
                audio_system->pushPlaybackFrame(pcm_frame);
            }
        }
    );

    // 设置信令状态变化回调
    signaling->onStatusChanged([](app::protocol::webrtc::SignalingStatus status) {
        LOG_INFO("Main", "信令状态: %s", app::protocol::webrtc::Signaling::statusToString(status).c_str());
        // switch (status) {
        //     case app::protocol::webrtc::SignalingStatus::DISCONNECTED:
        //         LOG_INFO("Main", "信令状态: DISCONNECTED (未连接)");
        //         break;
        //     case app::protocol::webrtc::SignalingStatus::CONNECTING:
        //         LOG_INFO("Main", "信令状态: CONNECTING (连接中...)");
        //         break;
        //     case app::protocol::webrtc::SignalingStatus::CONNECTED:
        //         LOG_INFO("Main", "信令状态: CONNECTED (已连接)");
        //         break;
        //     case app::protocol::webrtc::SignalingStatus::JOINED:
        //         LOG_INFO("Main", "信令状态: JOINED (已加入房间，等待配对...)");
        //         break;
        //     case app::protocol::webrtc::SignalingStatus::PAIRED:
        //         LOG_INFO("Main", "信令状态: PAIRED (已配对，可以开始WebRTC连接)");
        //         break;
        // }
    });

    // 设置错误回调
    signaling->onError([](app::protocol::webrtc::SignalingError error, const std::string& message) {
        LOG_ERROR("Main", "错误: %s", message.c_str());
    });
    
    // 设置房间信息变化回调
    signaling->onRoomInfoChanged([](const app::protocol::webrtc::RoomInfo& room_info) {
        LOG_INFO("Main", "房间信息变化: 房间ID=%s, 人数=%d, 状态=%s", 
                 room_info.roomId.c_str(), room_info.num, room_info.roomStatus.c_str());
    });

    // 连接信令服务器
    if (!signaling->connect()) {
        LOG_ERROR("Main", "连接信令服务器失败");
        return 1;
    }

    // 初始化WebRTC系统
    if (webrtc->open(signaling) != app::protocol::webrtc::WebRTCError::NONE) {
        LOG_ERROR("Main", "WebRTCSystem 初始化失败");
        return 1;
    }

    // 等待连接成功
    LOG_INFO("Main", "等待连接建立...");
    int wait_count = 0;
    while (signaling->getStatus() != app::protocol::webrtc::SignalingStatus::CONNECTED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
        if (wait_count > 100) {  // 10秒超时
            LOG_ERROR("Main", "连接超时，请检查服务器地址和网络连接");
            return 1;
        }
    }
    
    // 加入房间
    if (!signaling->joinRoom()) {
        LOG_ERROR("Main", "加入房间失败");
        return 1;
    }

    // 主循环
    std::cout << "[Main] 输入 'q' 退出" << std::endl;
    std::string input;
    while (std::cin >> input) {
        if (input == "q" || input == "Q") {
            break;
        }
    }

    audio_system->stopWebRTCMode();           // 停掉采集/编码
    audio_system->stopPlayback();             // 停掉播放
    audio_system->shutdown();                 // 释放 PortAudio、内存池等

    video_system->stopWebRTCMode();           // 停止视频推流
    video_system->stopRecord();               // 停止录像
    video_system->shutdown();                 // 释放RKMPI资源

    sync_deinit(sync_ctx.get());    // 关闭时间同步

    webrtc->close();                         // 关闭 PeerConnection/任务队列
    signaling->disconnect();                 // 断开 WebSocket
    
    return 0;
}