#ifndef WEBSOCKET_HPP
#define WEBSOCKET_HPP
 
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

class WebSocketClient;

enum class ConnectionState {
    DISCONNECTED = 0,   // 未连接
    CONNECTING,         // 连接中
    CONNECTED,          // 已连接
    HANDSHAKED,         // 已握手（Hello消息已发送）
    CLOSING,            // 关闭中
    CLOSED,             // 已关闭
    ERROR
};

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
    UNKNOWN
};

struct WebSocketConfig {
    std::string url = "wss://api.tenclass.net/xiaozhi/v1/";
    std::map<std::string, std::string> headers;
    std::string hello_message;

    bool auto_reconnect = true;
    int reconnect_interval_ms = 5000;
    int max_reconnect_attempts = 5;

    int connect_timeout_ms = 10000;
    int ping_interval_ms = 30000;
    int pong_timeout_ms = 5000;

    bool verify_ssl = false;
    bool enable_detailed_logging = false;
};

using MessageCallback = std::function<bool(const char* data, size_t size)>;
using ConnectionStateCallback = std::function<void(ConnectionState old_state, ConnectionState new_state)>;
using WebSocketErrorCallback = std::function<void(WebSocketError error, const std::string& message)>;

class WebSocketClient {
public:
    explicit WebSocketClient(const WebSocketConfig& config = WebSocketConfig());
    ~WebSocketClient();

    WebSocketError connect();
    void disconnect();
    WebSocketError reconnect();
    bool shouldReconnect() const;
    void processReconnect();

    WebSocketError sendBinary(const char* data, size_t size);
    WebSocketError sendText(const char* data, size_t size);
    WebSocketError sendText(const std::string& message);

    void setBinaryCallback(MessageCallback callback);
    void setTextCallback(MessageCallback callback);
    void setStateCallback(ConnectionStateCallback callback);
    void setErrorCallback(WebSocketErrorCallback callback);

    ConnectionState getState() const;
    bool isConnected() const;
    bool isHandshaked() const;
    int getReconnectCount() const;

    void setUrl(const std::string& url);
    void addHeader(const std::string& key, const std::string& value);
    void setHelloMessage(const std::string& hello_msg);
    void setAutoReconnect(bool enable);
    const WebSocketConfig& getConfig() const;

    struct Stats {
        std::atomic<uint64_t> messages_sent{0};
        std::atomic<uint64_t> messages_received{0};
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<uint64_t> send_errors{0};
        std::atomic<uint64_t> connection_attempts{0};
        std::atomic<uint64_t> connection_failures{0};
        std::atomic<uint64_t> reconnections{0};
        std::atomic<uint64_t> callback_exceptions{0};
        std::atomic<uint64_t> total_uptime_us{0};
    };

    void getStats(Stats& out_stats) const;
    void resetStats();
    void logStats() const;
     
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

inline std::unique_ptr<WebSocketClient> createXiaozhiClient(
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

    auto client = std::make_unique<WebSocketClient>(config);

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

#endif // WEBSOCKET_HPP
 