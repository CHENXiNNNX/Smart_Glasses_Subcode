// /**
//  * @file websocket.h
//  * @brief WebSocket客户端模块
//  * @details 用于连接xiaozhi云端AI服务
//  * 
//  * @author Smart_Glasses Team
//  * @date 2025-01-09
//  */

// #ifndef WEBSOCKET_H
// #define WEBSOCKET_H

// #include <string>
// #include <functional>
// #include <map>

// namespace glasses {
// namespace protocol {
// namespace websocket {

// /**
//  * @brief WebSocket消息接收回调函数类型
//  * 
//  * @param buffer 接收到的数据缓冲区
//  * @param size 数据大小（字节）
//  * @param user_data 用户自定义数据指针
//  */
// using MessageCallback = std::function<void(const char* buffer, size_t size, void* user_data)>;

// /**
//  * @brief WebSocket连接状态
//  */
// enum class ConnectionState {
//     DISCONNECTED = 0,  // 未连接
//     CONNECTING   = 1,  // 连接中
//     CONNECTED    = 2,  // 已连接
//     CLOSING      = 3,  // 关闭中
//     CLOSED       = 4   // 已关闭
// };

// /**
//  * @brief WebSocket配置结构
//  */
// struct WebSocketConfig {
//     std::string url;                        // WebSocket URL (wss://...)
//     std::map<std::string, std::string> headers;  // HTTP headers
//     std::string hello_message;              // 连接成功后发送的hello消息
//     bool auto_reconnect;                    // 是否自动重连
//     int reconnect_interval_ms;              // 重连间隔（毫秒）
//     void* user_data;                        // 用户自定义数据
    
//     WebSocketConfig()
//         : url("wss://api.tenclass.net/xiaozhi/v1/")
//         , auto_reconnect(true)
//         , reconnect_interval_ms(5000)
//         , user_data(nullptr) {}
// };

// /**
//  * @brief WebSocket客户端类
//  * @details 基于websocketpp的WebSocket实现
//  *          支持：
//  *          - WSS安全连接
//  *          - 自动重连
//  *          - 二进制/文本消息
//  *          - 自定义HTTP headers
//  */
// class WebSocketClient {
// public:
//     /**
//      * @brief 构造函数
//      * @param config WebSocket配置
//      */
//     WebSocketClient(const WebSocketConfig& config = WebSocketConfig());
    
//     /**
//      * @brief 析构函数
//      */
//     ~WebSocketClient();

//     /**
//      * @brief 设置消息接收回调
//      * 
//      * @param binary_callback 二进制消息回调
//      * @param text_callback 文本消息回调
//      * @param user_data 用户数据
//      */
//     void setCallbacks(MessageCallback binary_callback,
//                      MessageCallback text_callback,
//                      void* user_data = nullptr);

//     /**
//      * @brief 连接到WebSocket服务器
//      * @return true 连接成功
//      * @return false 连接失败
//      */
//     bool connect();

//     /**
//      * @brief 断开连接
//      */
//     void disconnect();

//     /**
//      * @brief 发送二进制消息
//      * 
//      * @param data 数据指针
//      * @param size 数据大小
//      * @return true 发送成功
//      * @return false 发送失败
//      */
//     bool sendBinary(const char* data, size_t size);

//     /**
//      * @brief 发送文本消息
//      * 
//      * @param data 数据指针
//      * @param size 数据大小
//      * @return true 发送成功
//      * @return false 发送失败
//      */
//     bool sendText(const char* data, size_t size);

//     /**
//      * @brief 获取连接状态
//      * @return ConnectionState 当前连接状态
//      */
//     ConnectionState getState() const;

//     /**
//      * @brief 检查是否已连接
//      * @return true 已连接
//      * @return false 未连接
//      */
//     bool isConnected() const;

//     /**
//      * @brief 检查是否已完成握手（发送了hello消息）
//      * @return true 已握手
//      * @return false 未握手
//      */
//     bool isHandshaked() const;

//     /**
//      * @brief 设置URL
//      * @param url WebSocket URL
//      */
//     void setUrl(const std::string& url);

//     /**
//      * @brief 设置HTTP headers
//      * @param headers HTTP headers映射
//      */
//     void setHeaders(const std::map<std::string, std::string>& headers);

//     /**
//      * @brief 添加HTTP header
//      * @param key Header名称
//      * @param value Header值
//      */
//     void addHeader(const std::string& key, const std::string& value);

//     /**
//      * @brief 设置Hello消息
//      * @param hello_msg Hello消息内容（JSON字符串）
//      */
//     void setHelloMessage(const std::string& hello_msg);

//     /**
//      * @brief 设置是否自动重连
//      * @param enable true启用，false禁用
//      */
//     void setAutoReconnect(bool enable);

//     // 禁止拷贝和赋值
//     WebSocketClient(const WebSocketClient&) = delete;
//     WebSocketClient& operator=(const WebSocketClient&) = delete;

// private:
//     class Impl;  // 前向声明，使用Pimpl惯用法
//     Impl* pimpl_;
// };

// /**
//  * @brief 创建xiaozhi AI的WebSocket客户端
//  * 
//  * @param device_id 设备ID（MAC地址）
//  * @param client_id 客户端ID（UUID）
//  * @param binary_cb 二进制消息回调（TTS音频）
//  * @param text_cb 文本消息回调（STT/LLM/IoT消息）
//  * @param user_data 用户数据
//  * @return WebSocketClient* 客户端指针
//  * 
//  * @example
//  *   auto ws = createXiaozhiClient("00:0c:29:bd:43:05", 
//  *                                  "d560294c-01d9-47d0-b538-085f38744b05",
//  *                                  onBinaryMessage, onTextMessage, nullptr);
//  *   ws->connect();
//  */
// WebSocketClient* createXiaozhiClient(
//     const std::string& device_id,
//     const std::string& client_id,
//     MessageCallback binary_cb = nullptr,
//     MessageCallback text_cb = nullptr,
//     void* user_data = nullptr);

// } // namespace websocket
// } // namespace protocol
// } // namespace glasses

// #endif // WEBSOCKET_H
