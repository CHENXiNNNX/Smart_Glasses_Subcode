 #include <iostream>
 #include <signal.h>
 #include <thread>
 #include <chrono>
 #include "app/chatbot/chatbot.h"
 #include "app/chatbot/mcp/mcp.h"
 #include "app/tool/mcp_tool/mcp_tool.h"
 #include "app/chatbot/activation/activation.h"
 #include "app/chatbot/uuid/uuid.h"
 #include "app/tool/mac/mac.h"
 #include "app/media/audio/audio.h"
 #include "app/media/sync.h"
 #include "common/common.h"
  
 using namespace glasses::chatbot;
 using namespace glasses::chatbot::mcp;
 using namespace glasses::chatbot::activation;
 using namespace glasses::tool;
  
 // 全局变量
 static bool g_running = true;
 static AIManager* g_ai_manager = nullptr;
  
 // 信号处理
 void signal_handler(int sig) {
     (void)sig;
     std::cout << "\n[Test] Caught signal, shutting down..." << std::endl;
     g_running = false;
 }
  
 // ============================================================================
 // 空实现：保留以便以后添加自定义逻辑
 // ============================================================================
  
 // ============================================================================
 // 主函数
 // ============================================================================
  
 int main() {
     std::cout << "========================================" << std::endl;
     std::cout << "  xiaozhi AI Chatbot Test " << std::endl;
     std::cout << "========================================" << std::endl;
     
     // 注册信号处理
     signal(SIGINT, signal_handler);
     signal(SIGTERM, signal_handler);
     
     // 1. 初始化音频系统
     std::cout << "\n[Test] Step 1: Initialize audio system..." << std::endl;
     
     audio_system_t audio_system;
     sync_context_t sync_ctx;
     
     if (sync_init(&sync_ctx) != 0) {
         std::cerr << "[Test] ✗ Failed to init sync" << std::endl;
         return -1;
     }
     
     if (audio_system_init(&audio_system, &sync_ctx) != AUDIO_ERROR_NONE) {
         std::cerr << "[Test] ✗ Failed to init audio system" << std::endl;
         return -1;
     }
     std::cout << "[Test] ✓ Audio system initialized" << std::endl;
     
     // 2. 获取设备信息（用于激活检查）
     std::cout << "\n[Test] Step 2: Get device information..." << std::endl;
     
     std::string device_id = getWirelessMacAddress();
     std::string client_id = generateUUID();
     
     if (device_id.empty()) {
         std::cerr << "[Test] ⚠ Failed to get MAC address, using default" << std::endl;
         device_id = "00:00:00:00:00:00";
     }
     
     std::cout << "[Test]   Device-Id: " << device_id << std::endl;
     std::cout << "[Test]   Client-Id: " << client_id << std::endl;
     
     // 3. 创建AI管理器 
     std::cout << "\n[Test] Step 3: Create AI Manager..." << std::endl;
     
     AIConfig ai_config;
     // 手动设置device_id和client_id（用于激活检查）
     ai_config.device_id = device_id;
     ai_config.client_id = client_id;
     
     AIManager ai_manager(ai_config);
     g_ai_manager = &ai_manager;
     
     // 4. 设置回调
     std::cout << "\n[Test] Step 4: Setup callbacks..." << std::endl;
      
     ai_manager.onSTTText([](const std::string& text, bool is_final) {
         std::cout << "[Callback] STT: \"" << text << "\" (final: " 
                   << (is_final ? "✓" : "...") << ")" << std::endl;
     });
     
     ai_manager.onLLMText([](const std::string& text, bool is_final) {
         std::cout << "[Callback] LLM: \"" << text << "\"" << std::endl;
     });
     
     ai_manager.onTTSAudio([](const uint8_t* data, size_t size) {
         // std::cout << "[Callback] TTS Audio: " << size << " bytes" << std::endl;
     });
     
     ai_manager.onStateChanged([](AIManagerState state) {
         std::cout << "[Callback] Manager State changed to: " << static_cast<int>(state) << std::endl;
     });
     
     ai_manager.onError([](const std::string& error) {
         std::cerr << "[Callback] Error: " << error << std::endl;
     });
     
     std::cout << "[Test] ✓ Callbacks set" << std::endl;
     
     // 5. 检查设备激活状态
     std::cout << "\n[Test] Step 5: Check device activation..." << std::endl;
     
     std::string activation_code;
     int activation_status = DeviceActivation::checkActivation(
         ai_config.device_id, 
         ai_config.client_id, 
         activation_code
     );
     
     if (activation_status == 1) {
         // 设备未激活，等待激活
         std::cout << "[Test] ⚠ Device not activated, waiting for activation..." << std::endl;
         if (!DeviceActivation::waitForActivation(ai_config.device_id, ai_config.client_id, 300)) {
             std::cerr << "[Test] ✗ Activation timeout or failed" << std::endl;
             std::cerr << "[Test] ℹ You can still try to continue, but AI may not work properly" << std::endl;
             // 不退出，继续尝试
         }
     } else if (activation_status == -1) {
         std::cerr << "[Test] ⚠ Activation check failed, but will try to continue..." << std::endl;
     } else {
         std::cout << "[Test] ✓ Device is activated" << std::endl;
     }
     
     // 6. 初始化AI管理器
     std::cout << "\n[Test] Step 6: Initialize AI Manager..." << std::endl;
     
     if (!ai_manager.initialize(&audio_system)) {
         std::cerr << "[Test] ✗ Failed to initialize AI manager" << std::endl;
         audio_system_deinit(&audio_system);
         return -1;
     }
     
     // 7. 注册MCP工具
     std::cout << "\n[Test] Step 7: Register MCP tools..." << std::endl;
     McpServer* mcp_server = ai_manager.getMCPServer();
     if (mcp_server) {
         McpToolManager::register_all_tools(*mcp_server);
         std::cout << "[Test] ✓ MCP tools registered (total: " << mcp_server->tool_count() << ")" << std::endl;
     } else {
         std::cerr << "[Test] ⚠ MCP server not available" << std::endl;
     }
      
     // 8. 启动AI服务
     std::cout << "\n[Test] Step 8: Start AI service..." << std::endl;
      
     if (!ai_manager.start()) {
         std::cerr << "[Test] ✗ Failed to start AI service" << std::endl;
         ai_manager.shutdown();
         audio_system_deinit(&audio_system);
         return -1;
     }
      
     std::cout << "\n========================================" << std::endl;
     std::cout << "  AI Service Started Successfully!" << std::endl;
     std::cout << "========================================" << std::endl;
     
     // 9. 设置音频模式并启动录音
     std::cout << "\n[Test] Step 9: Start AI audio mode for wakeword detection..." << std::endl;
     audio_system.current_mode = AUDIO_MODE_AI;  // 设置为AI模式
     std::cout << "[Test]   Audio mode set to: AI" << std::endl;
     
     if (start_recording(&audio_system) != AUDIO_ERROR_NONE) {
         std::cerr << "[Test] ✗ Failed to start recording" << std::endl;
         ai_manager.shutdown();
         audio_system_deinit(&audio_system);
         return -1;
     }
     std::cout << "[Test] ✓ AI audio mode started (recording for wakeword detection)" << std::endl;
     
     std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
     std::cout << "║  🎙️  唤醒词检测已启用！                  ║" << std::endl;
     std::cout << "╚════════════════════════════════════════╝" << std::endl;
     std::cout << "\n🔊 请说出唤醒词：echo" << std::endl;
     std::cout << "   系统处于待机状态(IDLE)" << std::endl;
     std::cout << "\n💡 工作流程:" << std::endl;
     std::cout << "   1. 说\"echo\"唤醒 → 进入LISTENING状态" << std::endl;
     std::cout << "   2. 说出你的问题 → AI开始思考(THINKING)" << std::endl;
     std::cout << "   3. AI回复 → 进入SPEAKING状态" << std::endl;
     std::cout << "   4. AI说完 → 回到LISTENING，继续对话" << std::endl;
     std::cout << "\n⌨️  操作提示:" << std::endl;
     std::cout << "   - 按 'i' 显示状态信息" << std::endl;
     std::cout << "   - 按 'q' 退出程序" << std::endl;
     std::cout << "\n⏳ 等待唤醒词..." << std::endl;
     
     // 10. 主循环
     while (g_running) {
         // 检查键盘输入
         char ch = getchar();
         
         if (ch == 'i' || ch == 'I') {
             std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
             std::cout << "║    状态信息                             ║" << std::endl;
             std::cout << "╚════════════════════════════════════════╝" << std::endl;
             std::cout << "  Manager State: " << static_cast<int>(ai_manager.getState()) << std::endl;
             std::cout << "  Connected: " << (ai_manager.isConnected() ? "✓" : "✗") << std::endl;
             std::cout << "  Active: " << (ai_manager.isActive() ? "✓" : "✗") << std::endl;
             std::cout << "  Session ID: " << ai_manager.getSessionId() << std::endl;
             std::cout << "  Audio Streaming: " << (audio_system.is_ai_streaming ? "✓" : "✗") << std::endl;
             std::cout << "  Audio Mode: AI" << std::endl;
             std::cout << "========================================" << std::endl;
         }
         else if (ch == 'q' || ch == 'Q') {
             std::cout << "\n[Test] Quitting..." << std::endl;
             g_running = false;
         }
         
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
     }
     
     // 11. 清理
     std::cout << "\n[Test] Cleaning up..." << std::endl;
      
     ai_manager.stop();
     ai_manager.shutdown();
     audio_system_deinit(&audio_system);
     sync_deinit(&sync_ctx);
     
     std::cout << "[Test] ✓ Test completed successfully!" << std::endl;
     return 0;
 }
 
 