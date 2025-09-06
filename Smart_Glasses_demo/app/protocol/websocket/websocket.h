#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H


#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

namespace glasses {
namespace protocol {

// WebSocket连接状态枚举
enum class ConnectionStatus {
    DISCONNECTED,   // 未连接
    CONNECTING,     // 连接中
    CONNECTED,      // 已连接
    JOINED,         // 已加入房间
    PAIRED          // 已配对
};

// 错误码定义（与服务器保持一致）
enum class ErrorCode {
    ROOM_FULL = 1001,
    ROOM_NOT_EXISTS = 1002,
    MESSAGE_FORMAT_ERROR = 1003,
    DEVICE_ID_ERROR = 1004,
    CONNECTION_TIMEOUT = 1005,
    PEER_OFFLINE = 1006,
    SERVER_ERROR = 1007
};

// 消息回调函数类型定义
using MessageCallback = std::function<void(const nlohmann::json&)>;
using ErrorCallback = std::function<void(ErrorCode, const std::string&)>;
using StatusCallback = std::function<void(ConnectionStatus)>;

/**
 * WebSocket客户端类
 * 用于智能眼镜设备与信令服务器的通信
 */
class WebSocketClient {
public:
    /**
     * 构造函数
     * @param deviceId 设备唯一标识（如：glasses_123456）
     * @param serverUrl 服务器地址（如：ws://localhost:8000）
     */
    WebSocketClient(const std::string& deviceId, const std::string& serverUrl);
    
    /**
     * 析构函数
     */
    ~WebSocketClient();

    /**
     * 连接到信令服务器
     * @return true 连接成功，false 连接失败
     */
    bool connect();

    /**
     * 断开连接
     */
    void disconnect();

    /**
     * 加入房间
     * @return true 发送成功，false 发送失败
     */
    bool joinRoom();

    /**
     * 离开房间
     * @return true 发送成功，false 发送失败
     */
    bool leaveRoom();

    /**
     * 发送SDP Offer
     * @param sdp SDP内容
     * @param targetDeviceId 目标设备ID
     * @return true 发送成功，false 发送失败
     */
    bool sendOffer(const std::string& sdp, const std::string& targetDeviceId);

    /**
     * 发送SDP Answer
     * @param sdp SDP内容
     * @param targetDeviceId 目标设备ID
     * @return true 发送成功，false 发送失败
     */
    bool sendAnswer(const std::string& sdp, const std::string& targetDeviceId);

    /**
     * 发送ICE候选
     * @param candidate ICE候选信息
     * @param targetDeviceId 目标设备ID
     * @return true 发送成功，false 发送失败
     */
    bool sendIceCandidate(const std::string& candidate, const std::string& targetDeviceId);

    /**
     * 获取当前连接状态
     * @return 连接状态
     */
    ConnectionStatus getStatus() const { return status_; }

    /**
     * 获取设备ID
     * @return 设备ID
     */
    const std::string& getDeviceId() const { return deviceId_; }

    /**
     * 获取对端设备ID
     * @return 对端设备ID，如果未配对则为空
     */
    const std::string& getPeerDeviceId() const { return peerDeviceId_; }

    /**
     * 获取角色信息
     * @return 角色信息（offerer/answerer），如果未配对则为空
     */
    const std::string& getRole() const { return role_; }

    /**
     * 检查是否已连接
     * @return true 已连接，false 未连接
     */
    bool isConnected() const { return status_ != ConnectionStatus::DISCONNECTED; }

    /**
     * 检查是否已配对
     * @return true 已配对，false 未配对
     */
    bool isPaired() const { return status_ == ConnectionStatus::PAIRED; }

    // 回调函数设置
    void onStatusChanged(StatusCallback callback) { statusCallback_ = callback; }
    void onOfferReceived(MessageCallback callback) { offerCallback_ = callback; }
    void onAnswerReceived(MessageCallback callback) { answerCallback_ = callback; }
    void onIceCandidateReceived(MessageCallback callback) { iceCandidateCallback_ = callback; }
    void onRoleReceived(MessageCallback callback) { roleCallback_ = callback; }
    void onError(ErrorCallback callback) { errorCallback_ = callback; }

private:
    // 内部方法
    void handleMessage(const std::string& message);
    void handleJoinMessage(const nlohmann::json& msg);
    void handleLeaveMessage(const nlohmann::json& msg);
    void handleOfferMessage(const nlohmann::json& msg);
    void handleAnswerMessage(const nlohmann::json& msg);
    void handleIceMessage(const nlohmann::json& msg);
    void handleRoleMessage(const nlohmann::json& msg);
    void handleErrorMessage(const nlohmann::json& msg);
    
    bool sendMessage(const nlohmann::json& message);
    void setStatus(ConnectionStatus newStatus);
    uint64_t getCurrentTimestamp() const;
    
    // 成员变量
    std::string deviceId_;              // 设备ID
    std::string serverUrl_;             // 服务器地址
    std::string peerDeviceId_;          // 对端设备ID
    std::string role_;                  // 角色信息
    
    ConnectionStatus status_;           // 连接状态
    std::shared_ptr<rtc::WebSocket> ws_; // WebSocket连接
    
    // 回调函数
    StatusCallback statusCallback_;
    MessageCallback offerCallback_;
    MessageCallback answerCallback_;
    MessageCallback iceCandidateCallback_;
    MessageCallback roleCallback_;
    ErrorCallback errorCallback_;
};

} // namespace protocol
} // namespace glasses

#endif // WEBSOCKET_CLIENT_H
