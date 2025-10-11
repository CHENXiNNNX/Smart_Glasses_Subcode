/**
 * @file log_example.cc
 * @brief 日志系统使用示例
 */

 #include "log.h"
 #include <thread>
 #include <chrono>
 
 using namespace glasses::tool::logger;
 
 // ============================================================================
 // 模拟不同模块的日志输出
 // ============================================================================
 
 void websocketTask() {
     LOG_INFO("WebSocket", "Connecting to server...");
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     LOG_INFO("WebSocket", "Connected to wss://api.tenclass.net");
     LOG_DEBUG("WebSocket", "WebSocket握手完成，协议版本: %d", 13);
     
     for (int i = 0; i < 5; i++) {
         LOG_DEBUG("WebSocket", "Sending message #%d", i);
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
     }
     
     LOG_WARN("WebSocket", "Connection timeout, reconnecting in %d ms", 5000);
 }
 
 void aiManagerTask() {
     LOG_INFO("AIManager", "Initializing AI Manager...");
     std::this_thread::sleep_for(std::chrono::milliseconds(50));
     
     LOG_INFO("AIManager", "Device-Id: %s", "00:0c:29:bd:43:05");
     LOG_INFO("AIManager", "Client-Id: %s", "d560294c-01d9-47d0-b538-085f38744b05");
     
     LOG_DEBUG("AIManager", "Creating protocol handler...");
     LOG_DEBUG("AIManager", "Creating state machine...");
     LOG_DEBUG("AIManager", "Creating MCP server...");
     
     LOG_INFO("AIManager", "AI Manager initialized successfully!");
     
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     LOG_WARN("AIManager", "Audio buffer is %d%% full", 85);
 }
 
 void wakewordTask() {
     LOG_INFO("Wakeword", "Initializing detector...");
     LOG_DEBUG("Wakeword", "Loading model: echo.pmdl");
     LOG_DEBUG("Wakeword", "Sample rate: %d Hz", 16000);
     
     std::this_thread::sleep_for(std::chrono::milliseconds(200));
     
     LOG_INFO("Wakeword", "Wakeword detector initialized!");
     LOG_INFO("Wakeword", "Listening for hotword 'echo'...");
     
     std::this_thread::sleep_for(std::chrono::milliseconds(500));
     
     LOG_INFO("Wakeword", "🎙️  Hotword detected!");
 }
 
 void audioTask() {
     LOG_INFO("Audio", "Initializing audio system...");
     LOG_DEBUG("Audio", "Sample rate: %d Hz, Channels: %d", 48000, 1);
     
     LOG_INFO("Audio", "Audio system initialized");
     LOG_DEBUG("Audio", "Starting recording stream...");
     
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     LOG_ERROR("Audio", "Failed to open audio device: %s", "Device busy");
     LOG_INFO("Audio", "Retrying in %d ms...", 1000);
     
     std::this_thread::sleep_for(std::chrono::milliseconds(1000));
     
     LOG_INFO("Audio", "Audio device opened successfully");
 }
 
 // ============================================================================
 // 测试不同的日志宏
 // ============================================================================
 
 void testUserLogMacro() {
     // USER_LOG 宏测试
     USER_LOG("DEBUG", "TestModule", "This is a DEBUG message");
     USER_LOG("INFO", "TestModule", "This is an INFO message");
     USER_LOG("WARN", "TestModule", "This is a WARN message");
     USER_LOG("ERROR", "TestModule", "This is an ERROR message");
     
     // 带参数的日志
     USER_LOG("INFO", "TestModule", "Testing with integer: %d", 42);
     USER_LOG("INFO", "TestModule", "Testing with float: %.2f", 3.14159);
     USER_LOG("INFO", "TestModule", "Testing with string: %s", "Hello World");
     USER_LOG("INFO", "TestModule", "Multiple params: %d, %.2f, %s", 100, 2.71828, "test");
 }
 
 // ============================================================================
 // 主函数
 // ============================================================================
 
 int main() {
   // 1. 配置日志系统
   LogConfig config;
   config.enable_console = true;           // 启用控制台输出
   config.enable_file = true;              // 启用文件输出
   config.enable_color = true;             // 启用彩色输出
   config.enable_timestamp = true;         // 时间戳
   config.enable_thread_id = false;         // 线程ID
   config.log_file_path = "./log/smart_glasses.log";  // 日志文件路径
   config.max_file_size = 5 * 1024 * 1024; // 5MB
   config.buffer_size = 8192;              // 8K 消息缓冲区
   config.min_level = LogLevel::DEBUG;     // 最小日志级别
     
     // 2. 初始化日志系统
     Logger& logger = Logger::getInstance();
     if (!logger.initialize(config)) {
         fprintf(stderr, "Failed to initialize logger\n");
         return -1;
     }
     
     printf("\n========================================\n");
     printf("  Log System Example\n");
     printf("========================================\n\n");
     
     // 3. 测试 USER_LOG 宏
     printf(">> Testing USER_LOG macro...\n\n");
     testUserLogMacro();
     
     std::this_thread::sleep_for(std::chrono::seconds(1));
     
     // 4. 模拟多线程日志输出
     printf("\n>> Testing multi-threaded logging...\n\n");
     
     std::thread t1(websocketTask);
     std::thread t2(aiManagerTask);
     std::thread t3(wakewordTask);
     std::thread t4(audioTask);
     
     t1.join();
     t2.join();
     t3.join();
     t4.join();
     
     std::this_thread::sleep_for(std::chrono::seconds(1));
     
     // 5. 测试大量日志（文件轮转测试）
     printf("\n>> Testing file rotation (this may take a while)...\n\n");
     LOG_INFO("Test", "Generating large amount of logs for rotation test...");
     
     for (int i = 0; i < 100; i++) {
         LOG_DEBUG("StressTest", "Message #%d: Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                   "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.", i);
         
         if (i % 20 == 0) {
             LOG_INFO("StressTest", "Progress: %d%%", i);
         }
     }
     
     LOG_INFO("Test", "Stress test completed!");
     
     // 6. 刷新并关闭
     logger.flush();
     std::this_thread::sleep_for(std::chrono::milliseconds(500));
     
     printf("\n========================================\n");
     printf("  Example Completed!\n");
     printf("  Check 'smart_glasses.log' for output\n");
     printf("========================================\n\n");
     
     logger.shutdown();
     
     return 0;
 }
 
 