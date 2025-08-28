
#include <iostream>
#include <stdexcept>

#include "Application/Application.h"

// ./build/AIChatClient 192.168.211.1 8000 123456
void print_usage(const char* progname) {
    std::cout << "Usage: " << progname << "<server_address> <port> <token>" << std::endl;
}

int main(int argc, char* argv[]) {
    // 检查命令行参数的数量是否正确
    if (argc < 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    // 定义server地址、端口和鉴权头
    try {
        // 解析命令行参数
        std::string address = argv[1];
        int port = std::stoi(argv[2]);
        std::string token = argv[3];
        // 默认值
        std::string deviceId = "00:11:22:33:44:55";
        std::string aliyun_api_key = "sk-d8e4dc07bc01425fa83e851bc1d66b7f";
        int protocolVersion = 2;
        int sample_rate = 16000;
        int channels = 1;
        int frame_duration = 40;
        
        // 创建 Application 实例
        Application app(address, port, token, deviceId, aliyun_api_key, protocolVersion, sample_rate, channels, frame_duration);

        // 运行应用程序
        app.Run();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}