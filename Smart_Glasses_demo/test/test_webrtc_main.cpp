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
#include <nlohmann/json.hpp>

using namespace glasses::protocol;
using json = nlohmann::json;

// 设备配置
constexpr const char* DEVICE_ID = "glasses_123456";
constexpr const char* SERVER_URL = "ws://192.168.10.75:8000";

// 全局变量
std::shared_ptr<Signaling> signaling;
std::shared_ptr<WebRTCManager> webrtcManager;

// 创建WebRTC配置
WebRTCConfig createWebRTCConfig() {
    WebRTCConfig config;
    
    // 功能开关配置
    config.enableDataChannel = true;    // 启用数据通道
    config.enableAudioSend = false;     // 音频发送暂时关闭
    config.enableAudioReceive = false;  // 音频接收暂时关闭
    config.enableVideoSend = false;     // 视频发送暂时关闭
    
    // 数据通道配置
    config.dataChannelLabel = "glasses_data_channel";
    
    // STUN服务器配置
    config.stunServers = {
        "stun:stun.l.google.com:19302"
    };
    
    return config;
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

    // 连接到服务器
    if (!signaling->connect()) {
        std::cout << "[Main] 连接服务器失败" << std::endl;
        return -1;
    }

    std::cout << "[Main] 正在连接服务器..." << std::endl;

    // 主循环 - 用户交互
    std::cout << "[Main] 客户端运行中，输入命令:" << std::endl;
    std::cout << "  q - 退出程序" << std::endl;
    std::cout << "  s - 发送数据通道测试消息" << std::endl;
    std::cout << "  i - 显示连接信息" << std::endl;
    std::cout << "  a - 测试音频功能（空实现）" << std::endl;
    std::cout << "  v - 测试视频功能（空实现）" << std::endl;
    
    char input;
    while (std::cin >> input) {
        if (input == 'q' || input == 'Q') {
            break;
        } else if (input == 's') {
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
        } else if (input == 'a') {
            // 测试音频功能（空实现）
            std::cout << "[Main] 测试音频发送..." << std::endl;
            bool result = webrtcManager->startAudioSend();
            std::cout << "[Main] 音频发送结果: " << (result ? "成功" : "失败（空实现）") << std::endl;
            
            std::cout << "[Main] 测试音频接收..." << std::endl;
            result = webrtcManager->startAudioReceive();
            std::cout << "[Main] 音频接收结果: " << (result ? "成功" : "失败（空实现）") << std::endl;
        } else if (input == 'v') {
            // 测试视频功能（空实现）
            std::cout << "[Main] 测试视频发送..." << std::endl;
            bool result = webrtcManager->startVideoSend();
            std::cout << "[Main] 视频发送结果: " << (result ? "成功" : "失败（空实现）") << std::endl;
            
            // 模拟发送视频帧
            uint8_t dummyData[] = {0x00, 0x01, 0x02, 0x03};
            webrtcManager->sendVideoFrame(dummyData, sizeof(dummyData), 123456789);
            std::cout << "[Main] 发送视频帧（空实现）" << std::endl;
        } else {
            std::cout << "[Main] 未知命令: " << input << std::endl;
        }
    }

    std::cout << "[Main] 正在关闭客户端..." << std::endl;
    
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