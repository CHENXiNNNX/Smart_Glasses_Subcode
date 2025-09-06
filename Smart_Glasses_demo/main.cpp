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
#include "app/protocol/websocket/websocket.h"
#include <nlohmann/json.hpp>

using namespace glasses::protocol;
using json = nlohmann::json;

// 设备配置
constexpr const char* DEVICE_ID = "glasses_123456";
constexpr const char* SERVER_URL = "ws://192.168.2.17:8000";

// 全局变量
std::shared_ptr<WebSocketClient> wsClient;
std::shared_ptr<rtc::PeerConnection> peerConnection;
std::shared_ptr<rtc::DataChannel> dataChannel;

// 创建WebRTC PeerConnection
std::shared_ptr<rtc::PeerConnection> createPeerConnection() {
    rtc::Configuration config;
    // 可选：添加STUN服务器用于NAT穿透
    // config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    
    return std::make_shared<rtc::PeerConnection>(config);
}

// 处理WebRTC连接建立
void setupWebRTCCallbacks() {
    if (!peerConnection) return;

    // 本地描述生成回调
    peerConnection->onLocalDescription([](rtc::Description description) {
        std::cout << "[WebRTC] 本地SDP生成: " << description.typeString() << std::endl;
        
        if (wsClient && wsClient->isPaired()) {
            std::string sdp = std::string(description);
            std::string peerDeviceId = wsClient->getPeerDeviceId();
            
            if (description.type() == rtc::Description::Type::Offer) {
                wsClient->sendOffer(sdp, peerDeviceId);
            } else if (description.type() == rtc::Description::Type::Answer) {
                wsClient->sendAnswer(sdp, peerDeviceId);
            }
        }
    });

    // ICE候选生成回调
    peerConnection->onLocalCandidate([](rtc::Candidate candidate) {
        std::cout << "[WebRTC] 本地ICE候选: " << candidate.candidate() << std::endl;
        
        if (wsClient && wsClient->isPaired()) {
            std::string candidateStr = std::string(candidate);
            std::string peerDeviceId = wsClient->getPeerDeviceId();
            wsClient->sendIceCandidate(candidateStr, peerDeviceId);
        }
    });

    // 连接状态变化回调
    peerConnection->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC] 连接状态: " << static_cast<int>(state) << std::endl;
    });

    // 收集状态变化回调
    peerConnection->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        std::cout << "[WebRTC] ICE收集状态: " << static_cast<int>(state) << std::endl;
    });
}

// 设置WebSocket回调
void setupWebSocketCallbacks() {
    if (!wsClient) return;

    // 状态变化回调
    wsClient->onStatusChanged([](ConnectionStatus status) {
        std::cout << "[WebSocket] 状态变化: " << static_cast<int>(status) << std::endl;
        
        if (status == ConnectionStatus::CONNECTED) {
            std::cout << "[INFO] 自动加入房间..." << std::endl;
            wsClient->joinRoom();
        }
    });

    // 角色分配回调
    wsClient->onRoleReceived([](const json& msg) {
        std::cout << "[WebSocket] 收到角色分配，开始WebRTC连接..." << std::endl;
        
        // 创建PeerConnection
        peerConnection = createPeerConnection();
        setupWebRTCCallbacks();
        
        std::string role = wsClient->getRole();
        if (role == "offerer") {
            // 作为发起方，创建DataChannel并生成Offer
            std::cout << "[WebRTC] 作为发起方创建DataChannel" << std::endl;
            
            dataChannel = peerConnection->createDataChannel("test");
            dataChannel->onOpen([]() {
                std::cout << "[DataChannel] 连接已打开" << std::endl;
                
                // 发送测试消息
                std::string testMessage = "Hello from glasses device!";
                dataChannel->send(testMessage);
                std::cout << "[DataChannel] 发送消息: " << testMessage << std::endl;
            });
            
            dataChannel->onMessage([](auto data) {
                if (std::holds_alternative<std::string>(data)) {
                    std::string message = std::get<std::string>(data);
                    std::cout << "[DataChannel] 收到消息: " << message << std::endl;
                }
            });
            
        } else if (role == "answerer") {
            // 作为应答方，等待DataChannel
            std::cout << "[WebRTC] 作为应答方等待DataChannel" << std::endl;
            
            peerConnection->onDataChannel([](std::shared_ptr<rtc::DataChannel> dc) {
                std::cout << "[DataChannel] 收到远程DataChannel: " << dc->label() << std::endl;
                dataChannel = dc;
                
                dataChannel->onOpen([]() {
                    std::cout << "[DataChannel] 连接已打开" << std::endl;
                });
                
                dataChannel->onMessage([](auto data) {
                    if (std::holds_alternative<std::string>(data)) {
                        std::string message = std::get<std::string>(data);
                        std::cout << "[DataChannel] 收到消息: " << message << std::endl;
                        
                        // 回复消息
                        std::string reply = "Reply from peer: " + message;
                        dataChannel->send(reply);
                    }
                });
            });
        }
    });

    // SDP Offer接收回调
    wsClient->onOfferReceived([](const json& msg) {
        std::cout << "[WebSocket] 收到SDP Offer" << std::endl;
        
        if (peerConnection && msg.contains("data") && msg["data"].contains("sdp")) {
            std::string sdp = msg["data"]["sdp"].get<std::string>();
            
            try {
                rtc::Description remoteDesc(sdp, rtc::Description::Type::Offer);
                peerConnection->setRemoteDescription(remoteDesc);
                std::cout << "[WebRTC] 设置远程SDP Offer成功" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "[ERROR] 设置远程SDP失败: " << e.what() << std::endl;
            }
        }
    });

    // SDP Answer接收回调
    wsClient->onAnswerReceived([](const json& msg) {
        std::cout << "[WebSocket] 收到SDP Answer" << std::endl;
        
        if (peerConnection && msg.contains("data") && msg["data"].contains("sdp")) {
            std::string sdp = msg["data"]["sdp"].get<std::string>();
            
            try {
                rtc::Description remoteDesc(sdp, rtc::Description::Type::Answer);
                peerConnection->setRemoteDescription(remoteDesc);
                std::cout << "[WebRTC] 设置远程SDP Answer成功" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "[ERROR] 设置远程SDP失败: " << e.what() << std::endl;
            }
        }
    });

    // ICE候选接收回调
    wsClient->onIceCandidateReceived([](const json& msg) {
        std::cout << "[WebSocket] 收到ICE候选" << std::endl;
        
        if (peerConnection && msg.contains("data") && msg["data"].contains("candidate")) {
            std::string candidateStr = msg["data"]["candidate"].get<std::string>();
            
            try {
                rtc::Candidate candidate(candidateStr);
                peerConnection->addRemoteCandidate(candidate);
                std::cout << "[WebRTC] 添加远程ICE候选成功" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "[ERROR] 添加远程ICE候选失败: " << e.what() << std::endl;
            }
        }
    });

    // 错误处理回调
    wsClient->onError([](ErrorCode errorCode, const std::string& errorMessage) {
        std::cout << "[ERROR] WebSocket错误 [" << static_cast<int>(errorCode) 
                  << "]: " << errorMessage << std::endl;
    });
}

int main(void) {
    std::cout << "=== 智能眼镜WebSocket客户端启动 ===" << std::endl;
    std::cout << "设备ID: " << DEVICE_ID << std::endl;
    std::cout << "服务器地址: " << SERVER_URL << std::endl;

    // 初始化日志
    rtc::InitLogger(rtc::LogLevel::Info);

    // 创建WebSocket客户端
    wsClient = std::make_shared<WebSocketClient>(DEVICE_ID, SERVER_URL);
    setupWebSocketCallbacks();

    // 连接到服务器
    if (!wsClient->connect()) {
        std::cout << "[ERROR] 连接服务器失败" << std::endl;
        return -1;
    }

    std::cout << "[INFO] 正在连接服务器..." << std::endl;

    // 主循环
    std::cout << "[INFO] 客户端运行中，按 'q' 退出..." << std::endl;
    
    char input;
    while (std::cin >> input) {
        if (input == 'q' || input == 'Q') {
            break;
        } else if (input == 's' && dataChannel && dataChannel->isOpen()) {
            // 发送测试消息
            std::string testMessage = "Test message from main loop";
            dataChannel->send(testMessage);
            std::cout << "[INFO] 发送测试消息: " << testMessage << std::endl;
        } else if (input == 'i') {
            // 显示连接信息
            std::cout << "[INFO] 连接状态: " << static_cast<int>(wsClient->getStatus()) << std::endl;
            std::cout << "[INFO] 设备ID: " << wsClient->getDeviceId() << std::endl;
            std::cout << "[INFO] 对端ID: " << wsClient->getPeerDeviceId() << std::endl;
            std::cout << "[INFO] 角色: " << wsClient->getRole() << std::endl;
        }
    }

    std::cout << "[INFO] 正在关闭客户端..." << std::endl;
    
    // 清理资源
    if (dataChannel) {
        dataChannel.reset();
    }
    
    if (peerConnection) {
        peerConnection.reset();
    }
    
    if (wsClient) {
        wsClient->disconnect();
        wsClient.reset();
    }

    std::cout << "[INFO] 客户端已退出" << std::endl;
    return 0;
}
