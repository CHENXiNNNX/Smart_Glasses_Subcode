#include "signaling.h"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace glasses::protocol;
using json = nlohmann::json;

Signaling::Signaling(const std::string& deviceId, const std::string& serverUrl)
    : deviceId_(deviceId)
    , serverUrl_(serverUrl)
    , status_(ConnectionStatus::DISCONNECTED)
    , ws_(nullptr) {
    
    std::cout << "[INFO] WebSocket客户端初始化: " << deviceId_ << std::endl;
}

Signaling::~Signaling() {
    disconnect();
}

bool Signaling::connect() {
    if (status_ != ConnectionStatus::DISCONNECTED) {
        std::cout << "[WARN] 客户端已连接或正在连接中" << std::endl;
        return false;
    }

    try {
        setStatus(ConnectionStatus::CONNECTING);
        
        // 创建WebSocket连接
        ws_ = std::make_shared<rtc::WebSocket>();
        
        // 设置连接成功回调
        ws_->onOpen([this]() {
            std::cout << "[INFO] WebSocket连接成功" << std::endl;
            setStatus(ConnectionStatus::CONNECTED);
        });

        // 设置消息接收回调
        ws_->onMessage([this](auto data) {
            if (std::holds_alternative<std::string>(data)) {
                std::string message = std::get<std::string>(data);
                handleMessage(message);
            }
        });

        // 设置连接关闭回调
        ws_->onClosed([this]() {
            std::cout << "[INFO] WebSocket连接已关闭" << std::endl;
            setStatus(ConnectionStatus::DISCONNECTED);
            peerDeviceId_.clear();
            role_.clear();
        });

        // 设置错误回调
        ws_->onError([this](std::string error) {
            std::cout << "[ERROR] WebSocket错误: " << error << std::endl;
            setStatus(ConnectionStatus::DISCONNECTED);
            if (errorCallback_) {
                errorCallback_(ErrorCode::SERVER_ERROR, error);
            }
        });

        // 连接到服务器
        ws_->open(serverUrl_);
        
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "[ERROR] WebSocket连接失败: " << e.what() << std::endl;
        setStatus(ConnectionStatus::DISCONNECTED);
        return false;
    }
}

void Signaling::disconnect() {
    if (ws_ && ws_->readyState() != rtc::WebSocket::State::Closed) {
        // 发送离开房间消息
        if (status_ == ConnectionStatus::JOINED || status_ == ConnectionStatus::PAIRED) {
            leaveRoom();
        }
        
        ws_->close();
    }
    
    setStatus(ConnectionStatus::DISCONNECTED);
    peerDeviceId_.clear();
    role_.clear();
}

bool Signaling::joinRoom() {
    if (status_ != ConnectionStatus::CONNECTED) {
        std::cout << "[ERROR] 未连接到服务器，无法加入房间" << std::endl;
        return false;
    }

    json message = {
        {"type", "join"},
        {"device_id", deviceId_},
        {"from", deviceId_},
        {"to", "server"},
        {"time", getCurrentTimestamp()}
    };

    if (sendMessage(message)) {
        std::cout << "[INFO] 发送加入房间消息" << std::endl;
        return true;
    }
    
    return false;
}

bool Signaling::leaveRoom() {
    if (status_ == ConnectionStatus::DISCONNECTED) {
        return false;
    }

    json message = {
        {"type", "leave"},
        {"device_id", deviceId_},
        {"from", deviceId_},
        {"to", "server"},
        {"time", getCurrentTimestamp()}
    };

    if (sendMessage(message)) {
        std::cout << "[INFO] 发送离开房间消息" << std::endl;
        setStatus(ConnectionStatus::CONNECTED);
        peerDeviceId_.clear();
        role_.clear();
        return true;
    }
    
    return false;
}

bool Signaling::sendOffer(const std::string& sdp, const std::string& targetDeviceId) {
    if (status_ != ConnectionStatus::PAIRED) {
        std::cout << "[ERROR] 未配对，无法发送Offer" << std::endl;
        return false;
    }

    json message = {
        {"type", "offer"},
        {"device_id", deviceId_},
        {"from", deviceId_},
        {"to", targetDeviceId},
        {"data", {{"sdp", sdp}}},
        {"time", getCurrentTimestamp()}
    };

    if (sendMessage(message)) {
        std::cout << "[INFO] 发送SDP Offer" << std::endl;
        return true;
    }
    
    return false;
}

bool Signaling::sendAnswer(const std::string& sdp, const std::string& targetDeviceId) {
    if (status_ != ConnectionStatus::PAIRED) {
        std::cout << "[ERROR] 未配对，无法发送Answer" << std::endl;
        return false;
    }

    json message = {
        {"type", "answer"},
        {"device_id", deviceId_},
        {"from", deviceId_},
        {"to", targetDeviceId},
        {"data", {{"sdp", sdp}}},
        {"time", getCurrentTimestamp()}
    };

    if (sendMessage(message)) {
        std::cout << "[INFO] 发送SDP Answer" << std::endl;
        return true;
    }
    
    return false;
}

bool Signaling::sendIceCandidate(const std::string& candidate, const std::string& targetDeviceId) {
    if (status_ != ConnectionStatus::PAIRED) {
        std::cout << "[ERROR] 未配对，无法发送ICE候选" << std::endl;
        return false;
    }

    json message = {
        {"type", "ice"},
        {"device_id", deviceId_},
        {"from", deviceId_},
        {"to", targetDeviceId},
        {"data", {{"candidate", candidate}}},
        {"time", getCurrentTimestamp()}
    };

    if (sendMessage(message)) {
        std::cout << "[INFO] 发送ICE候选" << std::endl;
        return true;
    }
    
    return false;
}

void Signaling::handleMessage(const std::string& message) {
    try {
        json msg = json::parse(message);
        
        // 验证消息基本字段
        if (!msg.contains("type") || !msg.contains("from") || !msg.contains("to")) {
            std::cout << "[ERROR] 收到格式错误的消息" << std::endl;
            return;
        }

        std::string type = msg["type"];
        std::cout << "[INFO] 收到消息: " << type << " from " << msg["from"].get<std::string>() << std::endl;

        // 根据消息类型处理
        if (type == "role") {
            handleRoleMessage(msg);
        } else if (type == "offer") {
            handleOfferMessage(msg);
        } else if (type == "answer") {
            handleAnswerMessage(msg);
        } else if (type == "ice") {
            handleIceMessage(msg);
        } else if (type == "error") {
            handleErrorMessage(msg);
        } else {
            std::cout << "[WARN] 收到未知消息类型: " << type << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "[ERROR] 解析消息失败: " << e.what() << std::endl;
        std::cout << "[ERROR] 原始消息: " << message << std::endl;
    }
}

void Signaling::handleRoleMessage(const json& msg) {
    if (!msg.contains("data")) {
        std::cout << "[ERROR] 角色消息缺少data字段" << std::endl;
        return;
    }

    auto data = msg["data"];
    if (data.contains("peer_device_id")) {
        peerDeviceId_ = data["peer_device_id"].get<std::string>();
    }
    
    if (data.contains("role")) {
        role_ = data["role"].get<std::string>();
    }

    setStatus(ConnectionStatus::PAIRED);
    
    std::cout << "[INFO] 配对成功 - 对端设备: " << peerDeviceId_ 
              << ", 角色: " << role_ << std::endl;

    if (roleCallback_) {
        roleCallback_(msg);
    }
}

void Signaling::handleOfferMessage(const json& msg) {
    std::cout << "[INFO] 收到SDP Offer" << std::endl;
    
    if (offerCallback_) {
        offerCallback_(msg);
    }
}

void Signaling::handleAnswerMessage(const json& msg) {
    std::cout << "[INFO] 收到SDP Answer" << std::endl;
    
    if (answerCallback_) {
        answerCallback_(msg);
    }
}

void Signaling::handleIceMessage(const json& msg) {
    std::cout << "[INFO] 收到ICE候选" << std::endl;
    
    if (iceCandidateCallback_) {
        iceCandidateCallback_(msg);
    }
}

void Signaling::handleErrorMessage(const json& msg) {
    if (!msg.contains("data")) {
        std::cout << "[ERROR] 错误消息缺少data字段" << std::endl;
        return;
    }

    auto data = msg["data"];
    int errorCode = data.value("error_code", 1007);
    std::string errorMessage = data.value("error_message", "未知错误");

    std::cout << "[ERROR] 服务器错误 [" << errorCode << "]: " << errorMessage << std::endl;

    if (errorCallback_) {
        errorCallback_(static_cast<ErrorCode>(errorCode), errorMessage);
    }

    // 根据错误类型调整状态
    switch (errorCode) {
        case 1001: // 房间已满
        case 1002: // 房间不存在
        case 1005: // 连接超时
            if (status_ == ConnectionStatus::JOINED || status_ == ConnectionStatus::PAIRED) {
                setStatus(ConnectionStatus::CONNECTED);
                peerDeviceId_.clear();
                role_.clear();
            }
            break;
        case 1006: // 对端已离线
            if (status_ == ConnectionStatus::PAIRED) {
                setStatus(ConnectionStatus::JOINED);
                peerDeviceId_.clear();
                role_.clear();
            }
            break;
        default:
            break;
    }
}

bool Signaling::sendMessage(const json& message) {
    if (!ws_ || ws_->readyState() != rtc::WebSocket::State::Open) {
        std::cout << "[ERROR] WebSocket未连接，无法发送消息" << std::endl;
        return false;
    }

    try {
        std::string messageStr = message.dump();
        ws_->send(messageStr);
        return true;
    } catch (const std::exception& e) {
        std::cout << "[ERROR] 发送消息失败: " << e.what() << std::endl;
        return false;
    }
}

void Signaling::setStatus(ConnectionStatus newStatus) {
    if (status_ != newStatus) {
        ConnectionStatus oldStatus = status_;
        status_ = newStatus;
        
        std::cout << "[INFO] 状态变更: " << static_cast<int>(oldStatus) 
                  << " -> " << static_cast<int>(newStatus) << std::endl;

        if (statusCallback_) {
            statusCallback_(newStatus);
        }
    }
}

uint64_t Signaling::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    return microseconds.count();
}
