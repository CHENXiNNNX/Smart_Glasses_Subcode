#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <string>
#include <functional>
#include <map>
#include <iostream>  
#include <thread>
#include <memory>

// 用于打包opus二进制数据的协议
struct BinProtocol {
    uint16_t version;
    uint16_t type;
    uint32_t payload_size;
    uint8_t payload[];
} __attribute__((packed));

struct BinProtocolInfo {
    uint16_t version;
    uint16_t type;
};

class WebSocketClient {
public:
    using message_callback_t = std::function<void(const std::string&, bool)>;
    using close_callback_t = std::function<void()>;

    WebSocketClient(const std::string& address, int port, const std::string& token, const std::string& deviceId, int protocolVersion);
    ~WebSocketClient();

    /**
     * 运行 WebSocket 客户端
     */
    void Run();

    /**
     * 设置 WebSocket 连接服务器
     */
    void Connect();

    /**
     * 断开连接
     */
    void Close();

    /**
     * 停止 WebSocket 客户端
     */
    void Terminate();

    /**
     * 发送文本消息。
     * 
     * @param message 要发送的消息。
     */
    void SendText(const std::string& message);

    /**
     * 发送二进制数据。
     * 
     * @param data 要发送的数据。
     * @param size 数据大小。
     */
    void SendBinary(const uint8_t* data, size_t size);
    
    /**
     * 设置接收到消息的回调函数
     */
    void SetMessageCallback(message_callback_t callback);

    /**
     * 设置关闭的回调函数
     */
    void SetCloseCallback(close_callback_t callback);    

    /**
     * check if the client is connected
     */
    bool IsConnected() const { return is_connected_; }

private:
    using client_t = websocketpp::client<websocketpp::config::asio_client>;
    client_t ws_client_;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> thread_;
    websocketpp::connection_hdl connection_hdl_;
    std::map<std::string, std::string> headers_;
    message_callback_t on_message_;
    close_callback_t on_close_;  // 关闭回调
    std::string uri_;
    bool is_connected_ = false;

    void on_open(websocketpp::connection_hdl hdl);
    void on_message(websocketpp::connection_hdl hdl, client_t::message_ptr msg);
    void on_close(websocketpp::connection_hdl hdl); 
};

#endif // WEBSOCKETCLIENT_H
