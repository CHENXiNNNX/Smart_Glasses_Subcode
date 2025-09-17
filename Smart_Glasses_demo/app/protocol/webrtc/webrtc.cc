#include "webrtc.h"


using namespace glasses::protocol;

WebRTCManager::WebRTCManager(const WebRTCConfig& config)
    : config_(config)
    , status_(WebRTCStatus::DISCONNECTED)
    , signaling_(nullptr)
    , peerConnection_(nullptr)
    , dataChannel_(nullptr)
    , videoTrack_(nullptr)
    , videoRtpConfig_(nullptr)
    , videoPacketizer_(nullptr)
    , videoSrReporter_(nullptr) {
    
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
        
        rtcConfig.disableAutoNegotiation = true;

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
    // 清理视频轨道相关资源
    if (videoSrReporter_) {
        videoSrReporter_.reset();
    }
    if (videoPacketizer_) {
        videoPacketizer_.reset();
    }
    if (videoRtpConfig_) {
        videoRtpConfig_.reset();
    }
    if (videoTrack_) {
        videoTrack_.reset();
    }
    
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
        // 如果启用数据通道，创建数据通道
        if (config_.enableDataChannel) {
            setupDataChannel();
        }
        
        // 如果启用视频发送，创建视频轨道
        if (config_.enableVideoSend) {
            setupVideoTrack();
        }

        peerConnection_->setLocalDescription();
        
    } else if (role_ == "answerer") {
        // 作为应答方，等待接收DataChannel
        std::cout << "[WebRTC] 作为应答方等待接收DataChannel" << std::endl;
        
        // TODO: 如果启用音频接收，准备接收音频轨道
        // TODO: 如果启用视频接收，准备接收视频轨道
    }
}

void WebRTCManager::handleRemoteOffer(const std::string& sdp) {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法处理Offer" << std::endl;
        return;
    }
    
    try {
        // 打印接收到的SDP内容
        std::cout << "[WebRTC] ========== 接收到的SDP Offer ==========" << std::endl;
        std::cout << sdp << std::endl;
        std::cout << "[WebRTC] ========== SDP内容结束 ==========" << std::endl;
        
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
        // 打印接收到的SDP内容
        std::cout << "[WebRTC] ========== 接收到的SDP Answer ==========" << std::endl;
        std::cout << sdp << std::endl;
        std::cout << "[WebRTC] ========== SDP内容结束 ==========" << std::endl;
        
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
            
            // 打印SDP内容
            std::cout << "[WebRTC] ========== 本地SDP内容 ==========" << std::endl;
            std::cout << sdp << std::endl;
            std::cout << "[WebRTC] ========== SDP内容结束 ==========" << std::endl;
            
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
            // // 如果启用视频发送，创建视频轨道
            // if (config_.enableVideoSend) {
            //     setupVideoTrack();
            // }
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

void WebRTCManager::setupVideoTrack() {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法创建视频轨道" << std::endl;
        return;
    }
    
    try {
        // 1. 创建视频描述
        auto video = rtc::Description::Video("video");  // 使用简短的MID
        video.addH264Codec(102);  // H264 payload type
        video.addSSRC(1, "video", "stream1", "video");
        
        // 2. 添加轨道到PeerConnection
        videoTrack_ = peerConnection_->addTrack(video);
        
        // 3. 创建RTP配置
        videoRtpConfig_ = std::make_shared<rtc::RtpPacketizationConfig>(
            1,  // SSRC
            "video",  // CNAME (简短)
            102,  // payload type
            rtc::H264RtpPacketizer::ClockRate  // clock rate (90kHz)
        );
        
        // 4. 创建H264 RTP封装器
        videoPacketizer_ = std::make_shared<rtc::H264RtpPacketizer>(
            rtc::NalUnit::Separator::StartSequence,  // 使用起始序列分隔符
            videoRtpConfig_
        );
        
        // 5. 创建RTCP SR报告器
        videoSrReporter_ = std::make_shared<rtc::RtcpSrReporter>(videoRtpConfig_);
        videoPacketizer_->addToChain(videoSrReporter_);
        
        // 6. 添加RTCP NACK处理器（官方例程要求）
        auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
        videoPacketizer_->addToChain(nackResponder);
        
        // 7. 设置媒体处理器
        videoTrack_->setMediaHandler(videoPacketizer_);
        
        // 8. 设置轨道打开回调
        videoTrack_->onOpen([this]() {
            std::cout << "[WebRTC] H264视频轨道已打开，可以开始发送视频数据" << std::endl;
        });
        
        std::cout << "[WebRTC] H264视频轨道创建成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 创建H264视频轨道失败: " << e.what() << std::endl;
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

// ========== 音频接口实现 ==========

bool WebRTCManager::startAudioSend() {
    std::cout << "[WebRTC] 音频发送功能暂未实现" << std::endl;
    return false;
}

bool WebRTCManager::stopAudioSend() {
    std::cout << "[WebRTC] 音频发送功能暂未实现" << std::endl;
    return false;
}

bool WebRTCManager::startAudioReceive() {
    std::cout << "[WebRTC] 音频接收功能暂未实现" << std::endl;
    return false;
}

bool WebRTCManager::stopAudioReceive() {
    std::cout << "[WebRTC] 音频接收功能暂未实现" << std::endl;
    return false;
}

void WebRTCManager::sendAudioData(const uint8_t* data, size_t size) {
    std::cout << "[WebRTC] 音频数据发送功能暂未实现" << std::endl;
}

// ========== 视频接口实现 ==========

bool WebRTCManager::startVideoSend() {
    std::cout << "[WebRTC] 视频发送功能暂未实现" << std::endl;
    return false;
}

bool WebRTCManager::stopVideoSend() {
    std::cout << "[WebRTC] 视频发送功能暂未实现" << std::endl;
    return false;
}

void WebRTCManager::sendVideoFrame(const uint8_t* data, size_t size, uint64_t timestamp) {
    if (!videoTrack_ || !videoTrack_->isOpen()) {
        std::cout << "[WebRTC] 视频轨道未打开，无法发送视频数据" << std::endl;
        return;
    }
    
    if (!data || size == 0) {
        std::cout << "[WebRTC] 视频数据无效，跳过发送" << std::endl;
        return;
    }
    
    // 验证H.264数据格式
    if (size < 4) {
        std::cout << "[WebRTC] H.264数据太小，跳过发送: " << size << " 字节" << std::endl;
        return;
    }
    
    // 检查H.264起始码
    bool hasValidStartCode = false;
    if (size >= 4) {
        // 检查0x00000001起始码
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
            hasValidStartCode = true;
        }
        // 检查0x000001起始码
        else if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
            hasValidStartCode = true;
        }
    }
    
    if (!hasValidStartCode) {
        std::cout << "[WebRTC] H.264数据格式无效，缺少起始码，跳过发送" << std::endl;
        // 打印前16字节用于调试
        std::cout << "[WebRTC] 数据前16字节: ";
        for (int i = 0; i < std::min(16, (int)size); i++) {
            printf("%02x ", data[i]);
        }
        std::cout << std::endl;
        return;
    }
    
    try {
        // 转换时间戳 (PTS -> RTP timestamp, 90kHz)
        // 假设timestamp是微秒单位，需要转换为RTP时间戳
        uint32_t rtpTimestamp = static_cast<uint32_t>((timestamp * 90) / 1000);
        
        // 创建FrameInfo对象
        rtc::FrameInfo frameInfo(rtpTimestamp);
        
        // 发送H264数据，RTP封装自动完成
        // 需要将uint8_t*转换为std::byte*
        videoTrack_->sendFrame(reinterpret_cast<const std::byte*>(data), size, frameInfo);
        
        std::cout << "[WebRTC] 视频帧发送成功: " << size << " 字节, RTP时间戳: " << rtpTimestamp << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 发送视频帧失败: " << e.what() << std::endl;
    }
}

