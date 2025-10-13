/**
 * @file test_ai_chatbotv2.cpp
 * @brief xiaozhi AI ChatbotSystemV2 测试程序
 * 
 * 测试内容：
 * - AudioSystemV2音频系统集成
 * - ChatbotSystemV2完整生命周期
 * - 设备激活流程
 * - 唤醒词检测
 * - AI对话交互
 * - MCP工具调用
 * - 状态监控和统计
 */

 #include <iostream>
 #include <signal.h>
 #include <thread>
 #include <chrono>
 #include <memory>
 #include <atomic>
 
 // V2模块
 #include "app/chatbot/chatbotv2.h"
 #include "app/media/audio/audiov2.h"
 #include "app/tool/log/log.h"
 #include "common/common.h"
 
 // 命名空间
 using namespace glasses::chatbot;
 using namespace glasses::media::audio;
 using namespace glasses::tool::logger;
 
 // ============================================================================
 // 全局变量（信号处理需要）
 // ============================================================================
 
 static std::atomic<bool> g_running{true};
 static std::shared_ptr<ChatbotSystemV2> g_chatbot_system;
 static std::shared_ptr<AudioSystemV2> g_audio_system;
 
 // ============================================================================
 // 信号处理
 // ============================================================================
 
 void signal_handler(int sig) {
     (void)sig;
     std::cout << "\n[Test] ⚠️  Caught signal, shutting down gracefully..." << std::endl;
     g_running.store(false, std::memory_order_release);
 }
 
 // ============================================================================
 // 辅助函数
 // ============================================================================
 
 /**
  * @brief 打印欢迎信息
  */
 void printWelcome() {
     std::cout << "\n";
     std::cout << "╔════════════════════════════════════════════════════════╗\n";
     std::cout << "║                                                        ║\n";
     std::cout << "║         xiaozhi AI ChatbotSystemV2 Test                ║\n";
     std::cout << "║                                                        ║\n";
     std::cout << "║  🚀 Modern C++ Implementation                          ║\n";
     std::cout << "║  ✨ RAII + Smart Pointers + Thread Safety              ║\n";
     std::cout << "║                                                        ║\n";
     std::cout << "╚════════════════════════════════════════════════════════╝\n";
     std::cout << std::endl;
 }
 
 /**
  * @brief 打印操作提示
  */
 void printInstructions() {
     std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
     std::cout << "║  🎙️  唤醒词检测已启用！                                 ║\n";
     std::cout << "╚════════════════════════════════════════════════════════╝\n";
     std::cout << "\n🔊 请说出唤醒词：echo\n";
     std::cout << "   系统处于待机状态(READY)\n";
     std::cout << "\n💡 工作流程:\n";
     std::cout << "   1. 说\"echo\"唤醒 → 进入LISTENING状态\n";
     std::cout << "   2. 说出你的问题 → AI开始思考(THINKING)\n";
     std::cout << "   3. AI回复 → 进入SPEAKING状态\n";
     std::cout << "   4. AI说完 → 自动回到LISTENING，继续对话\n";
     std::cout << "\n⌨️  操作提示:\n";
     std::cout << "   - 按 'i' 或 'I' 显示系统状态\n";
     std::cout << "   - 按 's' 或 'S' 显示统计信息\n";
     std::cout << "   - 按 'w' 或 'W' 手动触发唤醒词\n";
     std::cout << "   - 按 'q' 或 'Q' 退出程序\n";
     std::cout << "\n⏳ 等待唤醒词...\n" << std::endl;
 }
 
 /**
  * @brief 打印系统状态
  */
 void printSystemStatus(const ChatbotSystemV2& chatbot, const AudioSystemV2& audio) {
     std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
     std::cout << "║                    系统状态                             ║\n";
     std::cout << "╚════════════════════════════════════════════════════════╝\n";
     
     // Chatbot状态
     std::cout << "📡 Chatbot 状态:\n";
     std::cout << "   State:         " << static_cast<int>(chatbot.getState()) << "\n";
     std::cout << "   Ready:         " << (chatbot.isReady() ? "✓" : "✗") << "\n";
     std::cout << "   Activated:     " << (chatbot.isActivated() ? "✓" : "✗") << "\n";
     std::cout << "   Connected:     " << (chatbot.isConnected() ? "✓" : "✗") << "\n";
     std::cout << "   Session ID:    " << chatbot.getSessionId() << "\n";
     std::cout << "   Device ID:     " << chatbot.getDeviceId() << "\n";
     std::cout << "   MCP Tools:     " << chatbot.getMCPToolCount() << "\n";
     
     // 音频状态
     std::cout << "\n🔊 Audio 状态:\n";
     std::cout << "   AI Stream:     " << (audio.isAIStreamActive() ? "✓ Active" : "✗ Inactive") << "\n";
     std::cout << "   Playing:       " << (audio.isPlaying() ? "✓ Playing" : "✗ Stopped") << "\n";
     std::cout << "   Recording:     " << (audio.isRecording() ? "✓ Recording" : "✗ Stopped") << "\n";
     
     std::cout << "════════════════════════════════════════════════════════\n" << std::endl;
 }
 
 /**
  * @brief 打印统计信息
  */
 void printStatistics(const ChatbotSystemV2& chatbot) {
     std::cout << "\n╔════════════════════════════════════════════════════════╗\n";
     std::cout << "║                    统计信息                             ║\n";
     std::cout << "╚════════════════════════════════════════════════════════╝\n";
     
     ChatbotSystemV2::Stats stats;
     chatbot.getStats(stats);
     
     std::cout << "📊 消息统计:\n";
     std::cout << "   Messages Sent:       " << stats.messages_sent.load() << "\n";
     std::cout << "   Messages Received:   " << stats.messages_received.load() << "\n";
     std::cout << "   STT Received:        " << stats.stt_received.load() << "\n";
     std::cout << "   LLM Received:        " << stats.llm_received.load() << "\n";
     std::cout << "   TTS Received:        " << stats.tts_received.load() << "\n";
     
     std::cout << "\n🎙️  交互统计:\n";
     std::cout << "   Wakeword Detected:   " << stats.wakeword_detected.load() << "\n";
     std::cout << "   Conversations:       " << stats.conversations.load() << "\n";
     std::cout << "   MCP Calls:           " << stats.mcp_calls.load() << "\n";
     
     std::cout << "\n⚠️  错误统计:\n";
     std::cout << "   Errors:              " << stats.errors.load() << "\n";
     
     std::cout << "\n⏱️  运行时间:\n";
     double uptime_hours = stats.total_uptime_us.load() / (1000000.0 * 3600.0);
     std::cout << "   Total Uptime:        " << uptime_hours << " hours\n";
     
     std::cout << "════════════════════════════════════════════════════════\n" << std::endl;
 }
 
 // ============================================================================
 // 主函数
 // ============================================================================
 
 int main() {
     printWelcome();
     
     // 注册信号处理
     signal(SIGINT, signal_handler);
     signal(SIGTERM, signal_handler);
     
     try {
         // ====================================================================
         // 步骤 1: 初始化日志系统
         // ====================================================================
         std::cout << "[Test] Step 1: Initialize log system..." << std::endl;
         
        LogConfig log_config;
        log_config.enable_console = true;
        log_config.enable_file = true;
        log_config.log_file_path = "/root/bin/log/xiaozhi_chatbotv2_test.log";
        log_config.buffer_size = 8192;  // 环形缓冲区大小
        log_config.min_level = LogLevel::DEBUG;
        
        Logger::getInstance().initialize(log_config);
         
         std::cout << "[Test] ✓ Log system initialized" << std::endl;
         LOG_INFO("Test", "========================================");
         LOG_INFO("Test", "ChatbotSystemV2 Test Started");
         LOG_INFO("Test", "========================================");
         
         // ====================================================================
         // 步骤 2: 创建并初始化AudioSystemV2
         // ====================================================================
         std::cout << "\n[Test] Step 2: Initialize AudioSystemV2..." << std::endl;
         LOG_INFO("Test", "Initializing AudioSystemV2...");
         
        AudioConfig audio_config;
        audio_config.sample_rate = 48000;
        audio_config.channels = 1;
        audio_config.frame_duration_ms = 20;
        audio_config.enable_denoise = true;
        audio_config.enable_agc = true;
        audio_config.enable_vad = false;
        audio_config.enable_dereverb = false;
        
        g_audio_system = std::make_shared<AudioSystemV2>(audio_config);
         if (!g_audio_system) {
             std::cerr << "[Test] ✗ Failed to create AudioSystemV2" << std::endl;
             LOG_ERROR("Test", "Failed to create AudioSystemV2");
             return -1;
         }
         
         AudioError audio_err = g_audio_system->initialize();
         if (audio_err != AudioError::NONE) {
             std::cerr << "[Test] ✗ Failed to initialize AudioSystemV2" << std::endl;
             LOG_ERROR("Test", "Failed to initialize AudioSystemV2");
             return -1;
         }
         
         std::cout << "[Test] ✓ AudioSystemV2 initialized" << std::endl;
         LOG_INFO("Test", "AudioSystemV2 initialized successfully");
         
         // ====================================================================
         // 步骤 3: 配置ChatbotSystemV2
         // ====================================================================
         std::cout << "\n[Test] Step 3: Configure ChatbotSystemV2..." << std::endl;
         LOG_INFO("Test", "Configuring ChatbotSystemV2...");
         
         ChatbotConfig chatbot_config;
         chatbot_config.device_id = "";  // 自动获取MAC地址
         chatbot_config.client_id = "";  // 自动生成UUID
         chatbot_config.config_file_path = "/root/bin/system_para.conf";
         
         // 激活配置
         chatbot_config.auto_activate = true;
         chatbot_config.activation_api_url = "https://api.xiaozhi.me/device/activation";
         chatbot_config.activation_timeout_sec = 300;
         
          // 唤醒词配置
          chatbot_config.enable_wakeword = true;
          chatbot_config.wakeword_resource_file = "/root/bin/third_party/snowboy/resources/common.res";
          chatbot_config.wakeword_model_file = "/root/bin/third_party/snowboy/resources/models/echo.pmdl";
         chatbot_config.wakeword_sensitivity = 0.5f;
         chatbot_config.wakeword_audio_gain = 1.0f;
         
         // MCP工具配置
         chatbot_config.enable_mcp_tools = true;
         
         // WebSocket配置
         chatbot_config.auto_connect = true;
         
         std::cout << "[Test] ✓ ChatbotSystemV2 configured" << std::endl;
         
         // ====================================================================
         // 步骤 4: 创建ChatbotSystemV2
         // ====================================================================
         std::cout << "\n[Test] Step 4: Create ChatbotSystemV2..." << std::endl;
         LOG_INFO("Test", "Creating ChatbotSystemV2...");
         
         g_chatbot_system = std::make_shared<ChatbotSystemV2>(chatbot_config);
         
         std::cout << "[Test] ✓ ChatbotSystemV2 created" << std::endl;
         
         // ====================================================================
         // 步骤 5: 设置回调
         // ====================================================================
         std::cout << "\n[Test] Step 5: Setup callbacks..." << std::endl;
         LOG_INFO("Test", "Setting up callbacks...");
         
         // STT回调
         g_chatbot_system->setSTTCallback([](const std::string& text, bool is_final) {
             std::cout << "[Callback] 🎤 STT: \"" << text << "\" " 
                       << (is_final ? "(✓ final)" : "(... partial)") << std::endl;
             LOG_INFO("Test", "STT: %s (final: %d)", text.c_str(), is_final);
         });
         
         // LLM回调
         g_chatbot_system->setLLMCallback([](const std::string& text, bool is_final) {
             std::cout << "[Callback] 🤖 LLM: \"" << text << "\"" << std::endl;
             LOG_INFO("Test", "LLM: %s (final: %d)", text.c_str(), is_final);
         });
         
         // TTS回调
         g_chatbot_system->setTTSCallback([](const uint8_t* data, size_t size) {
             (void)data;
             (void)size;
             // std::cout << "[Callback] 🔊 TTS Audio: " << size << " bytes" << std::endl;
             // 不打印，太频繁
         });
         
         // 状态变化回调
         g_chatbot_system->setStateCallback([](ChatbotState old_state, ChatbotState new_state) {
             std::cout << "[Callback] 📡 State: " << static_cast<int>(old_state) 
                       << " → " << static_cast<int>(new_state) << std::endl;
             LOG_INFO("Test", "State changed: %d → %d", static_cast<int>(old_state), static_cast<int>(new_state));
         });
         
         // 错误回调
         g_chatbot_system->setErrorCallback([](ChatbotError error, const std::string& message) {
             std::cerr << "[Callback] ⚠️  Error: " << message 
                       << " (code: " << static_cast<int>(error) << ")" << std::endl;
             LOG_ERROR("Test", "Error: %s (code: %d)", message.c_str(), static_cast<int>(error));
         });
         
         // 唤醒词回调
         g_chatbot_system->setWakewordCallback([](int hotword_index) {
             std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
             std::cout << "║  🎙️  唤醒词检测到！Hotword " << hotword_index << "         ║" << std::endl;
             std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
             LOG_INFO("Test", "Wakeword detected: %d", hotword_index);
         });
         
         // 激活回调
         g_chatbot_system->setActivationCallback([](bool is_activated, const std::string& activation_code) {
             if (is_activated) {
                 std::cout << "[Callback] ✓ Device activated successfully!" << std::endl;
                 LOG_INFO("Test", "Device activated");
             } else {
                 std::cout << "[Callback] ⚠️  Device not activated" << std::endl;
                 std::cout << "[Callback] 📱 Activation Code: " << activation_code << std::endl;
                 std::cout << "[Callback] 🌐 Please visit: https://xiaozhi.me" << std::endl;
                 LOG_WARN("Test", "Device not activated, code: %s", activation_code.c_str());
             }
         });
         
         std::cout << "[Test] ✓ Callbacks set" << std::endl;
         LOG_INFO("Test", "Callbacks configured");
         
         // ====================================================================
         // 步骤 6: 初始化ChatbotSystemV2
         // ====================================================================
         std::cout << "\n[Test] Step 6: Initialize ChatbotSystemV2..." << std::endl;
         LOG_INFO("Test", "Initializing ChatbotSystemV2...");
         
         ChatbotError chatbot_err = g_chatbot_system->initialize(g_audio_system);
         if (chatbot_err != ChatbotError::NONE) {
             std::cerr << "[Test] ✗ Failed to initialize ChatbotSystemV2" << std::endl;
             LOG_ERROR("Test", "Failed to initialize ChatbotSystemV2");
             return -1;
         }
         
         std::cout << "[Test] ✓ ChatbotSystemV2 initialized" << std::endl;
         LOG_INFO("Test", "ChatbotSystemV2 initialized successfully");
         
         // ====================================================================
         // 步骤 7: 启动ChatbotSystemV2
         // ====================================================================
         std::cout << "\n[Test] Step 7: Start ChatbotSystemV2..." << std::endl;
         LOG_INFO("Test", "Starting ChatbotSystemV2...");
         
         chatbot_err = g_chatbot_system->start();
         if (chatbot_err != ChatbotError::NONE) {
             std::cerr << "[Test] ✗ Failed to start ChatbotSystemV2" << std::endl;
             LOG_ERROR("Test", "Failed to start ChatbotSystemV2");
             g_chatbot_system->shutdown();
             return -1;
         }
         
         std::cout << "[Test] ✓ ChatbotSystemV2 started" << std::endl;
         LOG_INFO("Test", "ChatbotSystemV2 started successfully");
         
         // ====================================================================
         // 步骤 8: 启动音频录音（用于唤醒词检测和AI对话）
         // ====================================================================
        std::cout << "\n[Test] Step 8: Start audio recording..." << std::endl;
        LOG_INFO("Test", "Starting audio recording...");
        
        audio_err = g_audio_system->startRecord();
         if (audio_err != AudioError::NONE) {
             std::cerr << "[Test] ✗ Failed to start recording" << std::endl;
             LOG_ERROR("Test", "Failed to start recording");
             g_chatbot_system->stop();
             g_chatbot_system->shutdown();
             return -1;
         }
         
         std::cout << "[Test] ✓ Audio recording started" << std::endl;
         LOG_INFO("Test", "Audio recording started");
         
         // ====================================================================
         // 系统就绪！
         // ====================================================================
         std::cout << "\n";
         std::cout << "╔════════════════════════════════════════════════════════╗\n";
         std::cout << "║                                                        ║\n";
         std::cout << "║       🎉 ChatbotSystemV2 Started Successfully! 🎉      ║\n";
         std::cout << "║                                                        ║\n";
         std::cout << "╚════════════════════════════════════════════════════════╝\n";
         
         LOG_INFO("Test", "========================================");
         LOG_INFO("Test", "System Ready!");
         LOG_INFO("Test", "========================================");
         
         printInstructions();
         
         // ====================================================================
         // 步骤 9: 主循环
         // ====================================================================
         while (g_running.load(std::memory_order_acquire)) {
             // 非阻塞的键盘输入检查
             // 注意：这里使用简单的getchar()，实际生产环境可能需要更复杂的输入处理
             
             // 使用select来实现非阻塞输入（Linux）
             fd_set readfds;
             struct timeval tv;
             FD_ZERO(&readfds);
             FD_SET(STDIN_FILENO, &readfds);
             tv.tv_sec = 0;
             tv.tv_usec = 100000;  // 100ms
             
             int ret = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
             if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                 char ch = getchar();
                 
                 if (ch == 'i' || ch == 'I') {
                     // 显示系统状态
                     printSystemStatus(*g_chatbot_system, *g_audio_system);
                     
                 } else if (ch == 's' || ch == 'S') {
                     // 显示统计信息
                     printStatistics(*g_chatbot_system);
                     
                 } else if (ch == 'w' || ch == 'W') {
                     // 手动触发唤醒词
                     std::cout << "\n[Test] Manual wakeword trigger..." << std::endl;
                     LOG_INFO("Test", "Manual wakeword trigger");
                     g_chatbot_system->triggerWakeword();
                     
                 } else if (ch == 'q' || ch == 'Q') {
                     // 退出
                     std::cout << "\n[Test] User requested quit..." << std::endl;
                     LOG_INFO("Test", "User requested quit");
                     g_running.store(false, std::memory_order_release);
                 }
             }
             
             std::this_thread::sleep_for(std::chrono::milliseconds(10));
         }
         
         // ====================================================================
         // 步骤 10: 清理
         // ====================================================================
         std::cout << "\n[Test] Cleaning up..." << std::endl;
         LOG_INFO("Test", "Cleaning up...");
         
        // 停止录音
        if (g_audio_system->isRecording()) {
            g_audio_system->stopRecord();
            std::cout << "[Test] ✓ Recording stopped" << std::endl;
        }
         
         // 停止Chatbot
         g_chatbot_system->stop();
         std::cout << "[Test] ✓ ChatbotSystemV2 stopped" << std::endl;
         
         // 关闭Chatbot
         g_chatbot_system->shutdown();
         std::cout << "[Test] ✓ ChatbotSystemV2 shutdown" << std::endl;
         
         // 打印最终统计
         std::cout << "\n[Test] Final Statistics:" << std::endl;
         LOG_INFO("Test", "========================================");
         LOG_INFO("Test", "Final Statistics");
         LOG_INFO("Test", "========================================");
         g_chatbot_system->logAllStats();
         
         // 重置智能指针（自动调用析构函数）
         g_chatbot_system.reset();
         std::cout << "[Test] ✓ ChatbotSystemV2 destroyed" << std::endl;
         
         g_audio_system.reset();
         std::cout << "[Test] ✓ AudioSystemV2 destroyed" << std::endl;
         
        // 停止日志系统
        Logger::getInstance().shutdown();
        std::cout << "[Test] ✓ Log system stopped" << std::endl;
         
         std::cout << "\n";
         std::cout << "╔════════════════════════════════════════════════════════╗\n";
         std::cout << "║                                                        ║\n";
         std::cout << "║              ✅ Test Completed Successfully!            ║\n";
         std::cout << "║                                                        ║\n";
         std::cout << "╚════════════════════════════════════════════════════════╝\n";
         std::cout << std::endl;
         
         return 0;
         
     } catch (const std::exception& e) {
        std::cerr << "\n[Test] ❌ Fatal exception: " << e.what() << std::endl;
        LOG_ERROR("Test", "Fatal exception: %s", e.what());
         
         // 尝试清理
         if (g_chatbot_system) {
             g_chatbot_system->shutdown();
             g_chatbot_system.reset();
         }
        if (g_audio_system) {
            g_audio_system.reset();
        }
        Logger::getInstance().shutdown();
         
         return -1;
     }
 }
 
 