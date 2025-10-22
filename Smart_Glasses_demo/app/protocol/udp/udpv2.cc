/**
 * @file udpv2.cc
 * @brief UDP进程间通信(IPC)模块V2实现
 */

#include "udpv2.h"
#include "../../tool/log/log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <condition_variable>

namespace app {
namespace protocol {
namespace udp {

using namespace app::tool::log;

// ============================================================================
// RAII套接字封装
// ============================================================================

/**
 * @brief RAII套接字包装器
 */
class SocketWrapper {
public:
    SocketWrapper() : fd_(-1) {}
    
    explicit SocketWrapper(int fd) : fd_(fd) {}
    
    ~SocketWrapper() {
        close();
    }
    
    bool create(int domain, int type, int protocol) {
        if (fd_ >= 0) {
            LOG_WARN("UDP", "Socket already created");
            return false;
        }
        
        fd_ = socket(domain, type, protocol);
        if (fd_ < 0) {
            LOG_ERROR("UDP", "Failed to create socket: %s", strerror(errno));
            return false;
        }
        
        return true;
    }
    
    bool bind(const sockaddr_in& addr) {
        if (fd_ < 0) {
            LOG_ERROR("UDP", "Socket not created");
            return false;
        }
        
        struct sockaddr_in addr_copy = addr;
        if (::bind(fd_, (struct sockaddr*)&addr_copy, sizeof(addr_copy)) < 0) {
            LOG_ERROR("UDP", "Failed to bind socket: %s", strerror(errno));
            return false;
        }
        
        return true;
    }
    
    bool setNonBlocking(bool non_blocking) {
        if (fd_ < 0) {
            return false;
        }
        
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0) {
            return false;
        }
        
        if (non_blocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        
        return fcntl(fd_, F_SETFL, flags) == 0;
    }
    
    bool setReceiveTimeout(int timeout_ms) {
        if (fd_ < 0) {
            return false;
        }
        
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        return setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
    }
    
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    
    int get() const { return fd_; }
    bool isValid() const { return fd_ >= 0; }
    
    // 禁止拷贝
    SocketWrapper(const SocketWrapper&) = delete;
    SocketWrapper& operator=(const SocketWrapper&) = delete;
    
    // 允许移动
    SocketWrapper(SocketWrapper&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    
    SocketWrapper& operator=(SocketWrapper&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

private:
    int fd_;
};

// ============================================================================
// UdpEndpointV2::Impl 内部实现
// ============================================================================

class UdpEndpointV2::Impl {
public:
    // 配置
    UdpConfig config;
    
    // 套接字（RAII管理）
    SocketWrapper send_socket;
    SocketWrapper recv_socket;
    
    // 远程地址
    struct sockaddr_in remote_addr;
    
    // 接收线程（智能指针管理）
    std::unique_ptr<std::thread> recv_thread;
    std::atomic<bool> should_stop{false};
    std::condition_variable recv_cv;
    std::mutex recv_mutex;
    
    // 回调函数
    ReceiveCallback receive_callback;
    UdpErrorCallback error_callback;
    mutable std::mutex callback_mutex;
    
    // 状态
    std::atomic<bool> is_valid{false};
    std::atomic<bool> is_receiving{false};
    
    // 统计信息
    UdpEndpointV2::Stats stats;
    
    explicit Impl(const UdpConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("UDPV2", "Impl created");
        std::memset(&remote_addr, 0, sizeof(remote_addr));
    }
    
    ~Impl() {
        LOG_DEBUG("UDPV2", "Impl destroying...");
        stopReceiveThread();
        LOG_DEBUG("UDPV2", "Impl destroyed");
    }
    
    // ========================================================================
    // 初始化
    // ========================================================================
    
    bool initialize() {
        // 1. 创建发送套接字
        if (!send_socket.create(AF_INET, SOCK_DGRAM, 0)) {
            LOG_ERROR("UDPV2", "Failed to create send socket");
            return false;
        }
        
        // 2. 配置远程地址
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(config.port_remote);
        if (inet_pton(AF_INET, config.remote_ip.c_str(), &remote_addr.sin_addr) <= 0) {
            LOG_ERROR("UDPV2", "Invalid remote IP: %s", config.remote_ip.c_str());
            return false;
        }
        
        // 3. 创建接收套接字
        if (!recv_socket.create(AF_INET, SOCK_DGRAM, 0)) {
            LOG_ERROR("UDPV2", "Failed to create receive socket");
            return false;
        }
        
        // 4. 绑定本地地址
        struct sockaddr_in local_addr;
        std::memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(config.port_local);
        if (inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) <= 0) {
            LOG_ERROR("UDPV2", "Invalid local IP");
            return false;
        }
        
        if (!recv_socket.bind(local_addr)) {
            LOG_ERROR("UDPV2", "Failed to bind to port %d", config.port_local);
            return false;
        }
        
        // 5. 设置接收超时（用于优雅退出）
        if (!recv_socket.setReceiveTimeout(config.recv_timeout_ms)) {
            LOG_WARN("UDPV2", "Failed to set receive timeout");
        }
        
        is_valid.store(true, std::memory_order_release);
        
        LOG_INFO("UDPV2", "UDP endpoint initialized: listen=%d, send=%d → %s",
                config.port_local, config.port_remote, config.remote_ip.c_str());
        
        // 6. 启动接收线程（如果启用）
        if (config.enable_async_receive) {
            startReceiveThread();
        }
        
        return true;
    }
    
    // ========================================================================
    // 发送数据
    // ========================================================================
    
    UdpError sendData(const uint8_t* data, size_t size) {
        if (!is_valid.load(std::memory_order_acquire)) {
            return UdpError::NOT_INITIALIZED;
        }
        
        if (!send_socket.isValid()) {
            return UdpError::NOT_INITIALIZED;
        }
        
        if (!data || size == 0) {
            return UdpError::INVALID_PARAM;
        }
        
        // 发送数据
        ssize_t bytes_sent = sendto(send_socket.get(), data, size, 0,
                                   (struct sockaddr*)&remote_addr, sizeof(remote_addr));
        
        if (bytes_sent < 0) {
            LOG_ERROR("UDPV2", "Send failed: %s", strerror(errno));
            stats.send_errors.fetch_add(1, std::memory_order_relaxed);
            invokeErrorCallback(UdpError::SEND_FAILED, strerror(errno));
            return UdpError::SEND_FAILED;
        }
        
        if (static_cast<size_t>(bytes_sent) != size) {
            LOG_WARN("UDPV2", "Partial send: %zd/%zu bytes", bytes_sent, size);
        }
        
        // 更新统计
        stats.packets_sent.fetch_add(1, std::memory_order_relaxed);
        stats.bytes_sent.fetch_add(bytes_sent, std::memory_order_relaxed);
        
        return UdpError::NONE;
    }
    
    UdpError sendDataWithRetry(const uint8_t* data, size_t size, int max_retries) {
        for (int retry = 0; retry < max_retries; retry++) {
            UdpError err = sendData(data, size);
            
            if (err == UdpError::NONE) {
                return UdpError::NONE;
            }
            
            if (retry < max_retries - 1) {
                LOG_INFO("UDPV2", "Send retry %d/%d", retry + 1, max_retries);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        LOG_ERROR("UDPV2", "Send failed after %d retries", max_retries);
        return UdpError::SEND_FAILED;
    }
    
    // ========================================================================
    // 接收线程管理（安全退出，无pthread_cancel）
    // ========================================================================
    
    void startReceiveThread() {
        if (recv_thread && recv_thread->joinable()) {
            LOG_WARN("UDPV2", "Receive thread already running");
            return;
        }
        
        should_stop.store(false, std::memory_order_release);
        is_receiving.store(true, std::memory_order_release);
        
        recv_thread = std::make_unique<std::thread>([this]() {
            LOG_INFO("UDPV2", "Receive thread started (port: %d)", config.port_local);
            
            std::vector<uint8_t> buffer(config.recv_buffer_size);
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            while (!should_stop.load(std::memory_order_acquire)) {
                // ✅ 使用带超时的recvfrom，配合should_stop实现优雅退出
                ssize_t bytes_received = recvfrom(
                    recv_socket.get(),
                    buffer.data(),
                    buffer.size(),
                    0,
                    (struct sockaddr*)&client_addr,
                    &client_len
                );
                
                if (bytes_received > 0) {
                    // 更新统计
                    stats.packets_received.fetch_add(1, std::memory_order_relaxed);
                    stats.bytes_received.fetch_add(bytes_received, std::memory_order_relaxed);
                    
                    LOG_DEBUG("UDPV2", "Received %zd bytes", bytes_received);
                    
                    // 触发回调（异常安全）
                    invokeReceiveCallback(buffer.data(), bytes_received);
                    
                } else if (bytes_received < 0) {
                    // 超时或错误
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 超时，正常（用于检查should_stop）
                        continue;
                    }
                    
                    // 其他错误
                    if (!should_stop.load(std::memory_order_acquire)) {
                        LOG_ERROR("UDPV2", "Receive error: %s", strerror(errno));
                        stats.recv_errors.fetch_add(1, std::memory_order_relaxed);
                        invokeErrorCallback(UdpError::RECV_FAILED, strerror(errno));
                    }
                    break;
                }
            }
            
            is_receiving.store(false, std::memory_order_release);
            LOG_INFO("UDPV2", "Receive thread stopped");
        });
    }
    
    void stopReceiveThread() {
        // ✅ 安全退出：设置标志，等待线程自然结束
        should_stop.store(true, std::memory_order_release);
        recv_cv.notify_all();
        
        if (recv_thread && recv_thread->joinable()) {
            recv_thread->join();  // 等待线程正常退出
        }
        recv_thread.reset();
        
        LOG_DEBUG("UDPV2", "Receive thread stopped safely");
    }
    
    // ========================================================================
    // 回调调用（异常安全）
    // ========================================================================
    
    void invokeReceiveCallback(const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (receive_callback) {
            try {
                bool success = receive_callback(data, size);
                if (!success) {
                    LOG_WARN("UDPV2", "Receive callback returned false");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("UDPV2", "Receive callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeErrorCallback(UdpError error, const std::string& message) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (error_callback) {
            try {
                error_callback(error, message);
            } catch (const std::exception& e) {
                LOG_ERROR("UDPV2", "Error callback exception: %s", e.what());
            }
        }
    }
};

// ============================================================================
// UdpEndpointV2 公共接口实现
// ============================================================================

UdpEndpointV2::UdpEndpointV2(const UdpConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    
    LOG_INFO("UDPV2", "UDP Endpoint V2 creating...");
    
    // 初始化套接字
    if (!pImpl_->initialize()) {
        LOG_ERROR("UDPV2", "Failed to initialize UDP endpoint");
        return;
    }
    
    LOG_INFO("UDPV2", "UDP Endpoint V2 created successfully");
}

UdpEndpointV2::~UdpEndpointV2() {
    LOG_INFO("UDPV2", "UDP Endpoint V2 destroying...");
    
    // 输出统计
    logStats();
    
    // RAII自动清理（pImpl_析构会停止线程并关闭套接字）
    LOG_INFO("UDPV2", "UDP Endpoint V2 destroyed");
}

// ========================================================================
// 数据收发
// ========================================================================

UdpError UdpEndpointV2::send(const uint8_t* data, size_t size) {
    return pImpl_->sendData(data, size);
}

UdpError UdpEndpointV2::sendWithRetry(const uint8_t* data, size_t size, int max_retries) {
    return pImpl_->sendDataWithRetry(data, size, max_retries);
}

UdpError UdpEndpointV2::receive(uint8_t* buffer, size_t max_size, 
                                size_t& received_size, int timeout_ms) {
    if (!pImpl_->is_valid.load(std::memory_order_acquire)) {
        return UdpError::NOT_INITIALIZED;
    }
    
    if (!pImpl_->recv_socket.isValid()) {
        return UdpError::NOT_INITIALIZED;
    }
    
    if (!buffer || max_size == 0) {
        return UdpError::INVALID_PARAM;
    }
    
    // 临时设置超时
    pImpl_->recv_socket.setReceiveTimeout(timeout_ms);
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // 同步接收
    ssize_t bytes_received = recvfrom(
        pImpl_->recv_socket.get(),
        buffer,
        max_size,
        0,
        (struct sockaddr*)&client_addr,
        &client_len
    );
    
    if (bytes_received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return UdpError::RECV_FAILED;  // 超时
        }
        
        LOG_ERROR("UDPV2", "Receive failed: %s", strerror(errno));
        pImpl_->stats.recv_errors.fetch_add(1, std::memory_order_relaxed);
        return UdpError::RECV_FAILED;
    }
    
    received_size = bytes_received;
    pImpl_->stats.packets_received.fetch_add(1, std::memory_order_relaxed);
    pImpl_->stats.bytes_received.fetch_add(bytes_received, std::memory_order_relaxed);
    
    return UdpError::NONE;
}

// ========================================================================
// 回调设置
// ========================================================================

void UdpEndpointV2::setReceiveCallback(ReceiveCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->receive_callback = callback;
    
    LOG_DEBUG("UDPV2", "Receive callback set");
}

void UdpEndpointV2::setErrorCallback(UdpErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->error_callback = callback;
    
    LOG_DEBUG("UDPV2", "Error callback set");
}

// ========================================================================
// 状态查询
// ========================================================================

bool UdpEndpointV2::isValid() const {
    return pImpl_->is_valid.load(std::memory_order_acquire);
}

bool UdpEndpointV2::isReceiving() const {
    return pImpl_->is_receiving.load(std::memory_order_acquire);
}

int UdpEndpointV2::getLocalPort() const {
    return pImpl_->config.port_local;
}

int UdpEndpointV2::getRemotePort() const {
    return pImpl_->config.port_remote;
}

// ========================================================================
// 统计信息
// ========================================================================

void UdpEndpointV2::getStats(Stats& out_stats) const {
    out_stats.packets_sent.store(pImpl_->stats.packets_sent.load());
    out_stats.packets_received.store(pImpl_->stats.packets_received.load());
    out_stats.bytes_sent.store(pImpl_->stats.bytes_sent.load());
    out_stats.bytes_received.store(pImpl_->stats.bytes_received.load());
    out_stats.send_errors.store(pImpl_->stats.send_errors.load());
    out_stats.recv_errors.store(pImpl_->stats.recv_errors.load());
    out_stats.callback_exceptions.store(pImpl_->stats.callback_exceptions.load());
}

void UdpEndpointV2::resetStats() {
    pImpl_->stats.packets_sent.store(0);
    pImpl_->stats.packets_received.store(0);
    pImpl_->stats.bytes_sent.store(0);
    pImpl_->stats.bytes_received.store(0);
    pImpl_->stats.send_errors.store(0);
    pImpl_->stats.recv_errors.store(0);
    pImpl_->stats.callback_exceptions.store(0);
    
    LOG_INFO("UDPV2", "Stats reset");
}

void UdpEndpointV2::logStats() const {
    uint64_t sent = pImpl_->stats.packets_sent.load();
    uint64_t received = pImpl_->stats.packets_received.load();
    uint64_t bytes_s = pImpl_->stats.bytes_sent.load();
    uint64_t bytes_r = pImpl_->stats.bytes_received.load();
    uint64_t send_err = pImpl_->stats.send_errors.load();
    uint64_t recv_err = pImpl_->stats.recv_errors.load();
    uint64_t exceptions = pImpl_->stats.callback_exceptions.load();
    
    LOG_INFO("UDPV2", "=== UDP Endpoint V2 Statistics ===");
    LOG_INFO("UDPV2", "  Packets sent:        %llu", sent);
    LOG_INFO("UDPV2", "  Packets received:    %llu", received);
    LOG_INFO("UDPV2", "  Bytes sent:          %llu (%.2f KB)", bytes_s, bytes_s / 1024.0);
    LOG_INFO("UDPV2", "  Bytes received:      %llu (%.2f KB)", bytes_r, bytes_r / 1024.0);
    LOG_INFO("UDPV2", "  Send errors:         %llu", send_err);
    LOG_INFO("UDPV2", "  Receive errors:      %llu", recv_err);
    LOG_INFO("UDPV2", "  Callback exceptions: %llu", exceptions);
    
    // 健康度评估
    if (sent > 0) {
        double send_error_rate = (double)send_err / sent * 100.0;
        if (send_error_rate > 1.0) {
            LOG_WARN("UDPV2", "Send error rate: %.2f%% (check network)", send_error_rate);
        }
    }
    
    if (received > 0) {
        double recv_error_rate = (double)recv_err / received * 100.0;
        if (recv_error_rate > 1.0) {
            LOG_WARN("UDPV2", "Receive error rate: %.2f%%", recv_error_rate);
        }
    }
}

} // namespace udp
} // namespace protocol
} // namespace glasses

