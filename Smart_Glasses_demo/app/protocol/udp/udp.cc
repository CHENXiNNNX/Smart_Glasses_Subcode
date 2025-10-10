/**
 * @file udp.cc
 * @brief UDP进程间通信(IPC)模块实现
 * @details 基于xiaozhi项目的UDP IPC，适配为C++面向对象风格
 */

#include "udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <iostream>

namespace glasses {
namespace protocol {
namespace udp {

/**
 * @brief UDP端点的内部实现（Pimpl惯用法）
 */
class UdpEndpoint::Impl {
public:
    int socket_send;              // 发送套接字
    int socket_recv;              // 接收套接字
    int port_local;               // 本地监听端口
    int port_remote;              // 远程发送端口
    struct sockaddr_in remote_addr;  // 远程地址结构
    TransferCallback callback;    // 数据接收回调
    void* user_data;              // 用户数据
    pthread_t recv_thread;        // 接收线程
    bool thread_running;          // 线程运行标志
    bool is_valid;                // 端点是否有效

    Impl() : socket_send(-1), socket_recv(-1), 
             port_local(0), port_remote(0),
             callback(nullptr), user_data(nullptr),
             recv_thread(0), thread_running(false),
             is_valid(false) {
        memset(&remote_addr, 0, sizeof(remote_addr));
    }

    ~Impl() {
        cleanup();
    }

    void cleanup() {
        // 停止接收线程
        if (thread_running) {
            thread_running = false;
            // 注意：这里简化处理，实际应该优雅地关闭线程
            pthread_cancel(recv_thread);
            pthread_join(recv_thread, nullptr);
        }

        // 关闭套接字
        if (socket_send >= 0) {
            close(socket_send);
            socket_send = -1;
        }
        if (socket_recv >= 0) {
            close(socket_recv);
            socket_recv = -1;
        }
    }
};

/**
 * @brief 接收线程函数
 */
void* UdpEndpoint::receiveThreadFunc(void* arg) {
    UdpEndpoint::Impl* impl = static_cast<UdpEndpoint::Impl*>(arg);
    
    char buffer[2048];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received;

    std::cout << "[UDP] Receive thread started, listening on port " 
              << impl->port_local << std::endl;

    while (impl->thread_running) {
        // 接收数据（阻塞）
        bytes_received = recvfrom(impl->socket_recv, buffer, sizeof(buffer), 0,
                                 (struct sockaddr*)&client_addr, &client_len);
        
        if (bytes_received > 0) {
            // 调用回调函数处理数据
            if (impl->callback) {
                impl->callback(buffer, bytes_received, impl->user_data);
            }
        } else if (bytes_received < 0) {
            if (impl->thread_running) {
                perror("[UDP] recvfrom error");
            }
            break;
        }
    }

    std::cout << "[UDP] Receive thread stopped" << std::endl;
    return nullptr;
}

// ============================================================================
// UdpEndpoint 公共接口实现
// ============================================================================

UdpEndpoint::UdpEndpoint(int port_local, int port_remote,
                         TransferCallback callback, void* user_data)
    : pimpl_(new Impl()) {
    
    pimpl_->port_local = port_local;
    pimpl_->port_remote = port_remote;
    pimpl_->callback = callback;
    pimpl_->user_data = user_data;

    // ========== 1. 初始化发送套接字 ==========
    pimpl_->socket_send = socket(AF_INET, SOCK_DGRAM, 0);
    if (pimpl_->socket_send < 0) {
        perror("[UDP] Failed to create send socket");
        return;
    }

    // 设置远程地址
    memset(&pimpl_->remote_addr, 0, sizeof(pimpl_->remote_addr));
    pimpl_->remote_addr.sin_family = AF_INET;
    pimpl_->remote_addr.sin_port = htons(port_remote);
    if (inet_pton(AF_INET, "127.0.0.1", &pimpl_->remote_addr.sin_addr) <= 0) {
        perror("[UDP] Invalid remote address");
        return;
    }

    // ========== 2. 初始化接收套接字 ==========
    pimpl_->socket_recv = socket(AF_INET, SOCK_DGRAM, 0);
    if (pimpl_->socket_recv < 0) {
        perror("[UDP] Failed to create receive socket");
        return;
    }

    // 设置本地地址
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port_local);
    if (inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) <= 0) {
        perror("[UDP] Invalid local address");
        return;
    }

    // 绑定本地端口
    if (bind(pimpl_->socket_recv, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("[UDP] Failed to bind socket");
        return;
    }

    // ========== 3. 启动接收线程（如果有回调） ==========
    if (callback) {
        pimpl_->thread_running = true;
        if (pthread_create(&pimpl_->recv_thread, nullptr, 
                          UdpEndpoint::receiveThreadFunc, pimpl_) != 0) {
            perror("[UDP] Failed to create receive thread");
            pimpl_->thread_running = false;
            return;
        }
    }

    pimpl_->is_valid = true;
    std::cout << "[UDP] Endpoint created: listen=" << port_local 
              << ", send=" << port_remote << std::endl;
}

UdpEndpoint::~UdpEndpoint() {
    if (pimpl_) {
        delete pimpl_;
        pimpl_ = nullptr;
    }
}

int UdpEndpoint::send(const char* data, int len) {
    if (!pimpl_ || !pimpl_->is_valid) {
        std::cerr << "[UDP] ERROR: Endpoint not valid" << std::endl;
        return -1;
    }

    if (pimpl_->socket_send < 0) {
        std::cerr << "[UDP] ERROR: Send socket not initialized" << std::endl;
        return -1;
    }

    // 发送数据
    ssize_t bytes_sent = sendto(pimpl_->socket_send, data, len, 0,
                               (struct sockaddr*)&pimpl_->remote_addr,
                               sizeof(pimpl_->remote_addr));
    
    if (bytes_sent != len) {
        perror("[UDP] Failed to send data");
        return -1;
    }

    return 0;
}

int UdpEndpoint::recv(unsigned char* data, int maxlen, int* retlen) {
    if (!pimpl_ || !pimpl_->is_valid) {
        std::cerr << "[UDP] ERROR: Endpoint not valid" << std::endl;
        return -1;
    }

    if (pimpl_->socket_recv < 0) {
        std::cerr << "[UDP] ERROR: Receive socket not initialized" << std::endl;
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 接收数据（阻塞）
    ssize_t bytes_received = recvfrom(pimpl_->socket_recv, data, maxlen, 0,
                                     (struct sockaddr*)&client_addr, &client_len);
    
    if (bytes_received < 0) {
        perror("[UDP] Failed to receive data");
        return -1;
    }

    *retlen = static_cast<int>(bytes_received);
    return 0;
}

bool UdpEndpoint::isValid() const {
    return pimpl_ && pimpl_->is_valid;
}

int UdpEndpoint::getLocalPort() const {
    return pimpl_ ? pimpl_->port_local : -1;
}

int UdpEndpoint::getRemotePort() const {
    return pimpl_ ? pimpl_->port_remote : -1;
}

} // namespace udp
} // namespace protocol
} // namespace glasses

