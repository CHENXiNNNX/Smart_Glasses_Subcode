#include <iostream>
#include <memory>

#include "app/media/media_config.h"
#include "rtc/rtc.hpp"
#include "app/protocol/webrtc/signaling.h"
#include "app/protocol/webrtc/webrtc.h"
#include "app/media/camera/camera.h"
#include "app/media/audio/audio.h"
#include <nlohmann/json.hpp>

using namespace glasses::protocol;
using json = nlohmann::json;

// 设备配置
constexpr const char* DEVICE_ID = "glasses_123456";
constexpr const char* SERVER_URL = "ws://192.168.50.184:8000";

// 全局变量
std::shared_ptr<Signaling> signaling;
std::shared_ptr<WebRTCManager> webrtcManager;
video_system_t* video_system = nullptr;
audio_system_t* audio_system = nullptr;

// 创建WebRTC配置
WebRTCConfig createWebRTCConfig() {
    WebRTCConfig config;
    
    // 功能开关配置
    config.enableDataChannel = false;    // 启用数据通道
    config.enableAudioSend = true;      // 启用音频发送
    config.enableAudioReceive = false;   // 启用音频接收
    config.enableVideoSend = false;      // 启用视频发送
    
    // 数据通道配置
    config.dataChannelLabel = "glasses_data_channel";
    
    // STUN服务器配置
    config.stunServers = {
        "stun:stun.l.google.com:19302",
        "stun:stun.miwifi.com:3478",
        "stun:stun.chat.bilibili.com:3478"
    };
    
    return config;
}

// 视频帧回调函数
void onVideoFrameCallback(void *data, int len, uint64_t timestamp) {
    
    // 转发给WebRTC管理器
    if (webrtcManager) {
        webrtcManager->sendVideoFrame(static_cast<const uint8_t*>(data), len, timestamp);
    }
}

// 音频数据回调函数
void onAudioDataCallback(void *data, int len, uint64_t timestamp) {
    
    // 转发给WebRTC管理器
    if (webrtcManager) {
        webrtcManager->sendAudioData(static_cast<const uint8_t*>(data), len, timestamp);
    }
}

// 设置信令回调
void setupSignalingCallbacks() {
    if (!signaling) return;
    
    // 状态变化回调
    signaling->onStatusChanged([](ConnectionStatus status) {
        std::cout << "[Main] 信令状态变化: " << static_cast<int>(status) << std::endl;
        
        if (status == ConnectionStatus::CONNECTED) {
            std::cout << "[Main] 自动加入房间..." << std::endl;
            signaling->joinRoom();
        }
    });
    
    // 错误处理回调
    signaling->onError([](ErrorCode errorCode, const std::string& errorMessage) {
        std::cout << "[Main] 信令错误 [" << static_cast<int>(errorCode) 
                  << "]: " << errorMessage << std::endl;
    });
}

// 设置WebRTC回调
void setupWebRTCCallbacks() {
    if (!webrtcManager) return;
    
    // 状态变化回调
    webrtcManager->onStatusChanged([](WebRTCStatus status) {
        std::cout << "[Main] WebRTC状态变化: " << static_cast<int>(status) << std::endl;
        
        // 当WebRTC连接建立成功后，自动启动音视频流
        if (status == WebRTCStatus::CONNECTED) {
            std::cout << "[Main] WebRTC连接建立成功，启动音视频流..." << std::endl;
            
            // 启动视频流
            if (start_video_stream(video_system) == 0) {
                std::cout << "[Main] 基础视频流启动成功" << std::endl;
                
                if (set_video_mode(video_system, VIDEO_MODE_WEBRTC) == 0) {
                    std::cout << "[Main] 视频模式设置为WebRTC" << std::endl;
                    
                    #if USE_WEBRTC
                    if (start_webrtc_video_stream(video_system) == 0) {
                        std::cout << "[Main] WebRTC视频流启动成功" << std::endl;
                    } else {
                        std::cout << "[Main] WebRTC视频流启动失败" << std::endl;
                    }
                    #endif
                } else {
                    std::cout << "[Main] 设置视频模式失败" << std::endl;
                }
            } else {
                std::cout << "[Main] 启动基础视频流失败" << std::endl;
            }
            
            // 启动音频流
            if (audio_system) {
                if (set_audio_mode(audio_system, AUDIO_MODE_WEBRTC) == AUDIO_ERROR_NONE) {
                    std::cout << "[Main] 音频模式设置为WebRTC" << std::endl;
                    
                    #if USE_WEBRTC
                    if (start_webrtc_audio_stream(audio_system) == AUDIO_ERROR_NONE) {
                        std::cout << "[Main] WebRTC音频流启动成功" << std::endl;
                    } else {
                        std::cout << "[Main] WebRTC音频流启动失败" << std::endl;
                    }
                    #endif
                } else {
                    std::cout << "[Main] 设置音频模式失败" << std::endl;
                }
            }
        }
    });
    
    // 数据通道消息回调
    webrtcManager->onDataChannelMessage([](const std::string& message) {
        std::cout << "[Main] 收到数据通道消息: " << message << std::endl;
    });
}

int main(void) {
    std::cout << "=== 智能眼镜WebRTC客户端启动 ===" << std::endl;
    std::cout << "设备ID: " << DEVICE_ID << std::endl;
    std::cout << "服务器地址: " << SERVER_URL << std::endl;

    // 初始化libdatachannel日志
    rtc::InitLogger(rtc::LogLevel::Info);

    // 初始化视频系统
    std::cout << "[Main] 初始化视频系统..." << std::endl;
    if (init_video_system(&video_system, CAMERA_WIDTH, CAMERA_HEIGHT, VIDEO_MODE_NONE) != 0) {
        std::cout << "[Main] 视频系统初始化失败" << std::endl;
        return -1;
    }
    std::cout << "[Main] 视频系统初始化成功" << std::endl;

    // 初始化音频系统
    std::cout << "[Main] 初始化音频系统..." << std::endl;
    audio_system = new audio_system_t();
    if (audio_system_init(audio_system) != AUDIO_ERROR_NONE) {
        std::cout << "[Main] 音频系统初始化失败" << std::endl;
        delete audio_system;
        audio_system = nullptr;
        return -1;
    }
    std::cout << "[Main] 音频系统初始化成功" << std::endl;

    // 创建WebRTC配置
    WebRTCConfig config = createWebRTCConfig();
    
    // 创建模块实例
    signaling = std::make_shared<Signaling>(DEVICE_ID, SERVER_URL);
    webrtcManager = std::make_shared<WebRTCManager>(config);
    
    // 初始化WebRTC管理器
    if (!webrtcManager->initialize(signaling)) {
        std::cout << "[Main] WebRTC管理器初始化失败" << std::endl;
        return -1;
    }
    
    // 设置回调函数
    setupSignalingCallbacks();
    setupWebRTCCallbacks();
    
    // 设置视频WebRTC回调
    std::cout << "[Main] 设置视频WebRTC回调..." << std::endl;
    #if USE_WEBRTC
    if (set_webrtc_callback(video_system, webrtcManager.get(), onVideoFrameCallback) != 0) {
        std::cout << "[Main] 设置视频WebRTC回调失败" << std::endl;
        return -1;
    }
    #endif
    std::cout << "[Main] 视频WebRTC回调设置成功" << std::endl;

    // 设置音频WebRTC回调
    std::cout << "[Main] 设置音频WebRTC回调..." << std::endl;
    #if USE_WEBRTC
    if (set_webrtc_audio_callback(audio_system, webrtcManager.get(), onAudioDataCallback) != AUDIO_ERROR_NONE) {
        std::cout << "[Main] 设置音频WebRTC回调失败" << std::endl;
        return -1;
    }
    #endif
    std::cout << "[Main] 音频WebRTC回调设置成功" << std::endl;

    // 连接到服务器
    if (!signaling->connect()) {
        std::cout << "[Main] 连接服务器失败" << std::endl;
        return -1;
    }
    
    std::cout << "[Main] 正在连接服务器..." << std::endl;
    std::cout << "[Main] 客户端运行中，音视频流将在连接建立后自动启动" << std::endl;
    std::cout << "[Main] 输入 'q' 退出程序" << std::endl;
    
    // 简化的主循环 - 只等待退出命令
    char input;
    while (std::cin >> input) {
        if (input == 'q' || input == 'Q') {
            break;
        } else {
            std::cout << "[Main] 输入 'q' 退出程序" << std::endl;
        }
    }

    std::cout << "[Main] 正在关闭客户端..." << std::endl;
    
    // 停止音视频流
    if (audio_system) {
        #if USE_WEBRTC
        stop_webrtc_audio_stream(audio_system);
        #endif
        audio_system_deinit(audio_system);
        delete audio_system;
        audio_system = nullptr;
        std::cout << "[Main] 音频系统已释放" << std::endl;
    }
    
    if (video_system) {
        #if USE_WEBRTC
        stop_webrtc_video_stream(video_system);
        #endif
        stop_video_stream(video_system);
        release_video_system(&video_system);
        std::cout << "[Main] 视频系统已释放" << std::endl;
    }
    
    // 按依赖关系清理资源
    if (webrtcManager) {
        webrtcManager->shutdown();
        webrtcManager.reset();
    }
    
    if (signaling) {
        signaling->disconnect();
        signaling.reset();
    }

    std::cout << "[Main] 客户端已退出" << std::endl;
    return 0;
}