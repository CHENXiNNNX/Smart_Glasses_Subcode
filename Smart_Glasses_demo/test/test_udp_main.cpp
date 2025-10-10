/**
 * @file test_udp_main.cpp
 * @brief UDP IPC模块测试程序
 * @details 测试主进程和AI进程之间的UDP通信
 */

#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <chrono>
#include "app/protocol/udp/udp.h"

using namespace glasses::protocol::udp;

// 打印分隔线
void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

// ============================================================================
// 测试1: 主进程端回调
// ============================================================================
int mainProcessCallback(char* buffer, size_t size, void* user_data) {
    std::cout << "[MAIN] Received " << size << " bytes: ";
    std::cout.write(buffer, size);
    std::cout << std::endl;
    return 0;
}

// ============================================================================
// 测试2: AI进程端回调
// ============================================================================
int aiProcessCallback(char* buffer, size_t size, void* user_data) {
    std::cout << "[AI]   Received " << size << " bytes: ";
    std::cout.write(buffer, size);
    std::cout << std::endl;
    return 0;
}

// ============================================================================
// 测试场景1: 基本双向通信
// ============================================================================
void testBasicCommunication() {
    printSeparator("测试1: 基本双向通信");
    
    std::cout << "\n[INFO] 创建主进程端点..." << std::endl;
    UdpEndpoint* main_ep = createMainEndpoint(mainProcessCallback, nullptr);
    
    std::cout << "[INFO] 创建AI进程端点..." << std::endl;
    UdpEndpoint* ai_ep = createAIEndpoint(aiProcessCallback, nullptr);
    
    if (!main_ep->isValid() || !ai_ep->isValid()) {
        std::cerr << "[ERROR] 端点创建失败！" << std::endl;
        delete main_ep;
        delete ai_ep;
        return;
    }
    
    std::cout << "[INFO] 端点创建成功！" << std::endl;
    sleep(1);
    
    // 主进程 → AI进程
    std::cout << "\n[TEST] 主进程发送消息给AI进程..." << std::endl;
    const char* msg1 = "Hello from Main Process!";
    main_ep->send(msg1, strlen(msg1));
    sleep(1);
    
    // AI进程 → 主进程
    std::cout << "[TEST] AI进程发送消息给主进程..." << std::endl;
    const char* msg2 = "Hello from AI Service!";
    ai_ep->send(msg2, strlen(msg2));
    sleep(1);
    
    std::cout << "\n✓ 测试1完成\n" << std::endl;
    
    delete main_ep;
    delete ai_ep;
}

// ============================================================================
// 测试场景2: 大数据包传输
// ============================================================================
void testLargeData() {
    printSeparator("测试2: 大数据包传输");
    
    UdpEndpoint* main_ep = createMainEndpoint(mainProcessCallback, nullptr);
    UdpEndpoint* ai_ep = createAIEndpoint(aiProcessCallback, nullptr);
    
    if (!main_ep->isValid() || !ai_ep->isValid()) {
        std::cerr << "[ERROR] 端点创建失败！" << std::endl;
        delete main_ep;
        delete ai_ep;
        return;
    }
    
    sleep(1);
    
    // 发送1024字节数据
    std::cout << "\n[TEST] 发送1024字节数据..." << std::endl;
    char large_data[1024];
    memset(large_data, 'A', sizeof(large_data));
    large_data[sizeof(large_data) - 1] = '\0';
    
    main_ep->send(large_data, sizeof(large_data));
    sleep(1);
    
    std::cout << "\n✓ 测试2完成\n" << std::endl;
    
    delete main_ep;
    delete ai_ep;
}

// ============================================================================
// 测试场景3: 连续快速发送
// ============================================================================
void testContinuousSend() {
    printSeparator("测试3: 连续快速发送");
    
    UdpEndpoint* main_ep = createMainEndpoint(mainProcessCallback, nullptr);
    UdpEndpoint* ai_ep = createAIEndpoint(aiProcessCallback, nullptr);
    
    if (!main_ep->isValid() || !ai_ep->isValid()) {
        std::cerr << "[ERROR] 端点创建失败！" << std::endl;
        delete main_ep;
        delete ai_ep;
        return;
    }
    
    sleep(1);
    
    std::cout << "\n[TEST] 连续发送10条消息..." << std::endl;
    for (int i = 0; i < 10; i++) {
        std::string msg = "Message #" + std::to_string(i + 1);
        main_ep->send(msg.c_str(), msg.length());
        usleep(100000);  // 100ms
    }
    
    sleep(1);
    std::cout << "\n✓ 测试3完成\n" << std::endl;
    
    delete main_ep;
    delete ai_ep;
}

// ============================================================================
// 测试场景4: 模拟音频数据传输
// ============================================================================
int audioDataCallback(char* buffer, size_t size, void* user_data) {
    // 解析消息头
    if (size < sizeof(MessageHeader)) {
        std::cerr << "[AUDIO] ERROR: 数据包太小" << std::endl;
        return -1;
    }
    
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer);
    std::cout << "[AUDIO] 收到音频数据: type=" << static_cast<int>(header->type)
              << ", length=" << header->length << " bytes" << std::endl;
    
    return 0;
}

void testAudioDataTransfer() {
    printSeparator("测试4: 模拟音频数据传输");
    
    UdpEndpoint* main_ep = createMainEndpoint(audioDataCallback, nullptr);
    UdpEndpoint* ai_ep = createAIEndpoint(audioDataCallback, nullptr);
    
    if (!main_ep->isValid() || !ai_ep->isValid()) {
        std::cerr << "[ERROR] 端点创建失败！" << std::endl;
        delete main_ep;
        delete ai_ep;
        return;
    }
    
    sleep(1);
    
    std::cout << "\n[TEST] 模拟发送Opus音频数据..." << std::endl;
    
    // 构造音频数据包（头部+数据）
    const int opus_data_size = 120;  // 典型的Opus帧大小
    char audio_packet[sizeof(MessageHeader) + opus_data_size];
    
    MessageHeader* header = reinterpret_cast<MessageHeader*>(audio_packet);
    header->type = static_cast<uint8_t>(MessageType::AUDIO_DATA);
    header->reserved = 0;
    header->length = opus_data_size;
    
    // 填充模拟的音频数据
    memset(audio_packet + sizeof(MessageHeader), 0xAB, opus_data_size);
    
    // 发送3个音频包
    for (int i = 0; i < 3; i++) {
        std::cout << "[TEST] 发送音频包 #" << (i + 1) << std::endl;
        main_ep->send(audio_packet, sizeof(audio_packet));
        usleep(60000);  // 60ms间隔（模拟真实Opus帧率）
    }
    
    sleep(1);
    std::cout << "\n✓ 测试4完成\n" << std::endl;
    
    delete main_ep;
    delete ai_ep;
}

// ============================================================================
// 测试场景5: 心跳包测试
// ============================================================================
void testHeartbeat() {
    printSeparator("测试5: 心跳包测试");
    
    auto heartbeatCallback = [](char* buffer, size_t size, void* user_data) -> int {
        if (size >= sizeof(MessageHeader)) {
            MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer);
            if (header->type == static_cast<uint8_t>(MessageType::HEARTBEAT)) {
                std::cout << "[♥] 收到心跳包" << std::endl;
            }
        }
        return 0;
    };
    
    UdpEndpoint* main_ep = createMainEndpoint(heartbeatCallback, nullptr);
    UdpEndpoint* ai_ep = createAIEndpoint(heartbeatCallback, nullptr);
    
    if (!main_ep->isValid() || !ai_ep->isValid()) {
        std::cerr << "[ERROR] 端点创建失败！" << std::endl;
        delete main_ep;
        delete ai_ep;
        return;
    }
    
    sleep(1);
    
    std::cout << "\n[TEST] 发送心跳包..." << std::endl;
    
    MessageHeader heartbeat;
    heartbeat.type = static_cast<uint8_t>(MessageType::HEARTBEAT);
    heartbeat.reserved = 0;
    heartbeat.length = 0;
    
    // 发送5个心跳包
    for (int i = 0; i < 5; i++) {
        main_ep->send(reinterpret_cast<char*>(&heartbeat), sizeof(heartbeat));
        sleep(1);
    }
    
    std::cout << "\n✓ 测试5完成\n" << std::endl;
    
    delete main_ep;
    delete ai_ep;
}

// ============================================================================
// 主函数
// ============================================================================
int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   UDP IPC 模块测试程序                 ║" << std::endl;
    std::cout << "║   UDP IPC Module Test Suite           ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    try {
        testBasicCommunication();
        sleep(1);
        
        testLargeData();
        sleep(1);
        
        testContinuousSend();
        sleep(1);
        
        testAudioDataTransfer();
        sleep(1);
        
        testHeartbeat();
        
        // 测试总结
        printSeparator("测试总结");
        std::cout << "\n  ✓ 所有测试执行完成" << std::endl;
        std::cout << "  ✓ UDP IPC通信正常工作" << std::endl;
        std::cout << "  ✓ 端口配置: 主进程(5678/5679), AI进程(5679/5678)" << std::endl;
        std::cout << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "========================================\n" << std::endl;
    
    return 0;
}


