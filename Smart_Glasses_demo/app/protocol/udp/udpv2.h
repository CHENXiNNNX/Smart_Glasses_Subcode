/**
 * @file udpv2.h
 * @brief UDP进程间通信(IPC)模块V2 - 现代C++重写版本
 * @details 特性：
 *          - RAII资源管理（智能指针封装套接字）
 *          - 现代C++线程管理（std::thread替代pthread）
 *          - 安全的线程退出（无pthread_cancel）
 *          - 异常安全的回调
 *          - 配置化设计
 *          - 统一日志系统
 *          - 完整统计监控
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-11
 */

#ifndef UDPV2_H
#define UDPV2_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

namespace glasses {
namespace protocol {
namespace udp {

// ============================================================================
// 前向声明
// ============================================================================
class UdpEndpointV2;

// ============================================================================
// UDP错误类型
// ============================================================================

/**
 * @brief UDP错误类型
 */
enum class UdpError {
    NONE = 0,
    SOCKET_CREATE_FAILED,   // 套接字创建失败
    BIND_FAILED,            // 绑定失败
    SEND_FAILED,            // 发送失败
    RECV_FAILED,            // 接收失败
    INVALID_PARAM,          // 无效参数
    CALLBACK_EXCEPTION,     // 回调异常
    THREAD_ERROR,           // 线程错误
    ALREADY_RUNNING,        // 已在运行
    NOT_INITIALIZED,        // 未初始化
    UNKNOWN                 // 未知错误
};

// ============================================================================
// UDP配置
// ============================================================================

/**
 * @brief UDP端点配置
 */
struct UdpConfig {
    int port_local = 5678;              // 本地监听端口
    int port_remote = 5679;             // 远程发送端口
    std::string remote_ip = "127.0.0.1"; // 远程IP地址
    
    // 缓冲区配置
    size_t recv_buffer_size = 2048;     // 接收缓冲区大小
    size_t send_buffer_size = 2048;     // 发送缓冲区大小
    
    // 超时配置
    int recv_timeout_ms = 1000;         // 接收超时（用于优雅退出）
    
    // 功能开关
    bool enable_async_receive = true;   // 启用异步接收线程
    bool enable_statistics = true;      // 启用统计信息
};

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * @brief UDP数据接收回调
 * @param data 接收到的数据
 * @param size 数据大小
 * @return true-成功处理, false-处理失败
 */
using ReceiveCallback = std::function<bool(const uint8_t* data, size_t size)>;

/**
 * @brief UDP错误回调
 */
using UdpErrorCallback = std::function<void(UdpError error, const std::string& message)>;

// ============================================================================
// IPC消息类型（Smart_Glasses项目专用）
// ============================================================================

/**
 * @brief IPC消息类型
 */
enum class IpcMessageType : uint8_t {
    AUDIO_DATA      = 0x01,     // 音频数据（Opus编码）
    CONTROL_CMD     = 0x02,     // 控制命令
    STATE_UPDATE    = 0x03,     // 状态更新
    TEXT_DATA       = 0x04,     // 文本数据（STT/LLM）
    TTS_DATA        = 0x05,     // TTS音频数据
    MCP_TOOL_CALL   = 0x06,     // MCP工具调用
    HEARTBEAT       = 0xFF      // 心跳包
};

/**
 * @brief IPC消息头
 */
struct IpcMessageHeader {
    uint8_t  type;          // 消息类型（IpcMessageType）
    uint8_t  reserved;      // 保留字段
    uint16_t length;        // 数据长度（不含头部）
} __attribute__((packed));

// ============================================================================
// UDP端点V2类
// ============================================================================

/**
 * @brief UDP IPC端点V2
 * @details 现代C++重写的UDP通信端点，特性：
 *          - RAII自动资源管理
 *          - 智能指针，无裸指针
 *          - std::thread替代pthread
 *          - 安全的线程退出（无cancel）
 *          - 异常安全的回调
 *          - 完整的统计监控
 */
class UdpEndpointV2 {
public:
    /**
     * @brief 构造函数
     * @param config UDP配置
     */
    explicit UdpEndpointV2(const UdpConfig& config);
    
    /**
     * @brief 析构函数（RAII自动清理所有资源）
     */
    ~UdpEndpointV2();
    
    // ========================================================================
    // 数据收发
    // ========================================================================
    
    /**
     * @brief 发送数据（同步）
     * @param data 数据指针
     * @param size 数据大小
     * @return UdpError::NONE 成功
     */
    UdpError send(const uint8_t* data, size_t size);
    
    /**
     * @brief 发送数据（带重试）
     * @param data 数据指针
     * @param size 数据大小
     * @param max_retries 最大重试次数
     * @return UdpError::NONE 成功
     */
    UdpError sendWithRetry(const uint8_t* data, size_t size, int max_retries = 3);
    
    /**
     * @brief 同步接收数据（阻塞）
     * @param buffer 接收缓冲区
     * @param max_size 缓冲区大小
     * @param received_size 实际接收大小
     * @param timeout_ms 超时时间（毫秒）
     * @return UdpError::NONE 成功
     */
    UdpError receive(uint8_t* buffer, size_t max_size, size_t& received_size, int timeout_ms = 1000);
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /**
     * @brief 设置接收回调（异步接收时调用）
     * @param callback 接收回调函数
     */
    void setReceiveCallback(ReceiveCallback callback);
    
    /**
     * @brief 设置错误回调
     * @param callback 错误回调函数
     */
    void setErrorCallback(UdpErrorCallback callback);
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 检查端点是否有效
     * @return true 端点已初始化
     */
    bool isValid() const;
    
    /**
     * @brief 检查接收线程是否运行
     * @return true 线程运行中
     */
    bool isReceiving() const;
    
    /**
     * @brief 获取本地端口
     */
    int getLocalPort() const;
    
    /**
     * @brief 获取远程端口
     */
    int getRemotePort() const;
    
    // ========================================================================
    // 统计信息
    // ========================================================================
    
    /**
     * @brief UDP统计信息
     */
    struct Stats {
        std::atomic<uint64_t> packets_sent{0};          // 发送包数
        std::atomic<uint64_t> packets_received{0};      // 接收包数
        std::atomic<uint64_t> bytes_sent{0};            // 发送字节数
        std::atomic<uint64_t> bytes_received{0};        // 接收字节数
        std::atomic<uint64_t> send_errors{0};           // 发送错误数
        std::atomic<uint64_t> recv_errors{0};           // 接收错误数
        std::atomic<uint64_t> callback_exceptions{0};   // 回调异常数
    };
    
    /**
     * @brief 获取统计信息
     * @param out_stats 输出统计
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
    UdpEndpointV2(const UdpEndpointV2&) = delete;
    UdpEndpointV2& operator=(const UdpEndpointV2&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;  // ✅ 智能指针管理
};

// ============================================================================
// Smart_Glasses IPC端口定义
// ============================================================================

namespace ports {
    // 主进程端口
    constexpr int MAIN_LISTEN = 5678;   // 主进程监听端口
    constexpr int MAIN_SEND   = 5679;   // 主进程发送端口
    
    // AI服务进程端口
    constexpr int AI_LISTEN   = 5679;   // AI进程监听端口
    constexpr int AI_SEND     = 5678;   // AI进程发送端口
}

// ============================================================================
// 便捷工厂函数（返回智能指针）
// ============================================================================

/**
 * @brief 创建主进程的UDP端点（返回智能指针）
 * @param callback 接收回调
 * @return 端点智能指针
 */
inline std::unique_ptr<UdpEndpointV2> createMainEndpointV2(ReceiveCallback callback = nullptr) {
    UdpConfig config;
    config.port_local = ports::MAIN_LISTEN;
    config.port_remote = ports::MAIN_SEND;
    
    auto endpoint = std::make_unique<UdpEndpointV2>(config);
    if (callback) {
        endpoint->setReceiveCallback(callback);
    }
    return endpoint;
}

/**
 * @brief 创建AI进程的UDP端点（返回智能指针）
 * @param callback 接收回调
 * @return 端点智能指针
 */
inline std::unique_ptr<UdpEndpointV2> createAIEndpointV2(ReceiveCallback callback = nullptr) {
    UdpConfig config;
    config.port_local = ports::AI_LISTEN;
    config.port_remote = ports::AI_SEND;
    
    auto endpoint = std::make_unique<UdpEndpointV2>(config);
    if (callback) {
        endpoint->setReceiveCallback(callback);
    }
    return endpoint;
}

} // namespace udp
} // namespace protocol
} // namespace glasses

#endif // UDPV2_H

