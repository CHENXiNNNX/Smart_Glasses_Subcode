/* signaling.cc - WebRTC信令客户端 */

#include "signaling.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/time/time.hpp"

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#else
#include "../../../third_party/libdatachannel/deps/json/single_include/nlohmann/json.hpp"
#endif

using namespace app::tool::log;
using json = nlohmann::json;

namespace app
{
    namespace protocol
    {
        namespace webrtc
        {

            namespace
            {
                constexpr const char* LOG_TAG                         = "SIGNALING";
                constexpr int         ERROR_CODE_ROOM_FULL            = 1001;
                constexpr int         ERROR_CODE_ROOM_NOT_EXISTS      = 1002;
                constexpr int         ERROR_CODE_MESSAGE_FORMAT_ERROR = 1003;
                constexpr int         ERROR_CODE_DEVICE_ID_ERROR      = 1004;
                constexpr int         ERROR_CODE_CONNECTION_TIMEOUT   = 1005;
                constexpr int         ERROR_CODE_PEER_OFFLINE         = 1006;
                constexpr int         ERROR_CODE_SERVER_ERROR         = 1007;
            } // namespace

            Signaling::Signaling(const SignalingConfig& config)
                : config_(config), status_(SignalingStatus::DISCONNECTED), room_info_()
            {
                // 初始化房间信息
                room_info_.room_id = extractRoomId(config_.device_id);
                room_info_.num     = 0;

                LOG_INFO(LOG_TAG, "信令客户端初始化: deviceId=%s, serverUrl=%s",
                         config_.device_id.c_str(), config_.server_url.c_str());
                LOG_DEBUG(LOG_TAG, "房间ID: %s", room_info_.room_id.c_str());
            }

            Signaling::~Signaling()
            {
                disconnect();
            }

            bool Signaling::connect()
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current != SignalingStatus::DISCONNECTED)
                {
                    LOG_WARN(LOG_TAG, "已经在连接或已连接");
                    return false;
                }

                try
                {
                    setStatus(SignalingStatus::CONNECTING);

                    // 创建WebSocket客户端配置
                    websocket::WebSocketConfig ws_config;
                    ws_config.url                    = config_.server_url;
                    ws_config.auto_reconnect         = config_.auto_reconnect;
                    ws_config.reconnect_interval_ms  = config_.reconnect_interval_ms;
                    ws_config.max_reconnect_attempts = config_.max_reconnect_attempts;
                    ws_config.verify_ssl = false; // 信令服务器通常不需要SSL验证

                    // 创建WebSocket客户端
                    ws_client_ = std::make_unique<websocket::WebSocketClient>(ws_config);

                    // 设置回调
                    ws_client_->setTextCallback(
                        [this](const char* data, size_t size) -> bool
                        {
                            handleWebSocketMessage(data, size);
                            return true;
                        });

                    ws_client_->setStateCallback([this](websocket::ConnectionState oldState,
                                                        websocket::ConnectionState newState)
                                                 { handleWebSocketState(oldState, newState); });

                    ws_client_->setErrorCallback(
                        [this](websocket::WebSocketError error, const std::string& message)
                        { handleWebSocketError(error, message); });

                    // 连接到服务器
                    websocket::WebSocketError err = websocket::WebSocketError::NONE;
                    err                           = ws_client_->connect();
                    if (err != websocket::WebSocketError::NONE)
                    {
                        LOG_ERROR(LOG_TAG, "WebSocket连接失败");
                        setStatus(SignalingStatus::DISCONNECTED);
                        return false;
                    }

                    return true;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "连接异常: %s", e.what());
                    setStatus(SignalingStatus::DISCONNECTED);
                    return false;
                }
            }

            void Signaling::disconnect()
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current == SignalingStatus::DISCONNECTED)
                {
                    return;
                }

                // 如果已加入房间，先离开
                if (current == SignalingStatus::JOINED || current == SignalingStatus::PAIRED)
                {
                    leaveRoom();
                }

                // 断开WebSocket连接
                if (ws_client_)
                {
                    ws_client_->disconnect();
                    ws_client_.reset();
                }

                setStatus(SignalingStatus::DISCONNECTED);

                // 清理状态
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    peer_device_id_.clear();
                    role_.clear();
                    room_info_.num = 0;
                }

                LOG_INFO(LOG_TAG, "已断开连接");
            }

            bool Signaling::joinRoom()
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current != SignalingStatus::CONNECTED)
                {
                    LOG_WARN(LOG_TAG, "未连接到服务器，无法加入房间");
                    return false;
                }

                json message = {{"type", "join"},
                                {"from", config_.device_id},
                                {"to", "server"},
                                {"time", getCurrentTimestamp()}};

                if (sendMessage(message))
                {
                    LOG_INFO(LOG_TAG, "发送加入房间消息");
                    setStatus(SignalingStatus::JOINED);
                    return true;
                }

                return false;
            }

            bool Signaling::leaveRoom()
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current == SignalingStatus::DISCONNECTED)
                {
                    return false;
                }

                json message = {{"type", "leave"},
                                {"from", config_.device_id},
                                {"to", "server"},
                                {"time", getCurrentTimestamp()}};

                if (sendMessage(message))
                {
                    LOG_INFO(LOG_TAG, "发送离开房间消息");
                    setStatus(SignalingStatus::CONNECTED);

                    // 清理配对信息
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        peer_device_id_.clear();
                        role_.clear();
                    }

                    return true;
                }

                return false;
            }

            bool Signaling::requestRoomInfo()
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current == SignalingStatus::DISCONNECTED)
                {
                    return false;
                }

                json message = {{"type", "get_room_info"},
                                {"from", config_.device_id},
                                {"to", "server"},
                                {"time", getCurrentTimestamp()}};

                if (sendMessage(message))
                {
                    LOG_DEBUG(LOG_TAG, "发送房间信息请求");
                    return true;
                }

                return false;
            }

            bool Signaling::sendConnectionRequest(const std::string&       peer_id,
                                                  const ConnectionRequest& request)
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current != SignalingStatus::JOINED && current != SignalingStatus::PAIRED)
                {
                    LOG_WARN(LOG_TAG, "未加入房间，无法发送连接请求");
                    return false;
                }

                // 确定目标设备ID
                std::string target_device_id;
                if (!peer_id.empty())
                {
                    target_device_id = peer_id;
                }
                else
                {
                    // 如果未指定，自动推导：设备端发送给APP端
                    target_device_id = "app_" + extractRoomId(config_.device_id);
                }

                json message = {{"type", "get_connect"},
                                {"from", config_.device_id},
                                {"to", target_device_id},
                                {"data",
                                 {{"message", request.message},
                                  {"audio", request.audio},
                                  {"video", request.video}}},
                                {"time", getCurrentTimestamp()}};

                if (sendMessage(message))
                {
                    LOG_INFO(LOG_TAG, "发送连接请求到 %s (message=%d, audio=%d, video=%d)",
                             target_device_id.c_str(), request.message, request.audio,
                             request.video);
                    return true;
                }

                return false;
            }

            bool Signaling::sendOffer(const std::string& sdp, const std::string& target_device_id)
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current != SignalingStatus::PAIRED)
                {
                    LOG_WARN(LOG_TAG, "未配对，无法发送Offer");
                    return false;
                }

                // 验证角色：只有设备端可以发送offer
                if (!isDevice())
                {
                    LOG_ERROR(LOG_TAG, "只有设备端可以发送Offer");
                    return false;
                }

                json message = {{"type", "offer"},
                                {"from", config_.device_id},
                                {"to", target_device_id},
                                {"data", {{"sdp", sdp}}},
                                {"time", getCurrentTimestamp()}};

                if (sendMessage(message))
                {
                    LOG_INFO(LOG_TAG, "发送Offer SDP 目标=%s", target_device_id.c_str());
                    return true;
                }

                return false;
            }

            bool Signaling::sendIceCandidate(const std::string& candidate,
                                             const std::string& target_device_id)
            {
                SignalingStatus current = status_.load(std::memory_order_acquire);
                if (current != SignalingStatus::PAIRED)
                {
                    LOG_WARN(LOG_TAG, "未配对，无法发送ICE候选");
                    return false;
                }

                json message = {{"type", "ice"},
                                {"from", config_.device_id},
                                {"to", target_device_id},
                                {"data", {{"candidate", candidate}}},
                                {"time", getCurrentTimestamp()}};

                if (sendMessage(message))
                {
                    LOG_DEBUG(LOG_TAG, "发送ICE候选");
                    return true;
                }

                return false;
            }

            void Signaling::handleWebSocketMessage(const char* data, size_t size)
            {
                try
                {
                    std::string message(data, size);
                    handleMessage(message);
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "处理WebSocket消息异常: %s", e.what());
                }
            }

            void Signaling::handleWebSocketState(websocket::ConnectionState /*oldState*/,
                                                 websocket::ConnectionState new_state)
            {
                if (new_state == websocket::ConnectionState::CONNECTED)
                {
                    setStatus(SignalingStatus::CONNECTED);
                }
                else if (new_state == websocket::ConnectionState::DISCONNECTED ||
                         new_state == websocket::ConnectionState::CLOSED ||
                         new_state == websocket::ConnectionState::ERROR)
                {
                    setStatus(SignalingStatus::DISCONNECTED);
                }
            }

            void Signaling::handleWebSocketError(websocket::WebSocketError error,
                                                 const std::string&        message)
            {
                SignalingError sig_error = SignalingError::CONNECTION_FAILED;

                switch (error)
                {
                case websocket::WebSocketError::CONNECTION_FAILED:
                    sig_error = SignalingError::CONNECTION_FAILED;
                    break;
                case websocket::WebSocketError::SEND_FAILED:
                    sig_error = SignalingError::SEND_FAILED;
                    break;
                case websocket::WebSocketError::TIMEOUT:
                    sig_error = SignalingError::CONNECTION_TIMEOUT;
                    break;
                default:
                    sig_error = SignalingError::SERVER_ERROR;
                    break;
                }

                invokeErrorCallback(sig_error, message);
            }

            void Signaling::handleMessage(const std::string& message)
            {
                try
                {
                    json msg = json::parse(message);

                    // 验证消息基本字段
                    if (!msg.contains("type") || !msg.contains("from") || !msg.contains("to"))
                    {
                        LOG_WARN(LOG_TAG, "收到格式错误的消息");
                        return;
                    }

                    std::string type = msg["type"].get<std::string>();
                    LOG_DEBUG(LOG_TAG, "收到消息: type=%s, from=%s", type.c_str(),
                              msg["from"].get<std::string>().c_str());

                    // 根据消息类型分发处理
                    if (type == "role")
                    {
                        handleRoleMessage(msg);
                    }
                    else if (type == "get_connect")
                    {
                        handleConnectionRequestMessage(msg);
                    }
                    else if (type == "respond_post")
                    {
                        handleConnectionResponseMessage(msg);
                    }
                    else if (type == "offer")
                    {
                        handleOfferMessage(msg);
                    }
                    else if (type == "answer")
                    {
                        handleAnswerMessage(msg);
                    }
                    else if (type == "ice")
                    {
                        handleIceMessage(msg);
                    }
                    else if (type == "info")
                    {
                        handleInfoMessage(msg);
                    }
                    else if (type == "error")
                    {
                        handleErrorMessage(msg);
                    }
                    else
                    {
                        LOG_WARN(LOG_TAG, "收到未知消息类型: %s", type.c_str());
                    }
                }
                catch (const json::parse_error& e)
                {
                    LOG_ERROR(LOG_TAG, "JSON解析失败: %s", e.what());
                    LOG_DEBUG(LOG_TAG, "原始消息: %s", message.c_str());
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "处理消息异常: %s", e.what());
                }
            }

            void Signaling::handleRoleMessage(const json& msg)
            {
                if (!msg.contains("data"))
                {
                    LOG_WARN(LOG_TAG, "角色消息缺少data字段");
                    return;
                }

                auto data = msg["data"];

                std::string peer_id;
                std::string role;

                if (data.contains("peer_id"))
                {
                    peer_id = data["peer_id"].get<std::string>();
                }

                // 设备端始终作为offerer
                role = "offerer";

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    peer_device_id_ = peer_id;
                    role_           = role;
                }

                setStatus(SignalingStatus::PAIRED);

                LOG_INFO(LOG_TAG, "配对成功 - 对端设备: %s, 角色: %s", peer_id.c_str(),
                         role.c_str());

                // 通知WebRTC管理器可以开始工作
                invokeWebRTCReadyCallback(role, peer_id);
            }

            void Signaling::handleConnectionRequestMessage(const json& msg)
            {
                if (!msg.contains("data"))
                {
                    LOG_WARN(LOG_TAG, "连接请求消息缺少data字段");
                    return;
                }

                auto data = msg["data"];

                ConnectionRequest request;
                request.message = data.value("message", false);
                request.audio   = data.value("audio", false);
                request.video   = data.value("video", false);

                std::string from = msg.value("from", "unknown");
                LOG_INFO(LOG_TAG, "收到连接请求 from=%s (message=%d, audio=%d, video=%d)",
                         from.c_str(), request.message, request.audio, request.video);

                // 触发连接请求回调，让上层决定如何处理
                invokeConnectionRequestCallback(request);
            }

            void Signaling::handleConnectionResponseMessage(const json& msg)
            {
                bool        respond = msg.value("respond", false);
                std::string from    = msg.value("from", "unknown");

                LOG_INFO(LOG_TAG, "收到连接请求回应 from=%s, respond=%s", from.c_str(),
                         respond ? "true(同意)" : "false(拒绝)");

                // 触发连接请求回应回调
                invokeConnectionResponseCallback(respond);
            }

            void Signaling::handleOfferMessage(const json& msg)
            {
                LOG_INFO(LOG_TAG, "收到Offer SDP");

                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (offer_callback_)
                {
                    try
                    {
                        offer_callback_(msg);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "Offer回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::handleAnswerMessage(const json& msg)
            {
                LOG_INFO(LOG_TAG, "收到Answer SDP");

                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (answer_callback_)
                {
                    try
                    {
                        answer_callback_(msg);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "Answer回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::handleIceMessage(const json& msg)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (ice_candidate_callback_)
                {
                    try
                    {
                        ice_candidate_callback_(msg);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "ICE回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::handleInfoMessage(const json& msg)
            {
                if (!msg.contains("data"))
                {
                    LOG_WARN(LOG_TAG, "房间信息消息缺少data字段");
                    return;
                }

                auto     data = msg["data"];
                RoomInfo info;

                if (data.contains("room_id"))
                {
                    info.room_id = data["room_id"].get<std::string>();
                }

                if (data.contains("num"))
                {
                    info.num = data["num"].get<int>();
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    room_info_ = info;
                }

                LOG_INFO(LOG_TAG, "房间信息更新 - 房间ID: %s, 人数: %d", info.room_id.c_str(),
                         info.num);

                // 根据房间信息更新连接状态
                if (info.num == 2)
                {
                    // 房间已满，可以配对
                    SignalingStatus current = status_.load(std::memory_order_acquire);
                    if (current == SignalingStatus::JOINED)
                    {
                        LOG_DEBUG(LOG_TAG, "房间已满，等待角色分配...");
                    }
                }
                else if (info.num < 2)
                {
                    // 房间人数不足或房间关闭
                    SignalingStatus current = status_.load(std::memory_order_acquire);
                    if (current == SignalingStatus::PAIRED)
                    {
                        LOG_INFO(LOG_TAG, "房间人数不足，重置为已加入状态");
                        setStatus(SignalingStatus::JOINED);

                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            peer_device_id_.clear();
                            role_.clear();
                        }
                    }
                }

                // 触发房间信息变动回调
                invokeRoomInfoCallback(info);
            }

            void Signaling::handleErrorMessage(const json& msg)
            {
                if (!msg.contains("data"))
                {
                    LOG_WARN(LOG_TAG, "错误消息缺少data字段");
                    return;
                }

                auto        data          = msg["data"];
                int         error_code    = data.value("error_code", ERROR_CODE_SERVER_ERROR);
                std::string error_message = data.value("error_message", "未知错误");

                LOG_ERROR(LOG_TAG, "服务器错误 [%d]: %s", error_code, error_message.c_str());

                auto error = static_cast<SignalingError>(error_code);
                invokeErrorCallback(error, error_message);

                // 根据错误类型调整状态
                switch (error_code)
                {
                case ERROR_CODE_ROOM_FULL:          // 房间已满
                case ERROR_CODE_ROOM_NOT_EXISTS:    // 房间不存在
                case ERROR_CODE_CONNECTION_TIMEOUT: // 连接超时
                {
                    SignalingStatus current = status_.load(std::memory_order_acquire);
                    if (current == SignalingStatus::JOINED || current == SignalingStatus::PAIRED)
                    {
                        setStatus(SignalingStatus::CONNECTED);

                        std::lock_guard<std::mutex> lock(mutex_);
                        peer_device_id_.clear();
                        role_.clear();
                    }
                }
                break;
                case ERROR_CODE_PEER_OFFLINE: // 对端已离线
                {
                    SignalingStatus current = status_.load(std::memory_order_acquire);
                    if (current == SignalingStatus::PAIRED)
                    {
                        setStatus(SignalingStatus::JOINED);

                        std::lock_guard<std::mutex> lock(mutex_);
                        peer_device_id_.clear();
                        role_.clear();
                    }
                }
                break;
                default:
                    break;
                }
            }

            bool Signaling::sendMessage(const nlohmann::json& message)
            {
                if (!ws_client_ || !ws_client_->isConnected())
                {
                    LOG_WARN(LOG_TAG, "WebSocket未连接，无法发送消息");
                    return false;
                }

                try
                {
                    std::string               message_str = message.dump();
                    websocket::WebSocketError err         = websocket::WebSocketError::NONE;
                    err                                   = ws_client_->sendText(message_str);

                    if (err != websocket::WebSocketError::NONE)
                    {
                        LOG_ERROR(LOG_TAG, "发送消息失败");
                        return false;
                    }

                    return true;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "发送消息异常: %s", e.what());
                    return false;
                }
            }

            void Signaling::setStatus(SignalingStatus new_status)
            {
                SignalingStatus old_status =
                    status_.exchange(new_status, std::memory_order_acq_rel);

                if (old_status != new_status)
                {
                    LOG_INFO(LOG_TAG, "状态变更: %s -> %s", statusToString(old_status).c_str(),
                             statusToString(new_status).c_str());

                    invokeStatusCallback(new_status);
                }
            }

            uint64_t Signaling::getCurrentTimestamp() const
            {
                return static_cast<uint64_t>(app::tool::time::uptime_us());
            }

            std::string Signaling::extractRoomId(const std::string& device_id) const
            {
                // 从设备ID中提取房间标识
                // glasses_123456 -> 123456
                // app_123456 -> 123456
                size_t pos = device_id.find_last_of('_');
                if (pos != std::string::npos && pos < device_id.length() - 1)
                {
                    return device_id.substr(pos + 1);
                }
                return device_id; // 如果格式不正确，返回原始ID
            }

            void Signaling::invokeStatusCallback(SignalingStatus status)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (status_callback_)
                {
                    try
                    {
                        status_callback_(status);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "状态回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::invokeErrorCallback(SignalingError error, const std::string& message)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (error_callback_)
                {
                    try
                    {
                        error_callback_(error, message);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "错误回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::invokeWebRTCReadyCallback(const std::string& role,
                                                      const std::string& peer_device_id)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (webrtc_ready_callback_)
                {
                    try
                    {
                        webrtc_ready_callback_(role, peer_device_id);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "WebRTC就绪回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::invokeRoomInfoCallback(const RoomInfo& info)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (room_info_callback_)
                {
                    try
                    {
                        room_info_callback_(info);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "房间信息回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::invokeConnectionRequestCallback(const ConnectionRequest& request)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (connection_request_callback_)
                {
                    try
                    {
                        connection_request_callback_(request);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "连接请求回调异常: %s", e.what());
                    }
                }
            }

            void Signaling::invokeConnectionResponseCallback(bool accepted)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (connection_response_callback_)
                {
                    try
                    {
                        connection_response_callback_(accepted);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "连接请求回应回调异常: %s", e.what());
                    }
                }
            }

            std::string Signaling::statusToString(SignalingStatus status)
            {
                switch (status)
                {
                case SignalingStatus::DISCONNECTED:
                    return "DISCONNECTED";
                case SignalingStatus::CONNECTING:
                    return "CONNECTING";
                case SignalingStatus::CONNECTED:
                    return "CONNECTED";
                case SignalingStatus::JOINED:
                    return "JOINED";
                case SignalingStatus::PAIRED:
                    return "PAIRED";
                default:
                    return "UNKNOWN";
                }
            }

            std::string Signaling::errorToString(SignalingError error)
            {
                switch (error)
                {
                case SignalingError::NONE:
                    return "NONE";
                case SignalingError::ROOM_FULL:
                    return "ROOM_FULL";
                case SignalingError::ROOM_NOT_EXISTS:
                    return "ROOM_NOT_EXISTS";
                case SignalingError::MESSAGE_FORMAT_ERROR:
                    return "MESSAGE_FORMAT_ERROR";
                case SignalingError::DEVICE_ID_ERROR:
                    return "DEVICE_ID_ERROR";
                case SignalingError::CONNECTION_TIMEOUT:
                    return "CONNECTION_TIMEOUT";
                case SignalingError::PEER_OFFLINE:
                    return "PEER_OFFLINE";
                case SignalingError::SERVER_ERROR:
                    return "SERVER_ERROR";
                case SignalingError::CONNECTION_FAILED:
                    return "CONNECTION_FAILED";
                case SignalingError::SEND_FAILED:
                    return "SEND_FAILED";
                default:
                    return "UNKNOWN";
                }
            }

        } // namespace webrtc
    }     // namespace protocol
} // namespace app
