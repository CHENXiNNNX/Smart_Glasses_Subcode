/**
 * @file signaling.hpp
 * @brief WebRTC信令客户端
 */

#ifndef SIGNALING_HPP
#define SIGNALING_HPP

#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
#include "../websocket/websocket.hpp"
#include <nlohmann/json.hpp>

namespace app
{
    namespace protocol
    {
        namespace webrtc
        {

            // ============================================================================
            // 前向声明
            // ============================================================================
            class Signaling;

            // ============================================================================
            // 信令连接状态枚举
            // ============================================================================

            /**
             * @brief 信令连接状态
             */
            enum class SignalingStatus
            {
                DISCONNECTED = 0, // 未连接
                CONNECTING,       // 连接中
                CONNECTED,        // 已连接
                JOINED,           // 已加入房间
                PAIRED            // 已配对
            };

            // ============================================================================
            // 错误码定义
            // ============================================================================

            /**
             * @brief 信令错误码（与服务器错误码保持一致）
             */
            enum class SignalingError
            {
                NONE                 = 0,
                ROOM_FULL            = 1001, // 房间已满
                ROOM_NOT_EXISTS      = 1002, // 房间不存在
                MESSAGE_FORMAT_ERROR = 1003, // 消息格式错误
                DEVICE_ID_ERROR      = 1004, // 设备ID错误
                CONNECTION_TIMEOUT   = 1005, // 连接超时
                PEER_OFFLINE         = 1006, // 对端离线
                SERVER_ERROR         = 1007, // 服务器错误
                CONNECTION_FAILED,           // 连接失败
                SEND_FAILED                  // 发送失败
            };

            // ============================================================================
            // 数据结构
            // ============================================================================

            /**
             * @brief 房间信息
             */
            struct RoomInfo
            {
                std::string roomId;     // 房间ID
                int         num;        // 房间人数
                std::string roomStatus; // 房间状态 (open/close)
            };

            // ============================================================================
            // 回调函数类型
            // ============================================================================

            /**
             * @brief 消息接收回调（用于offer/answer/ice消息）
             */
            using SignalingMessageCallback = std::function<void(const nlohmann::json&)>;

            /**
             * @brief 错误回调
             */
            using SignalingErrorCallback =
                std::function<void(SignalingError error, const std::string& message)>;

            /**
             * @brief 状态变化回调
             */
            using SignalingStatusCallback = std::function<void(SignalingStatus status)>;

            /**
             * @brief WebRTC就绪回调（配对成功，可以开始WebRTC连接）
             */
            using WebRTCReadyCallback =
                std::function<void(const std::string& role, const std::string& peerDeviceId)>;

            /**
             * @brief 房间信息变化回调
             */
            using RoomInfoCallback = std::function<void(const RoomInfo&)>;

            // ============================================================================
            // 信令客户端配置
            // ============================================================================

            /**
             * @brief 信令客户端配置
             */
            struct SignalingConfig
            {
                std::string deviceId;  // 设备ID（如：glasses_123456）
                std::string serverUrl; // 服务器地址（如：ws://192.168.2.17:8000）
                bool        auto_reconnect         = true; // 自动重连
                int         reconnect_interval_ms  = 5000; // 重连间隔
                int         max_reconnect_attempts = 0;    // 最大重连次数（0=无限）
            };

            // ============================================================================
            // 信令客户端类
            // ============================================================================

            /**
             * @brief WebRTC信令客户端
             * @details 负责与信令服务器通信，处理房间管理、SDP/ICE交换
             */
            class Signaling
            {
            public:
                /**
                 * @brief 构造函数
                 * @param config 信令配置
                 */
                explicit Signaling(const SignalingConfig& config);

                /**
                 * @brief 析构函数（RAII自动清理）
                 */
                ~Signaling();

                // ========================================================================
                // 连接管理
                // ========================================================================

                /**
                 * @brief 连接到信令服务器
                 * @return true 连接成功，false 连接失败
                 */
                bool connect();

                /**
                 * @brief 断开连接
                 */
                void disconnect();

                /**
                 * @brief 加入房间（根据设备ID自动提取房间标识）
                 * @return true 发送成功，false 发送失败
                 */
                bool joinRoom();

                /**
                 * @brief 离开房间
                 * @return true 发送成功，false 发送失败
                 */
                bool leaveRoom();

                /**
                 * @brief 请求房间信息
                 * @return true 发送成功，false 发送失败
                 */
                bool requestRoomInfo();

                // ========================================================================
                // SDP和ICE消息发送
                // ========================================================================

                /**
                 * @brief 发送SDP Offer
                 * @param sdp SDP内容
                 * @param target_device_id 目标设备ID
                 * @return true 发送成功，false 发送失败
                 */
                bool sendOffer(const std::string& sdp, const std::string& target_device_id);

                /**
                 * @brief 发送SDP Answer
                 * @param sdp SDP内容
                 * @param target_device_id 目标设备ID
                 * @return true 发送成功，false 发送失败
                 */
                bool sendAnswer(const std::string& sdp, const std::string& target_device_id);

                /**
                 * @brief 发送ICE候选
                 * @param candidate ICE候选信息
                 * @param target_device_id 目标设备ID
                 * @return true 发送成功，false 发送失败
                 */
                bool sendIceCandidate(const std::string& candidate,
                                      const std::string& target_device_id);

                // ========================================================================
                // 状态查询
                // ========================================================================

                /**
                 * @brief 获取连接状态
                 */
                SignalingStatus getStatus() const
                {
                    return status_.load(std::memory_order_acquire);
                }

                /**
                 * @brief 获取设备ID
                 */
                const std::string& getDeviceId() const
                {
                    return config_.deviceId;
                }

                /**
                 * @brief 获取对端设备ID
                 */
                const std::string& getPeerDeviceId() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return peerDeviceId_;
                }

                /**
                 * @brief 获取角色（offerer/answerer）
                 */
                const std::string& getRole() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return role_;
                }

                /**
                 * @brief 获取房间信息
                 */
                RoomInfo getRoomInfo() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return roomInfo_;
                }

                /**
                 * @brief 检查是否已连接
                 */
                bool isConnected() const
                {
                    SignalingStatus status = status_.load(std::memory_order_acquire);
                    return status != SignalingStatus::DISCONNECTED;
                }

                /**
                 * @brief 检查是否已配对
                 */
                bool isPaired() const
                {
                    return status_.load(std::memory_order_acquire) == SignalingStatus::PAIRED;
                }

                // ========================================================================
                // 回调设置
                // ========================================================================

                /**
                 * @brief 设置状态变化回调
                 */
                void onStatusChanged(SignalingStatusCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    statusCallback_ = callback;
                }

                /**
                 * @brief 设置Offer接收回调
                 */
                void onOfferReceived(SignalingMessageCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    offerCallback_ = callback;
                }

                /**
                 * @brief 设置Answer接收回调
                 */
                void onAnswerReceived(SignalingMessageCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    answerCallback_ = callback;
                }

                /**
                 * @brief 设置ICE候选接收回调
                 */
                void onIceCandidateReceived(SignalingMessageCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    iceCandidateCallback_ = callback;
                }

                /**
                 * @brief 设置WebRTC就绪回调
                 */
                void onWebRTCReady(WebRTCReadyCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    webrtcReadyCallback_ = callback;
                }

                /**
                 * @brief 设置房间信息变化回调
                 */
                void onRoomInfoChanged(RoomInfoCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    roomInfoCallback_ = callback;
                }

                /**
                 * @brief 设置错误回调
                 */
                void onError(SignalingErrorCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    errorCallback_ = callback;
                }

                // ========================================================================
                // 工具函数
                // ========================================================================

                /**
                 * @brief 状态转字符串
                 */
                static std::string statusToString(SignalingStatus status);

                /**
                 * @brief 错误码转字符串
                 */
                static std::string errorToString(SignalingError error);

                // 禁止拷贝和赋值
                Signaling(const Signaling&)            = delete;
                Signaling& operator=(const Signaling&) = delete;

            private:
                // ========================================================================
                // 内部方法
                // ========================================================================

                /**
                 * @brief 处理WebSocket消息
                 */
                void handleWebSocketMessage(const char* data, size_t size);

                /**
                 * @brief 处理WebSocket状态变化
                 */
                void handleWebSocketState(websocket::ConnectionState old_state,
                                          websocket::ConnectionState new_state);

                /**
                 * @brief 处理WebSocket错误
                 */
                void handleWebSocketError(websocket::WebSocketError error,
                                          const std::string&        message);

                /**
                 * @brief 解析并处理JSON消息
                 */
                void handleMessage(const std::string& message);

                /**
                 * @brief 处理角色消息
                 */
                void handleRoleMessage(const nlohmann::json& msg);

                /**
                 * @brief 处理Offer消息
                 */
                void handleOfferMessage(const nlohmann::json& msg);

                /**
                 * @brief 处理Answer消息
                 */
                void handleAnswerMessage(const nlohmann::json& msg);

                /**
                 * @brief 处理ICE消息
                 */
                void handleIceMessage(const nlohmann::json& msg);

                /**
                 * @brief 处理房间信息消息
                 */
                void handleInfoMessage(const nlohmann::json& msg);

                /**
                 * @brief 处理错误消息
                 */
                void handleErrorMessage(const nlohmann::json& msg);

                /**
                 * @brief 发送JSON消息
                 */
                bool sendMessage(const nlohmann::json& message);

                /**
                 * @brief 设置状态
                 */
                void setStatus(SignalingStatus new_status);

                /**
                 * @brief 获取当前时间戳（微秒）
                 */
                uint64_t getCurrentTimestamp() const;

                /**
                 * @brief 从设备ID提取房间ID
                 */
                std::string extractRoomId(const std::string& device_id) const;

                /**
                 * @brief 触发状态回调
                 */
                void invokeStatusCallback(SignalingStatus status);

                /**
                 * @brief 触发错误回调
                 */
                void invokeErrorCallback(SignalingError error, const std::string& message);

                /**
                 * @brief 触发WebRTC就绪回调
                 */
                void invokeWebRTCReadyCallback(const std::string& role,
                                               const std::string& peer_device_id);

                /**
                 * @brief 触发房间信息回调
                 */
                void invokeRoomInfoCallback(const RoomInfo& info);

                // ========================================================================
                // 成员变量
                // ========================================================================

                SignalingConfig                             config_;    // 配置
                std::atomic<SignalingStatus>                status_;    // 连接状态
                std::unique_ptr<websocket::WebSocketClient> ws_client_; // WebSocket客户端

                mutable std::mutex mutex_;        // 保护共享数据
                std::string        peerDeviceId_; // 对端设备ID
                std::string        role_;         // 角色（offerer/answerer）
                RoomInfo           roomInfo_;     // 房间信息

                std::mutex               callback_mutex_;       // 保护回调函数
                SignalingStatusCallback  statusCallback_;       // 状态回调
                SignalingMessageCallback offerCallback_;        // Offer回调
                SignalingMessageCallback answerCallback_;       // Answer回调
                SignalingMessageCallback iceCandidateCallback_; // ICE回调
                WebRTCReadyCallback      webrtcReadyCallback_;  // WebRTC就绪回调
                RoomInfoCallback         roomInfoCallback_;     // 房间信息回调
                SignalingErrorCallback   errorCallback_;        // 错误回调
            };

        } // namespace webrtc
    }     // namespace protocol
} // namespace app

#endif // SIGNALING_HPP
