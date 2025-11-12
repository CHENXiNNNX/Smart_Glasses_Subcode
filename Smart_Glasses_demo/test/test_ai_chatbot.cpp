#include "app/chatbot/chatbot.hpp"
#include "app/tool/log/log.hpp"

#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
 
using namespace app::tool::log;
using namespace app::chatbot;

// 全局标志，用于优雅退出
std::atomic<bool> g_running{true};

// 信号处理函数（Ctrl+C）
void signalHandler(int signal) {
    (void)signal;
    LOG_INFO("Main", "收到退出信号，正在关闭...");
    g_running = false;
}

int main() {
    // 注册信号处理（Ctrl+C）
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // 日志系统初始化
    Logger::getInstance().initialize(LogConfig());
     
    // 创建ChatbotSystem
    ChatbotSystem chatbot;
    
    // 硬件设备初始化和网络检测配网
    ChatbotError err = chatbot.open();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("Main", "ChatbotSystem初始化失败: %s", errorToString(err));
        Logger::getInstance().shutdown();
         return 1;
     }
     
    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "系统已就绪，等待唤醒词...");
    LOG_INFO("Main", "按 Ctrl+C 退出程序");
    LOG_INFO("Main", "========================================");
    
    // 查询已保存的网络信息
    // chatbot.searchSavedNetwork();
    
    // 保持程序运行，等待唤醒词检测
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 检查系统状态
        ChatbotState state = chatbot.getState();
        if (state == ChatbotState::ERROR || state == ChatbotState::CLOSED) {
            LOG_WARN("Main", "系统状态异常: %s，退出程序", stateToString(state));
                 break;
         }
    }
    
    // 关闭ChatbotSystem
    chatbot.close();
    
    // 关闭日志系统
    Logger::getInstance().shutdown();
    
    LOG_INFO("Main", "程序退出");
     return 0;
 }