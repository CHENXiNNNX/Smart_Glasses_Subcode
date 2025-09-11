#include "chatbot.h"
#include <iostream>
#include <signal.h>
#include <string>

// 全局Chatbot指针，用于信号处理
std::unique_ptr<Chatbot> g_chatbot;

// 信号处理函数
void SignalHandler(int signal) {
    std::cout << "Received signal " << signal << ", stopping chatbot..." << std::endl;
    if (g_chatbot) {
        g_chatbot->Stop();
    }
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    std::cout << "Smart Glasses Chatbot Demo" << std::endl;
    std::cout << "==========================" << std::endl;
    
    // 默认配置参数
    std::string ws_address = "127.0.0.1";
    int ws_port = 8080;
    std::string token = "default_token";
    std::string device_id = "smart_glasses_001";
    std::string aliyun_api_key = "";
    int protocol_version = 1;
    int sample_rate = 16000;
    int channels = 1;
    int frame_duration = 20; // ms
    
    // 从命令行参数获取配置（如果有）
    if (argc > 1) {
        ws_address = argv[1];
    }
    if (argc > 2) {
        ws_port = std::stoi(argv[2]);
    }
    if (argc > 3) {
        token = argv[3];
    }
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  WebSocket Address: " << ws_address << std::endl;
    std::cout << "  WebSocket Port: " << ws_port << std::endl;
    std::cout << "  Token: " << token << std::endl;
    std::cout << "  Device ID: " << device_id << std::endl;
    std::cout << "  Protocol Version: " << protocol_version << std::endl;
    std::cout << "  Sample Rate: " << sample_rate << "Hz" << std::endl;
    std::cout << "  Channels: " << channels << std::endl;
    std::cout << "  Frame Duration: " << frame_duration << "ms" << std::endl;
    
    try {
        // 创建Chatbot实例
        g_chatbot = std::make_unique<Chatbot>(
            ws_address, ws_port, token, device_id, 
            aliyun_api_key, protocol_version, 
            sample_rate, channels, frame_duration
        );
        
        std::cout << "\nChatbot initialized successfully. Press Ctrl+C to stop." << std::endl;
        
        // 启动Chatbot
        g_chatbot->Run();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\nChatbot demo finished." << std::endl;
    return 0;
}