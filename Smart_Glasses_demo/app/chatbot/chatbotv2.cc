/**
 * @file chatbotv2.cc
 * @brief xiaozhi AI系统主控制器V2实现
 */

#include "chatbotv2.h"
#include "protocol_handle/handlev2.h"
#include "statemachine/machinev2.h"
#include "mcp/mcpv2.h"
#include "activation/activationv2.h"
#include "wakeword/wakewordv2.h"
#include "uuid/uuid.h"
#include "../protocol/websocket/websocketv2.h"
#include "../protocol/udp/udpv2.h"
#include "../media/audio/audiov2.h"
#include "../tool/log/log.h"
#include "../tool/mac/mac.h"
#include "../tool/mcp_tool/mcp_toolv2.h"
#include "../../common/common.h"

#include <thread>
#include <condition_variable>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace glasses {
namespace chatbot {

using namespace tool::logger;

// 命名空间别名
namespace protocol_ns = glasses::chatbot::protocol;
namespace statemachine_ns = glasses::chatbot::statemachine;
namespace mcp_ns = glasses::chatbot::mcp;
namespace activation_ns = glasses::chatbot::activation;
namespace wakeword_ns = glasses::chatbot::wakeword;
namespace websocket_ns = glasses::protocol::websocket;
namespace udp_ns = glasses::protocol::udp;
namespace audio_ns = glasses::media::audio;

// ============================================================================
// ChatbotSystemV2::Impl 内部实现
// ============================================================================

class ChatbotSystemV2::Impl {
public:
    // 配置
    ChatbotConfig config;
    
    // 核心模块（智能指针管理）
    std::unique_ptr<protocol_ns::ProtocolHandlerV2> protocol_handler;
    std::unique_ptr<statemachine_ns::AIStateMachineV2> state_machine;
    std::unique_ptr<mcp_ns::McpServerV2> mcp_server;
    std::unique_ptr<activation_ns::DeviceActivationV2> activation_manager;
    std::unique_ptr<wakeword_ns::WakewordDetectorV2> wakeword_detector;
    std::unique_ptr<websocket_ns::WebSocketClientV2> ws_client;
    std::unique_ptr<udp_ns::UdpEndpointV2> udp_endpoint;  // 可选
    
    // 音频系统（依赖注入，shared_ptr共享所有权）
    std::shared_ptr<audio_ns::AudioSystemV2> audio_system;
    
    // 系统状态
    std::atomic<ChatbotState> chatbot_state{ChatbotState::UNINITIALIZED};
    std::string session_id;
    std::string device_id;
    std::string client_id;
    uint64_t system_start_time{0};
    
    // 回调函数（线程安全）
    STTTextCallback stt_callback;
    LLMTextCallback llm_callback;
    TTSAudioCallback tts_callback;
    ChatbotStateCallback state_callback;
    ChatbotErrorCallback error_callback;
    WakewordDetectedCallback wakeword_callback;
    ActivationStateCallback activation_callback;
    mutable std::mutex callback_mutex;
    
    // 延迟任务管理（用于TTS后重新监听）
    std::unique_ptr<std::thread> delay_thread;
    std::atomic<bool> should_cancel_delay{false};
    std::condition_variable delay_cv;
    std::mutex delay_mutex;
    
    // 统计信息
    ChatbotSystemV2::Stats stats;
    
    // 线程安全
    mutable std::mutex mutex;
    
    explicit Impl(const ChatbotConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("ChatbotV2", "Impl created");
        system_start_time = get_nowus();
    }
    
    ~Impl() {
        LOG_DEBUG("ChatbotV2", "Impl destroying...");
        cleanup();
        LOG_DEBUG("ChatbotV2", "Impl destroyed");
    }
    
    void cleanup() {
        // 停止所有延迟任务
        cancelDelayedTask();
        
        // 模块会通过智能指针自动析构，无需手动delete
    }
    
    // ========================================================================
    // 状态管理
    // ========================================================================
    
    void setState(ChatbotState new_state) {
        ChatbotState old_state = chatbot_state.exchange(new_state, std::memory_order_acq_rel);
        
        if (old_state != new_state) {
            LOG_INFO("ChatbotV2", "State: %s → %s",
                    stateToString(old_state).c_str(),
                    stateToString(new_state).c_str());
            
            // 触发状态回调
            invokeStateCallback(old_state, new_state);
        }
    }
    
    static std::string stateToString(ChatbotState state) {
        switch (state) {
            case ChatbotState::UNINITIALIZED:   return "UNINITIALIZED";
            case ChatbotState::INITIALIZED:     return "INITIALIZED";
            case ChatbotState::ACTIVATING:      return "ACTIVATING";
            case ChatbotState::ACTIVATED:       return "ACTIVATED";
            case ChatbotState::CONNECTING:      return "CONNECTING";
            case ChatbotState::CONNECTED:       return "CONNECTED";
            case ChatbotState::READY:           return "READY";
            case ChatbotState::ACTIVE:          return "ACTIVE";
            case ChatbotState::ERROR:           return "ERROR";
            case ChatbotState::SHUTDOWN:        return "SHUTDOWN";
            default:                            return "UNKNOWN";
        }
    }
    
    // ========================================================================
    // 模块化初始化（分阶段，失败自动回滚）
    // ========================================================================
    
    ChatbotError initializeDeviceInfo() {
        LOG_INFO("ChatbotV2", "Step 1: Initializing device info...");
        
        // 获取设备ID（MAC地址）
        if (config.device_id.empty()) {
            device_id = tool::getWirelessMacAddress();
            if (device_id.empty()) {
                LOG_ERROR("ChatbotV2", "Failed to get MAC address");
                return ChatbotError::INITIALIZATION_FAILED;
            }
        } else {
            device_id = config.device_id;
        }
        LOG_INFO("ChatbotV2", "  ✓ Device-Id: %s", device_id.c_str());
        
        // 获取客户端ID（UUID）
        if (config.client_id.empty()) {
            client_id = tool::generateUUID(config.config_file_path);
            if (client_id.empty()) {
                LOG_ERROR("ChatbotV2", "Failed to generate UUID");
                return ChatbotError::INITIALIZATION_FAILED;
            }
        } else {
            client_id = config.client_id;
        }
        LOG_INFO("ChatbotV2", "  ✓ Client-Id: %s", client_id.c_str());
        
        return ChatbotError::NONE;
    }
    
    ChatbotError initializeCoreModules() {
        LOG_INFO("ChatbotV2", "Step 2: Initializing core modules...");
        
        // 1. 创建协议处理器V2
        try {
            protocol_ns::ProtocolConfig proto_cfg;
            proto_cfg.enable_async_processing = true;
            proto_cfg.message_queue_size = 100;
            
            protocol_handler = std::make_unique<protocol_ns::ProtocolHandlerV2>(proto_cfg);
            LOG_INFO("ChatbotV2", "  ✓ Protocol Handler V2 created");
        } catch (const std::exception& e) {
            LOG_ERROR("ChatbotV2", "Failed to create protocol handler: %s", e.what());
            return ChatbotError::INITIALIZATION_FAILED;
        }
        
        // 2. 创建状态机V2
        try {
            statemachine_ns::StateMachineConfig sm_cfg;
            sm_cfg.enable_transition_validation = true;
            sm_cfg.enable_state_timeout = true;
            sm_cfg.enable_history_tracking = true;
            sm_cfg.tts_finish_delay_ms = 2000;
            
            state_machine = std::make_unique<statemachine_ns::AIStateMachineV2>(sm_cfg);
            LOG_INFO("ChatbotV2", "  ✓ State Machine V2 created");
        } catch (const std::exception& e) {
            LOG_ERROR("ChatbotV2", "Failed to create state machine: %s", e.what());
            return ChatbotError::INITIALIZATION_FAILED;
        }
        
        // 3. 创建MCP服务器V2
        try {
            mcp_ns::McpConfig mcp_cfg;
            mcp_cfg.server_name = "Smart_Glasses";
            mcp_cfg.server_version = "2.0.0";
            
            mcp_server = std::make_unique<mcp_ns::McpServerV2>(mcp_cfg);
            LOG_INFO("ChatbotV2", "  ✓ MCP Server V2 created");
            
            // 注册MCP工具
            if (config.enable_mcp_tools) {
                int tool_count = tool::McpToolManagerV2::register_all_tools(*mcp_server);
                LOG_INFO("ChatbotV2", "  ✓ MCP tools registered: %d", tool_count);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("ChatbotV2", "Failed to create MCP server: %s", e.what());
            return ChatbotError::INITIALIZATION_FAILED;
        }
        
        // 4. 创建激活管理器V2（如果启用）
        if (config.auto_activate) {
            try {
                activation_ns::ActivationConfig act_cfg;
                act_cfg.api_url = config.activation_api_url;
                act_cfg.poll_interval_sec = 5;
                act_cfg.poll_timeout_sec = config.activation_timeout_sec;
                act_cfg.verify_ssl = false;
                
                activation_manager = std::make_unique<activation_ns::DeviceActivationV2>(act_cfg);
                LOG_INFO("ChatbotV2", "  ✓ Activation Manager V2 created");
                    } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "Failed to create activation manager: %s", e.what());
                return ChatbotError::INITIALIZATION_FAILED;
            }
        }
        
        // 5. 创建唤醒词检测器V2（如果启用）
        if (config.enable_wakeword) {
            try {
                wakeword_ns::WakewordConfig ww_cfg;
                ww_cfg.resource_file = config.wakeword_resource_file;
                ww_cfg.model_file = config.wakeword_model_file;
                ww_cfg.sensitivity = config.wakeword_sensitivity;
                ww_cfg.audio_gain = config.wakeword_audio_gain;
                ww_cfg.apply_frontend = false;  // 已有3A算法
                
                wakeword_detector = std::make_unique<wakeword_ns::WakewordDetectorV2>(ww_cfg);
                
                wakeword_ns::WakewordError ww_err = wakeword_detector->initialize();
                if (ww_err == wakeword_ns::WakewordError::NONE) {
                    LOG_INFO("ChatbotV2", "  ✓ Wakeword Detector V2 initialized");
                    LOG_DEBUG("ChatbotV2", "    Sample rate: %d Hz", wakeword_detector->getSampleRate());
                    LOG_DEBUG("ChatbotV2", "    Channels: %d", wakeword_detector->getNumChannels());
                    LOG_DEBUG("ChatbotV2", "    Hotwords: %d", wakeword_detector->getNumHotwords());
            } else {
                    LOG_WARN("ChatbotV2", "  ⚠ Wakeword detector initialization failed");
                    LOG_WARN("ChatbotV2", "    Will continue without wakeword detection");
                    wakeword_detector.reset();  // 释放
                }
            } catch (const std::exception& e) {
                LOG_WARN("ChatbotV2", "Wakeword detector exception: %s", e.what());
                LOG_WARN("ChatbotV2", "  Will continue without wakeword detection");
                wakeword_detector.reset();
            }
        }
        
        return ChatbotError::NONE;
    }
    
    ChatbotError initializeCallbacks() {
        LOG_INFO("ChatbotV2", "Step 3: Setting up callbacks...");
        
        // 协议回调
        setupProtocolCallbacks();
        
        // 状态机回调
        setupStateMachineCallbacks();
        
        // 唤醒词回调（如果启用）
        if (wakeword_detector) {
            setupWakewordCallbacks();
        }
        
        // 激活回调（如果启用）
        if (activation_manager) {
            setupActivationCallbacks();
        }
        
        LOG_INFO("ChatbotV2", "  ✓ Callbacks configured");
        return ChatbotError::NONE;
    }
    
    ChatbotError initializeAudio() {
        LOG_INFO("ChatbotV2", "Step 4: Initializing audio integration...");
        
        if (!audio_system) {
            LOG_ERROR("ChatbotV2", "Audio system not provided");
            return ChatbotError::AUDIO_SYSTEM_ERROR;
        }
        
        // 设置音频回调（零拷贝，智能指针）
        audio_system->setAIAudioCallback([this](audio_ns::AudioFramePtr frame) {
            handleAIAudioFrame(frame);
        });
        
        LOG_INFO("ChatbotV2", "  ✓ AI audio callback set");
        
        // 设置唤醒词音频回调
        if (wakeword_detector) {
            audio_system->setWakewordCallback([this](const int16_t* data, size_t length) {
                handleWakewordAudio(data, length);
            });
            LOG_INFO("ChatbotV2", "  ✓ Wakeword audio callback set");
        }
        
        LOG_INFO("ChatbotV2", "  ✓ Audio integration complete");
        return ChatbotError::NONE;
    }
    
    // ========================================================================
    // 回调设置（各模块）
    // ========================================================================
    
    void setupProtocolCallbacks() {
        // Hello消息回调
        protocol_handler->setHelloCallback([this](const protocol_ns::HelloMessage& msg) {
            LOG_INFO("ChatbotV2", "← Hello received");
            LOG_DEBUG("ChatbotV2", "  Session ID: %s", msg.session_id.c_str());
            LOG_DEBUG("ChatbotV2", "  Audio: %dHz, %dch, %dms",
                     msg.audio_params.sample_rate,
                     msg.audio_params.channels,
                     msg.audio_params.frame_duration);
            
            // 保存session_id
            session_id = msg.session_id;
            protocol_handler->setSessionId(msg.session_id);
            
            // 触发状态机
            if (state_machine) {
                state_machine->onHello();
            }
            
            // 系统进入READY状态
            setState(ChatbotState::READY);
        });
        
        // STT消息回调
        protocol_handler->setSTTCallback([this](const protocol_ns::STTMessage& msg) {
            LOG_INFO("ChatbotV2", "← STT: \"%s\" (final: %s)",
                    msg.text.c_str(), msg.is_final ? "true" : "false");
            
            stats.stt_received.fetch_add(1, std::memory_order_relaxed);
            
            // 触发状态机
            if (state_machine) {
                state_machine->onSTT(msg.text, msg.is_final);
            }
            
            // 触发用户回调
            invokeSTTCallback(msg.text, msg.is_final);
        });
        
        // LLM消息回调
        protocol_handler->setLLMCallback([this](const protocol_ns::LLMMessage& msg) {
            LOG_INFO("ChatbotV2", "← LLM: \"%s\" (emotion: %s, final: %s)",
                    msg.text.c_str(),
                    protocol_ns::ProtocolHandlerV2::emotionTypeToString(msg.emotion).c_str(),
                    msg.is_final ? "true" : "false");
            
            stats.llm_received.fetch_add(1, std::memory_order_relaxed);
            
            // 触发状态机
            if (state_machine) {
                state_machine->onLLM(msg.text, msg.is_final);
            }
            
            // 触发用户回调
            invokeLLMCallback(msg.text, msg.is_final);
        });
        
        // TTS消息回调
        protocol_handler->setTTSCallback([this](const protocol_ns::TTSMessage& msg) {
            stats.tts_received.fetch_add(1, std::memory_order_relaxed);
            
            switch (msg.state) {
                case protocol_ns::TTSState::START:
                    LOG_INFO("ChatbotV2", "← TTS: START");
                    if (state_machine) {
                        state_machine->onTTS_start();
                    }
                    break;
                    
                case protocol_ns::TTSState::SENTENCE_START:
                    LOG_INFO("ChatbotV2", "← TTS: SENTENCE_START - \"%s\"", msg.text.c_str());
                    if (state_machine) {
                        state_machine->onTTS_sentenceStart(msg.text);
                    }
                    
                    // 重要：每个句子开始时发送listen请求
                    sendListenMessage(protocol_ns::ListenState::START, protocol_ns::ListenMode::AUTO);
                    break;
                    
                case protocol_ns::TTSState::STOP:
                    LOG_INFO("ChatbotV2", "← TTS: STOP");
                    if (state_machine) {
                        state_machine->onTTS_stop(2000);
                    }
                    
                    // ✅ 安全的延迟任务：TTS结束2秒后重新监听
                    scheduleDelayedListenRequest(2500);
                    break;
            }
        });
        
        // MCP消息回调
        protocol_handler->setMCPCallback([this](const std::string& mcp_payload) -> std::string {
            return handleMCPMessage(mcp_payload);
        });
        
        // 错误消息回调
        protocol_handler->setErrorCallback([this](const std::string& error) {
            LOG_ERROR("ChatbotV2", "← Protocol Error: %s", error.c_str());
            
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            
            if (state_machine) {
                state_machine->onError(error);
            }
            
            invokeErrorCallback(ChatbotError::NETWORK_ERROR, error);
        });
    }
    
    void setupStateMachineCallbacks() {
        // 状态变化回调
        state_machine->setStateChangeCallback([this](statemachine_ns::AIState old_state, 
                                                     statemachine_ns::AIState new_state) {
            LOG_INFO("ChatbotV2", "AI State: %s → %s",
                    statemachine_ns::AIStateMachineV2::stateToString(old_state).c_str(),
                    statemachine_ns::AIStateMachineV2::stateToString(new_state).c_str());
            
            // 根据AI状态更新Chatbot状态
            if (new_state == statemachine_ns::AIState::IDLE && 
                chatbot_state.load() == ChatbotState::READY) {
                // 保持READY状态
            } else if (new_state != statemachine_ns::AIState::IDLE) {
                // 对话进行中
                setState(ChatbotState::ACTIVE);
            }
        });
        
        // 音频上传控制回调
        state_machine->setAudioUploadCallback([this](bool enable) {
            if (enable) {
                LOG_DEBUG("ChatbotV2", "✅ Audio upload ENABLED");
                // 音频系统会自动上传
            } else {
                LOG_DEBUG("ChatbotV2", "❌ Audio upload DISABLED");
                
                // 如果回到IDLE状态，停止AI音频流
                statemachine_ns::AIState current = state_machine->getState();
                if (current == statemachine_ns::AIState::IDLE && audio_system) {
                    if (audio_system->isAIStreamActive()) {
                        LOG_INFO("ChatbotV2", "Back to IDLE, stopping AI audio stream");
                        audio_system->stopAIStream();
                    }
                }
            }
        });
    }
    
    void setupWakewordCallbacks() {
        // 唤醒词检测回调
        wakeword_detector->setWakewordCallback([this](wakeword_ns::WakewordResult result, 
                                                      int hotword_index) {
            (void)result;  // 未使用，避免警告
            LOG_INFO("ChatbotV2", "╔════════════════════════════════════════╗");
            LOG_INFO("ChatbotV2", "║  🎙️  唤醒词检测到！Hotword %d         ║", hotword_index);
            LOG_INFO("ChatbotV2", "╚════════════════════════════════════════╝");
            
            stats.wakeword_detected.fetch_add(1, std::memory_order_relaxed);
            
            // 检查是否在IDLE状态
            if (state_machine && state_machine->getState() != statemachine_ns::AIState::IDLE) {
                LOG_WARN("ChatbotV2", "Not in IDLE state, ignoring wakeword");
                return;
            }
            
            // 触发状态机
            if (state_machine) {
                state_machine->onWakewordDetected();
            }
            
            // 触发用户回调
            invokeWakewordCallback(hotword_index);
            
            // 自动开始监听
            autoStartListeningAfterWakeword();
        });
        
        // 唤醒词错误回调
        wakeword_detector->setErrorCallback([this](wakeword_ns::WakewordError error, 
                                                   const std::string& message) {
            (void)error;  // 未使用，避免警告
            LOG_ERROR("ChatbotV2", "Wakeword error: %s", message.c_str());
        });
    }
    
    void setupActivationCallbacks() {
        // 激活状态回调
        activation_manager->setStatusCallback([this](activation_ns::ActivationStatus status,
                                                     const activation_ns::ActivationResult& result) {
            LOG_INFO("ChatbotV2", "Activation Status: %s",
                    activation_ns::DeviceActivationV2::statusToString(status).c_str());
            
            if (status == activation_ns::ActivationStatus::ACTIVATED) {
                LOG_INFO("ChatbotV2", "✓ Device activated successfully!");
                setState(ChatbotState::ACTIVATED);
                
                // 触发用户回调
                invokeActivationCallback(true, "");
                
            } else if (status == activation_ns::ActivationStatus::NOT_ACTIVATED) {
                LOG_WARN("ChatbotV2", "⚠ Device not activated");
                LOG_INFO("ChatbotV2", "  Activation Code: %s", result.activation_code.c_str());
                LOG_INFO("ChatbotV2", "  Please activate at: https://xiaozhi.me");
                
                // 触发用户回调
                invokeActivationCallback(false, result.activation_code);
            }
        });
        
        // 激活进度回调
        activation_manager->setProgressCallback([this](int elapsed_sec, int total_sec) {
            if (elapsed_sec % 30 == 0) {  // 每30秒输出一次
                LOG_INFO("ChatbotV2", "Waiting for activation... (%d/%d sec)", 
                        elapsed_sec, total_sec);
            }
        });
    }
    
    void setupWebSocketCallbacks() {
        // 二进制消息回调（TTS音频）
        ws_client->setBinaryCallback([this](const char* data, size_t size) -> bool {
            LOG_DEBUG("ChatbotV2", "← Binary message: %zu bytes (TTS audio)", size);
            handleTTSAudio(reinterpret_cast<const uint8_t*>(data), size);
            return true;
        });
        
        // 文本消息回调（JSON协议）
        ws_client->setTextCallback([this](const char* data, size_t size) -> bool {
            LOG_DEBUG("ChatbotV2", "← Text message: %zu bytes", size);
            stats.messages_received.fetch_add(1, std::memory_order_relaxed);
            
            // 解析协议消息
            protocol_handler->parseMessage(data, size);
            return true;
        });
        
        // 连接状态回调
        ws_client->setStateCallback([this](websocket_ns::ConnectionState old_state,
                                           websocket_ns::ConnectionState new_state) {
            LOG_INFO("ChatbotV2", "WebSocket State: %s → %s",
                    wsStateToString(old_state).c_str(),
                    wsStateToString(new_state).c_str());
            
            if (new_state == websocket_ns::ConnectionState::HANDSHAKED) {
                setState(ChatbotState::CONNECTED);
            } else if (new_state == websocket_ns::ConnectionState::CLOSED ||
                      new_state == websocket_ns::ConnectionState::DISCONNECTED) {
                if (state_machine) {
                    state_machine->onWebSocketClosed();
                }
                
                if (chatbot_state.load() != ChatbotState::SHUTDOWN) {
                    setState(ChatbotState::ACTIVATED);  // 回到已激活状态
                }
            }
        });
        
        // WebSocket错误回调
        ws_client->setErrorCallback([this](websocket_ns::WebSocketError error,
                                           const std::string& message) {
            (void)error;  // 未使用，避免警告
            LOG_ERROR("ChatbotV2", "WebSocket error: %s", message.c_str());
            invokeErrorCallback(ChatbotError::NETWORK_ERROR, message);
        });
    }
    
    static std::string wsStateToString(websocket_ns::ConnectionState state) {
        switch (state) {
            case websocket_ns::ConnectionState::DISCONNECTED: return "DISCONNECTED";
            case websocket_ns::ConnectionState::CONNECTING:   return "CONNECTING";
            case websocket_ns::ConnectionState::CONNECTED:    return "CONNECTED";
            case websocket_ns::ConnectionState::HANDSHAKED:   return "HANDSHAKED";
            case websocket_ns::ConnectionState::CLOSING:      return "CLOSING";
            case websocket_ns::ConnectionState::CLOSED:       return "CLOSED";
            case websocket_ns::ConnectionState::ERROR:        return "ERROR";
            default:                                          return "UNKNOWN";
        }
}

// ========================================================================
    // 消息处理
// ========================================================================

    void handleTTSAudio(const uint8_t* data, size_t size) {
    if (!audio_system) {
            return;
        }
        
        // 解码Opus并添加到播放队列
        auto pcm_frame = audio_system->decodeOpus(data, size);
        
        if (pcm_frame) {
            // 添加到播放队列
            audio_system->pushPlaybackFrame(pcm_frame);
            
            // 如果未播放，启动播放
            if (!audio_system->isPlaying()) {
                audio_system->startPlayback();
            }
            
            // 触发用户回调
            invokeTTSCallback(data, size);
        } else {
            LOG_ERROR("ChatbotV2", "Failed to decode TTS audio");
        }
    }
    
    void handleAIAudioFrame(audio_ns::AudioFramePtr frame) {
        if (!frame || !ws_client) {
            return;
        }
        
        // 检查是否应该上传音频
        if (!state_machine || !state_machine->isAudioUploadEnabled()) {
            return;
        }
        
        if (!ws_client->isConnected()) {
            return;
        }
        
        // 发送Opus编码的音频数据
        websocket_ns::WebSocketError err = ws_client->sendBinary(
            reinterpret_cast<const char*>(frame->data), 
            frame->size
        );
        
        if (err == websocket_ns::WebSocketError::NONE) {
            stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
        } else {
            LOG_ERROR("ChatbotV2", "Failed to send audio frame");
        }
    }
    
    void handleWakewordAudio(const int16_t* data, size_t length) {
        if (!wakeword_detector || !wakeword_detector->isEnabled()) {
            return;
        }
        
        // 只在IDLE状态下检测唤醒词
        if (state_machine && state_machine->getState() != statemachine_ns::AIState::IDLE) {
            return;
        }
        
        // 处理音频帧
        wakeword_detector->processAudioFrame(data, length);
    }
    
    std::string handleMCPMessage(const std::string& mcp_payload) {
        if (!mcp_server) {
            LOG_ERROR("ChatbotV2", "MCP server not initialized");
            return "";
        }
        
        LOG_INFO("ChatbotV2", "← MCP message received");
        stats.mcp_calls.fetch_add(1, std::memory_order_relaxed);
        
        // 处理MCP消息
        std::string response = mcp_server->handle_message(mcp_payload);
        
        if (!response.empty() && ws_client && ws_client->isConnected()) {
            // 封装为完整的MCP响应消息
            try {
                json full_msg;
                full_msg["session_id"] = session_id;
                full_msg["type"] = "mcp";
                full_msg["payload"] = json::parse(response);
                
                std::string full_response = full_msg.dump();
                
                LOG_INFO("ChatbotV2", "→ Sending MCP response");
                ws_client->sendText(full_response);
                stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
                
            } catch (const json::exception& e) {
                LOG_ERROR("ChatbotV2", "MCP response build error: %s", e.what());
            }
        }
        
        return response;
    }
    
    // ========================================================================
    // 唤醒词后自动开始监听
    // ========================================================================
    
    void autoStartListeningAfterWakeword() {
        LOG_INFO("ChatbotV2", "→ Auto starting listening after wakeword...");
        
        // 检查状态
        statemachine_ns::AIState ai_state = state_machine->getState();
        if (ai_state != statemachine_ns::AIState::IDLE) {
            LOG_WARN("ChatbotV2", "Cannot start listening, AI state: %s",
                    statemachine_ns::AIStateMachineV2::stateToString(ai_state).c_str());
            return;
        }
        
        // 1. 启动AI音频流
        if (audio_system) {
            audio_ns::AudioError err = audio_system->startAIStream();
            if (err != audio_ns::AudioError::NONE) {
                LOG_ERROR("ChatbotV2", "Failed to start AI audio stream");
                return;
            }
        }
        
        // 2. 发送listen消息
        if (!sendListenMessage(protocol_ns::ListenState::START, protocol_ns::ListenMode::AUTO)) {
            LOG_ERROR("ChatbotV2", "Failed to send listen message");
            if (audio_system) {
                audio_system->stopAIStream();
            }
            return;
        }
        
        // 3. 触发状态机
        state_machine->onListenStart();
        stats.conversations.fetch_add(1, std::memory_order_relaxed);
        
        LOG_INFO("ChatbotV2", "✓ Listening started automatically");
}

// ========================================================================
    // 延迟任务（可中断，无detached线程）
// ========================================================================

    void scheduleDelayedListenRequest(int delay_ms) {
        // 取消旧的延迟任务
        cancelDelayedTask();
        
        should_cancel_delay.store(false, std::memory_order_release);
        
        // 创建新的延迟线程（智能指针管理，可join）
        delay_thread = std::make_unique<std::thread>([this, delay_ms]() {
            std::unique_lock<std::mutex> lock(delay_mutex);
            
            LOG_DEBUG("ChatbotV2", "Delay task started: %dms", delay_ms);
            
            // ✅ 可中断的等待
            bool cancelled = delay_cv.wait_for(lock, std::chrono::milliseconds(delay_ms),
                [this]() { return should_cancel_delay.load(std::memory_order_acquire); });
            
            if (cancelled) {
                LOG_DEBUG("ChatbotV2", "Delay task cancelled");
                return;
            }
            
            // 双重检查
            if (should_cancel_delay.load(std::memory_order_acquire)) {
                LOG_DEBUG("ChatbotV2", "Delay task cancelled (double check)");
                return;
            }
            
            // 检查WebSocket是否还连接
            if (!ws_client || !ws_client->isConnected()) {
                LOG_WARN("ChatbotV2", "WebSocket disconnected, skip delayed listen");
                return;
            }
            
            LOG_INFO("ChatbotV2", "TTS finished, sending listen request...");
            
            // 发送listen消息
            sendListenMessage(protocol::ListenState::START, protocol::ListenMode::AUTO);
            
            LOG_DEBUG("ChatbotV2", "✓ Delayed listen request sent");
        });
    }
    
    void cancelDelayedTask() {
        should_cancel_delay.store(true, std::memory_order_release);
        delay_cv.notify_all();
        
        if (delay_thread && delay_thread->joinable()) {
            delay_thread->join();  // ✅ 安全的join，不是detach
        }
        delay_thread.reset();
    }
    
    // ========================================================================
    // WebSocket消息发送辅助函数
    // ========================================================================
    
    bool sendListenMessage(protocol_ns::ListenState state, protocol_ns::ListenMode mode) {
        if (!ws_client || !ws_client->isConnected()) {
            LOG_ERROR("ChatbotV2", "WebSocket not connected");
            return false;
        }
        
        std::string listen_msg = protocol_handler->generateListenMessage(state, mode);
        
        websocket_ns::WebSocketError err = ws_client->sendText(listen_msg);
        if (err != websocket_ns::WebSocketError::NONE) {
            LOG_ERROR("ChatbotV2", "Failed to send listen message");
            return false;
        }
        
        stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
        return true;
}

// ========================================================================
    // 回调调用（异常安全+线程安全）
// ========================================================================

    void invokeSTTCallback(const std::string& text, bool is_final) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (stt_callback) {
            try {
                stt_callback(text, is_final);
            } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "STT callback exception: %s", e.what());
            }
        }
    }
    
    void invokeLLMCallback(const std::string& text, bool is_final) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (llm_callback) {
            try {
                llm_callback(text, is_final);
            } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "LLM callback exception: %s", e.what());
            }
        }
    }
    
    void invokeTTSCallback(const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (tts_callback) {
            try {
                tts_callback(data, size);
            } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "TTS callback exception: %s", e.what());
            }
        }
    }
    
    void invokeStateCallback(ChatbotState old_state, ChatbotState new_state) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (state_callback) {
            try {
                state_callback(old_state, new_state);
            } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "State callback exception: %s", e.what());
            }
        }
    }
    
    void invokeErrorCallback(ChatbotError error, const std::string& message) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (error_callback) {
            try {
                error_callback(error, message);
            } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "Error callback exception: %s", e.what());
            }
        }
    }
    
    void invokeWakewordCallback(int hotword_index) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (wakeword_callback) {
            try {
                wakeword_callback(hotword_index);
            } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "Wakeword callback exception: %s", e.what());
            }
        }
    }
    
    void invokeActivationCallback(bool is_activated, const std::string& activation_code) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (activation_callback) {
            try {
                activation_callback(is_activated, activation_code);
            } catch (const std::exception& e) {
                LOG_ERROR("ChatbotV2", "Activation callback exception: %s", e.what());
            }
        }
    }
};

// ============================================================================
// ChatbotSystemV2 公共接口实现
// ============================================================================

ChatbotSystemV2::ChatbotSystemV2(const ChatbotConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_INFO("ChatbotV2", "========================================");
    LOG_INFO("ChatbotV2", "  ChatbotSystemV2 Created");
    LOG_INFO("ChatbotV2", "========================================");
}

ChatbotSystemV2::~ChatbotSystemV2() {
    LOG_INFO("ChatbotV2", "ChatbotSystemV2 destroying...");
    
    // 输出统计
    logAllStats();
    
    // RAII自动清理
    LOG_INFO("ChatbotV2", "ChatbotSystemV2 destroyed");
}

// ========================================================================
// 初始化和关闭
// ========================================================================

ChatbotError ChatbotSystemV2::initialize(std::shared_ptr<audio_ns::AudioSystemV2> audio_system) {
    LOG_INFO("ChatbotV2", "========================================");
    LOG_INFO("ChatbotV2", "  Initializing ChatbotSystemV2...");
    LOG_INFO("ChatbotV2", "========================================");
    
    if (!audio_system) {
        LOG_ERROR("ChatbotV2", "Audio system is null");
        return ChatbotError::AUDIO_SYSTEM_ERROR;
    }
    
    pImpl_->audio_system = audio_system;
    
    // 分阶段初始化
    ChatbotError err;
    
    // 阶段1：设备信息
    err = pImpl_->initializeDeviceInfo();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("ChatbotV2", "✗ Device info initialization failed");
        return err;
    }
    
    // 阶段2：核心模块
    err = pImpl_->initializeCoreModules();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("ChatbotV2", "✗ Core modules initialization failed");
        return err;
    }
    
    // 阶段3：回调
    err = pImpl_->initializeCallbacks();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("ChatbotV2", "✗ Callbacks initialization failed");
        return err;
    }
    
    // 阶段4：音频集成
    err = pImpl_->initializeAudio();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("ChatbotV2", "✗ Audio integration failed");
        return err;
    }
    
    pImpl_->setState(ChatbotState::INITIALIZED);
    
    LOG_INFO("ChatbotV2", "========================================");
    LOG_INFO("ChatbotV2", "  ✓ ChatbotSystemV2 Initialized!");
    LOG_INFO("ChatbotV2", "========================================");
    
    return ChatbotError::NONE;
}

ChatbotError ChatbotSystemV2::start() {
    ChatbotState current = pImpl_->chatbot_state.load(std::memory_order_acquire);
    
    if (current != ChatbotState::INITIALIZED && current != ChatbotState::ACTIVATED) {
        LOG_ERROR("ChatbotV2", "Cannot start, invalid state: %s",
                 Impl::stateToString(current).c_str());
        return ChatbotError::INVALID_STATE;
    }
    
    LOG_INFO("ChatbotV2", "========================================");
    LOG_INFO("ChatbotV2", "  Starting ChatbotSystemV2...");
    LOG_INFO("ChatbotV2", "========================================");
    
    // 步骤1：检查激活状态（如果启用）
    if (pImpl_->config.auto_activate && pImpl_->activation_manager) {
        LOG_INFO("ChatbotV2", "Step 1: Checking activation status...");
        pImpl_->setState(ChatbotState::ACTIVATING);
        
        activation_ns::ActivationResult result = pImpl_->activation_manager->checkActivation(
            pImpl_->device_id, 
            pImpl_->client_id
        );
        
        if (!result.isActivated()) {
            LOG_WARN("ChatbotV2", "✗ Device not activated");
            LOG_INFO("ChatbotV2", "  Activation Code: %s", result.activation_code.c_str());
            LOG_INFO("ChatbotV2", "  Please visit: https://xiaozhi.me");
            
            pImpl_->invokeActivationCallback(false, result.activation_code);
            
            // 启动异步轮询
            if (pImpl_->activation_manager->startPolling(
                pImpl_->device_id, 
                pImpl_->client_id, 
                pImpl_->config.activation_timeout_sec)) {
                LOG_INFO("ChatbotV2", "  ⏳ Activation polling started...");
            }
            
            // 注意：即使未激活，也继续启动（方便测试）
            // 实际产品可以在这里返回错误
        } else {
            LOG_INFO("ChatbotV2", "  ✓ Device already activated!");
            pImpl_->setState(ChatbotState::ACTIVATED);
        }
    } else {
        pImpl_->setState(ChatbotState::ACTIVATED);
    }
    
    // 步骤2：创建并配置WebSocket客户端
    LOG_INFO("ChatbotV2", "Step 2: Creating WebSocket client...");
    
    try {
        // 生成Hello消息
        std::string hello_msg = pImpl_->protocol_handler->generateHelloMessage(
            48000,  // 48kHz
            1,      // Mono
            20      // 20ms frame
        );
        
        // 创建WebSocket客户端V2（工厂函数返回智能指针）
        pImpl_->ws_client = websocket_ns::createXiaozhiClientV2(
            pImpl_->device_id,
            pImpl_->client_id,
            nullptr,  // binary_cb在setupWebSocketCallbacks中设置
            nullptr   // text_cb在setupWebSocketCallbacks中设置
        );
        
        if (!pImpl_->ws_client) {
            LOG_ERROR("ChatbotV2", "✗ Failed to create WebSocket client");
            pImpl_->setState(ChatbotState::ERROR);
            return ChatbotError::CONNECTION_FAILED;
        }
        
        // 设置Hello消息
        pImpl_->ws_client->setHelloMessage(hello_msg);
        
        // 设置WebSocket回调
        pImpl_->setupWebSocketCallbacks();
        
        LOG_INFO("ChatbotV2", "  ✓ WebSocket client created");
        
    } catch (const std::exception& e) {
        LOG_ERROR("ChatbotV2", "WebSocket creation exception: %s", e.what());
        pImpl_->setState(ChatbotState::ERROR);
        return ChatbotError::CONNECTION_FAILED;
    }
    
    // 步骤3：连接AI服务器（如果启用）
    if (pImpl_->config.auto_connect) {
        LOG_INFO("ChatbotV2", "Step 3: Connecting to AI server...");
        pImpl_->setState(ChatbotState::CONNECTING);
        
        websocket_ns::WebSocketError ws_err = pImpl_->ws_client->connect();
        if (ws_err != websocket_ns::WebSocketError::NONE) {
            LOG_ERROR("ChatbotV2", "✗ Failed to connect WebSocket");
            pImpl_->setState(ChatbotState::ERROR);
            return ChatbotError::CONNECTION_FAILED;
        }
        
        // 等待连接和握手完成（最多等待10秒）
        int wait_count = 0;
        while (wait_count < 100) {
            if (pImpl_->ws_client->isHandshaked()) {
                LOG_INFO("ChatbotV2", "  ✓ WebSocket connected and handshaked!");
                pImpl_->setState(ChatbotState::CONNECTED);
                
                // 等待Hello响应，状态机会自动设置为IDLE
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                // 进入READY状态
                pImpl_->setState(ChatbotState::READY);
                
                LOG_INFO("ChatbotV2", "========================================");
                LOG_INFO("ChatbotV2", "  ✓ ChatbotSystemV2 Ready!");
                LOG_INFO("ChatbotV2", "  ✓ Waiting for wakeword...");
                LOG_INFO("ChatbotV2", "========================================");
                
                return ChatbotError::NONE;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_count++;
        }
        
        LOG_ERROR("ChatbotV2", "✗ Connection timeout");
        pImpl_->setState(ChatbotState::ERROR);
        return ChatbotError::TIMEOUT;
    }
    
    LOG_INFO("ChatbotV2", "========================================");
    LOG_INFO("ChatbotV2", "  ✓ ChatbotSystemV2 Started!");
    LOG_INFO("ChatbotV2", "========================================");
    
    return ChatbotError::NONE;
}

void ChatbotSystemV2::stop() {
    LOG_INFO("ChatbotV2", "Stopping ChatbotSystemV2...");
    
    // 取消所有延迟任务
    pImpl_->cancelDelayedTask();
    
    // 停止音频流
    if (pImpl_->audio_system && pImpl_->audio_system->isAIStreamActive()) {
        pImpl_->audio_system->stopAIStream();
    }
    
    // 断开WebSocket
    if (pImpl_->ws_client) {
        pImpl_->ws_client->disconnect();
    }
    
    // 停止激活轮询
    if (pImpl_->activation_manager && pImpl_->activation_manager->isPolling()) {
        pImpl_->activation_manager->stopPolling();
    }
    
    // 重置状态机
    if (pImpl_->state_machine) {
        pImpl_->state_machine->reset();
    }
    
    pImpl_->setState(ChatbotState::INITIALIZED);
    
    LOG_INFO("ChatbotV2", "✓ ChatbotSystemV2 stopped");
}

void ChatbotSystemV2::shutdown() {
    LOG_INFO("ChatbotV2", "Shutting down ChatbotSystemV2...");
    
    stop();
    
    pImpl_->setState(ChatbotState::SHUTDOWN);
    
    LOG_INFO("ChatbotV2", "✓ ChatbotSystemV2 shutdown complete");
}

// ========================================================================
// AI交互控制
// ========================================================================

ChatbotError ChatbotSystemV2::startListening(const std::string& mode) {
    ChatbotState current = pImpl_->chatbot_state.load(std::memory_order_acquire);
    
    if (current != ChatbotState::READY && current != ChatbotState::ACTIVE) {
        LOG_ERROR("ChatbotV2", "Cannot start listening, invalid state: %s",
                 Impl::stateToString(current).c_str());
        return ChatbotError::INVALID_STATE;
    }
    
    LOG_INFO("ChatbotV2", "Starting listening (mode: %s)...", mode.c_str());
    
    // 启动AI音频流
    if (pImpl_->audio_system) {
        audio_ns::AudioError err = pImpl_->audio_system->startAIStream();
        if (err != audio_ns::AudioError::NONE) {
            LOG_ERROR("ChatbotV2", "Failed to start AI audio stream");
            return ChatbotError::AUDIO_SYSTEM_ERROR;
        }
    }
    
    // 发送listen消息
    protocol_ns::ListenMode listen_mode = protocol_ns::ListenMode::AUTO;
    if (mode == "manual") {
        listen_mode = protocol_ns::ListenMode::MANUAL;
    } else if (mode == "realtime") {
        listen_mode = protocol_ns::ListenMode::REALTIME;
    }
    
    if (!pImpl_->sendListenMessage(protocol_ns::ListenState::START, listen_mode)) {
        if (pImpl_->audio_system) {
            pImpl_->audio_system->stopAIStream();
        }
        return ChatbotError::NETWORK_ERROR;
    }
    
    // 触发状态机
    if (pImpl_->state_machine) {
        pImpl_->state_machine->onListenStart();
    }
    
    pImpl_->stats.conversations.fetch_add(1, std::memory_order_relaxed);
    pImpl_->setState(ChatbotState::ACTIVE);
    
    LOG_INFO("ChatbotV2", "✓ Listening started");
    return ChatbotError::NONE;
}

ChatbotError ChatbotSystemV2::stopListening() {
    LOG_INFO("ChatbotV2", "Stopping listening...");
    
    // 停止音频流
    if (pImpl_->audio_system && pImpl_->audio_system->isAIStreamActive()) {
        pImpl_->audio_system->stopAIStream();
    }
    
    // 发送listen stop消息
    pImpl_->sendListenMessage(protocol_ns::ListenState::STOP, protocol_ns::ListenMode::AUTO);
    
    // 重置状态机
    if (pImpl_->state_machine) {
        pImpl_->state_machine->reset();
    }
    
    LOG_INFO("ChatbotV2", "✓ Listening stopped");
    return ChatbotError::NONE;
}

ChatbotError ChatbotSystemV2::sendTextMessage(const std::string& text) {
    if (!pImpl_->ws_client || !pImpl_->ws_client->isConnected()) {
        LOG_ERROR("ChatbotV2", "WebSocket not connected");
        return ChatbotError::CONNECTION_FAILED;
    }
    
    websocket_ns::WebSocketError err = pImpl_->ws_client->sendText(text);
    if (err != websocket_ns::WebSocketError::NONE) {
        LOG_ERROR("ChatbotV2", "Failed to send text message");
        return ChatbotError::NETWORK_ERROR;
    }
    
    pImpl_->stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
    return ChatbotError::NONE;
}

void ChatbotSystemV2::triggerWakeword() {
    LOG_INFO("ChatbotV2", "Manual wakeword trigger");
    
    if (pImpl_->wakeword_detector) {
        pImpl_->stats.wakeword_detected.fetch_add(1, std::memory_order_relaxed);
        
        if (pImpl_->state_machine) {
            pImpl_->state_machine->onWakewordDetected();
        }
        
        pImpl_->invokeWakewordCallback(1);
        pImpl_->autoStartListeningAfterWakeword();
    }
}

// ========================================================================
// 状态查询
// ========================================================================

ChatbotState ChatbotSystemV2::getState() const {
    return pImpl_->chatbot_state.load(std::memory_order_acquire);
}

bool ChatbotSystemV2::isReady() const {
    return pImpl_->chatbot_state.load() == ChatbotState::READY;
}

bool ChatbotSystemV2::isActivated() const {
    ChatbotState state = pImpl_->chatbot_state.load();
    return (state == ChatbotState::ACTIVATED || 
            state == ChatbotState::CONNECTING ||
            state == ChatbotState::CONNECTED ||
            state == ChatbotState::READY ||
            state == ChatbotState::ACTIVE);
}

bool ChatbotSystemV2::isConnected() const {
    return pImpl_->ws_client && pImpl_->ws_client->isConnected();
}

std::string ChatbotSystemV2::getSessionId() const {
    return pImpl_->session_id;
}

std::string ChatbotSystemV2::getDeviceId() const {
    return pImpl_->device_id;
}

std::string ChatbotSystemV2::getClientId() const {
    return pImpl_->client_id;
}

// ========================================================================
// MCP工具注册（未来扩展）
// ========================================================================

size_t ChatbotSystemV2::getMCPToolCount() const {
    return pImpl_->mcp_server ? pImpl_->mcp_server->tool_count() : 0;
}

// ========================================================================
// 回调设置
// ========================================================================

void ChatbotSystemV2::setSTTCallback(STTTextCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->stt_callback = std::move(callback);
}

void ChatbotSystemV2::setLLMCallback(LLMTextCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->llm_callback = std::move(callback);
}

void ChatbotSystemV2::setTTSCallback(TTSAudioCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->tts_callback = std::move(callback);
}

void ChatbotSystemV2::setStateCallback(ChatbotStateCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->state_callback = std::move(callback);
}

void ChatbotSystemV2::setErrorCallback(ChatbotErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->error_callback = std::move(callback);
}

void ChatbotSystemV2::setWakewordCallback(WakewordDetectedCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->wakeword_callback = std::move(callback);
}

void ChatbotSystemV2::setActivationCallback(ActivationStateCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->activation_callback = std::move(callback);
}

// ========================================================================
// 统计信息
// ========================================================================

void ChatbotSystemV2::getStats(Stats& out_stats) const {
    out_stats.messages_sent.store(pImpl_->stats.messages_sent.load());
    out_stats.messages_received.store(pImpl_->stats.messages_received.load());
    out_stats.wakeword_detected.store(pImpl_->stats.wakeword_detected.load());
    out_stats.conversations.store(pImpl_->stats.conversations.load());
    out_stats.stt_received.store(pImpl_->stats.stt_received.load());
    out_stats.llm_received.store(pImpl_->stats.llm_received.load());
    out_stats.tts_received.store(pImpl_->stats.tts_received.load());
    out_stats.mcp_calls.store(pImpl_->stats.mcp_calls.load());
    out_stats.errors.store(pImpl_->stats.errors.load());
    
    // 计算运行时间
    uint64_t uptime = get_nowus() - pImpl_->system_start_time;
    out_stats.total_uptime_us.store(uptime);
}

void ChatbotSystemV2::resetStats() {
    pImpl_->stats.messages_sent.store(0);
    pImpl_->stats.messages_received.store(0);
    pImpl_->stats.wakeword_detected.store(0);
    pImpl_->stats.conversations.store(0);
    pImpl_->stats.stt_received.store(0);
    pImpl_->stats.llm_received.store(0);
    pImpl_->stats.tts_received.store(0);
    pImpl_->stats.mcp_calls.store(0);
    pImpl_->stats.errors.store(0);
    pImpl_->stats.total_uptime_us.store(0);
    
    LOG_INFO("ChatbotV2", "Stats reset");
}

void ChatbotSystemV2::logStats() const {
    uint64_t uptime = get_nowus() - pImpl_->system_start_time;
    
    LOG_INFO("ChatbotV2", "========================================");
    LOG_INFO("ChatbotV2", "  ChatbotSystemV2 Statistics");
    LOG_INFO("ChatbotV2", "========================================");
    LOG_INFO("ChatbotV2", "  Messages sent:       %llu", pImpl_->stats.messages_sent.load());
    LOG_INFO("ChatbotV2", "  Messages received:   %llu", pImpl_->stats.messages_received.load());
    LOG_INFO("ChatbotV2", "  Wakeword detected:   %llu", pImpl_->stats.wakeword_detected.load());
    LOG_INFO("ChatbotV2", "  Conversations:       %llu", pImpl_->stats.conversations.load());
    LOG_INFO("ChatbotV2", "  STT received:        %llu", pImpl_->stats.stt_received.load());
    LOG_INFO("ChatbotV2", "  LLM received:        %llu", pImpl_->stats.llm_received.load());
    LOG_INFO("ChatbotV2", "  TTS received:        %llu", pImpl_->stats.tts_received.load());
    LOG_INFO("ChatbotV2", "  MCP calls:           %llu", pImpl_->stats.mcp_calls.load());
    LOG_INFO("ChatbotV2", "  Errors:              %llu", pImpl_->stats.errors.load());
    LOG_INFO("ChatbotV2", "  System uptime:       %.2f hours", uptime / (1000000.0 * 3600.0));
    LOG_INFO("ChatbotV2", "========================================");
}

void ChatbotSystemV2::logAllStats() const {
    // Chatbot主统计
    logStats();
    
    // 各模块统计
    if (pImpl_->protocol_handler) {
        pImpl_->protocol_handler->logStats();
    }
    
    if (pImpl_->state_machine) {
        pImpl_->state_machine->logStats();
    }
    
    if (pImpl_->mcp_server) {
        pImpl_->mcp_server->logStats();
    }
    
    if (pImpl_->activation_manager) {
        pImpl_->activation_manager->logStats();
    }
    
    if (pImpl_->ws_client) {
        pImpl_->ws_client->logStats();
    }
    
    if (pImpl_->audio_system) {
        pImpl_->audio_system->logStats();
    }
}

} // namespace chatbot
} // namespace glasses

