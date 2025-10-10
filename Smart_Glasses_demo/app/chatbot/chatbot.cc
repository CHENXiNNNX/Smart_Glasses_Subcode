/**
 * @file chatbot.cc
 * @brief xiaozhi AI主控制器实现
 */

#include "chatbot.h"
#include "protocol_handle/handle.h"
#include "statemachine/machine.h"
#include "mcp/mcp.h"
#include "uuid/uuid.h"
#include "../protocol/websocket/websocket.h"
#include "../tool/mac/mac.h"

#include <iostream>
#include <atomic>
#include <mutex>
#include <thread>

namespace glasses {
namespace chatbot {

// 导入命名空间
using namespace protocol;
using namespace statemachine;
using namespace mcp;
using namespace tool;

// 导入MCP类型
using mcp::MethodHandler;
using mcp::StateGetter;

// ============================================================================
// 全局音频回调管理（用于C函数指针回调）
// ============================================================================

// 全局AIManager实例指针（用于音频回调）
static AIManager* g_ai_manager_instance = nullptr;
static std::mutex g_callback_mutex;

// ============================================================================
// AIManager::Impl 内部实现
// ============================================================================

class AIManager::Impl {
public:
    // 配置
    AIConfig config;
    
    // 核心模块
    std::unique_ptr<ProtocolHandler> protocol_handler;
    std::unique_ptr<AIStateMachine> state_machine;
    std::unique_ptr<MCPManager> mcp_manager;
    websocket::WebSocketClient* ws_client;
    
    // 音频系统
    audio_system_t* audio_system;
    
    // 状态
    std::atomic<AIManagerState> manager_state;
    std::string session_id;
    
    // 回调函数
    STTTextCallback stt_callback;
    LLMTextCallback llm_callback;
    TTSAudioCallback tts_callback;
    StateChangedCallback state_callback;
    ErrorOccurredCallback error_callback;
    
    // 线程安全
    mutable std::mutex mutex;
    
    Impl(const AIConfig& cfg)
        : config(cfg)
        , ws_client(nullptr)
        , audio_system(nullptr)
        , manager_state(AIManagerState::UNINITIALIZED) {
    }
    
    ~Impl() {
        cleanup();
    }
    
    void cleanup() {
        if (ws_client) {
            ws_client->disconnect();
            delete ws_client;
            ws_client = nullptr;
        }
    }
    
    void setState(AIManagerState new_state) {
        AIManagerState old_state = manager_state.exchange(new_state);
        
        if (old_state != new_state) {
            std::cout << "[AIManager] State: " << static_cast<int>(old_state) 
                      << " → " << static_cast<int>(new_state) << std::endl;
            
            if (state_callback) {
                state_callback(new_state);
            }
        }
    }
    
    // 内部方法声明
    void setupProtocolCallbacks();
    void setupStateMachineCallbacks();
    void handleProtocolMessage(const char* buffer, size_t size);
    void handleTTSAudio(const uint8_t* data, size_t size);
    void handleAudioData(const uint8_t* data, size_t size);
    void sendMCPDescriptors();
};

// ============================================================================
// AIManager 公共接口实现
// ============================================================================

AIManager::AIManager(const AIConfig& config)
    : pImpl_(new Impl(config)) {
    std::cout << "[AIManager] AI Manager created" << std::endl;
    
    // 注册全局实例
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    g_ai_manager_instance = this;
}

AIManager::~AIManager() {
    shutdown();
    
    // 注销全局实例
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    if (g_ai_manager_instance == this) {
        g_ai_manager_instance = nullptr;
    }
}

bool AIManager::initialize(audio_system_t* audio_system) {
    if (!audio_system) {
        std::cerr << "[AIManager] ✗ Audio system is null" << std::endl;
        return false;
    }
    
    pImpl_->audio_system = audio_system;
    
    std::cout << "[AIManager] ========================================" << std::endl;
    std::cout << "[AIManager] Initializing xiaozhi AI Manager..." << std::endl;
    std::cout << "[AIManager] ========================================" << std::endl;
    
    // 1. 获取设备ID（MAC地址）
    if (pImpl_->config.device_id.empty()) {
        pImpl_->config.device_id = getWirelessMacAddress();
        if (pImpl_->config.device_id.empty()) {
            std::cerr << "[AIManager] ✗ Failed to get MAC address" << std::endl;
            return false;
        }
    }
    std::cout << "[AIManager] ✓ Device-Id: " << pImpl_->config.device_id << std::endl;
    
    // 2. 获取客户端ID（UUID）
    if (pImpl_->config.client_id.empty()) {
        pImpl_->config.client_id = generateUUID(pImpl_->config.config_file_path);
        if (pImpl_->config.client_id.empty()) {
            std::cerr << "[AIManager] ✗ Failed to generate UUID" << std::endl;
            return false;
        }
    }
    std::cout << "[AIManager] ✓ Client-Id: " << pImpl_->config.client_id << std::endl;
    
    // 3. 创建协议处理器
    pImpl_->protocol_handler = std::make_unique<ProtocolHandler>();
    std::cout << "[AIManager] ✓ Protocol handler created" << std::endl;
    
    // 4. 创建AI状态机
    pImpl_->state_machine = std::make_unique<AIStateMachine>();
    std::cout << "[AIManager] ✓ State machine created" << std::endl;
    
    // 5. 创建MCP管理器
    pImpl_->mcp_manager = std::make_unique<MCPManager>();
    std::cout << "[AIManager] ✓ MCP manager created" << std::endl;
    
    // 6. 设置协议处理器回调
    pImpl_->setupProtocolCallbacks();
    
    // 7. 设置状态机回调
    pImpl_->setupStateMachineCallbacks();
    
    // 8. 创建WebSocket客户端
    pImpl_->ws_client = websocket::createXiaozhiClient(
        pImpl_->config.device_id,
        pImpl_->config.client_id,
        // 二进制回调（TTS音频）
        [this](const char* buffer, size_t size, void*) {
            pImpl_->handleTTSAudio(reinterpret_cast<const uint8_t*>(buffer), size);
        },
        // 文本回调（JSON消息）
        [this](const char* buffer, size_t size, void*) {
            pImpl_->handleProtocolMessage(buffer, size);
        },
        nullptr
    );
    
    if (!pImpl_->ws_client) {
        std::cerr << "[AIManager] ✗ Failed to create WebSocket client" << std::endl;
        return false;
    }
    std::cout << "[AIManager] ✓ WebSocket client created" << std::endl;
    
    // 9. 设置音频回调
    if (set_ai_audio_callback(audio_system, this, 
        glasses::chatbot::audioDataCallback) != AUDIO_ERROR_NONE) {
        std::cerr << "[AIManager] ✗ Failed to set audio callback" << std::endl;
        return false;
    }
    std::cout << "[AIManager] ✓ AI audio callback set" << std::endl;
    
    pImpl_->setState(AIManagerState::INITIALIZED);
    std::cout << "[AIManager] ========================================" << std::endl;
    std::cout << "[AIManager] ✓ Initialization complete!" << std::endl;
    std::cout << "[AIManager] ========================================" << std::endl;
    
    return true;
}

bool AIManager::start() {
    if (pImpl_->manager_state != AIManagerState::INITIALIZED) {
        std::cerr << "[AIManager] ✗ Not initialized" << std::endl;
        return false;
    }
    
    std::cout << "[AIManager] Starting AI service..." << std::endl;
    
    pImpl_->setState(AIManagerState::CONNECTING);
    
    // 连接WebSocket
    if (!pImpl_->ws_client->connect()) {
        std::cerr << "[AIManager] ✗ Failed to connect WebSocket" << std::endl;
        pImpl_->setState(AIManagerState::ERROR);
        return false;
    }
    
    // 等待连接和握手完成（最多等待10秒）
    for (int i = 0; i < 100; i++) {
        if (pImpl_->ws_client->isHandshaked()) {
            pImpl_->setState(AIManagerState::CONNECTED);
            std::cout << "[AIManager] ✓ Connected and handshaked!" << std::endl;
            
            // 发送MCP设备描述符
            pImpl_->sendMCPDescriptors();
            
            pImpl_->setState(AIManagerState::ACTIVE);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cerr << "[AIManager] ✗ Connection timeout" << std::endl;
    pImpl_->setState(AIManagerState::ERROR);
    return false;
}

void AIManager::stop() {
    std::cout << "[AIManager] Stopping AI service..." << std::endl;
    
    // 停止音频推流
    if (pImpl_->audio_system && pImpl_->audio_system->is_ai_streaming) {
        stop_ai_audio_stream(pImpl_->audio_system);
    }
    
    // 断开WebSocket
    if (pImpl_->ws_client) {
        pImpl_->ws_client->disconnect();
    }
    
    // 重置状态机
    if (pImpl_->state_machine) {
        pImpl_->state_machine->reset();
    }
    
    pImpl_->setState(AIManagerState::INITIALIZED);
    std::cout << "[AIManager] ✓ AI service stopped" << std::endl;
}

void AIManager::shutdown() {
    std::cout << "[AIManager] Shutting down..." << std::endl;
    
    stop();
    pImpl_->cleanup();
    
    pImpl_->setState(AIManagerState::SHUTDOWN);
    std::cout << "[AIManager] ✓ Shutdown complete" << std::endl;
}

// ========================================================================
// AI交互控制
// ========================================================================

bool AIManager::startListening(const std::string& mode) {
    if (pImpl_->manager_state != AIManagerState::ACTIVE) {
        std::cerr << "[AIManager] ✗ Not in active state" << std::endl;
        return false;
    }
    
    std::cout << "[AIManager] Starting listening (mode: " << mode << ")..." << std::endl;
    
    // 1. 启动音频推流
    if (start_ai_audio_stream(pImpl_->audio_system) != AUDIO_ERROR_NONE) {
        std::cerr << "[AIManager] ✗ Failed to start audio stream" << std::endl;
        return false;
    }
    
    // 2. 发送listen消息
    ListenState listen_state = ListenState::START;
    ListenMode listen_mode = ListenMode::AUTO;
    
    if (mode == "manual") {
        listen_mode = ListenMode::MANUAL;
    } else if (mode == "realtime") {
        listen_mode = ListenMode::REALTIME;
    }
    
    std::string listen_msg = pImpl_->protocol_handler->generateListenMessage(
        listen_state, listen_mode);
    
    if (!pImpl_->ws_client->sendText(listen_msg.c_str(), listen_msg.length())) {
        std::cerr << "[AIManager] ✗ Failed to send listen message" << std::endl;
        return false;
    }
    
    // 3. 触发状态机
    pImpl_->state_machine->onListenStart();
    
    std::cout << "[AIManager] ✓ Listening started" << std::endl;
    return true;
}

bool AIManager::stopListening() {
    std::cout << "[AIManager] Stopping listening..." << std::endl;
    
    // 停止音频推流
    if (pImpl_->audio_system && pImpl_->audio_system->is_ai_streaming) {
        stop_ai_audio_stream(pImpl_->audio_system);
    }
    
    // 发送listen stop消息
    std::string listen_msg = pImpl_->protocol_handler->generateListenMessage(
        ListenState::STOP, ListenMode::AUTO);
    
    pImpl_->ws_client->sendText(listen_msg.c_str(), listen_msg.length());
    
    std::cout << "[AIManager] ✓ Listening stopped" << std::endl;
    return true;
}

bool AIManager::sendTextMessage(const std::string& text) {
    if (!pImpl_->ws_client || !pImpl_->ws_client->isConnected()) {
        std::cerr << "[AIManager] ✗ WebSocket not connected" << std::endl;
        return false;
    }
    
    return pImpl_->ws_client->sendText(text.c_str(), text.length());
}

// ========================================================================
// MCP设备注册
// ========================================================================

bool AIManager::registerDevice(
    const IoTDescriptor& descriptor,
    MethodHandler handler,
    StateGetter getter
) {
    if (!pImpl_->mcp_manager) {
        std::cerr << "[AIManager] ✗ MCP manager not initialized" << std::endl;
        return false;
    }
    
    bool result = pImpl_->mcp_manager->registerDevice(descriptor, handler, getter);
    
    if (result && pImpl_->manager_state == AIManagerState::ACTIVE) {
        // 如果已连接，立即发送更新
        pImpl_->sendMCPDescriptors();
    }
    
    return result;
}

bool AIManager::unregisterDevice(const std::string& device_name) {
    if (!pImpl_->mcp_manager) {
        return false;
    }
    
    return pImpl_->mcp_manager->unregisterDevice(device_name);
}

// ========================================================================
// 状态查询
// ========================================================================

AIManagerState AIManager::getState() const {
    return pImpl_->manager_state;
}

bool AIManager::isConnected() const {
    return pImpl_->manager_state == AIManagerState::CONNECTED ||
           pImpl_->manager_state == AIManagerState::ACTIVE;
}

bool AIManager::isActive() const {
    return pImpl_->manager_state == AIManagerState::ACTIVE;
}

std::string AIManager::getSessionId() const {
    return pImpl_->protocol_handler ? 
           pImpl_->protocol_handler->getSessionId() : "";
}

// ========================================================================
// 回调设置
// ========================================================================

void AIManager::onSTTText(STTTextCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->mutex);
    pImpl_->stt_callback = callback;
}

void AIManager::onLLMText(LLMTextCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->mutex);
    pImpl_->llm_callback = callback;
}

void AIManager::onTTSAudio(TTSAudioCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->mutex);
    pImpl_->tts_callback = callback;
}

void AIManager::onStateChanged(StateChangedCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->mutex);
    pImpl_->state_callback = callback;
}

void AIManager::onError(ErrorOccurredCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->mutex);
    pImpl_->error_callback = callback;
}

// ========================================================================
// 内部方法实现
// ========================================================================

void AIManager::Impl::setupProtocolCallbacks() {
    // Hello消息回调
    protocol_handler->setHelloCallback([this](const HelloMessage& msg) {
        std::cout << "[AIManager] ← Hello received" << std::endl;
        std::cout << "[AIManager]   Session ID: " << msg.session_id << std::endl;
        std::cout << "[AIManager]   Audio: " << msg.audio_params.sample_rate 
                  << "Hz, " << msg.audio_params.channels << "ch, "
                  << msg.audio_params.frame_duration << "ms" << std::endl;
        
        // 保存session_id
        session_id = msg.session_id;
        protocol_handler->setSessionId(msg.session_id);
        
        // 触发状态机
        state_machine->onHello();
    });
    
    // STT消息回调
    protocol_handler->setSTTCallback([this](const STTMessage& msg) {
        std::cout << "[AIManager] ← STT: \"" << msg.text << "\" (final: " 
                  << (msg.is_final ? "true" : "false") << ")" << std::endl;
        
        // 触发状态机
        state_machine->onSTT(msg.text, msg.is_final);
        
        // 触发用户回调
        if (stt_callback) {
            stt_callback(msg.text, msg.is_final);
        }
    });
    
    // LLM消息回调
    protocol_handler->setLLMCallback([this](const LLMMessage& msg) {
        std::cout << "[AIManager] ← LLM: \"" << msg.text << "\" (emotion: " 
                  << ProtocolHandler::emotionTypeToString(msg.emotion) << ")" << std::endl;
        
        // 触发状态机
        state_machine->onLLM(msg.text, msg.is_final);
        
        // 触发用户回调
        if (llm_callback) {
            llm_callback(msg.text, msg.is_final);
        }
    });
    
    // TTS消息回调
    protocol_handler->setTTSCallback([this](const TTSMessage& msg) {
        switch (msg.state) {
            case TTSState::START:
                std::cout << "[AIManager] ← TTS: START" << std::endl;
                state_machine->onTTS_start();
                break;
                
            case TTSState::SENTENCE_START:
                std::cout << "[AIManager] ← TTS: SENTENCE_START - \"" 
                          << msg.text << "\"" << std::endl;
                state_machine->onTTS_sentenceStart(msg.text);
                break;
                
            case TTSState::STOP:
                std::cout << "[AIManager] ← TTS: STOP" << std::endl;
                state_machine->onTTS_stop(2000);  // 延迟2秒
                
                // TTS结束后，重新启动音频流和监听
                std::thread([this]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                    
                    std::cout << "[AIManager] Checking state after TTS delay..." << std::endl;
                    std::cout << "[AIManager]   State: " << AIStateMachine::stateToString(state_machine->getState()) << std::endl;
                    std::cout << "[AIManager]   Audio streaming: " << (audio_system->is_ai_streaming ? "YES" : "NO") << std::endl;
                    std::cout << "[AIManager]   Audio recording: " << (audio_system->isRecording ? "YES" : "NO") << std::endl;
                    
                    if (state_machine->getState() == AIState::LISTENING) {
                        // 重新启动音频流（如果停止了）
                        if (!audio_system->is_ai_streaming) {
                            std::cout << "[AIManager] Restarting audio stream..." << std::endl;
                            if (start_ai_audio_stream(audio_system) == AUDIO_ERROR_NONE) {
                                std::cout << "[AIManager] ✓ Audio stream restarted" << std::endl;
                            } else {
                                std::cerr << "[AIManager] ✗ Failed to restart audio stream" << std::endl;
                            }
                        }
                        
                        // 重新发送listen消息
                        std::string listen_msg = protocol_handler->generateListenMessage(
                            ListenState::START, ListenMode::AUTO);
                        ws_client->sendText(listen_msg.c_str(), listen_msg.length());
                        std::cout << "[AIManager] → Continue listening (listen message sent)" << std::endl;
                    }
                }).detach();
                break;
        }
    });
    
    // IoT消息回调
    protocol_handler->setIoTCallback([this](const IoTMessage& msg) {
        std::cout << "[AIManager] ← IoT message" << std::endl;
        
        // 如果是设备方法调用
        if (!msg.device_name.empty() && !msg.method_name.empty()) {
            std::cout << "[AIManager]   Invoke: " << msg.device_name 
                      << "." << msg.method_name << std::endl;
            
            // 调用MCP处理
            bool result = mcp_manager->handleIoTInvoke(msg);
            
            // 发送调用结果
            std::string result_msg = protocol_handler->generateIoTInvokeResultMessage(
                msg.device_name, msg.method_name, result, "");
            
            ws_client->sendText(result_msg.c_str(), result_msg.length());
        }
    });
    
    // 错误消息回调
    protocol_handler->setErrorCallback([this](const std::string& error) {
        std::cerr << "[AIManager] ← Error: " << error << std::endl;
        
        state_machine->onError(error);
        
        if (error_callback) {
            error_callback(error);
        }
    });
}

void AIManager::Impl::setupStateMachineCallbacks() {
    // 状态变化回调
    state_machine->setStateChangeCallback([this](AIState old_state, AIState new_state) {
        std::cout << "[AIManager] State: " 
                  << AIStateMachine::stateToString(old_state) << " → " 
                  << AIStateMachine::stateToString(new_state) << std::endl;
    });
    
    // 音频上传控制回调
    state_machine->setAudioUploadCallback([this](bool enable) {
        if (enable) {
            std::cout << "[AIManager] ✅ Audio upload ENABLED" << std::endl;
            // 实际控制在recordCallback中通过is_ai_streaming控制
            // 这里可以添加额外的控制逻辑
        } else {
            std::cout << "[AIManager] ❌ Audio upload DISABLED" << std::endl;
        }
    });
}

void AIManager::Impl::handleProtocolMessage(const char* buffer, size_t size) {
    // 解析协议消息
    MessageType msg_type = protocol_handler->parseMessage(buffer, size);
    
    if (msg_type == MessageType::UNKNOWN) {
        std::cerr << "[AIManager] ✗ Unknown message type" << std::endl;
    }
}

void AIManager::Impl::handleTTSAudio(const uint8_t* data, size_t size) {
    // std::cout << "[AIManager] ← TTS Audio: " << size << " bytes" << std::endl;
    
    // 解码Opus
    uint8_t pcm_buffer[8192];
    size_t pcm_size = 8192;
    
    if (decode_opus(audio_system, 
                   const_cast<uint8_t*>(data), size, 
                   pcm_buffer, &pcm_size) == AUDIO_ERROR_NONE) {
        
        // 转换为int16_t向量
        std::vector<int16_t> pcm_frame(
            reinterpret_cast<int16_t*>(pcm_buffer),
            reinterpret_cast<int16_t*>(pcm_buffer + pcm_size)
        );
        
        // 应用音量控制
        float volume = AUDIO_MASTER_VOLUME;
        for (auto& sample : pcm_frame) {
            sample = static_cast<int16_t>(sample * volume);
        }
        
        // 添加到播放队列
        add_frame_to_playback_queue(audio_system, pcm_frame);
        
        // 如果播放未启动，启动播放
        if (!audio_system->isPlaying) {
            start_playback(audio_system);
        }
        
        // 触发用户回调
        if (tts_callback) {
            tts_callback(data, size);
        }
    }
}

void AIManager::Impl::sendMCPDescriptors() {
    if (!mcp_manager || !ws_client) {
        return;
    }
    
    // 获取所有设备描述符
    auto descriptors = mcp_manager->getAllDescriptors();
    
    if (descriptors.empty()) {
        std::cout << "[AIManager] No MCP devices to send" << std::endl;
        return;
    }
    
    // 生成IoT描述符消息
    std::string msg = mcp_manager->generateDescriptorMessage(
        protocol_handler->getSessionId());
    
    std::cout << "[AIManager] → Sending " << descriptors.size() 
              << " MCP device descriptors" << std::endl;
    
    ws_client->sendText(msg.c_str(), msg.length());
}

// ========================================================================
// 音频数据发送回调（命名空间内函数）
// ========================================================================

void audioDataCallback(void* data, int len, uint64_t timestamp) {
    (void)timestamp;  // 未使用，忽略警告
    
    // data指向Opus编码后的音频数据
    // 通过全局指针访问AIManager实例
    
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    
    if (!g_ai_manager_instance) {
        return;  // AIManager未初始化或已销毁
    }
    
    // 转发到AIManager的handleAudioData
    g_ai_manager_instance->pImpl_->handleAudioData(
        reinterpret_cast<const uint8_t*>(data), 
        static_cast<size_t>(len)
    );
}

void AIManager::Impl::handleAudioData(const uint8_t* data, size_t size) {
    if (!ws_client || !ws_client->isConnected()) {
        return;
    }
    
    if (!state_machine->isAudioUploadEnabled()) {
        return;  // 状态机控制：TTS期间不上传音频
    }
    
    // 发送Opus音频数据到服务器
    ws_client->sendBinary(reinterpret_cast<const char*>(data), size);
}

} // namespace chatbot
} // namespace glasses

