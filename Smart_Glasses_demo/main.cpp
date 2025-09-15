#include "app/chatbot/chatbot.h"
#include "utils/user_log.h"
#include <iostream>
#include <signal.h>
#include <memory>

// 全局智能指针，用于信号处理
std::unique_ptr<Chatbot> g_chatbot = nullptr;

/**
 * @brief 信号处理函数
 * 
 * 处理 Ctrl+C 等中断信号，优雅关闭程序
 */
void signalHandler(int signal) {
    USER_LOG_WARN("Received signal %d, shutting down...", signal);
    
    if (g_chatbot) {
        g_chatbot->Stop();
    }
    
    // 等待一段时间让程序优雅关闭
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    USER_LOG_WARN("Program terminated by signal %d", signal);
    exit(0);
}

/**
 * @brief 主函数
 * 
 * 直接启动智能眼镜聊天机器人，无需命令行参数
 */
int main() {
    // 设置日志级别
    USER_LOG_INFO("=== Smart Glasses Chatbot Starting ===");
    
    // 注册信号处理器
    signal(SIGINT, signalHandler);   // Ctrl+C
    signal(SIGTERM, signalHandler);  // 终止信号
    
    try {
        // ==================== 配置参数 ====================
        
        // WebSocket服务器配置
        const std::string server_address = "172.32.0.101";  // 修改为您的服务器地址
        const int server_port = 8000;                        // 修改为您的服务器端口
        const std::string auth_token = "123456";    // 修改为您的认证令牌
        const std::string device_id = "00:11:22:33:44:55";   // 设备ID
        const std::string aliyun_api_key = "sk-d8e4dc07bc01425fa83e851bc1d66b7f"; // 阿里云API密钥
        const int protocol_version = 2;                      // 协议版本
        
        // 音频配置
        const int sample_rate = 16000;    // 采样率 16kHz
        const int channels = 1;           // 单声道
        const int frame_duration = 20;    // 帧时长 20ms
        
        USER_LOG_INFO("Configuration:");
        USER_LOG_INFO("  Server: %s:%d", server_address.c_str(), server_port);
        USER_LOG_INFO("  Device ID: %s", device_id.c_str());
        USER_LOG_INFO("  Sample Rate: %d Hz", sample_rate);
        USER_LOG_INFO("  Channels: %d", channels);
        USER_LOG_INFO("  Frame Duration: %d ms", frame_duration);
        
        // ==================== 创建聊天机器人实例 ====================
        
        USER_LOG_INFO("Creating Chatbot instance...");
        
        g_chatbot = std::make_unique<Chatbot>(
            server_address,      // WebSocket服务器地址
            server_port,         // WebSocket服务器端口
            auth_token,          // 认证令牌
            device_id,           // 设备ID
            aliyun_api_key,      // 阿里云API密钥
            protocol_version,    // 协议版本
            sample_rate,         // 音频采样率
            channels,            // 音频声道数
            frame_duration       // 音频帧时长
        );
        
        USER_LOG_INFO("Chatbot instance created successfully");
        
        // ==================== 启动聊天机器人 ====================
        
        USER_LOG_INFO("Starting Chatbot...");
        USER_LOG_INFO("Press Ctrl+C to stop the program");
        
        // 运行聊天机器人（阻塞调用）
        g_chatbot->Run();
        
        USER_LOG_INFO("Chatbot stopped normally");
        
    } catch (const std::exception& e) {
        USER_LOG_ERROR("Exception occurred: %s", e.what());
        return 1;
    } catch (...) {
        USER_LOG_ERROR("Unknown exception occurred");
        return 1;
    }
    
    // ==================== 清理资源 ====================
    
    USER_LOG_INFO("Cleaning up resources...");
    g_chatbot.reset();
    
    USER_LOG_INFO("=== Smart Glasses Chatbot Ended ===");
    return 0;
}
