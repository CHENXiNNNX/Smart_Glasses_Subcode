/**
 * @file test_websocket_main.cpp
 * @brief 测试小智服务器连接（包含激活+WebSocket连接）
 * @details 验证完整的连接流程：
 *          1. 设备激活（阻塞式等待）
 *          2. 获取设备ID和客户端ID
 *          3. 创建WebSocket客户端
 *          4. 连接WebSocket服务器
 *          5. 发送Hello消息
 *          6. 接收服务器响应
 */

#include <iostream>
#include <string>
#include <unistd.h>
#include <time.h>
#include <cstdint>
#include <thread>
#include <chrono>
#include "../app/chatbot/activation/activation.h"
#include "../app/chatbot/protocol_handle/handle.h"
#include "../app/protocol/websocket/websocket.h"
#include "../app/tool/mac/mac.h"
#include "../app/tool/uuid/uuid.h"
#include "../app/tool/log/log.h"

using namespace app::chatbot::activation;
using namespace app::chatbot::protocol_handle;
using namespace app::protocol::websocket;
using namespace app::tool::mac;
using namespace app::tool::uuid;
using namespace app::tool::log;

// 时间函数（替代common.h中的get_nowus）
inline uint64_t get_nowus(void) {
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
}

// 打印分隔线
void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

// 打印WebSocket状态
std::string stateToString(ConnectionState state) {
    switch (state) {
        case ConnectionState::DISCONNECTED: return "未连接";
        case ConnectionState::CONNECTING:   return "连接中";
        case ConnectionState::CONNECTED:    return "已连接";
        case ConnectionState::HANDSHAKED:   return "已握手";
        case ConnectionState::CLOSING:      return "关闭中";
        case ConnectionState::CLOSED:        return "已关闭";
        case ConnectionState::ERROR:        return "错误";
        default:                           return "未知";
    }
}

// 测试1：完整的连接流程（激活 + WebSocket连接）
void testFullConnection() {
    printSeparator("测试1: 完整连接流程（激活 + WebSocket）");
    
    // ========== 步骤1: 获取设备信息 ==========
    std::cout << "\n[步骤1] 获取设备信息..." << std::endl;
    std::string device_id = getWirelessMacAddress();
    std::string client_id = generateUUID();
    
    if (device_id.empty() || client_id.empty()) {
        std::cerr << "✗ 获取设备信息失败！" << std::endl;
        return;
    }
    
    std::cout << "✓ 设备ID: " << device_id << std::endl;
    std::cout << "✓ 客户端ID: " << client_id << std::endl;
    
    // ========== 步骤2: 设备激活 ==========
    printSeparator("步骤2: 设备激活（阻塞式等待）");
    
    ActivationConfig act_config;
    act_config.enable_detailed_logging = true;
    DeviceActivation activation(act_config);
    
    // 检查激活状态
    std::cout << "\n正在检查激活状态..." << std::endl;
    ActivationResult result = activation.checkActivation(device_id, client_id);
    
    if (!result.isActivated()) {
        std::cout << "\n设备未激活，开始阻塞式等待激活..." << std::endl;
        std::cout << "激活码: " << result.activation_code << std::endl;
        std::cout << "请访问: https://xiaozhi.me 使用激活码激活设备" << std::endl;
        
        // 设置回调
        activation.setStatusCallback([](ActivationStatus status, const ActivationResult& /*result*/) {
            if (status == ActivationStatus::ACTIVATED) {
                std::cout << "\n[回调] ✓ 设备已激活！" << std::endl;
            }
        });
        
        activation.setProgressCallback([](int elapsed_sec, int total_sec) {
            std::cout << "[进度] 已等待: " << elapsed_sec << "/" << total_sec << " 秒" << std::endl;
        });
        
        // 启动轮询（阻塞式等待）
        activation.startPolling(device_id, client_id, 300);  // 5分钟超时
        
        // 等待激活完成
        result = activation.waitForPollingComplete(0);  // 无限等待
        
        if (!result.isActivated()) {
            std::cerr << "\n✗ 激活失败或超时" << std::endl;
            return;
        }
    } else {
        std::cout << "✓ 设备已激活" << std::endl;
    }
    
    // ========== 步骤3: 创建协议处理器 ==========
    printSeparator("步骤3: 创建协议处理器");
    
    ProtocolConfig protocol_config;
    protocol_config.default_sample_rate = 16000;  // 16kHz
    protocol_config.default_channels = 1;
    protocol_config.default_frame_duration = 20;
    
    ProtocolHandler protocol_handler(protocol_config);
    
    // 设置协议回调
    protocol_handler.setHelloCallback([](const HelloMessage& msg) {
        std::cout << "\n[协议] ← 收到Hello响应！" << std::endl;
        std::cout << "  会话ID: " << msg.session_id << std::endl;
        std::cout << "  版本: " << msg.version << std::endl;
        std::cout << "  传输: " << msg.transport << std::endl;
        std::cout << "  音频参数: " << msg.audio_params.sample_rate << "Hz, "
                  << msg.audio_params.channels << "声道, "
                  << msg.audio_params.frame_duration << "ms" << std::endl;
    });
    
    protocol_handler.setSTTCallback([](const STTMessage& msg) {
        std::cout << "\n[协议] ← STT: \"" << msg.text << "\" (完成: " 
                  << (msg.is_final ? "是" : "否") << ")" << std::endl;
    });
    
    protocol_handler.setLLMCallback([](const LLMMessage& msg) {
        std::cout << "\n[协议] ← LLM: \"" << msg.text << "\" (完成: " 
                  << (msg.is_final ? "是" : "否") << ")" << std::endl;
    });
    
    protocol_handler.setTTSCallback([](const TTSMessage& msg) {
        std::string state_str;
        switch (msg.state) {
            case TTSState::START: state_str = "START"; break;
            case TTSState::SENTENCE_START: state_str = "SENTENCE_START"; break;
            case TTSState::STOP: state_str = "STOP"; break;
            default: state_str = "UNKNOWN"; break;
        }
        std::cout << "\n[协议] ← TTS状态: " << state_str << std::endl;
        if (!msg.text.empty()) {
            std::cout << "  文本: \"" << msg.text << "\"" << std::endl;
        }
    });
    
    protocol_handler.setErrorCallback([](const std::string& error) {
        std::cerr << "\n[协议] ✗ 错误: " << error << std::endl;
    });
    
    // 生成Hello消息
    std::string hello_msg = protocol_handler.generateHelloMessage(16000, 1, 20);
    std::cout << "\n生成的Hello消息:" << std::endl;
    std::cout << hello_msg << std::endl;
    
    // ========== 步骤4: 创建WebSocket客户端 ==========
    printSeparator("步骤4: 创建WebSocket客户端");
    
    WebSocketConfig ws_config;
    ws_config.url = "wss://api.tenclass.net/xiaozhi/v1/";
    ws_config.headers["Device-Id"] = device_id;
    ws_config.headers["Client-Id"] = client_id;
    ws_config.auto_reconnect = true;
    ws_config.reconnect_interval_ms = 5000;
    ws_config.verify_ssl = false;
    ws_config.enable_detailed_logging = true;
    
    WebSocketClient ws_client(ws_config);
    ws_client.setHelloMessage(hello_msg);
    
    // 设置WebSocket回调
    ws_client.setStateCallback([](ConnectionState old_state, ConnectionState new_state) {
        std::cout << "\n[WebSocket] 状态变化: " << stateToString(old_state) 
                  << " → " << stateToString(new_state) << std::endl;
    });
    
    ws_client.setTextCallback([&protocol_handler](const char* data, size_t size) -> bool {
        std::cout << "\n[WebSocket] ← 收到文本消息 (" << size << " 字节)" << std::endl;
        protocol_handler.parseMessage(data, size);
        return true;
    });
    
    ws_client.setBinaryCallback([](const char* /*data*/, size_t size) -> bool {
        std::cout << "\n[WebSocket] ← 收到二进制消息 (" << size << " 字节)" << std::endl;
        return true;
    });
    
    ws_client.setErrorCallback([](WebSocketError /*error*/, const std::string& message) {
        std::cerr << "\n[WebSocket] ✗ 错误: " << message << std::endl;
    });
    
    // ========== 步骤5: 连接WebSocket服务器 ==========
    printSeparator("步骤5: 连接WebSocket服务器");
    
    std::cout << "\n正在连接到: " << ws_config.url << std::endl;
    
    WebSocketError connect_err = ws_client.connect();
    if (connect_err != WebSocketError::NONE) {
        std::cerr << "✗ 连接失败" << std::endl;
        return;
    }
    
    std::cout << "✓ 连接请求已发送" << std::endl;
    
    // 等待连接和握手完成
    std::cout << "\n等待连接和握手完成..." << std::endl;
    int wait_count = 0;
    while (wait_count < 100) {  // 最多等待10秒
        if (ws_client.isHandshaked()) {
            std::cout << "\n✓ WebSocket已连接并握手成功！" << std::endl;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
        
        if (wait_count % 10 == 0) {
            std::cout << "  等待中... (" << wait_count / 10 << "秒)" << std::endl;
        }
    }
    
    if (!ws_client.isHandshaked()) {
        std::cerr << "\n✗ 连接超时" << std::endl;
        return;
    }
    
    // ========== 步骤6: 测试消息发送 ==========
    printSeparator("步骤6: 测试消息发送");
    
    // 等待一下，确保Hello响应已收到
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // 发送一个Listen消息测试
    std::string listen_msg = protocol_handler.generateListenMessage(
        ListenState::START, 
        ListenMode::AUTO
    );
    
    std::cout << "\n发送Listen消息:" << std::endl;
    std::cout << listen_msg << std::endl;
    
    WebSocketError send_err = ws_client.sendText(listen_msg);
    if (send_err == WebSocketError::NONE) {
        std::cout << "✓ Listen消息已发送" << std::endl;
    } else {
        std::cerr << "✗ 发送失败" << std::endl;
    }
    
    // ========== 步骤7: 保持连接并显示统计 ==========
    printSeparator("步骤7: 保持连接（30秒）");
    
    std::cout << "\n连接已建立，保持连接30秒，等待服务器响应..." << std::endl;
    std::cout << "按Ctrl+C可以提前结束" << std::endl;
    
    for (int i = 0; i < 30; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        if (!ws_client.isConnected()) {
            std::cerr << "\n✗ 连接已断开" << std::endl;
            break;
        }
        
        if ((i + 1) % 5 == 0) {
            std::cout << "  已连接 " << (i + 1) << " 秒..." << std::endl;
        }
    }
    
    // 打印统计信息
    printSeparator("连接统计");
    
    WebSocketClient::Stats stats;
    ws_client.getStats(stats);
    
    std::cout << "\nWebSocket统计:" << std::endl;
    std::cout << "  发送消息数: " << stats.messages_sent.load() << std::endl;
    std::cout << "  接收消息数: " << stats.messages_received.load() << std::endl;
    std::cout << "  发送字节数: " << stats.bytes_sent.load() << std::endl;
    std::cout << "  接收字节数: " << stats.bytes_received.load() << std::endl;
    std::cout << "  连接尝试次数: " << stats.connection_attempts.load() << std::endl;
    std::cout << "  重连次数: " << stats.reconnections.load() << std::endl;
    
    // 断开连接
    printSeparator("断开连接");
    ws_client.disconnect();
    std::cout << "\n✓ 已断开连接" << std::endl;
    
    printSeparator("测试完成");
    std::cout << "\n✓ 完整连接流程测试成功！" << std::endl;
}

// 测试2：仅测试WebSocket连接（跳过激活，假设已激活）
void testWebSocketOnly() {
    printSeparator("测试2: 仅WebSocket连接（跳过激活）");
    
    std::cout << "\n注意：此测试假设设备已激活" << std::endl;
    
    // 获取设备信息
    std::string device_id = getWirelessMacAddress();
    std::string client_id = generateUUID();
    
    if (device_id.empty() || client_id.empty()) {
        std::cerr << "✗ 获取设备信息失败！" << std::endl;
        return;
    }
    
    std::cout << "设备ID: " << device_id << std::endl;
    std::cout << "客户端ID: " << client_id << std::endl;
    
    // 创建WebSocket客户端（使用工厂函数）
    auto ws_client = createXiaozhiClient(device_id, client_id);
    
    // 创建协议处理器
    ProtocolConfig protocol_config;
    ProtocolHandler protocol_handler(protocol_config);
    
    std::string hello_msg = protocol_handler.generateHelloMessage(16000, 1, 20);
    ws_client->setHelloMessage(hello_msg);
    
    // 设置回调
    ws_client->setTextCallback([&protocol_handler](const char* data, size_t size) -> bool {
        protocol_handler.parseMessage(data, size);
        return true;
    });
    
    // 连接
    std::cout << "\n正在连接..." << std::endl;
    WebSocketError err = ws_client->connect();
    
    if (err != WebSocketError::NONE) {
        std::cerr << "✗ 连接失败" << std::endl;
        return;
    }
    
    // 等待握手
    int wait_count = 0;
    while (wait_count < 100) {
        if (ws_client->isHandshaked()) {
            std::cout << "✓ 连接成功！" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
    }
    
    if (ws_client->isHandshaked()) {
        std::cout << "\n✓ WebSocket连接测试成功！" << std::endl;
        std::cout << "保持连接10秒..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    } else {
        std::cerr << "✗ 连接超时" << std::endl;
    }
    
    ws_client->disconnect();
}

// 主函数
int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   小智服务器连接测试程序                ║" << std::endl;
    std::cout << "║   Xiaozhi Connection Test Suite       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    // 初始化日志系统
    LogConfig log_config;
    log_config.enable_console = true;
    log_config.enable_color = true;
    log_config.min_level = LogLevel::INFO;
    Logger::getInstance().initialize(log_config);
    
    try {
        int test_num = 1;
        
        // 如果提供了命令行参数，选择测试
        if (argc > 1) {
            test_num = std::stoi(argv[1]);
        }
        
        switch (test_num) {
            case 1:
                testFullConnection();
                break;
            case 2:
                testWebSocketOnly();
                break;
            default:
                std::cerr << "无效的测试编号: " << test_num << std::endl;
                std::cerr << "用法: " << argv[0] << " [1|2]" << std::endl;
                std::cerr << "  1: 完整连接流程（激活 + WebSocket，默认）" << std::endl;
                std::cerr << "  2: 仅WebSocket连接（跳过激活）" << std::endl;
                return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n========================================\n" << std::endl;
    
    return 0;
}
