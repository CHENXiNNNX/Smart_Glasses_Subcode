// /**
//  * @file udp.h
//  * @brief UDP进程间通信(IPC)模块
//  * @details 用于Smart_Glasses多进程架构中的本地UDP通信
//  *          主进程(main) ←→ AI进程(ai_service)
//  * 
//  * @author Smart_Glasses Team
//  * @date 2025-01-09
//  */

// #ifndef UDP_H
// #define UDP_H

// #include <cstddef>
// #include <cstdint>
// #include <functional>

// namespace glasses {
// namespace protocol {
// namespace udp {

// /**
//  * @brief UDP数据接收回调函数类型
//  * 
//  * @param buffer 接收到的数据缓冲区
//  * @param size 数据大小（字节）
//  * @param user_data 用户自定义数据指针
//  * @return int 处理结果（0表示成功，-1表示失败）
//  */
// using TransferCallback = std::function<int(char* buffer, size_t size, void* user_data)>;

// /**
//  * @brief UDP IPC端点类
//  * @details 封装UDP通信的发送和接收功能
//  *          - 支持异步接收（后台线程）
//  *          - 支持同步发送
//  *          - 支持回调处理接收数据
//  */
// class UdpEndpoint {
// public:
//     /**
//      * @brief 构造UDP端点
//      * 
//      * @param port_local 本地监听端口
//      * @param port_remote 远程发送端口
//      * @param callback 数据接收回调函数（可选）
//      * @param user_data 用户自定义数据（传递给回调函数）
//      * 
//      * @example
//      *   // 主进程创建端点（监听5678，发送到5679）
//      *   UdpEndpoint ep(5678, 5679, onDataReceived, nullptr);
//      * 
//      *   // AI进程创建端点（监听5679，发送到5678）
//      *   UdpEndpoint ep(5679, 5678, onDataReceived, nullptr);
//      */
//     UdpEndpoint(int port_local, int port_remote, 
//                 TransferCallback callback = nullptr, 
//                 void* user_data = nullptr);
    
//     /**
//      * @brief 析构函数，自动释放资源
//      */
//     ~UdpEndpoint();

//     /**
//      * @brief 发送数据到远程端点
//      * 
//      * @param data 要发送的数据
//      * @param len 数据长度
//      * @return int 0表示成功，-1表示失败
//      * 
//      * @example
//      *   const char* msg = "Hello";
//      *   endpoint.send(msg, strlen(msg));
//      */
//     int send(const char* data, int len);

//     /**
//      * @brief 同步接收数据（阻塞）
//      * 
//      * @param data 接收缓冲区
//      * @param maxlen 缓冲区最大长度
//      * @param retlen 实际接收的数据长度
//      * @return int 0表示成功，-1表示失败
//      * 
//      * @note 如果使用了回调函数，通常不需要调用此函数
//      */
//     int recv(unsigned char* data, int maxlen, int* retlen);

//     /**
//      * @brief 检查端点是否有效
//      * @return true 端点已成功初始化
//      * @return false 端点初始化失败
//      */
//     bool isValid() const;

//     /**
//      * @brief 获取本地监听端口
//      */
//     int getLocalPort() const;

//     /**
//      * @brief 获取远程发送端口
//      */
//     int getRemotePort() const;

//     // 禁止拷贝和赋值
//     UdpEndpoint(const UdpEndpoint&) = delete;
//     UdpEndpoint& operator=(const UdpEndpoint&) = delete;

// private:
//     class Impl;  // 前向声明，使用Pimpl惯用法
//     Impl* pimpl_;
    
//     // 接收线程函数
//     static void* receiveThreadFunc(void* arg);
// };

// /**
//  * @brief Smart_Glasses项目的IPC端口定义
//  */
// namespace ports {
//     // 主进程端口
//     constexpr int MAIN_LISTEN = 5678;    // 主进程监听端口
//     constexpr int MAIN_SEND   = 5679;    // 主进程发送端口
    
//     // AI服务进程端口
//     constexpr int AI_LISTEN   = 5679;    // AI进程监听端口
//     constexpr int AI_SEND     = 5678;    // AI进程发送端口
// }

// /**
//  * @brief IPC消息类型定义
//  */
// enum class MessageType : uint8_t {
//     AUDIO_DATA      = 0x01,  // 音频数据（Opus编码）
//     CONTROL_CMD     = 0x02,  // 控制命令
//     STATE_UPDATE    = 0x03,  // 状态更新
//     TEXT_DATA       = 0x04,  // 文本数据（STT/LLM）
//     TTS_DATA        = 0x05,  // TTS音频数据
//     MCP_TOOL_CALL   = 0x06,  // MCP工具调用
//     HEARTBEAT       = 0xFF   // 心跳包
// };

// /**
//  * @brief IPC消息头（简单协议）
//  */
// struct MessageHeader {
//     uint8_t  type;        // 消息类型（MessageType）
//     uint8_t  reserved;    // 保留字段
//     uint16_t length;      // 数据长度（不含头部）
// } __attribute__((packed));

// /**
//  * @brief 创建主进程的UDP端点
//  * 
//  * @param callback 数据接收回调
//  * @param user_data 用户数据
//  * @return UdpEndpoint* 端点指针（需要手动delete）
//  */
// inline UdpEndpoint* createMainEndpoint(TransferCallback callback = nullptr, 
//                                        void* user_data = nullptr) {
//     return new UdpEndpoint(ports::MAIN_LISTEN, ports::MAIN_SEND, callback, user_data);
// }

// /**
//  * @brief 创建AI服务进程的UDP端点
//  * 
//  * @param callback 数据接收回调
//  * @param user_data 用户数据
//  * @return UdpEndpoint* 端点指针（需要手动delete）
//  */
// inline UdpEndpoint* createAIEndpoint(TransferCallback callback = nullptr, 
//                                      void* user_data = nullptr) {
//     return new UdpEndpoint(ports::AI_LISTEN, ports::AI_SEND, callback, user_data);
// }

// } // namespace udp
// } // namespace protocol
// } // namespace glasses

// #endif // UDP_H

