/**
 * @file websocketv2.h
 * @brief WebSocket客户端模块V2 - 现代C++重写版本
 * @details 特性：
 *          - RAII资源管理（智能指针）
 *          - 安全的线程管理（std::unique_ptr<std::thread>）
 *          - 异常安全的回调
 *          - 配置化设计
 *          - 自动重连优化
 *          - 统一日志系统
 *          - 完整统计监控
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-11
 */

#ifndef WEBSOCKETV2_H
#define WEBSOCKETV2_H

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>

namespace app {
namespace protocol {
namespace websocket {

// ============================================================================
// 前向声明
// ============================================================================
class WebSocketClientV2;

// ============================================================================
// WebSocket状态枚举
// ============================================================================

/**
 * @brief WebSocket连接状态
 */
enum class ConnectionState {
    DISCONNECTED = 0,   // 未连接
    CONNECTING,         // 连接中
    CONNECTED,          // 已连接
    HANDSHAKED,         // 已握手（Hello消息已发送）
    CLOSING,            // 关闭中
    CLOSED,             // 已关闭
    ERROR               // 错误状态
};

/**
 * @brief WebSocket错误类型
 */
enum class WebSocketError {
    NONE = 0,
    CONNECTION_FAILED,      // 连接失败
    SEND_FAILED,            // 发送失败
    RECEIVE_FAILED,         // 接收失败
    TLS_INIT_FAILED,        // TLS初始化失败
    ALREADY_CONNECTED,      // 已连接
    NOT_CONNECTED,          // 未连接
    INVALID_URL,            // 无效URL
    CALLBACK_EXCEPTION,     // 回调异常
    RECONNECT_FAILED,       // 重连失败
    TIMEOUT,                // 超时
    UNKNOWN                 // 未知错误
};

// ============================================================================
// WebSocket配置
// ============================================================================

/**
 * @brief WebSocket配置
 */
struct WebSocketConfig {
    // 连接配置
    std::string url = "wss://api.tenclass.net/xiaozhi/v1/";
    std::map<std::string, std::string> headers;     // HTTP headers
    std::string hello_message;                      // 握手消息
    
    // 重连配置
    bool auto_reconnect = true;                     // 自动重连
    int reconnect_interval_ms = 5000;               // 重连间隔
    int max_reconnect_attempts = 5;                 // 最大重连次数（0=无限）
    
    // 超时配置
    int connect_timeout_ms = 10000;                 // 连接超时
    int ping_interval_ms = 30000;                   // Ping间隔
    int pong_timeout_ms = 5000;                     // Pong超时
    
    // TLS配置
    bool verify_ssl = false;                        // 验证SSL证书
    
    // 功能开关
    bool enable_detailed_logging = false;           // 详细日志
};

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * @brief 消息接收回调
 * @param data 消息数据
 * @param size 消息大小
 * @return true-成功处理, false-处理失败
 */
using MessageCallback = std::function<bool(const char* data, size_t size)>;

/**
 * @brief 连接状态变化回调
 */
using ConnectionStateCallback = std::function<void(ConnectionState old_state, ConnectionState new_state)>;

/**
 * @brief WebSocket错误回调
 */
using WebSocketErrorCallback = std::function<void(WebSocketError error, const std::string& message)>;

// ============================================================================
// WebSocket客户端V2类
// ============================================================================

/**
 * @brief WebSocket客户端V2
 * @details 现代C++重写的WebSocket客户端，特性：
 *          - RAII自动资源管理
 *          - 智能指针，无裸指针
 *          - 安全的线程管理
 *          - 异常安全的回调
 *          - 智能重连机制
 */
class WebSocketClientV2 {
public:
    /**
     * @brief 构造函数
     * @param config WebSocket配置
     */
    explicit WebSocketClientV2(const WebSocketConfig& config = WebSocketConfig());
    
    /**
     * @brief 析构函数（RAII自动清理所有资源）
     */
    ~WebSocketClientV2();
    
    // ========================================================================
    // 连接管理
    // ========================================================================
    
    /**
     * @brief 连接到WebSocket服务器
     * @return WebSocketError::NONE 成功
     */
    WebSocketError connect();
    
    /**
     * @brief 断开连接
     */
    void disconnect();
    
    /**
     * @brief 重新连接
     * @return WebSocketError::NONE 成功
     */
    WebSocketError reconnect();
    
    /**
     * @brief 检查是否应该重连
     * @return true 应该重连
     */
    bool shouldReconnect() const;
    
    /**
     * @brief 处理计划的重连
     */
    void processReconnect();
    
    // ========================================================================
    // 消息发送
    // ========================================================================
    
    /**
     * @brief 发送二进制消息
     * @param data 数据指针
     * @param size 数据大小
     * @return WebSocketError::NONE 成功
     */
    WebSocketError sendBinary(const char* data, size_t size);
    
    /**
     * @brief 发送文本消息
     * @param data 数据指针
     * @param size 数据大小
     * @return WebSocketError::NONE 成功
     */
    WebSocketError sendText(const char* data, size_t size);
    
    /**
     * @brief 发送文本消息（string版本）
     * @param message 消息字符串
     * @return WebSocketError::NONE 成功
     */
    WebSocketError sendText(const std::string& message);
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /**
     * @brief 设置二进制消息回调（TTS音频）
     */
    void setBinaryCallback(MessageCallback callback);
    
    /**
     * @brief 设置文本消息回调（JSON协议）
     */
    void setTextCallback(MessageCallback callback);
    
    /**
     * @brief 设置连接状态回调
     */
    void setStateCallback(ConnectionStateCallback callback);
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(WebSocketErrorCallback callback);
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取连接状态
     */
    ConnectionState getState() const;
    
    /**
     * @brief 检查是否已连接
     */
    bool isConnected() const;
    
    /**
     * @brief 检查是否已握手
     */
    bool isHandshaked() const;
    
    /**
     * @brief 获取重连次数
     */
    int getReconnectCount() const;
    
    // ========================================================================
    // 配置管理
    // ========================================================================
    
    /**
     * @brief 设置URL
     */
    void setUrl(const std::string& url);
    
    /**
     * @brief 添加HTTP Header
     */
    void addHeader(const std::string& key, const std::string& value);
    
    /**
     * @brief 设置Hello消息
     */
    void setHelloMessage(const std::string& hello_msg);
    
    /**
     * @brief 设置自动重连
     */
    void setAutoReconnect(bool enable);
    
    /**
     * @brief 获取当前配置
     */
    const WebSocketConfig& getConfig() const;
    
    // ========================================================================
    // 统计信息
    // ========================================================================
    
    /**
     * @brief WebSocket统计信息
     */
    struct Stats {
        std::atomic<uint64_t> messages_sent{0};         // 发送消息数
        std::atomic<uint64_t> messages_received{0};     // 接收消息数
        std::atomic<uint64_t> bytes_sent{0};            // 发送字节数
        std::atomic<uint64_t> bytes_received{0};        // 接收字节数
        std::atomic<uint64_t> send_errors{0};           // 发送错误数
        std::atomic<uint64_t> connection_attempts{0};   // 连接尝试次数
        std::atomic<uint64_t> connection_failures{0};   // 连接失败次数
        std::atomic<uint64_t> reconnections{0};         // 重连次数
        std::atomic<uint64_t> callback_exceptions{0};   // 回调异常数
        std::atomic<uint64_t> total_uptime_us{0};       // 总在线时间（微秒）
    };
    
    /**
     * @brief 获取统计信息
     */
    void getStats(Stats& out_stats) const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStats();
    
    /**
     * @brief 输出统计日志
     */
    void logStats() const;
    
    // 禁止拷贝和赋值
    WebSocketClientV2(const WebSocketClientV2&) = delete;
    WebSocketClientV2& operator=(const WebSocketClientV2&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;  // ✅ 智能指针管理
};

// ============================================================================
// 便捷工厂函数（xiaozhi AI专用）
// ============================================================================

/**
 * @brief 创建xiaozhi AI的WebSocket客户端（返回智能指针）
 * @param device_id 设备ID（MAC地址）
 * @param client_id 客户端ID（UUID）
 * @param binary_cb 二进制消息回调（TTS音频）
 * @param text_cb 文本消息回调（STT/LLM/IoT消息）
 * @return 客户端智能指针
 */
inline std::unique_ptr<WebSocketClientV2> createXiaozhiClientV2(
    const std::string& device_id,
    const std::string& client_id,
    MessageCallback binary_cb = nullptr,
    MessageCallback text_cb = nullptr) {
    
    WebSocketConfig config;
    config.url = "wss://api.tenclass.net/xiaozhi/v1/";
    config.headers["Device-Id"] = device_id;
    config.headers["Client-Id"] = client_id;
    config.auto_reconnect = true;
    config.reconnect_interval_ms = 5000;
    
    auto client = std::make_unique<WebSocketClientV2>(config);
    
    if (binary_cb) {
        client->setBinaryCallback(binary_cb);
    }
    if (text_cb) {
        client->setTextCallback(text_cb);
    }
    
    return client;
}

} // namespace websocket
} // namespace protocol
} // namespace app

#endif // WEBSOCKETV2_H

