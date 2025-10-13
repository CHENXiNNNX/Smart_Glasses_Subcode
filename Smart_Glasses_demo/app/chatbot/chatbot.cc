// #include "chatbot.h"
// #include "protocol_handle/handle.h"
// #include "statemachine/machine.h"
// #include "mcp/mcp.h"
// #include "uuid/uuid.h"
// #include "wakeword/wakeword.h"
// #include "../protocol/websocket/websocket.h"
// #include "../tool/mac/mac.h"

// #include <iostream>
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <samplerate.h>

// namespace glasses {
// namespace chatbot {

// // 导入命名空间
// using namespace protocol;
// using namespace statemachine;
// using namespace mcp;
// using namespace tool;
// using namespace wakeword;

// // ============================================================================
// // 全局音频回调管理（用于C函数指针回调）
// // ============================================================================

// // 全局AIManager实例指针（用于音频回调）
// static AIManager* g_ai_manager_instance = nullptr;
// static std::mutex g_callback_mutex;

// // 唤醒词音频数据回调（声明，定义在文件末尾）
// void wakewordAudioCallback(void* ai_manager, const int16_t* data, int len);

// // ============================================================================
// // AIManager::Impl 内部实现
// // ============================================================================

// class AIManager::Impl {
// public:
//     // 配置
//     AIConfig config;
    
//     // 核心模块
//     std::unique_ptr<ProtocolHandler> protocol_handler;
//     std::unique_ptr<AIStateMachine> state_machine;
//     std::unique_ptr<McpServer> mcp_server; 
//     std::unique_ptr<WakewordDetector> wakeword_detector;
//     websocket::WebSocketClient* ws_client;
    
//     // 音频系统
//     audio_system_t* audio_system;
    
//     // 唤醒词重采样器（48kHz → 16kHz）
//     SRC_STATE* wakeword_resampler;
    
//     // 状态
//     std::atomic<AIManagerState> manager_state;
//     std::string session_id;
    
//     // 运行控制
//     std::atomic<bool> is_running;
    
//     // 回调函数
//     STTTextCallback stt_callback;
//     LLMTextCallback llm_callback;
//     TTSAudioCallback tts_callback;
//     StateChangedCallback state_callback;
//     ErrorOccurredCallback error_callback;
    
//     // 线程安全
//     mutable std::mutex mutex;
    
//     Impl(const AIConfig& cfg)
//         : config(cfg)
//         , ws_client(nullptr)
//         , audio_system(nullptr)
//         , wakeword_resampler(nullptr)
//         , manager_state(AIManagerState::UNINITIALIZED)
//         , is_running(true) {
//     }
    
//     ~Impl() {
//         // 标记为已停止，让detached线程安全退出
//         is_running = false;
        
//         // 等待一段时间，让正在运行的detached线程检查标志并退出
//         std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
//         cleanup();
//     }
    
//     void cleanup() {
//         if (ws_client) {
//             ws_client->disconnect();
//             delete ws_client;
//             ws_client = nullptr;
//         }
        
//         if (wakeword_resampler) {
//             src_delete(wakeword_resampler);
//             wakeword_resampler = nullptr;
//         }
//     }
    
//     void setState(AIManagerState new_state) {
//         AIManagerState old_state = manager_state.exchange(new_state);
        
//         if (old_state != new_state) {
//             std::cout << "[AIManager] State: " << static_cast<int>(old_state) 
//                       << " → " << static_cast<int>(new_state) << std::endl;
            
//             if (state_callback) {
//                 state_callback(new_state);
//             }
//         }
//     }
    
//     // 内部方法声明
//     void setupProtocolCallbacks();
//     void setupStateMachineCallbacks();
//     void setupWebSocketCallbacks();
//     void handleProtocolMessage(const char* buffer, size_t size);
//     void handleTTSAudio(const uint8_t* data, size_t size);
//     void handleAudioData(const uint8_t* data, size_t size);
//     void handleWakewordAudio(const int16_t* data, int length);
//     void onWakewordDetected(int hotword_index);
//     void onWebSocketClosed();
//     std::string handleMCPMessage(const std::string& mcp_payload);
// };

// // ============================================================================
// // AIManager 公共接口实现
// // ============================================================================

// AIManager::AIManager(const AIConfig& config)
//     : pImpl_(new Impl(config)) {
//     std::cout << "[AIManager] AI Manager created" << std::endl;
    
//     // 注册全局实例
//     std::lock_guard<std::mutex> lock(g_callback_mutex);
//     g_ai_manager_instance = this;
// }

// AIManager::~AIManager() {
//     shutdown();
    
//     // 注销全局实例
//     std::lock_guard<std::mutex> lock(g_callback_mutex);
//     if (g_ai_manager_instance == this) {
//         g_ai_manager_instance = nullptr;
//     }
// }

// bool AIManager::initialize(audio_system_t* audio_system) {
//     if (!audio_system) {
//         std::cerr << "[AIManager] ✗ Audio system is null" << std::endl;
//         return false;
//     }
    
//     pImpl_->audio_system = audio_system;
    
//     std::cout << "[AIManager] ========================================" << std::endl;
//     std::cout << "[AIManager] Initializing xiaozhi AI Manager..." << std::endl;
//     std::cout << "[AIManager] ========================================" << std::endl;
    
//     // 获取设备ID（MAC地址）
//     if (pImpl_->config.device_id.empty()) {
//         pImpl_->config.device_id = getWirelessMacAddress();
//         if (pImpl_->config.device_id.empty()) {
//             std::cerr << "[AIManager] ✗ Failed to get MAC address" << std::endl;
//             return false;
//         }
//     }
//     std::cout << "[AIManager] ✓ Device-Id: " << pImpl_->config.device_id << std::endl;
    
//     // 获取客户端ID（UUID）
//     if (pImpl_->config.client_id.empty()) {
//         pImpl_->config.client_id = generateUUID(pImpl_->config.config_file_path);
//         if (pImpl_->config.client_id.empty()) {
//             std::cerr << "[AIManager] ✗ Failed to generate UUID" << std::endl;
//             return false;
//         }
//     }
//     std::cout << "[AIManager] ✓ Client-Id: " << pImpl_->config.client_id << std::endl;
    
//     // 创建协议处理器
//     pImpl_->protocol_handler = std::make_unique<ProtocolHandler>();
//     std::cout << "[AIManager] ✓ Protocol handler created" << std::endl;
    
//     // 创建AI状态机
//     pImpl_->state_machine = std::make_unique<AIStateMachine>();
//     std::cout << "[AIManager] ✓ State machine created" << std::endl;
    
//     // 创建MCP服务器
//     pImpl_->mcp_server = std::make_unique<McpServer>();
//     std::cout << "[AIManager] ✓ MCP server created" << std::endl;
    
//     // 创建唤醒词检测器
//     pImpl_->wakeword_detector = std::make_unique<WakewordDetector>();
    
//     // 配置唤醒词检测器
//     std::string resource_file = "./third_party/snowboy/resources/common.res";
//     std::string model_file = "./third_party/snowboy/resources/models/echo.pmdl";
//     float sensitivity = 0.5f;
//     float audio_gain = 1.0f;
    
//     std::cout << "[Wakeword] Initializing detector..." << std::endl;
//     std::cout << "[Wakeword]   Resource: " << resource_file << std::endl;
//     std::cout << "[Wakeword]   Model: " << model_file << std::endl;
//     std::cout << "[Wakeword]   Sensitivity: " << sensitivity << std::endl;
//     std::cout << "[Wakeword]   Audio Gain: " << audio_gain << std::endl;
    
//     if (pImpl_->wakeword_detector->initialize(resource_file, model_file, sensitivity, audio_gain)) {
//         std::cout << "[AIManager] ✓ Wakeword detector initialized!" << std::endl;
//         std::cout << "[Wakeword]   Sample rate: " << pImpl_->wakeword_detector->getSampleRate() << " Hz" << std::endl;
        
//         // 设置唤醒词检测回调
//         pImpl_->wakeword_detector->setCallback([this](int hotword_index) {
//             pImpl_->onWakewordDetected(hotword_index);
//         });
        
//         // 创建重采样器（48kHz → 16kHz）
//         int wakeword_sr = pImpl_->wakeword_detector->getSampleRate();  // 16000
//         if (wakeword_sr != pImpl_->config.audio_sample_rate) {
//             int error;
//             pImpl_->wakeword_resampler = src_new(SRC_SINC_FASTEST, 1, &error);
//             if (!pImpl_->wakeword_resampler) {
//                 std::cerr << "[AIManager] ⚠ Failed to create wakeword resampler: " 
//                           << src_strerror(error) << std::endl;
//             } else {
//                 std::cout << "[AIManager] ✓ Wakeword resampler created ("
//                           << pImpl_->config.audio_sample_rate << "Hz → " << wakeword_sr << "Hz)" << std::endl;
//             }
//         }
//     } else {
//         std::cerr << "[AIManager] ⚠ Failed to initialize wakeword detector" << std::endl;
//         std::cerr << "[AIManager]   Will continue without wakeword detection" << std::endl;
//     }
    
//     // 设置协议处理器回调
//     pImpl_->setupProtocolCallbacks();
    
//     // 设置状态机回调
//     pImpl_->setupStateMachineCallbacks();
    
//     // 创建WebSocket客户端
//     pImpl_->ws_client = websocket::createXiaozhiClient(
//         pImpl_->config.device_id,
//         pImpl_->config.client_id,
//         // 二进制回调（TTS音频）
//         [this](const char* buffer, size_t size, void*) {
//             pImpl_->handleTTSAudio(reinterpret_cast<const uint8_t*>(buffer), size);
//         },
//         // 文本回调（JSON消息）
//         [this](const char* buffer, size_t size, void*) {
//             pImpl_->handleProtocolMessage(buffer, size);
//         },
//         nullptr
//     );
    
//     if (!pImpl_->ws_client) {
//         std::cerr << "[AIManager] ✗ Failed to create WebSocket client" << std::endl;
//         return false;
//     }
//     std::cout << "[AIManager] ✓ WebSocket client created" << std::endl;
    
//     // 设置WebSocket回调
//     pImpl_->setupWebSocketCallbacks();
    
//     // 设置音频回调
//     if (set_ai_audio_callback(audio_system, this, 
//         glasses::chatbot::audioDataCallback) != AUDIO_ERROR_NONE) {
//         std::cerr << "[AIManager] ✗ Failed to set audio callback" << std::endl;
//         return false;
//     }
//     std::cout << "[AIManager] ✓ AI audio callback set" << std::endl;
    
//     // 设置唤醒词音频回调
//     audio_system->wakeword_audio_callback = wakewordAudioCallback;
//     audio_system->ai_manager = this;
//     std::cout << "[AIManager] ✓ Wakeword audio callback set" << std::endl;
    
//     pImpl_->setState(AIManagerState::INITIALIZED);
//     std::cout << "[AIManager] ========================================" << std::endl;
//     std::cout << "[AIManager] ✓ Initialization complete!" << std::endl;
//     std::cout << "[AIManager] ========================================" << std::endl;
    
//     return true;
// }

// bool AIManager::start() {
//     if (pImpl_->manager_state != AIManagerState::INITIALIZED) {
//         std::cerr << "[AIManager] ✗ Not initialized" << std::endl;
//         return false;
//     }
    
//     std::cout << "[AIManager] Starting AI service..." << std::endl;
    
//     pImpl_->setState(AIManagerState::CONNECTING);
    
//     // 连接WebSocket
//     if (!pImpl_->ws_client->connect()) {
//         std::cerr << "[AIManager] ✗ Failed to connect WebSocket" << std::endl;
//         pImpl_->setState(AIManagerState::ERROR);
//         return false;
//     }
    
//     // 等待连接和握手完成（最多等待10秒）
//     for (int i = 0; i < 100; i++) {
//         if (pImpl_->ws_client->isHandshaked()) {
//             pImpl_->setState(AIManagerState::CONNECTED);
//             std::cout << "[AIManager] ✓ Connected and handshaked!" << std::endl;
            
//             // MCP工具应该在调用start()之前通过getMCPServer()注册
//             std::cout << "[AIManager] MCP server ready (tools: " 
//                       << pImpl_->mcp_server->tool_count() << ")" << std::endl;
            
//             pImpl_->setState(AIManagerState::ACTIVE);
//             return true;
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     }
    
//     std::cerr << "[AIManager] ✗ Connection timeout" << std::endl;
//     pImpl_->setState(AIManagerState::ERROR);
//     return false;
// }

// void AIManager::stop() {
//     std::cout << "[AIManager] Stopping AI service..." << std::endl;
    
//     // 停止音频推流
//     if (pImpl_->audio_system && pImpl_->audio_system->is_ai_streaming) {
//         stop_ai_audio_stream(pImpl_->audio_system);
//     }
    
//     // 断开WebSocket
//     if (pImpl_->ws_client) {
//         pImpl_->ws_client->disconnect();
//     }
    
//     // 重置状态机
//     if (pImpl_->state_machine) {
//         pImpl_->state_machine->reset();
//     }
    
//     pImpl_->setState(AIManagerState::INITIALIZED);
//     std::cout << "[AIManager] ✓ AI service stopped" << std::endl;
// }

// void AIManager::shutdown() {
//     std::cout << "[AIManager] Shutting down..." << std::endl;
    
//     stop();
//     pImpl_->cleanup();
    
//     pImpl_->setState(AIManagerState::SHUTDOWN);
//     std::cout << "[AIManager] ✓ Shutdown complete" << std::endl;
// }

// // ========================================================================
// // AI交互控制
// // ========================================================================

// bool AIManager::startListening(const std::string& mode) {
//     if (pImpl_->manager_state != AIManagerState::ACTIVE) {
//         std::cerr << "[AIManager] ✗ Not in active state" << std::endl;
//         return false;
//     }
    
//     std::cout << "[AIManager] Starting listening (mode: " << mode << ")..." << std::endl;
    
//     // 启动音频推流
//     if (start_ai_audio_stream(pImpl_->audio_system) != AUDIO_ERROR_NONE) {
//         std::cerr << "[AIManager] ✗ Failed to start audio stream" << std::endl;
//         return false;
//     }
    
//     // 发送listen消息
//     ListenState listen_state = ListenState::START;
//     ListenMode listen_mode = ListenMode::AUTO;
    
//     if (mode == "manual") {
//         listen_mode = ListenMode::MANUAL;
//     } else if (mode == "realtime") {
//         listen_mode = ListenMode::REALTIME;
//     }
    
//     std::string listen_msg = pImpl_->protocol_handler->generateListenMessage(
//         listen_state, listen_mode);
    
//     if (!pImpl_->ws_client->sendText(listen_msg.c_str(), listen_msg.length())) {
//         std::cerr << "[AIManager] ✗ Failed to send listen message" << std::endl;
//         return false;
//     }
    
//     // 触发状态机
//     pImpl_->state_machine->onListenStart();
    
//     std::cout << "[AIManager] ✓ Listening started" << std::endl;
//     return true;
// }

// bool AIManager::stopListening() {
//     std::cout << "[AIManager] Stopping listening..." << std::endl;
    
//     // 停止音频推流
//     if (pImpl_->audio_system && pImpl_->audio_system->is_ai_streaming) {
//         stop_ai_audio_stream(pImpl_->audio_system);
//     }
    
//     // 发送listen stop消息
//     std::string listen_msg = pImpl_->protocol_handler->generateListenMessage(
//         ListenState::STOP, ListenMode::AUTO);
    
//     pImpl_->ws_client->sendText(listen_msg.c_str(), listen_msg.length());
    
//     std::cout << "[AIManager] ✓ Listening stopped" << std::endl;
//     return true;
// }

// bool AIManager::sendTextMessage(const std::string& text) {
//     if (!pImpl_->ws_client || !pImpl_->ws_client->isConnected()) {
//         std::cerr << "[AIManager] ✗ WebSocket not connected" << std::endl;
//         return false;
//     }
    
//     return pImpl_->ws_client->sendText(text.c_str(), text.length());
// }

// // ========================================================================
// // MCP工具访问
// // ========================================================================

// mcp::McpServer* AIManager::getMCPServer() {
//     return pImpl_->mcp_server.get();
// }

// // ========================================================================
// // 状态查询
// // ========================================================================

// AIManagerState AIManager::getState() const {
//     return pImpl_->manager_state;
// }

// bool AIManager::isConnected() const {
//     return pImpl_->manager_state == AIManagerState::CONNECTED ||
//            pImpl_->manager_state == AIManagerState::ACTIVE;
// }

// bool AIManager::isActive() const {
//     return pImpl_->manager_state == AIManagerState::ACTIVE;
// }

// std::string AIManager::getSessionId() const {
//     return pImpl_->protocol_handler ? 
//            pImpl_->protocol_handler->getSessionId() : "";
// }

// // ========================================================================
// // 回调设置
// // ========================================================================

// void AIManager::onSTTText(STTTextCallback callback) {
//     std::lock_guard<std::mutex> lock(pImpl_->mutex);
//     pImpl_->stt_callback = callback;
// }

// void AIManager::onLLMText(LLMTextCallback callback) {
//     std::lock_guard<std::mutex> lock(pImpl_->mutex);
//     pImpl_->llm_callback = callback;
// }

// void AIManager::onTTSAudio(TTSAudioCallback callback) {
//     std::lock_guard<std::mutex> lock(pImpl_->mutex);
//     pImpl_->tts_callback = callback;
// }

// void AIManager::onStateChanged(StateChangedCallback callback) {
//     std::lock_guard<std::mutex> lock(pImpl_->mutex);
//     pImpl_->state_callback = callback;
// }

// void AIManager::onError(ErrorOccurredCallback callback) {
//     std::lock_guard<std::mutex> lock(pImpl_->mutex);
//     pImpl_->error_callback = callback;
// }

// // ========================================================================
// // 内部方法实现
// // ========================================================================

// void AIManager::Impl::setupProtocolCallbacks() {
//     // Hello消息回调
//     protocol_handler->setHelloCallback([this](const HelloMessage& msg) {
//         std::cout << "[AIManager] ← Hello received" << std::endl;
//         std::cout << "[AIManager]   Session ID: " << msg.session_id << std::endl;
//         std::cout << "[AIManager]   Audio: " << msg.audio_params.sample_rate 
//                   << "Hz, " << msg.audio_params.channels << "ch, "
//                   << msg.audio_params.frame_duration << "ms" << std::endl;
        
//         // 保存session_id
//         session_id = msg.session_id;
//         protocol_handler->setSessionId(msg.session_id);
        
//         // 触发状态机
//         state_machine->onHello();
//     });
    
//     // STT消息回调
//     protocol_handler->setSTTCallback([this](const STTMessage& msg) {
//         std::cout << "[AIManager] ← STT: \"" << msg.text << "\" (final: " 
//                   << (msg.is_final ? "true" : "false") << ")" << std::endl;
        
//         // 触发状态机
//         state_machine->onSTT(msg.text, msg.is_final);
        
//         // 触发用户回调
//         if (stt_callback) {
//             stt_callback(msg.text, msg.is_final);
//         }
//     });
    
//     // LLM消息回调
//     protocol_handler->setLLMCallback([this](const LLMMessage& msg) {
//         std::cout << "[AIManager] ← LLM: \"" << msg.text << "\" (emotion: " 
//                   << ProtocolHandler::emotionTypeToString(msg.emotion) << ")" << std::endl;
        
//         // 触发状态机
//         state_machine->onLLM(msg.text, msg.is_final);
        
//         // 触发用户回调
//         if (llm_callback) {
//             llm_callback(msg.text, msg.is_final);
//         }
//     });
    
//     // TTS消息回调
//     protocol_handler->setTTSCallback([this](const TTSMessage& msg) {
//         switch (msg.state) {
//             case TTSState::START:
//                 std::cout << "[AIManager] ← TTS: START" << std::endl;
//                 state_machine->onTTS_start();
//                 break;
                
//             case TTSState::SENTENCE_START: {
//                 std::cout << "[AIManager] ← TTS: SENTENCE_START - \"" 
//                           << msg.text << "\"" << std::endl;
//                 state_machine->onTTS_sentenceStart(msg.text);
                
//                 // 重要！每个句子开始时也发送listen请求（参考官方xiaozhi）
//                 std::string listen_msg = protocol_handler->generateListenMessage(
//                     ListenState::START, ListenMode::AUTO);
//                 ws_client->sendText(listen_msg.c_str(), listen_msg.length());
//                 break;
//             }
                
//             case TTSState::STOP:
//                 std::cout << "[AIManager] ← TTS: STOP" << std::endl;
//                 state_machine->onTTS_stop(1000);  // 延迟一段时间再进行下次对话，防止AI自说自答
                
//                 // 重要！TTS结束后，主动发送listen请求告诉服务器继续监听
//                 // 捕获必要的指针和运行标志，避免访问已销毁的对象
//                 auto protocol_handler_ptr = protocol_handler.get();
//                 auto ws_client_ptr = ws_client;
//                 auto is_running_ptr = &is_running;
                
//                 std::thread([protocol_handler_ptr, ws_client_ptr, is_running_ptr]() {
//                     std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                    
//                     // 检查AIManager是否还在运行
//                     if (!is_running_ptr->load()) {
//                         std::cout << "[AIManager] ⚠ AIManager shutting down, skip listen request" << std::endl;
//                         return;
//                     }
                    
//                     // 检查WebSocket是否还连接
//                     if (!ws_client_ptr || !ws_client_ptr->isConnected()) {
//                         std::cout << "[AIManager] ⚠ WebSocket disconnected, skip listen request" << std::endl;
//                         return;
//                     }
                    
//                     std::cout << "[AIManager] TTS finished, sending listen request..." << std::endl;
                    
//                     // 主动发送listen消息进行连续对话
//                     std::string listen_msg = protocol_handler_ptr->generateListenMessage(
//                         ListenState::START, ListenMode::AUTO);
//                     ws_client_ptr->sendText(listen_msg.c_str(), listen_msg.length());
//                     std::cout << "[AIManager] ✓ Listen request sent" << std::endl;
//                 }).detach();
//                 break;
//         }
//     });
    
//     // MCP消息回调
//     protocol_handler->setMCPCallback([this](const std::string& mcp_payload) -> std::string {
//         return handleMCPMessage(mcp_payload);
//     });
    
//     // 错误消息回调
//     protocol_handler->setErrorCallback([this](const std::string& error) {
//         std::cerr << "[AIManager] ← Error: " << error << std::endl;
        
//         state_machine->onError(error);
        
//         if (error_callback) {
//             error_callback(error);
//         }
//     });
// }

// void AIManager::Impl::setupStateMachineCallbacks() {
//     // 状态变化回调
//     state_machine->setStateChangeCallback([this](AIState old_state, AIState new_state) {
//         std::cout << "[AIManager] AI State: " 
//                   << AIStateMachine::stateToString(old_state) << " → " 
//                   << AIStateMachine::stateToString(new_state) << std::endl;
//     });
    
//     // 音频上传控制回调
//     state_machine->setAudioUploadCallback([this](bool enable) {
//         if (enable) {
//             std::cout << "[AIManager] ✅ Audio upload ENABLED" << std::endl;
//             // 实际控制在recordCallback中通过is_ai_streaming控制
//         } else {
//             std::cout << "[AIManager] ❌ Audio upload DISABLED" << std::endl;
            
//             // 当禁用音频上传时，检查是否需要停止AI音频流
//             // 如果当前在IDLE状态且禁用上传，说明是回到IDLE了，需要停止AI音频流
//             AIState current_state = state_machine->getState();
//             if (current_state == AIState::IDLE && audio_system && audio_system->is_ai_streaming) {
//                 std::cout << "[AIManager] Back to IDLE, stopping AI audio stream..." << std::endl;
//                 stop_ai_audio_stream(audio_system);
//                 std::cout << "[AIManager] ✓ AI audio stream stopped (back to IDLE for wakeword)" << std::endl;
//             }
//         }
//     });
// }

// void AIManager::Impl::setupWebSocketCallbacks() {
//     // 设置WebSocket关闭回调
//     std::cout << "[AIManager] ✓ WebSocket close callback configured" << std::endl;

//     // TODO: ws关闭回调的空实现
//     // ws_client->setOnCloseCallback([this]() { onWebSocketClosed(); });
// }

// void AIManager::Impl::onWebSocketClosed() {
//     std::cout << "[AIManager] WebSocket connection closed!" << std::endl;
    
//     // 通知状态机WebSocket已关闭
//     if (state_machine) {
//         state_machine->onWebSocketClosed();
//     }
// }

// void AIManager::Impl::handleProtocolMessage(const char* buffer, size_t size) {
//     // 解析协议消息
//     MessageType msg_type = protocol_handler->parseMessage(buffer, size);
    
//     if (msg_type == MessageType::UNKNOWN) {
//         std::cerr << "[AIManager] ✗ Unknown message type" << std::endl;
//     }
// }

// void AIManager::Impl::handleTTSAudio(const uint8_t* data, size_t size) {
//     // 解码Opus
//     uint8_t pcm_buffer[8192];
//     size_t pcm_size = 8192;
    
//     if (decode_opus(audio_system, 
//                    const_cast<uint8_t*>(data), size, 
//                    pcm_buffer, &pcm_size) == AUDIO_ERROR_NONE) {
        
//         // 转换为int16_t向量
//         std::vector<int16_t> pcm_frame(
//             reinterpret_cast<int16_t*>(pcm_buffer),
//             reinterpret_cast<int16_t*>(pcm_buffer + pcm_size)
//         );
        
//         // 应用音量控制
//         float volume = AUDIO_MASTER_VOLUME;
//         for (auto& sample : pcm_frame) {
//             sample = static_cast<int16_t>(sample * volume);
//         }
        
//         // 添加到播放队列
//         add_frame_to_playback_queue(audio_system, pcm_frame);
        
//         // 如果播放未启动，启动播放
//         if (!audio_system->isPlaying) {
//             start_playback(audio_system);
//         }
        
//         // 触发用户回调
//         if (tts_callback) {
//             tts_callback(data, size);
//         }
//     }
// }

// std::string AIManager::Impl::handleMCPMessage(const std::string& mcp_payload) {
//     if (!mcp_server) {
//         std::cerr << "[AIManager] ✗ MCP server not initialized" << std::endl;
//         return "";
//     }
    
//     std::cout << "[AIManager] ← MCP message received" << std::endl;
    
//     // 处理MCP消息并获取响应
//     std::string response = mcp_server->handle_message(mcp_payload);
    
//     if (!response.empty() && ws_client && ws_client->isConnected()) {
//         // 封装为完整的MCP消息
//         json full_msg;
//         full_msg["session_id"] = protocol_handler->getSessionId();
//         full_msg["type"] = "mcp";
//         full_msg["payload"] = json::parse(response);
        
//         std::string full_response = full_msg.dump();
        
//         std::cout << "[AIManager] → Sending MCP response" << std::endl;
//         ws_client->sendText(full_response.c_str(), full_response.length());
//     }
    
//     return response;
// }

// // ========================================================================
// // 唤醒词处理方法
// // ========================================================================

// void AIManager::Impl::onWakewordDetected(int hotword_index) {
//     std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
//     std::cout << "║   🎙️  唤醒词检测到！Hotword " << hotword_index << "       ║" << std::endl;
//     std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    
//     // 检查当前状态
//     AIState current_state = state_machine->getState();
//     if (current_state != AIState::IDLE) {
//         std::cout << "[AIManager] ⚠ Not in IDLE state, ignoring wakeword" << std::endl;
//         return;
//     }
    
//     // 触发状态机事件
//     state_machine->onWakewordDetected();
    
//     // 自动开始监听
//     std::cout << "[AIManager] → Auto starting listening after wakeword..." << std::endl;
    
//     // 启动AI音频流
//     if (start_ai_audio_stream(audio_system) != AUDIO_ERROR_NONE) {
//         std::cerr << "[AIManager] ✗ Failed to start audio stream" << std::endl;
//         return;
//     }
    
//     // 发送listen消息
//     std::string listen_msg = protocol_handler->generateListenMessage(
//         ListenState::START, ListenMode::AUTO);
    
//     if (!ws_client->sendText(listen_msg.c_str(), listen_msg.length())) {
//         std::cerr << "[AIManager] ✗ Failed to send listen message" << std::endl;
//         stop_ai_audio_stream(audio_system);
//         return;
//     }
    
//     // 触发状态机进入监听状态
//     state_machine->onListenStart();
    
//     std::cout << "[AIManager] ✓ Listening started automatically" << std::endl;
// }

// void AIManager::Impl::handleWakewordAudio(const int16_t* data, int length) {
//     // 只在 IDLE 状态下进行唤醒词检测
//     if (state_machine->getState() != AIState::IDLE) {
//         return;
//     }
    
//     if (!wakeword_detector || !wakeword_detector->isEnabled()) {
//         return;
//     }
    
//     // 如果需要重采样
//     if (wakeword_resampler) {
//         int wakeword_sr = wakeword_detector->getSampleRate();
//         double src_ratio = (double)wakeword_sr / config.audio_sample_rate;
        
//         // 准备重采样
//         SRC_DATA src_data;
//         std::vector<float> input_float(length);
//         std::vector<float> output_float(length * 2);  // 预留空间
        
//         // int16 → float
//         src_short_to_float_array(data, input_float.data(), length);
        
//         // 重采样
//         src_data.data_in = input_float.data();
//         src_data.input_frames = length;
//         src_data.data_out = output_float.data();
//         src_data.output_frames = output_float.size();
//         src_data.src_ratio = src_ratio;
//         src_data.end_of_input = 0;
        
//         int error = src_process(wakeword_resampler, &src_data);
//         if (error) {
//             std::cerr << "[AIManager] Wakeword resample error: " << src_strerror(error) << std::endl;
//             return;
//         }
        
//         // float → int16
//         std::vector<int16_t> resampled(src_data.output_frames_gen);
//         src_float_to_short_array(output_float.data(), resampled.data(), src_data.output_frames_gen);
        
//         // 唤醒词检测
//         wakeword_detector->processAudioFrame(resampled.data(), resampled.size());
//     } else {
//         // 无需重采样，直接检测
//         wakeword_detector->processAudioFrame(data, length);
//     }
// }

// // ========================================================================
// // 音频数据发送回调（命名空间内函数）
// // ========================================================================

// void audioDataCallback(void* data, int len, uint64_t timestamp) {
//     (void)timestamp;  // 未使用，忽略警告
    
//     // data指向Opus编码后的音频数据
//     // 通过全局指针访问AIManager实例
    
//     std::lock_guard<std::mutex> lock(g_callback_mutex);
    
//     if (!g_ai_manager_instance) {
//         return;  // AIManager未初始化或已销毁
//     }
    
//     // 转发到AIManager的handleAudioData
//     g_ai_manager_instance->pImpl_->handleAudioData(
//         reinterpret_cast<const uint8_t*>(data), 
//         static_cast<size_t>(len)
//     );
// }

// void AIManager::Impl::handleAudioData(const uint8_t* data, size_t size) {
//     if (!ws_client || !ws_client->isConnected()) {
//         return;
//     }
    
//     if (!state_machine->isAudioUploadEnabled()) {
//         return;  // 状态机控制：TTS期间不上传音频
//     }
    
//     // 发送Opus音频数据到服务器
//     ws_client->sendBinary(reinterpret_cast<const char*>(data), size);
// }

// // ========================================================================
// // 唤醒词音频回调实现（必须在 Impl 定义之后）
// // ========================================================================

// void wakewordAudioCallback(void* ai_manager, const int16_t* data, int len) {
//     if (!ai_manager) {
//         return;
//     }
    
//     // 调用AIManager的handleWakewordAudio
//     AIManager* manager = static_cast<AIManager*>(ai_manager);
//     manager->pImpl_->handleWakewordAudio(data, len);
// }

// } // namespace chatbot
// } // namespace glasses

