/**
 * @file test_activation_main.cpp
 * @brief 测试连接小智服务器获取激活码
 * @details 验证设备激活功能，包括：
 *          1. 获取设备ID（MAC地址）
 *          2. 获取客户端ID（UUID）
 *          3. 连接小智服务器检查激活状态
 *          4. 获取激活码（如果未激活）
 */

#include <iostream>
#include <string>
#include <unistd.h>
#include <time.h>
#include <cstdint>
#include "../app/chatbot/activation/activation.h"
#include "../app/tool/mac/mac.h"
#include "../app/tool/uuid/uuid.h"
#include "../app/tool/log/log.h"

// 时间函数（替代common.h中的get_nowus）
inline uint64_t get_nowus(void) {
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
}

using namespace app::chatbot::activation;
using namespace app::tool::mac;
using namespace app::tool::uuid;
using namespace app::tool::log;

// 打印分隔线
void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

// 打印激活结果
void printActivationResult(const ActivationResult& result) {
    std::cout << "\n激活检查结果：" << std::endl;
    std::cout << "  状态: " << DeviceActivation::statusToString(result.status) << std::endl;
    
    if (result.error != ActivationError::NONE) {
        std::cout << "  错误: " << DeviceActivation::errorToString(result.error) << std::endl;
        std::cout << "  错误信息: " << result.error_message << std::endl;
    }
    
    if (result.http_status_code > 0) {
        std::cout << "  HTTP状态码: " << result.http_status_code << std::endl;
    }
    
    if (!result.activation_code.empty()) {
        std::cout << "\n  ╔════════════════════════════════════════╗" << std::endl;
        std::cout << "  ║  激活码 (Activation Code)              ║" << std::endl;
        std::cout << "  ╚════════════════════════════════════════╝" << std::endl;
        std::cout << "  " << result.activation_code << std::endl;
        std::cout << "\n  请访问: https://xiaozhi.me" << std::endl;
        std::cout << "  使用上述激活码完成设备激活" << std::endl;
    } else if (result.isActivated()) {
        std::cout << "\n  ✓ 设备已激活，可以正常使用" << std::endl;
    }
    
    if (result.check_timestamp > 0) {
        uint64_t elapsed_ms = (get_nowus() - result.check_timestamp) / 1000;
        std::cout << "  检查耗时: " << elapsed_ms << " ms" << std::endl;
    }
}

// 测试1：基本激活检查
void testBasicActivationCheck() {
    printSeparator("测试1: 基本激活检查");
    
    // 1. 获取设备ID（MAC地址）
    std::cout << "\n正在获取设备ID（MAC地址）..." << std::endl;
    std::string device_id = getWirelessMacAddress();
    if (device_id.empty()) {
        std::cerr << "✗ 获取MAC地址失败！" << std::endl;
        return;
    }
    std::cout << "✓ 设备ID: " << device_id << std::endl;
    
    // 2. 获取客户端ID（UUID）
    std::cout << "\n正在获取客户端ID（UUID）..." << std::endl;
    std::string client_id = generateUUID();
    if (client_id.empty()) {
        std::cerr << "✗ 生成UUID失败！" << std::endl;
        return;
    }
    std::cout << "✓ 客户端ID: " << client_id << std::endl;
    
    // 3. 创建激活管理器
    std::cout << "\n正在创建激活管理器..." << std::endl;
    ActivationConfig config;
    config.enable_detailed_logging = true;  // 启用详细日志
    DeviceActivation activation(config);
    std::cout << "✓ 激活管理器已创建" << std::endl;
    
    // 4. 检查激活状态
    std::cout << "\n正在连接小智服务器检查激活状态..." << std::endl;
    std::cout << "API地址: " << config.api_url << std::endl;
    
    ActivationResult result = activation.checkActivation(device_id, client_id);
    
    // 5. 打印结果
    printActivationResult(result);
}

// 测试2：激活状态轮询（模拟阻塞式等待）
void testActivationPolling() {
    printSeparator("测试2: 激活状态轮询（阻塞式等待）");
    
    // 获取设备ID和客户端ID
    std::string device_id = getWirelessMacAddress();
    std::string client_id = generateUUID();
    
    if (device_id.empty() || client_id.empty()) {
        std::cerr << "✗ 获取设备信息失败！" << std::endl;
        return;
    }
    
    std::cout << "\n设备信息：" << std::endl;
    std::cout << "  设备ID: " << device_id << std::endl;
    std::cout << "  客户端ID: " << client_id << std::endl;
    
    // 创建激活管理器
    ActivationConfig config;
    config.poll_interval_sec = 5;      // 每5秒轮询一次
    config.poll_timeout_sec = 60;      // 60秒超时（测试用）
    config.enable_detailed_logging = true;
    
    DeviceActivation activation(config);
    
    // 设置回调
    activation.setStatusCallback([](ActivationStatus status, const ActivationResult& result) {
        std::cout << "\n[回调] 状态变化: " 
                  << DeviceActivation::statusToString(status) << std::endl;
        if (!result.activation_code.empty()) {
            std::cout << "[回调] 激活码: " << result.activation_code << std::endl;
        }
    });
    
    activation.setProgressCallback([](int elapsed_sec, int total_sec) {
        std::cout << "[进度] 已等待: " << elapsed_sec << "/" << total_sec << " 秒" << std::endl;
    });
    
    // 启动轮询
    std::cout << "\n启动激活轮询（阻塞式等待）..." << std::endl;
    std::cout << "轮询间隔: " << config.poll_interval_sec << " 秒" << std::endl;
    std::cout << "超时时间: " << config.poll_timeout_sec << " 秒" << std::endl;
    std::cout << "\n提示：如果设备未激活，请访问 https://xiaozhi.me 使用激活码激活" << std::endl;
    
    activation.startPolling(device_id, client_id, config.poll_timeout_sec);
    
    // 等待轮询完成
    ActivationResult result = activation.waitForPollingComplete(0);  // 0表示无限等待
    
    // 打印最终结果
    printActivationResult(result);
    
    // 打印统计信息
    std::cout << "\n统计信息：" << std::endl;
    activation.logStats();
}

// 测试3：快速检查（仅检查一次）
void testQuickCheck() {
    printSeparator("测试3: 快速检查（单次检查）");
    
    std::string device_id = getWirelessMacAddress();
    std::string client_id = generateUUID();
    
    if (device_id.empty() || client_id.empty()) {
        std::cerr << "✗ 获取设备信息失败！" << std::endl;
        return;
    }
    
    ActivationConfig config;
    DeviceActivation activation(config);
    
    std::cout << "\n执行快速检查..." << std::endl;
    
    bool is_activated = activation.isActivated(device_id, client_id);
    
    if (is_activated) {
        std::cout << "✓ 设备已激活" << std::endl;
    } else {
        std::cout << "✗ 设备未激活" << std::endl;
        std::cout << "\n使用详细检查获取激活码..." << std::endl;
        ActivationResult result = activation.checkActivation(device_id, client_id);
        printActivationResult(result);
    }
}

// 主函数
int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   小智服务器激活测试程序                ║" << std::endl;
    std::cout << "║   Xiaozhi Activation Test Suite       ║" << std::endl;
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
                testBasicActivationCheck();
                break;
            case 2:
                testActivationPolling();
                break;
            case 3:
                testQuickCheck();
                break;
            default:
                std::cerr << "无效的测试编号: " << test_num << std::endl;
                std::cerr << "用法: " << argv[0] << " [1|2|3]" << std::endl;
                std::cerr << "  1: 基本激活检查（默认）" << std::endl;
                std::cerr << "  2: 激活状态轮询（阻塞式等待）" << std::endl;
                std::cerr << "  3: 快速检查（单次检查）" << std::endl;
                return 1;
        }
        
        // 测试总结
        printSeparator("测试完成");
        std::cout << "\n  ✓ 激活测试执行完成" << std::endl;
        std::cout << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "========================================\n" << std::endl;
    
    return 0;
}
