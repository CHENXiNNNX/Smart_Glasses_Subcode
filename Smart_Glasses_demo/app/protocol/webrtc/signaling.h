#ifndef SIGNALING_H
#define SIGNALING_H

#include <string>
#include <memory>
#include <functional>
#include <chrono>
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

// 错误码定义 - 与方案中的错误码保持一致
enum class ErrorCode {
    ROOM_FULL = 1001,           // 房间已满
    ROOM_NOT_EXISTS = 1002,     // 房间不存在
    MESSAGE_FORMAT_ERROR = 1003, // 消息格式错误
    DEVICE_ID_ERROR = 1004,     // 设备ID错误
    CONNECTION_TIMEOUT = 1005,  // 连接超时
    PEER_OFFLINE = 1006,        // 对端离线
    SERVER_ERROR = 1007         // 服务器错误
};

// 回调函数类型定义
using MessageCallback = std::function<void(const nlohmann::json&)>;
using ErrorCallback = std::function<void(ErrorCode, const std::string&)>;
using StatusCallback = std::function<void(ConnectionStatus)>;
using WebRTCReadyCallback = std::function<void(const std::string& role, const std::string& peerDeviceId)>;

/**
 * 信令类 - 负责WebRTC连接建立前的所有信令通信
 * 职责：WebSocket连接、房间管理、SDP/ICE交换、配对管理
 */
class Signaling {
public:
    /**
     * 构造函数
     * @param deviceId 设备唯一标识（如：glasses_123456）
     * @param serverUrl 服务器地址（如：ws://192.168.2.17:8000）
     */
    Signaling(const std::string& deviceId, const std::string& serverUrl);
    
    /**
     * 析构函数
     */
    ~Signaling();

    // ========== 连接管理接口 ==========
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
     * 加入房间 - 根据设备ID自动提取房间标识
     * @return true 发送成功，false 发送失败
     */
    bool joinRoom();

    /**
     * 离开房间
     * @return true 发送成功，false 发送失败
     */
    bool leaveRoom();

    // ========== SDP和ICE消息发送接口 ==========
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

    // ========== 状态查询接口 ==========
    ConnectionStatus getStatus() const { return status_; }
    const std::string& getDeviceId() const { return deviceId_; }
    const std::string& getPeerDeviceId() const { return peerDeviceId_; }
    const std::string& getRole() const { return role_; }
    bool isConnected() const { return status_ != ConnectionStatus::DISCONNECTED; }
    bool isPaired() const { return status_ == ConnectionStatus::PAIRED; }

    // ========== 回调函数设置接口 ==========
    void onStatusChanged(StatusCallback callback) { statusCallback_ = callback; }
    void onOfferReceived(MessageCallback callback) { offerCallback_ = callback; }
    void onAnswerReceived(MessageCallback callback) { answerCallback_ = callback; }
    void onIceCandidateReceived(MessageCallback callback) { iceCandidateCallback_ = callback; }
    void onWebRTCReady(WebRTCReadyCallback callback) { webrtcReadyCallback_ = callback; }
    void onError(ErrorCallback callback) { errorCallback_ = callback; }

private:
    // ========== 内部消息处理方法 ==========
    void handleMessage(const std::string& message);
    void handleRoleMessage(const nlohmann::json& msg);
    void handleOfferMessage(const nlohmann::json& msg);
    void handleAnswerMessage(const nlohmann::json& msg);
    void handleIceMessage(const nlohmann::json& msg);
    void handleErrorMessage(const nlohmann::json& msg);
    
    // ========== 内部工具方法 ==========
    bool sendMessage(const nlohmann::json& message);
    void setStatus(ConnectionStatus newStatus);
    uint64_t getCurrentTimestamp() const;
    std::string extractRoomId(const std::string& deviceId) const;
    
    // ========== 成员变量 ==========
    std::string deviceId_;              // 设备ID (glasses_123456)
    std::string serverUrl_;             // 服务器地址
    std::string peerDeviceId_;          // 对端设备ID (app_123456)
    std::string role_;                  // 角色信息 (offerer/answerer)
    
    ConnectionStatus status_;           // 连接状态
    std::shared_ptr<rtc::WebSocket> ws_; // WebSocket连接
    
    // 回调函数存储
    StatusCallback statusCallback_;
    MessageCallback offerCallback_;
    MessageCallback answerCallback_;
    MessageCallback iceCandidateCallback_;
    WebRTCReadyCallback webrtcReadyCallback_;
    ErrorCallback errorCallback_;
};

} // namespace protocol
} // namespace glasses

#endif // SIGNALING_H