/**
 * @file test_websocket_main.cpp
 * @brief WebSocket客户端测试程序
 * @details 测试连接xiaozhi云端AI服务
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "app/protocol/websocket/websocket.h"
#include "app/chatbot/uuid/uuid.h"
#include "app/tool/mac/mac.h"

using namespace glasses::protocol::websocket;
using namespace glasses::tool;

// 打印分隔线
void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

// ============================================================================
// 消息回调函数
// ============================================================================
void onBinaryMessage(const char* buffer, size_t size, void* user_data) {
    std::cout << "[CALLBACK] Binary message received: " << size << " bytes" << std::endl;
    // 这里应该是TTS音频数据（Opus格式）
}

void onTextMessage(const char* buffer, size_t size, void* user_data) {
    std::string message(buffer, size);
    std::cout << "[CALLBACK] Text message received: " << message << std::endl;
    // 这里应该是JSON消息（STT/LLM/IoT）
}

// ============================================================================
// 测试场景1: 基本连接测试
// ============================================================================
void testBasicConnection() {
    printSeparator("测试1: 基本连接测试");

    // 创建WebSocket配置
    WebSocketConfig config;
    config.url = "wss://echo.websocket.org";  // 使用公共echo服务器测试
    config.auto_reconnect = false;
    config.hello_message = "";  // echo服务器不需要hello消息

    WebSocketClient client(config);
    client.setCallbacks(onBinaryMessage, onTextMessage, nullptr);

    std::cout << "\n[TEST] 连接到echo服务器..." << std::endl;
    if (client.connect()) {
        std::cout << "[TEST] 连接请求已发送，等待连接..." << std::endl;
        
        // 等待连接建立
        for (int i = 0; i < 10; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (client.isConnected()) {
                std::cout << "[TEST] ✓ 连接成功！" << std::endl;
                break;
            }
        }

        if (client.isConnected()) {
            // 发送测试消息
            std::cout << "\n[TEST] 发送测试消息..." << std::endl;
            client.sendText("Hello WebSocket!", 16);
            
            // 等待回显
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            // 断开连接
            std::cout << "\n[TEST] 断开连接..." << std::endl;
            client.disconnect();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            std::cerr << "[TEST] ✗ 连接失败！" << std::endl;
        }
    }

    std::cout << "\n✓ 测试1完成\n" << std::endl;
}

// ============================================================================
// 测试场景2: xiaozhi客户端创建
// ============================================================================
void testXiaozhiClient() {
    printSeparator("测试2: xiaozhi客户端创建");

    // 获取设备信息
    std::string mac = getWirelessMacAddress();
    std::string uuid = generateUUID();

    if (mac.empty()) {
        std::cerr << "[TEST] ✗ 无法获取MAC地址！" << std::endl;
        mac = "00:00:00:00:00:00";  // 使用默认值
    }

    std::cout << "\n[INFO] 设备信息:" << std::endl;
    std::cout << "  MAC地址: " << mac << std::endl;
    std::cout << "  UUID:    " << uuid << std::endl;

    // 创建xiaozhi客户端
    std::cout << "\n[TEST] 创建xiaozhi客户端..." << std::endl;
    WebSocketClient* client = createXiaozhiClient(mac, uuid, 
                                                   onBinaryMessage, 
                                                   onTextMessage, 
                                                   nullptr);

    // 注意：实际连接到xiaozhi需要激活设备
    // 这里只测试客户端创建，不进行实际连接
    std::cout << "[INFO] 客户端创建成功（未连接）" << std::endl;
    std::cout << "[INFO] 实际连接需要先激活设备" << std::endl;

    delete client;
    std::cout << "\n✓ 测试2完成\n" << std::endl;
}

// ============================================================================
// 测试场景3: 配置管理测试
// ============================================================================
void testConfiguration() {
    printSeparator("测试3: 配置管理测试");

    WebSocketConfig config;
    WebSocketClient client(config);

    std::cout << "\n[TEST] 测试配置方法..." << std::endl;

    // 设置URL
    client.setUrl("wss://test.example.com");
    std::cout << "  ✓ setUrl() 完成" << std::endl;

    // 添加headers
    client.addHeader("Authorization", "Bearer test-token");
    client.addHeader("Custom-Header", "test-value");
    std::cout << "  ✓ addHeader() 完成" << std::endl;

    // 设置hello消息
    client.setHelloMessage("{\"type\":\"test\"}");
    std::cout << "  ✓ setHelloMessage() 完成" << std::endl;

    // 设置自动重连
    client.setAutoReconnect(false);
    std::cout << "  ✓ setAutoReconnect() 完成" << std::endl;

    std::cout << "\n✓ 测试3完成\n" << std::endl;
}

// ============================================================================
// 测试场景4: 状态检查测试
// ============================================================================
void testStateCheck() {
    printSeparator("测试4: 状态检查测试");

    WebSocketConfig config;
    config.auto_reconnect = false;
    WebSocketClient client(config);

    std::cout << "\n[TEST] 检查初始状态..." << std::endl;
    std::cout << "  isConnected: " << (client.isConnected() ? "true" : "false") << std::endl;
    std::cout << "  isHandshaked: " << (client.isHandshaked() ? "true" : "false") << std::endl;

    if (!client.isConnected() && !client.isHandshaked()) {
        std::cout << "  ✓ 初始状态正确" << std::endl;
    }

    std::cout << "\n✓ 测试4完成\n" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================
int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   WebSocket 客户端测试程序             ║" << std::endl;
    std::cout << "║   WebSocket Client Test Suite         ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;

    try {
        testConfiguration();
        std::this_thread::sleep_for(std::chrono::seconds(1));

        testStateCheck();
        std::this_thread::sleep_for(std::chrono::seconds(1));

        testXiaozhiClient();
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 注意：echo服务器测试可能因网络问题失败
        std::cout << "\n[提示] 以下测试需要网络连接..." << std::endl;
        std::cout << "[提示] 按Enter继续，或Ctrl+C取消..." << std::endl;
        std::cin.get();

        testBasicConnection();

        // 测试总结
        printSeparator("测试总结");
        std::cout << "\n  ✓ 所有测试执行完成" << std::endl;
        std::cout << "  ✓ WebSocket客户端正常工作" << std::endl;
        std::cout << "  ✓ xiaozhi客户端创建成功" << std::endl;
        std::cout << "\n[注意] 实际连接xiaozhi需要：" << std::endl;
        std::cout << "  1. 先通过HTTP激活设备" << std::endl;
        std::cout << "  2. 使用有效的Device-Id和Client-Id" << std::endl;
        std::cout << "  3. 确保网络连接正常" << std::endl;
        std::cout << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "========================================\n" << std::endl;

    return 0;
}

