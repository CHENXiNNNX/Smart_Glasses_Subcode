#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "rtc/rtc.hpp"
#include "app/protocol/webrtc/signaling.h"
#include "app/protocol/webrtc/webrtc.h"
#include "app/media/camera/camera.h"
#include <nlohmann/json.hpp>

using namespace glasses::protocol;
using json = nlohmann::json;

// 设备配置
constexpr const char* DEVICE_ID = "glasses_123456";
constexpr const char* SERVER_URL = "ws://192.168.50.100:8081";

// 全局变量
std::shared_ptr<Signaling> signaling;
std::shared_ptr<WebRTCManager> webrtcManager;
video_system_t* video_system = nullptr;

// 创建WebRTC配置
WebRTCConfig createWebRTCConfig() {
    WebRTCConfig config;
    
    // 功能开关配置
    config.enableDataChannel = true;    // 启用数据通道
    config.enableAudioSend = false;     // 音频发送暂时关闭
    config.enableAudioReceive = false;  // 音频接收暂时关闭
    config.enableVideoSend = true;      // 启用视频发送
    
    // 数据通道配置
    config.dataChannelLabel = "glasses_data_channel";
    
    // STUN服务器配置
    config.stunServers = {
        "stun:stun.l.google.com:19302"
    };
    
    return config;
}

// Camera WebRTC帧回调函数
void onVideoFrameCallback(void *data, int len, uint64_t timestamp) {
    static int frame_count = 0;
    frame_count++;
    
    // 每10帧打印一次信息，避免日志过多
    if (frame_count % 10 == 0) {
        std::cout << "[Camera] 发送第 " << frame_count << " 帧: " 
                  << len << " 字节, 时间戳: " << timestamp << std::endl;
    }
    
    // 转发给WebRTC管理器
    if (webrtcManager) {
        webrtcManager->sendVideoFrame(static_cast<const uint8_t*>(data), len, timestamp);
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
        
        // 当WebRTC连接建立成功后，自动启动视频流
        if (status == WebRTCStatus::CONNECTED) {
            std::cout << "[Main] WebRTC连接建立成功，自动启动视频流..." << std::endl;
            
            // 1. 先启动基础视频流处理线程
            if (start_video_stream(video_system) != 0) {
                std::cout << "[Main] 启动基础视频流失败" << std::endl;
            } else {
                std::cout << "[Main] 基础视频流启动成功" << std::endl;
                
                // 2. 设置视频模式为WebRTC
                if (set_video_mode(video_system, VIDEO_MODE_WEBRTC) != 0) {
                    std::cout << "[Main] 设置视频模式失败" << std::endl;
                } else {
                    std::cout << "[Main] 视频模式设置为WebRTC" << std::endl;
                    
                    // 3. 启动WebRTC视频流
                    if (start_webrtc_video_stream(video_system) != 0) {
                        std::cout << "[Main] 启动WebRTC视频流失败" << std::endl;
                    } else {
                        std::cout << "[Main] WebRTC视频流启动成功，开始发送视频帧" << std::endl;
                    }
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
    if (init_video_system(&video_system, 1280, 720, VIDEO_MODE_NONE) != 0) {
        std::cout << "[Main] 视频系统初始化失败" << std::endl;
        return -1;
    }
    std::cout << "[Main] 视频系统初始化成功" << std::endl;

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
    
    // 设置Camera WebRTC回调
    std::cout << "[Main] 设置Camera WebRTC回调..." << std::endl;
    if (set_webrtc_callback(video_system, webrtcManager.get(), onVideoFrameCallback) != 0) {
        std::cout << "[Main] 设置WebRTC回调失败" << std::endl;
        return -1;
    }
    std::cout << "[Main] WebRTC回调设置成功" << std::endl;

    // 连接到服务器
    if (!signaling->connect()) {
        std::cout << "[Main] 连接服务器失败" << std::endl;
        return -1;
    }
    
    std::cout << "[Main] 正在连接服务器..." << std::endl;

    // 主循环 - 用户交互
    std::cout << "[Main] 客户端运行中，视频流将在连接建立后自动启动" << std::endl;
    std::cout << "[Main] 输入命令:" << std::endl;
    std::cout << "  q - 退出程序" << std::endl;
    std::cout << "  d - 发送数据通道测试消息" << std::endl;
    std::cout << "  i - 显示连接信息" << std::endl;
    std::cout << "  s - 停止视频流" << std::endl;
    
    char input;
    while (std::cin >> input) {
        if (input == 'q' || input == 'Q') {
            break;
        } else if (input == 'd') {
            // 发送数据通道测试消息
            if (webrtcManager->isDataChannelOpen()) {
                std::string testMessage = "Test message from main loop";
                webrtcManager->sendDataChannelMessage(testMessage);
            } else {
                std::cout << "[Main] 数据通道未打开" << std::endl;
            }
        } else if (input == 'i') {
            // 显示连接信息
            std::cout << "[Main] === 连接信息 ===" << std::endl;
            std::cout << "[Main] 信令状态: " << static_cast<int>(signaling->getStatus()) << std::endl;
            std::cout << "[Main] WebRTC状态: " << static_cast<int>(webrtcManager->getStatus()) << std::endl;
            std::cout << "[Main] 设备ID: " << signaling->getDeviceId() << std::endl;
            std::cout << "[Main] 对端ID: " << signaling->getPeerDeviceId() << std::endl;
            std::cout << "[Main] 角色: " << signaling->getRole() << std::endl;
            std::cout << "[Main] 数据通道: " << (webrtcManager->isDataChannelOpen() ? "已打开" : "未打开") << std::endl;
        } else if (input == 's') {
            // 停止视频流
            std::cout << "[Main] 停止WebRTC视频流..." << std::endl;
            stop_webrtc_video_stream(video_system);
            stop_video_stream(video_system);
            set_video_mode(video_system, VIDEO_MODE_NONE);
            std::cout << "[Main] WebRTC视频流已停止" << std::endl;
        } else {
            std::cout << "[Main] 未知命令: " << input << std::endl;
        }
    }

    std::cout << "[Main] 正在关闭客户端..." << std::endl;
    
    // 停止视频流
    if (video_system) {
        stop_webrtc_video_stream(video_system);
        stop_video_stream(video_system);
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
    
    if (video_system) {
        release_video_system(&video_system);
        std::cout << "[Main] 视频系统已释放" << std::endl;
    }

    std::cout << "[Main] 客户端已退出" << std::endl;
    return 0;
}