/**
 * @file websocketv2.cc
 * @brief WebSocket客户端模块V2实现
 */

#include "websocketv2.h"
#include "../../tool/log/log.h"
#include "../../../common/common.h"
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <thread>
#include <condition_variable>
#include <cstring>

namespace glasses {
namespace protocol {
namespace websocket {

using namespace tool::logger;

// WebSocket客户端类型定义
typedef websocketpp::client<websocketpp::config::asio_tls_client> ws_client_type;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;
typedef websocketpp::connection_hdl connection_hdl;

// ============================================================================
// WebSocketClientV2::Impl 内部实现
// ============================================================================

class WebSocketClientV2::Impl {
public:
    // 配置
    WebSocketConfig config;
    
    // WebSocket客户端（智能指针管理）
    std::unique_ptr<ws_client_type> client;
    connection_hdl connection_handle;
    
    // 状态
    std::atomic<ConnectionState> state{ConnectionState::DISCONNECTED};
    std::atomic<bool> handshaked{false};
    std::atomic<int> reconnect_count{0};
    uint64_t connected_timestamp{0};
    
    // 线程管理（智能指针）
    std::unique_ptr<std::thread> ws_thread;
    std::unique_ptr<std::thread> reconnect_thread;
    std::atomic<bool> should_stop{false};
    std::atomic<bool> should_reconnect{false};
    std::condition_variable reconnect_cv;
    std::mutex reconnect_mutex;
    
    // 回调函数
    MessageCallback binary_callback;
    MessageCallback text_callback;
    ConnectionStateCallback state_callback;
    WebSocketErrorCallback error_callback;
    mutable std::mutex callback_mutex;
    
    // 统计信息
    WebSocketClientV2::Stats stats;
    
    // 线程安全
    mutable std::mutex mutex;
    
    explicit Impl(const WebSocketConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("WebSocketV2", "Impl created");
    }
    
    ~Impl() {
        LOG_DEBUG("WebSocketV2", "Impl destroying...");
        cleanup();
        LOG_DEBUG("WebSocketV2", "Impl destroyed");
    }
    
    void cleanup() {
        // 停止所有线程
        should_stop.store(true, std::memory_order_release);
        should_reconnect.store(false, std::memory_order_release);
        reconnect_cv.notify_all();
        
        // 停止WebSocket客户端
        if (client) {
            try {
                client->stop();
            } catch (...) {
                LOG_WARN("WebSocketV2", "Exception during client stop");
            }
        }
        
        // 等待线程退出
        if (reconnect_thread && reconnect_thread->joinable()) {
            reconnect_thread->join();
        }
        reconnect_thread.reset();
        
        if (ws_thread && ws_thread->joinable()) {
            ws_thread->join();
        }
        ws_thread.reset();
    }
    
    // ========================================================================
    // 状态管理
    // ========================================================================
    
    void setState(ConnectionState new_state) {
        ConnectionState old_state = state.exchange(new_state, std::memory_order_acq_rel);
        
        if (old_state != new_state) {
            LOG_INFO("WebSocketV2", "State: %s → %s",
                    stateToString(old_state).c_str(),
                    stateToString(new_state).c_str());
            
            // 更新在线时间统计
            if (old_state == ConnectionState::CONNECTED && new_state != ConnectionState::CONNECTED) {
                uint64_t uptime = get_nowus() - connected_timestamp;
                stats.total_uptime_us.fetch_add(uptime, std::memory_order_relaxed);
            }
            
            if (new_state == ConnectionState::CONNECTED) {
                connected_timestamp = get_nowus();
            }
            
            // 触发状态回调
            invokeStateCallback(old_state, new_state);
        }
    }
    
    static std::string stateToString(ConnectionState state) {
        switch (state) {
            case ConnectionState::DISCONNECTED: return "DISCONNECTED";
            case ConnectionState::CONNECTING:   return "CONNECTING";
            case ConnectionState::CONNECTED:    return "CONNECTED";
            case ConnectionState::HANDSHAKED:   return "HANDSHAKED";
            case ConnectionState::CLOSING:      return "CLOSING";
            case ConnectionState::CLOSED:       return "CLOSED";
            case ConnectionState::ERROR:        return "ERROR";
            default:                            return "UNKNOWN";
        }
    }
    
    // ========================================================================
    // TLS处理
    // ========================================================================
    
    context_ptr onTlsInit(const std::string& hostname, websocketpp::connection_hdl) {
        context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(
            boost::asio::ssl::context::sslv23
        );
        
        try {
            ctx->set_options(boost::asio::ssl::context::default_workarounds |
                           boost::asio::ssl::context::no_sslv2 |
                           boost::asio::ssl::context::no_sslv3 |
                           boost::asio::ssl::context::single_dh_use);
            
            // 如果不验证SSL，设置宽松模式
            if (!config.verify_ssl) {
                ctx->set_verify_mode(boost::asio::ssl::verify_none);
            } else {
                ctx->set_verify_mode(boost::asio::ssl::verify_peer);
                // TODO: 添加证书验证回调
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("WebSocketV2", "TLS init error: %s", e.what());
        }
        
        return ctx;
    }
    
    // ========================================================================
    // 回调调用（异常安全）
    // ========================================================================
    
    void invokeBinaryCallback(const char* data, size_t size) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (binary_callback) {
            try {
                bool success = binary_callback(data, size);
                if (!success) {
                    LOG_WARN("WebSocketV2", "Binary callback returned false");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("WebSocketV2", "Binary callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeTextCallback(const char* data, size_t size) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (text_callback) {
            try {
                bool success = text_callback(data, size);
                if (!success) {
                    LOG_WARN("WebSocketV2", "Text callback returned false");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("WebSocketV2", "Text callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeStateCallback(ConnectionState old_state, ConnectionState new_state) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (state_callback) {
            try {
                state_callback(old_state, new_state);
            } catch (const std::exception& e) {
                LOG_ERROR("WebSocketV2", "State callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeErrorCallback(WebSocketError error, const std::string& message) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (error_callback) {
            try {
                error_callback(error, message);
            } catch (const std::exception& e) {
                LOG_ERROR("WebSocketV2", "Error callback exception: %s", e.what());
            }
        }
    }
    
    // ========================================================================
    // 连接和重连逻辑
    // ========================================================================
    
    WebSocketError connectInternal() {
        std::lock_guard<std::mutex> lock(mutex);
        
        // 检查当前状态
        ConnectionState current = state.load(std::memory_order_acquire);
        if (current != ConnectionState::DISCONNECTED && 
            current != ConnectionState::CLOSED &&
            current != ConnectionState::ERROR) {
            LOG_WARN("WebSocketV2", "Already connecting or connected");
            return WebSocketError::ALREADY_CONNECTED;
        }
        
        setState(ConnectionState::CONNECTING);
        stats.connection_attempts.fetch_add(1, std::memory_order_relaxed);
        
        try {
            // 创建WebSocket客户端
            client = std::make_unique<ws_client_type>();
            
            // 设置日志级别（减少噪音）
            client->clear_access_channels(websocketpp::log::alevel::all);
            if (config.enable_detailed_logging) {
                client->set_access_channels(websocketpp::log::alevel::app);
            }
            client->set_error_channels(websocketpp::log::elevel::all);
            
            // 初始化ASIO
            client->init_asio();
            
            // 提取hostname（用于TLS）
            std::string hostname = extractHostname(config.url);
            
            // 设置TLS初始化处理器
            client->set_tls_init_handler([this, hostname](websocketpp::connection_hdl hdl) {
                return onTlsInit(hostname, hdl);
            });
            
            // 设置消息处理器
            client->set_message_handler([this](connection_hdl hdl, ws_client_type::message_ptr msg) {
                auto opcode = msg->get_opcode();
                std::string payload = msg->get_payload();
                
                // 更新统计
                stats.messages_received.fetch_add(1, std::memory_order_relaxed);
                stats.bytes_received.fetch_add(payload.size(), std::memory_order_relaxed);
                
                if (opcode == websocketpp::frame::opcode::binary) {
                    // 二进制消息（TTS音频）
                    LOG_DEBUG("WebSocketV2", "← Binary message: %zu bytes", payload.size());
                    invokeBinaryCallback(payload.data(), payload.size());
                } else {
                    // 文本消息（JSON协议）
                    if (config.enable_detailed_logging) {
                        LOG_DEBUG("WebSocketV2", "← Text message: %s", payload.c_str());
                    }
                    invokeTextCallback(payload.data(), payload.size());
                }
            });
            
            // 设置连接打开处理器
            client->set_open_handler([this](connection_hdl hdl) {
                connection_handle = hdl;
                setState(ConnectionState::CONNECTED);
                
                LOG_INFO("WebSocketV2", "✓ Connection established");
                
                // 发送Hello消息
                if (!config.hello_message.empty()) {
                    WebSocketError err = sendTextInternal(config.hello_message);
                    if (err == WebSocketError::NONE) {
                        handshaked.store(true, std::memory_order_release);
                        setState(ConnectionState::HANDSHAKED);
                        LOG_INFO("WebSocketV2", "✓ Hello message sent, handshake complete");
                    } else {
                        LOG_ERROR("WebSocketV2", "Failed to send Hello message");
                    }
                }
            });
            
            // 设置连接关闭处理器
            client->set_close_handler([this](connection_hdl hdl) {
                setState(ConnectionState::CLOSED);
                handshaked.store(false, std::memory_order_release);
                
                ws_client_type::connection_ptr con = client->get_con_from_hdl(hdl);
                LOG_INFO("WebSocketV2", "Connection closed: code=%d, reason=%s",
                        con->get_remote_close_code(),
                        con->get_remote_close_reason().c_str());
                
                // 触发重连（如果启用）
                scheduleReconnect();
            });
            
            // 设置连接失败处理器
            client->set_fail_handler([this](connection_hdl hdl) {
                setState(ConnectionState::ERROR);
                stats.connection_failures.fetch_add(1, std::memory_order_relaxed);
                
                ws_client_type::connection_ptr con = client->get_con_from_hdl(hdl);
                std::string error_msg = con->get_ec().message();
                
                LOG_ERROR("WebSocketV2", "Connection failed: %s", error_msg.c_str());
                invokeErrorCallback(WebSocketError::CONNECTION_FAILED, error_msg);
                
                // 触发重连
                scheduleReconnect();
            });
            
            // 获取连接对象
            websocketpp::lib::error_code ec;
            ws_client_type::connection_ptr con = client->get_connection(config.url, ec);
            
            if (ec) {
                LOG_ERROR("WebSocketV2", "Could not create connection: %s", ec.message().c_str());
                setState(ConnectionState::ERROR);
                stats.connection_failures.fetch_add(1, std::memory_order_relaxed);
                return WebSocketError::CONNECTION_FAILED;
            }
            
            // 添加HTTP headers
            for (const auto& [key, value] : config.headers) {
                con->append_header(key, value);
                LOG_DEBUG("WebSocketV2", "Header: %s = %s", key.c_str(), value.c_str());
            }
            
            // 连接
            client->connect(con);
            
            // 启动WebSocket线程（智能指针管理）
            ws_thread = std::make_unique<std::thread>([this]() {
                LOG_DEBUG("WebSocketV2", "WebSocket thread started");
                
                try {
                    client->run();
                } catch (const std::exception& e) {
                    LOG_ERROR("WebSocketV2", "WebSocket thread exception: %s", e.what());
                }
                
                LOG_DEBUG("WebSocketV2", "WebSocket thread stopped");
            });
            
            LOG_INFO("WebSocketV2", "Connecting to: %s", config.url.c_str());
            return WebSocketError::NONE;
            
        } catch (const std::exception& e) {
            LOG_ERROR("WebSocketV2", "Connect exception: %s", e.what());
            setState(ConnectionState::ERROR);
            stats.connection_failures.fetch_add(1, std::memory_order_relaxed);
            return WebSocketError::CONNECTION_FAILED;
        }
    }
    
    void disconnectInternal() {
        std::lock_guard<std::mutex> lock(mutex);
        
        // 禁用重连
        should_reconnect.store(false, std::memory_order_release);
        should_stop.store(true, std::memory_order_release);
        reconnect_cv.notify_all();
        
        // 停止重连线程
        if (reconnect_thread && reconnect_thread->joinable()) {
            reconnect_thread->join();
        }
        reconnect_thread.reset();
        
        // 关闭连接
        if (client) {
            ConnectionState current = state.load(std::memory_order_acquire);
            
            if (current == ConnectionState::CONNECTED || 
                current == ConnectionState::HANDSHAKED) {
                
                setState(ConnectionState::CLOSING);
                
                try {
                    websocketpp::lib::error_code ec;
                    client->close(connection_handle, websocketpp::close::status::normal, "Client disconnect", ec);
                    
                    if (ec) {
                        LOG_WARN("WebSocketV2", "Close error: %s", ec.message().c_str());
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("WebSocketV2", "Disconnect exception: %s", e.what());
                }
            }
            
            // 停止客户端
            try {
                client->stop();
            } catch (...) {}
        }
        
        // 等待WebSocket线程
        if (ws_thread && ws_thread->joinable()) {
            ws_thread->join();
        }
        ws_thread.reset();
        
        setState(ConnectionState::DISCONNECTED);
        handshaked.store(false, std::memory_order_release);
        
        LOG_INFO("WebSocketV2", "Disconnected");
    }
    
    // ========================================================================
    // 消息发送
    // ========================================================================
    
    WebSocketError sendBinaryInternal(const char* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (!client) {
            return WebSocketError::NOT_CONNECTED;
        }
        
        ConnectionState current = state.load(std::memory_order_acquire);
        if (current != ConnectionState::CONNECTED && 
            current != ConnectionState::HANDSHAKED) {
            LOG_WARN("WebSocketV2", "Cannot send, not connected (state: %s)",
                    stateToString(current).c_str());
            return WebSocketError::NOT_CONNECTED;
        }
        
        try {
            websocketpp::lib::error_code ec;
            client->send(connection_handle, data, size, 
                        websocketpp::frame::opcode::binary, ec);
            
            if (ec) {
                LOG_ERROR("WebSocketV2", "Send binary failed: %s", ec.message().c_str());
                stats.send_errors.fetch_add(1, std::memory_order_relaxed);
                return WebSocketError::SEND_FAILED;
            }
            
            // 更新统计
            stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
            stats.bytes_sent.fetch_add(size, std::memory_order_relaxed);
            
            return WebSocketError::NONE;
            
        } catch (const std::exception& e) {
            LOG_ERROR("WebSocketV2", "Send binary exception: %s", e.what());
            stats.send_errors.fetch_add(1, std::memory_order_relaxed);
            return WebSocketError::SEND_FAILED;
        }
    }
    
    WebSocketError sendTextInternal(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (!client) {
            return WebSocketError::NOT_CONNECTED;
        }
        
        ConnectionState current = state.load(std::memory_order_acquire);
        if (current != ConnectionState::CONNECTED && 
            current != ConnectionState::HANDSHAKED) {
            LOG_WARN("WebSocketV2", "Cannot send, not connected");
            return WebSocketError::NOT_CONNECTED;
        }
        
        try {
            websocketpp::lib::error_code ec;
            client->send(connection_handle, message, 
                        websocketpp::frame::opcode::text, ec);
            
            if (ec) {
                LOG_ERROR("WebSocketV2", "Send text failed: %s", ec.message().c_str());
                stats.send_errors.fetch_add(1, std::memory_order_relaxed);
                return WebSocketError::SEND_FAILED;
            }
            
            // 更新统计
            stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
            stats.bytes_sent.fetch_add(message.size(), std::memory_order_relaxed);
            
            return WebSocketError::NONE;
            
        } catch (const std::exception& e) {
            LOG_ERROR("WebSocketV2", "Send text exception: %s", e.what());
            stats.send_errors.fetch_add(1, std::memory_order_relaxed);
            return WebSocketError::SEND_FAILED;
        }
    }
    
    // ========================================================================
    // 重连管理
    // ========================================================================
    
    void scheduleReconnect() {
        if (!config.auto_reconnect) {
            LOG_DEBUG("WebSocketV2", "Auto-reconnect disabled");
            return;
        }
        
        if (should_stop.load(std::memory_order_acquire)) {
            LOG_DEBUG("WebSocketV2", "Should stop, skip reconnect");
            return;
        }
        
        // 检查重连次数限制
        int current_count = reconnect_count.load(std::memory_order_acquire);
        if (config.max_reconnect_attempts > 0 && 
            current_count >= config.max_reconnect_attempts) {
            LOG_ERROR("WebSocketV2", "Max reconnect attempts reached: %d", current_count);
            invokeErrorCallback(WebSocketError::RECONNECT_FAILED, 
                              "Max reconnect attempts exceeded");
            return;
        }
        
        should_reconnect.store(true, std::memory_order_release);
        
        // 停止旧的重连线程
        if (reconnect_thread && reconnect_thread->joinable()) {
            reconnect_thread->join();
        }
        
        // 创建新的重连线程（智能指针管理）
        reconnect_thread = std::make_unique<std::thread>([this]() {
            LOG_INFO("WebSocketV2", "Scheduling reconnect in %dms", config.reconnect_interval_ms);
            
            std::unique_lock<std::mutex> lock(reconnect_mutex);
            
            // ✅ 可中断的等待
            bool cancelled = reconnect_cv.wait_for(
                lock,
                std::chrono::milliseconds(config.reconnect_interval_ms),
                [this]() { 
                    return !should_reconnect.load(std::memory_order_acquire) ||
                           should_stop.load(std::memory_order_acquire); 
                }
            );
            
            if (cancelled || should_stop.load(std::memory_order_acquire)) {
                LOG_DEBUG("WebSocketV2", "Reconnect cancelled");
                return;
            }
            
            if (should_reconnect.load(std::memory_order_acquire)) {
                int count = reconnect_count.fetch_add(1, std::memory_order_acq_rel) + 1;
                stats.reconnections.fetch_add(1, std::memory_order_relaxed);
                
                LOG_INFO("WebSocketV2", "Attempting to reconnect (attempt #%d)...", count);
                
                WebSocketError err = connectInternal();
                if (err != WebSocketError::NONE) {
                    LOG_ERROR("WebSocketV2", "Reconnect failed");
                }
            }
        });
    }
    
    // ========================================================================
    // 工具函数
    // ========================================================================
    
    std::string extractHostname(const std::string& url) const {
        std::string hostname = url;
        
        // 移除协议
        size_t start = hostname.find("://");
        if (start != std::string::npos) {
            hostname = hostname.substr(start + 3);
        }
        
        // 移除路径
        size_t end = hostname.find("/");
        if (end != std::string::npos) {
            hostname = hostname.substr(0, end);
        }
        
        // 移除端口
        end = hostname.find(":");
        if (end != std::string::npos) {
            hostname = hostname.substr(0, end);
        }
        
        return hostname;
    }
};

// ============================================================================
// WebSocketClientV2 公共接口实现
// ============================================================================

WebSocketClientV2::WebSocketClientV2(const WebSocketConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_INFO("WebSocketV2", "WebSocket Client V2 created");
}

WebSocketClientV2::~WebSocketClientV2() {
    LOG_INFO("WebSocketV2", "WebSocket Client V2 destroying...");
    
    // 输出统计
    logStats();
    
    // RAII自动清理
    LOG_INFO("WebSocketV2", "WebSocket Client V2 destroyed");
}

// ========================================================================
// 连接管理
// ========================================================================

WebSocketError WebSocketClientV2::connect() {
    return pImpl_->connectInternal();
}

void WebSocketClientV2::disconnect() {
    pImpl_->disconnectInternal();
}

WebSocketError WebSocketClientV2::reconnect() {
    // 先断开
    disconnect();
    
    // 短暂延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 重新连接
    pImpl_->reconnect_count.store(0, std::memory_order_release);
    return connect();
}

// ========================================================================
// 消息发送
// ========================================================================

WebSocketError WebSocketClientV2::sendBinary(const char* data, size_t size) {
    return pImpl_->sendBinaryInternal(data, size);
}

WebSocketError WebSocketClientV2::sendText(const char* data, size_t size) {
    std::string message(data, size);
    return pImpl_->sendTextInternal(message);
}

WebSocketError WebSocketClientV2::sendText(const std::string& message) {
    return pImpl_->sendTextInternal(message);
}

// ========================================================================
// 回调设置
// ========================================================================

void WebSocketClientV2::setBinaryCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->binary_callback = callback;
    LOG_DEBUG("WebSocketV2", "Binary callback set");
}

void WebSocketClientV2::setTextCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->text_callback = callback;
    LOG_DEBUG("WebSocketV2", "Text callback set");
}

void WebSocketClientV2::setStateCallback(ConnectionStateCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->state_callback = callback;
    LOG_DEBUG("WebSocketV2", "State callback set");
}

void WebSocketClientV2::setErrorCallback(WebSocketErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->error_callback = callback;
    LOG_DEBUG("WebSocketV2", "Error callback set");
}

// ========================================================================
// 状态查询
// ========================================================================

ConnectionState WebSocketClientV2::getState() const {
    return pImpl_->state.load(std::memory_order_acquire);
}

bool WebSocketClientV2::isConnected() const {
    ConnectionState current = getState();
    return (current == ConnectionState::CONNECTED || 
            current == ConnectionState::HANDSHAKED);
}

bool WebSocketClientV2::isHandshaked() const {
    return pImpl_->handshaked.load(std::memory_order_acquire);
}

int WebSocketClientV2::getReconnectCount() const {
    return pImpl_->reconnect_count.load(std::memory_order_acquire);
}

// ========================================================================
// 配置管理
// ========================================================================

void WebSocketClientV2::setUrl(const std::string& url) {
    pImpl_->config.url = url;
}

void WebSocketClientV2::addHeader(const std::string& key, const std::string& value) {
    pImpl_->config.headers[key] = value;
}

void WebSocketClientV2::setHelloMessage(const std::string& hello_msg) {
    pImpl_->config.hello_message = hello_msg;
}

void WebSocketClientV2::setAutoReconnect(bool enable) {
    pImpl_->config.auto_reconnect = enable;
}

const WebSocketConfig& WebSocketClientV2::getConfig() const {
    return pImpl_->config;
}

// ========================================================================
// 统计信息
// ========================================================================

void WebSocketClientV2::getStats(Stats& out_stats) const {
    out_stats.messages_sent.store(pImpl_->stats.messages_sent.load());
    out_stats.messages_received.store(pImpl_->stats.messages_received.load());
    out_stats.bytes_sent.store(pImpl_->stats.bytes_sent.load());
    out_stats.bytes_received.store(pImpl_->stats.bytes_received.load());
    out_stats.send_errors.store(pImpl_->stats.send_errors.load());
    out_stats.connection_attempts.store(pImpl_->stats.connection_attempts.load());
    out_stats.connection_failures.store(pImpl_->stats.connection_failures.load());
    out_stats.reconnections.store(pImpl_->stats.reconnections.load());
    out_stats.callback_exceptions.store(pImpl_->stats.callback_exceptions.load());
    out_stats.total_uptime_us.store(pImpl_->stats.total_uptime_us.load());
}

void WebSocketClientV2::resetStats() {
    pImpl_->stats.messages_sent.store(0);
    pImpl_->stats.messages_received.store(0);
    pImpl_->stats.bytes_sent.store(0);
    pImpl_->stats.bytes_received.store(0);
    pImpl_->stats.send_errors.store(0);
    pImpl_->stats.connection_attempts.store(0);
    pImpl_->stats.connection_failures.store(0);
    pImpl_->stats.reconnections.store(0);
    pImpl_->stats.callback_exceptions.store(0);
    pImpl_->stats.total_uptime_us.store(0);
    
    LOG_INFO("WebSocketV2", "Stats reset");
}

void WebSocketClientV2::logStats() const {
    uint64_t sent = pImpl_->stats.messages_sent.load();
    uint64_t received = pImpl_->stats.messages_received.load();
    uint64_t bytes_s = pImpl_->stats.bytes_sent.load();
    uint64_t bytes_r = pImpl_->stats.bytes_received.load();
    uint64_t send_err = pImpl_->stats.send_errors.load();
    uint64_t conn_attempts = pImpl_->stats.connection_attempts.load();
    uint64_t conn_failures = pImpl_->stats.connection_failures.load();
    uint64_t reconnects = pImpl_->stats.reconnections.load();
    uint64_t exceptions = pImpl_->stats.callback_exceptions.load();
    uint64_t uptime_us = pImpl_->stats.total_uptime_us.load();
    
    LOG_INFO("WebSocketV2", "=== WebSocket Client V2 Statistics ===");
    LOG_INFO("WebSocketV2", "  Messages sent:       %llu", sent);
    LOG_INFO("WebSocketV2", "  Messages received:   %llu", received);
    LOG_INFO("WebSocketV2", "  Bytes sent:          %llu (%.2f MB)", bytes_s, bytes_s / (1024.0 * 1024.0));
    LOG_INFO("WebSocketV2", "  Bytes received:      %llu (%.2f MB)", bytes_r, bytes_r / (1024.0 * 1024.0));
    LOG_INFO("WebSocketV2", "  Send errors:         %llu", send_err);
    LOG_INFO("WebSocketV2", "  Connection attempts: %llu", conn_attempts);
    LOG_INFO("WebSocketV2", "  Connection failures: %llu", conn_failures);
    LOG_INFO("WebSocketV2", "  Reconnections:       %llu", reconnects);
    LOG_INFO("WebSocketV2", "  Callback exceptions: %llu", exceptions);
    LOG_INFO("WebSocketV2", "  Total uptime:        %.2f hours", uptime_us / (1000000.0 * 3600.0));
    
    // 健康度评估
    if (conn_attempts > 0) {
        double failure_rate = (double)conn_failures / conn_attempts * 100.0;
        LOG_INFO("WebSocketV2", "  Connection success rate: %.2f%%", 100.0 - failure_rate);
        
        if (failure_rate > 20.0) {
            LOG_WARN("WebSocketV2", "High connection failure rate, check network stability");
        }
    }
    
    if (sent > 0) {
        double send_error_rate = (double)send_err / sent * 100.0;
        if (send_error_rate > 1.0) {
            LOG_WARN("WebSocketV2", "Send error rate: %.2f%%", send_error_rate);
        }
    }
}

} // namespace websocket
} // namespace protocol
} // namespace glasses

