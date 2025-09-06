#include "webrtc.h"
#include "signaling.h"
#include <iostream>

using namespace glasses::protocol;

WebRTCManager::WebRTCManager(const WebRTCConfig& config)
    : config_(config)
    , status_(WebRTCStatus::DISCONNECTED)
    , signaling_(nullptr)
    , peerConnection_(nullptr)
    , dataChannel_(nullptr) {
    
    std::cout << "[WebRTC] 初始化WebRTC管理器" << std::endl;
    std::cout << "[WebRTC] 数据通道: " << (config_.enableDataChannel ? "启用" : "禁用") << std::endl;
    std::cout << "[WebRTC] 音频发送: " << (config_.enableAudioSend ? "启用" : "禁用") << std::endl;
    std::cout << "[WebRTC] 音频接收: " << (config_.enableAudioReceive ? "启用" : "禁用") << std::endl;
    std::cout << "[WebRTC] 视频发送: " << (config_.enableVideoSend ? "启用" : "禁用") << std::endl;
}

WebRTCManager::~WebRTCManager() {
    shutdown();
}

bool WebRTCManager::initialize(std::shared_ptr<Signaling> signaling) {
    if (!signaling) {
        std::cout << "[WebRTC] 信令模块为空，初始化失败" << std::endl;
        return false;
    }
    
    signaling_ = signaling;
    
    // 设置信令回调
    signaling_->onWebRTCReady([this](const std::string& role, const std::string& peerDeviceId) {
        handleRole(role, peerDeviceId);
    });
    
    signaling_->onOfferReceived([this](const nlohmann::json& msg) {
        if (msg.contains("data") && msg["data"].contains("sdp")) {
            std::string sdp = msg["data"]["sdp"].get<std::string>();
            handleRemoteOffer(sdp);
        }
    });
    
    signaling_->onAnswerReceived([this](const nlohmann::json& msg) {
        if (msg.contains("data") && msg["data"].contains("sdp")) {
            std::string sdp = msg["data"]["sdp"].get<std::string>();
            handleRemoteAnswer(sdp);
        }
    });
    
    signaling_->onIceCandidateReceived([this](const nlohmann::json& msg) {
        if (msg.contains("data") && msg["data"].contains("candidate")) {
            std::string candidate = msg["data"]["candidate"].get<std::string>();
            handleRemoteIceCandidate(candidate);
        }
    });
    
    std::cout << "[WebRTC] 初始化完成，等待信令配对" << std::endl;
    return true;
}

void WebRTCManager::shutdown() {
    closePeerConnection();
    signaling_.reset();
    setStatus(WebRTCStatus::DISCONNECTED);
    std::cout << "[WebRTC] WebRTC管理器已关闭" << std::endl;
}

bool WebRTCManager::createPeerConnection() {
    try {
        rtc::Configuration rtcConfig;
        
        // 添加STUN服务器
        for (const auto& stunServer : config_.stunServers) {
            rtcConfig.iceServers.emplace_back(stunServer);
        }
        
        peerConnection_ = std::make_shared<rtc::PeerConnection>(rtcConfig);
        setupPeerConnectionCallbacks();
        
        std::cout << "[WebRTC] PeerConnection创建成功" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 创建PeerConnection失败: " << e.what() << std::endl;
        return false;
    }
}

void WebRTCManager::closePeerConnection() {
    if (dataChannel_) {
        dataChannel_.reset();
    }
    
    if (peerConnection_) {
        peerConnection_.reset();
    }
    
    role_.clear();
    peerDeviceId_.clear();
    setStatus(WebRTCStatus::DISCONNECTED);
}

void WebRTCManager::handleRole(const std::string& role, const std::string& peerDeviceId) {
    role_ = role;
    peerDeviceId_ = peerDeviceId;
    
    std::cout << "[WebRTC] 收到角色分配 - 角色: " << role_ 
              << ", 对端设备: " << peerDeviceId_ << std::endl;
    
    // 创建PeerConnection
    if (!createPeerConnection()) {
        std::cout << "[WebRTC] 创建PeerConnection失败" << std::endl;
        return;
    }
    
    setStatus(WebRTCStatus::CONNECTING);
    
    if (role_ == "offerer") {
        // 作为发起方，创建DataChannel
        if (config_.enableDataChannel) {
            setupDataChannel();
        }
        
        // TODO: 如果启用音频发送，创建音频轨道
        // TODO: 如果启用视频发送，创建视频轨道
        
    } else if (role_ == "answerer") {
        // 作为应答方，等待接收DataChannel
        std::cout << "[WebRTC] 作为应答方等待接收DataChannel" << std::endl;
        
        // TODO: 如果启用音频接收，准备接收音频轨道
    }
}

void WebRTCManager::handleRemoteOffer(const std::string& sdp) {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法处理Offer" << std::endl;
        return;
    }
    
    try {
        rtc::Description remoteDesc(sdp, rtc::Description::Type::Offer);
        peerConnection_->setRemoteDescription(remoteDesc);
        std::cout << "[WebRTC] 设置远程SDP Offer成功" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 设置远程SDP Offer失败: " << e.what() << std::endl;
        setStatus(WebRTCStatus::FAILED);
    }
}

void WebRTCManager::handleRemoteAnswer(const std::string& sdp) {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法处理Answer" << std::endl;
        return;
    }
    
    try {
        rtc::Description remoteDesc(sdp, rtc::Description::Type::Answer);
        peerConnection_->setRemoteDescription(remoteDesc);
        std::cout << "[WebRTC] 设置远程SDP Answer成功" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 设置远程SDP Answer失败: " << e.what() << std::endl;
        setStatus(WebRTCStatus::FAILED);
    }
}

void WebRTCManager::handleRemoteIceCandidate(const std::string& candidate) {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法处理ICE候选" << std::endl;
        return;
    }
    
    try {
        rtc::Candidate rtcCandidate(candidate);
        peerConnection_->addRemoteCandidate(rtcCandidate);
        std::cout << "[WebRTC] 添加远程ICE候选成功" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 添加远程ICE候选失败: " << e.what() << std::endl;
    }
}

bool WebRTCManager::sendDataChannelMessage(const std::string& message) {
    if (!dataChannel_ || !dataChannel_->isOpen()) {
        std::cout << "[WebRTC] 数据通道未打开，无法发送消息" << std::endl;
        return false;
    }
    
    try {
        dataChannel_->send(message);
        std::cout << "[WebRTC] 数据通道消息发送成功: " << message << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 数据通道消息发送失败: " << e.what() << std::endl;
        return false;
    }
}

bool WebRTCManager::isDataChannelOpen() const {
    return dataChannel_ && dataChannel_->isOpen();
}

void WebRTCManager::setupPeerConnectionCallbacks() {
    if (!peerConnection_) return;
    
    // 本地描述生成回调
    peerConnection_->onLocalDescription([this](rtc::Description description) {
        std::cout << "[WebRTC] 本地SDP生成: " << description.typeString() << std::endl;
        
        if (signaling_ && signaling_->isPaired()) {
            std::string sdp = std::string(description);
            
            if (description.type() == rtc::Description::Type::Offer) {
                signaling_->sendOffer(sdp, peerDeviceId_);
            } else if (description.type() == rtc::Description::Type::Answer) {
                signaling_->sendAnswer(sdp, peerDeviceId_);
            }
        }
    });
    
    // ICE候选生成回调
    peerConnection_->onLocalCandidate([this](rtc::Candidate candidate) {
        std::cout << "[WebRTC] 本地ICE候选生成" << std::endl;
        
        if (signaling_ && signaling_->isPaired()) {
            std::string candidateStr = std::string(candidate);
            signaling_->sendIceCandidate(candidateStr, peerDeviceId_);
        }
    });
    
    // 连接状态变化回调
    peerConnection_->onStateChange([this](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC] 连接状态变化: " << static_cast<int>(state) << std::endl;
        
        if (state == rtc::PeerConnection::State::Connected) {
            setStatus(WebRTCStatus::CONNECTED);
        } else if (state == rtc::PeerConnection::State::Failed) {
            setStatus(WebRTCStatus::FAILED);
        }
    });
    
    // 数据通道接收回调（应答方）
    if (role_ == "answerer") {
        peerConnection_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            std::cout << "[WebRTC] 接收到远程DataChannel: " << dc->label() << std::endl;
            dataChannel_ = dc;
            
            dataChannel_->onOpen([this]() {
                handleDataChannelOpen();
            });
            
            dataChannel_->onMessage([this](auto data) {
                if (std::holds_alternative<std::string>(data)) {
                    std::string message = std::get<std::string>(data);
                    handleDataChannelMessage(message);
                }
            });
        });
    }
}

void WebRTCManager::setupDataChannel() {
    if (!config_.enableDataChannel || !peerConnection_) {
        return;
    }
    
    try {
        dataChannel_ = peerConnection_->createDataChannel(config_.dataChannelLabel);
        
        dataChannel_->onOpen([this]() {
            handleDataChannelOpen();
        });
        
        dataChannel_->onMessage([this](auto data) {
            if (std::holds_alternative<std::string>(data)) {
                std::string message = std::get<std::string>(data);
                handleDataChannelMessage(message);
            }
        });
        
        std::cout << "[WebRTC] DataChannel创建成功: " << config_.dataChannelLabel << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 创建DataChannel失败: " << e.what() << std::endl;
    }
}

void WebRTCManager::handleDataChannelOpen() {
    std::cout << "[WebRTC] DataChannel连接已打开" << std::endl;
    
    // 发送测试消息
    if (role_ == "offerer") {
        std::string testMessage = "Hello from glasses device!";
        sendDataChannelMessage(testMessage);
    }
}

void WebRTCManager::handleDataChannelMessage(const std::string& message) {
    std::cout << "[WebRTC] 收到DataChannel消息: " << message << std::endl;
    
    // 应答方自动回复
    if (role_ == "answerer") {
        std::string reply = "Reply from peer: " + message;
        sendDataChannelMessage(reply);
    }
    
    // 触发回调
    if (dataMessageCallback_) {
        dataMessageCallback_(message);
    }
}

void WebRTCManager::setStatus(WebRTCStatus newStatus) {
    if (status_ != newStatus) {
        WebRTCStatus oldStatus = status_;
        status_ = newStatus;
        
        std::cout << "[WebRTC] 状态变更: " << static_cast<int>(oldStatus) 
                  << " -> " << static_cast<int>(newStatus) << std::endl;
        
        if (statusCallback_) {
            statusCallback_(newStatus);
        }
    }
}