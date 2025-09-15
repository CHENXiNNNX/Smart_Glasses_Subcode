#include "chatbot.h"
#include <chrono>
#include <thread>
#include <memory>

// ==================== ChatbotMsgHandler 实现 ====================

ChatbotMsgHandler::ChatbotMsgHandler(Chatbot* chatbot) : chatbot_(chatbot) {}

void ChatbotMsgHandler::handleMessage(const std::string& message, bool is_binary) {
    if (!is_binary) {
        // 处理JSON消息
        Json::Value root;
        Json::Reader reader;
        
        bool parsingSuccessful = reader.parse(message, root);
        if (!parsingSuccessful) {
            USER_LOG_WARN("Error parsing message: %s", reader.getFormattedErrorMessages().c_str());
            chatbot_->enqueueEvent(AppEvent::fault_happen);
            return;
        }
        
        USER_LOG_INFO("Received JSON message: %s", message.c_str());
        
        // 获取消息类型
        const Json::Value type = root["type"];
        if (type.isString()) {
            std::string typeStr = type.asString();
            if (typeStr == "vad") {
                handleVadMessage(root);
            } else if (typeStr == "asr") {
                handleAsrMessage(root);
            } else if (typeStr == "chat") {
                handleChatMessage(root);
            } else if (typeStr == "tts") {
                handleTtsMessage(root);
            } else if (typeStr == "error") {
                handleErrorMessage(root);
            } else {
                USER_LOG_WARN("Unknown message type: %s", typeStr.c_str());
            }
        }
        
        // 处理意图消息
        if (root.isMember("function_call") && root["function_call"].isObject()) {
            handleIntentMessage(root);
            chatbot_->enqueueIntent(root);
        }
        
    } else {
        // 处理二进制音频消息
        handleBinaryMessage(message);
    }
}

void ChatbotMsgHandler::handleVadMessage(const Json::Value& root) {
    const Json::Value state = root["state"];
    if (state.isString()) {
        std::string stateStr = state.asString();
        if (stateStr == "no_speech") {
            chatbot_->enqueueEvent(AppEvent::vad_no_speech);
        }
    }
}

void ChatbotMsgHandler::handleAsrMessage(const Json::Value& root) {
    const Json::Value text = root["text"];
    if (text.isString()) {
        std::string asr_text = text.asString();
        USER_LOG_INFO("Received ASR text: %s", asr_text.c_str());
    } else {
        USER_LOG_WARN("Invalid ASR text value.");
    }
    chatbot_->enqueueEvent(AppEvent::asr_received);
}

void ChatbotMsgHandler::handleChatMessage(const Json::Value& root) {
    const Json::Value dialogue = root["dialogue"];
    if (dialogue.isString()) {
        std::string dialogueStr = dialogue.asString();
        if (dialogueStr == "end") {
            USER_LOG_INFO("Received dialogue end.");
            chatbot_->set_dialogue_completed(true);
        }
    }
}

void ChatbotMsgHandler::handleTtsMessage(const Json::Value& root) {
    const Json::Value state = root["state"];
    if (state.isString()) {
        std::string stateStr = state.asString();
        if (stateStr == "end") {
            USER_LOG_INFO("Received TTS end.");
            chatbot_->set_tts_completed(true);
        }
    }
}

void ChatbotMsgHandler::handleIntentMessage(const Json::Value& root) {
    const Json::Value function_call = root["function_call"];
    if (function_call.isMember("name") && function_call["name"].isString() &&
        function_call.isMember("arguments") && function_call["arguments"].isObject()) {
        IntentHandler::HandleIntent(root);
    } else {
        USER_LOG_ERROR("Invalid function_call structure in JSON: %s", root.toStyledString().c_str());
    }
}

void ChatbotMsgHandler::handleBinaryMessage(const std::string& message) {
    // 首次接收到二进制消息
    if (chatbot_->get_first_audio_msg_received() == true) {
        chatbot_->set_first_audio_msg_received(false);
        chatbot_->enqueueEvent(AppEvent::speaking_msg_received);
    }
    
    BinProtocolInfo protocol_info;
    std::vector<uint8_t> opus_data;
    
    // 解包二进制数据
    if (chatbot_->unpackBinaryFrame(
        std::vector<uint8_t>(message.begin(), message.end()), 
        opus_data, protocol_info)) {
        
        // 检查版本和类型
        if (protocol_info.version == chatbot_->get_ws_protocolVersion() && protocol_info.type == 0) {
            // 解码Opus数据并加入播放队列
            std::vector<int16_t> pcm_data;
            if (chatbot_->decodeOpus(opus_data, pcm_data)) {
                chatbot_->addFrameToPlaybackQueue(pcm_data);
            }
        } else {
            USER_LOG_WARN("Received frame with unexpected version or type");
        }
    } else {
        USER_LOG_WARN("Failed to unpack binary frame");
    }
}

void ChatbotMsgHandler::handleErrorMessage(const Json::Value& root) {
    USER_LOG_ERROR("Server error message: %s", root.toStyledString().c_str());
    chatbot_->enqueueEvent(AppEvent::fault_happen);
}

// ==================== Chatbot 主类实现 ====================

Chatbot::Chatbot(const std::string& address, int port, const std::string& token, 
                 const std::string& deviceId, const std::string& aliyun_api_key, 
                 int protocolVersion, int sample_rate, int channels, int frame_duration)
    : audio_system_(),
      client_state_(static_cast<int>(ChatbotState::startup)),
      eventQueue_(),
      intentQueue_(),
      ws_client_(address, port, token, deviceId, protocolVersion),
      intent_handler_(),
      aliyun_api_key_(aliyun_api_key),
      ws_protocolVersion_(protocolVersion),
      sample_rate_(sample_rate),
      channels_(channels),
      frame_duration_(frame_duration),
      threads_stop_flag_(false),
      state_trans_thread_(),
      msg_handler_(std::make_unique<ChatbotMsgHandler>(this)),
      snowboy_detector_(nullptr),
      wakeword_detection_running_(false),
      wakeword_detection_thread_() {
    
    USER_LOG_INFO("Initializing Chatbot with server %s:%d", address.c_str(), port);
    
    // 初始化音频系统
    if (!initAudioSystem()) {
        USER_LOG_ERROR("Failed to initialize audio system");
        throw std::runtime_error("Audio system initialization failed");
    }
    
    // 初始化Snowboy唤醒检测器
    if (!initSnowboyDetector()) {
        USER_LOG_ERROR("Failed to initialize Snowboy detector");
        throw std::runtime_error("Snowboy detector initialization failed");
    }
    
    // 设置WebSocket回调
    setupWebSocketCallbacks();
    
    USER_LOG_INFO("Chatbot initialized successfully");
}

Chatbot::~Chatbot() {
    USER_LOG_INFO("Destroying Chatbot...");
    
    // 停止所有线程
    threads_stop_flag_.store(true);
    
    // 停止唤醒检测
    stopWakewordDetection();
    
    // 等待线程结束
    if (state_trans_thread_.joinable()) {
        state_trans_thread_.join();
    }
    
    // 断开WebSocket连接
    if (ws_client_.IsConnected()) {
        ws_client_.Close();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 释放Snowboy检测器
    deinitSnowboyDetector();
    
    // 释放音频系统
    deinitAudioSystem();
    
    USER_LOG_WARN("Chatbot destroyed.");
}

void Chatbot::Run() {
    USER_LOG_INFO("Starting Chatbot...");
    
    // 配置状态机
    configureStateMachine();
    
    // 连接WebSocket服务器
    connectWebSocket();
    
    // 启动状态机事件处理线程
    state_trans_thread_ = std::thread([this]() {
        stateEventLoop();
    });
    
    // 等待状态机线程结束
    state_trans_thread_.join();
    
    // 关闭WebSocket连接
    if (ws_client_.IsConnected()) {
        ws_client_.Close();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    USER_LOG_WARN("Chatbot run ended");
}

void Chatbot::Stop() {
    USER_LOG_INFO("Stopping Chatbot...");
    enqueueEvent(AppEvent::to_stop);
}

// ==================== 私有方法实现 ====================

bool Chatbot::initAudioSystem() {
    USER_LOG_INFO("Initializing audio system...");
    
    // 设置音频参数
    audio_system_.sample_rate = sample_rate_;
    audio_system_.channels = channels_;
    audio_system_.frame_duration_ms = frame_duration_;
    
    // 初始化音频系统
    audio_error_t result = audio_system_init(&audio_system_);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to initialize audio system: %d", result);
        return false;
    }
    
    // 设置音频模式为AI模式
    result = set_audio_mode(&audio_system_, AUDIO_MODE_AI);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to set audio mode: %d", result);
        return false;
    }
    
    // 初始化Opus编解码器
    result = init_opus_codec(&audio_system_);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to initialize Opus codec: %d", result);
        return false;
    }
    
    USER_LOG_INFO("Audio system initialized successfully");
    return true;
}

void Chatbot::deinitAudioSystem() {
    USER_LOG_INFO("Deinitializing audio system...");
    
    // 停止音频流
    stop_recording(&audio_system_);
    stop_playback(&audio_system_);
    
    // 释放Opus编解码器
    release_opus_codec(&audio_system_);
    
    // 释放音频系统
    audio_system_deinit(&audio_system_);
    
    USER_LOG_INFO("Audio system deinitialized");
}

void Chatbot::configureStateMachine() {
    USER_LOG_INFO("Configuring state machine...");
    
    // 注册状态和对应的Enter/Exit函数
    client_state_.RegisterState(static_cast<int>(ChatbotState::fault),
        [this]() { /* Fault Enter */ 
            USER_LOG_ERROR("Entered fault state");
            // 停止所有音频操作
            stopRecording();
            stopPlayback();
        },
        [this]() { /* Fault Exit */ 
            USER_LOG_INFO("Exiting fault state");
        });
    
    client_state_.RegisterState(static_cast<int>(ChatbotState::startup),
        [this]() { /* Startup Enter */
            USER_LOG_INFO("Entered startup state");
            // 发送启动完成事件
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            enqueueEvent(AppEvent::startup_done);
        },
        [this]() { /* Startup Exit */ 
            USER_LOG_INFO("Exiting startup state");
        });
    
    client_state_.RegisterState(static_cast<int>(ChatbotState::stopping),
        [this]() { /* Stopping Enter */
            USER_LOG_INFO("Entered stopping state");
            // 停止所有操作
            stopRecording();
            stopPlayback();
            disconnectWebSocket();
            // 设置线程停止标志
            set_threads_stop_sig(true);
        },
        [this]() { /* Stopping Exit */ 
            USER_LOG_INFO("Exiting stopping state");
        });
    
    client_state_.RegisterState(static_cast<int>(ChatbotState::idle),
        [this]() { /* Idle Enter */
            USER_LOG_INFO("Entered idle state");
            // 发送状态消息到服务器
            sendTextMessage(R"({"type": "state", "state": "idle"})");
            // 清空录音队列并开始录音（用于唤醒检测）
            clearRecordingQueue();
            startRecording();
            // 启动真实的Snowboy唤醒检测线程
            startWakewordDetection();
        },
        [this]() { /* Idle Exit */ 
            USER_LOG_INFO("Exiting idle state");
            stopWakewordDetection();
            stopRecording();
        });
    
    client_state_.RegisterState(static_cast<int>(ChatbotState::listening),
        [this]() { /* Listening Enter */
            USER_LOG_INFO("Entered listening state");
            // 发送状态消息
            sendTextMessage(R"({"type": "state", "state": "listening"})");
            // 设置首次音频消息标志
            set_first_audio_msg_received(true);
            // 开始录音
            startRecording();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            clearRecordingQueue();
            
            // 启动音频数据发送线程
            std::thread([this]() {
                while (getState() == static_cast<int>(ChatbotState::listening) && 
                       !get_threads_stop_sig()) {
                    std::vector<int16_t> audio_frame;
                    if (getRecordedAudio(audio_frame)) {
                        // 编码并发送音频数据
                        std::vector<uint8_t> opus_data;
                        if (encodeOpus(audio_frame, opus_data)) {
                            auto packed_data = packBinaryFrame(opus_data);
                            sendAudioData(packed_data.data(), packed_data.size());
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }).detach();
        },
        [this]() { /* Listening Exit */ 
            USER_LOG_INFO("Exiting listening state");
            stopRecording();
        });
    
    client_state_.RegisterState(static_cast<int>(ChatbotState::thinking),
        [this]() { /* Thinking Enter */
            USER_LOG_INFO("Entered thinking state");
            sendTextMessage(R"({"type": "state", "state": "thinking"})");
            // 停止录音
            stopRecording();
        },
        [this]() { /* Thinking Exit */ 
            USER_LOG_INFO("Exiting thinking state");
        });
    
    client_state_.RegisterState(static_cast<int>(ChatbotState::speaking),
        [this]() { /* Speaking Enter */
            USER_LOG_INFO("Entered speaking state");
            sendTextMessage(R"({"type": "state", "state": "speaking"})");
            // 开始播放
            startPlayback();
            
            // 监控播放状态
            std::thread([this]() {
                while (getState() == static_cast<int>(ChatbotState::speaking) && 
                       !get_threads_stop_sig()) {
                    // 检查TTS和对话是否完成
                    if (get_tts_completed()) {
                        if (get_dialogue_completed()) {
                            enqueueEvent(AppEvent::dialogue_end);
                        } else {
                            enqueueEvent(AppEvent::speaking_end);
                        }
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }).detach();
        },
        [this]() { /* Speaking Exit */ 
            USER_LOG_INFO("Exiting speaking state");
            stopPlayback();
            // 重置状态标志
            set_tts_completed(false);
            set_dialogue_completed(false);
        });
    
    // 注册状态转换
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::startup), 
                                    static_cast<int>(AppEvent::startup_done), 
                                    static_cast<int>(ChatbotState::idle));
    
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::idle), 
                                    static_cast<int>(AppEvent::wake_detected), 
                                    static_cast<int>(ChatbotState::speaking));
    
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::listening), 
                                    static_cast<int>(AppEvent::vad_no_speech), 
                                    static_cast<int>(ChatbotState::idle));
    
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::listening), 
                                    static_cast<int>(AppEvent::asr_received), 
                                    static_cast<int>(ChatbotState::thinking));
    
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::thinking), 
                                    static_cast<int>(AppEvent::speaking_msg_received), 
                                    static_cast<int>(ChatbotState::speaking));
    
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::speaking), 
                                    static_cast<int>(AppEvent::speaking_end), 
                                    static_cast<int>(ChatbotState::listening));
    
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::speaking), 
                                    static_cast<int>(AppEvent::dialogue_end), 
                                    static_cast<int>(ChatbotState::idle));
    
    // 全局转换
    client_state_.RegisterTransition(-1, static_cast<int>(AppEvent::fault_happen), 
                                    static_cast<int>(ChatbotState::fault));
    client_state_.RegisterTransition(-1, static_cast<int>(AppEvent::to_stop), 
                                    static_cast<int>(ChatbotState::stopping));
    client_state_.RegisterTransition(static_cast<int>(ChatbotState::fault), 
                                    static_cast<int>(AppEvent::fault_solved), 
                                    static_cast<int>(ChatbotState::idle));
    
    // 初始化状态机
    client_state_.Initialize();
    
    USER_LOG_INFO("State machine configured successfully");
}

void Chatbot::setupWebSocketCallbacks() {
    // 设置消息接收回调
    ws_client_.SetMessageCallback([this](const std::string& message, bool is_binary) {
        handleWebSocketMessage(message, is_binary);
    });
    
    // 设置连接关闭回调
    ws_client_.SetCloseCallback([this]() {
        handleWebSocketClose();
    });
}

void Chatbot::stateEventLoop() {
    USER_LOG_INFO("Starting state event loop...");
    
    while (!threads_stop_flag_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 处理事件队列
        if (!eventQueue_.IsEmpty()) {
            if (auto event_opt = eventQueue_.Dequeue(); event_opt) {
                client_state_.HandleEvent(event_opt.value());
            }
        }
        
        // 处理意图队列
        if (!intentQueue_.IsEmpty()) {
            if (auto intent_opt = intentQueue_.Dequeue(); intent_opt) {
                // 这里可以处理意图，比如调用相关功能
                USER_LOG_INFO("Processing intent: %s", intent_opt.value().toStyledString().c_str());
            }
        }
    }
    
    USER_LOG_INFO("State event loop ended");
}

void Chatbot::handleWebSocketMessage(const std::string& message, bool is_binary) {
    msg_handler_->handleMessage(message, is_binary);
}

void Chatbot::handleWebSocketClose() {
    USER_LOG_WARN("WebSocket connection closed");
    enqueueEvent(AppEvent::fault_happen);
}

// ==================== 公共接口实现 ====================

bool Chatbot::startRecording() {
    audio_error_t result = start_recording(&audio_system_);
    return result == AUDIO_ERROR_NONE;
}

bool Chatbot::stopRecording() {
    audio_error_t result = stop_recording(&audio_system_);
    return result == AUDIO_ERROR_NONE;
}

bool Chatbot::startPlayback() {
    audio_error_t result = start_playback(&audio_system_);
    return result == AUDIO_ERROR_NONE;
}

bool Chatbot::stopPlayback() {
    audio_error_t result = stop_playback(&audio_system_);
    return result == AUDIO_ERROR_NONE;
}

void Chatbot::clearRecordingQueue() {
    clear_recording_queue(&audio_system_);
}

void Chatbot::clearPlaybackQueue() {
    clear_playback_queue(&audio_system_);
}

bool Chatbot::getRecordedAudio(std::vector<int16_t>& recordedData) {
    return get_recorded_audio(&audio_system_, recordedData);
}

void Chatbot::addFrameToPlaybackQueue(const std::vector<int16_t>& pcm_frame) {
    add_frame_to_playback_queue(&audio_system_, pcm_frame);
}

void Chatbot::connectWebSocket() {
    USER_LOG_INFO("Connecting to WebSocket server...");
    ws_client_.Connect();
}

void Chatbot::disconnectWebSocket() {
    USER_LOG_INFO("Disconnecting from WebSocket server...");
    ws_client_.Close();
}

void Chatbot::sendTextMessage(const std::string& message) {
    if (ws_client_.IsConnected()) {
        ws_client_.SendText(message);
    } else {
        USER_LOG_WARN("WebSocket not connected, cannot send text message");
    }
}

void Chatbot::sendAudioData(const uint8_t* data, size_t size) {
    if (ws_client_.IsConnected()) {
        ws_client_.SendBinary(data, size);
    } else {
        USER_LOG_WARN("WebSocket not connected, cannot send audio data");
    }
}

bool Chatbot::isWebSocketConnected() const {
    return ws_client_.IsConnected();
}

void Chatbot::enqueueEvent(AppEvent event) {
    eventQueue_.Enqueue(static_cast<int>(event));
}

void Chatbot::enqueueIntent(const Json::Value& intent) {
    intentQueue_.Enqueue(intent);
}

bool Chatbot::encodeOpus(const std::vector<int16_t>& pcm_data, std::vector<uint8_t>& opus_data) {
    opus_data.resize(1536); // 预分配空间
    size_t opus_size = opus_data.size();
    
    audio_error_t result = encode_opus(&audio_system_, 
                                      reinterpret_cast<uint8_t*>(const_cast<int16_t*>(pcm_data.data())), 
                                      pcm_data.size() * sizeof(int16_t), 
                                      opus_data.data(), 
                                      &opus_size);
    
    if (result == AUDIO_ERROR_NONE) {
        opus_data.resize(opus_size);
        return true;
    }
    
    return false;
}

bool Chatbot::decodeOpus(const std::vector<uint8_t>& opus_data, std::vector<int16_t>& pcm_data) {
    pcm_data.resize(4096); // 预分配空间
    size_t pcm_size = pcm_data.size() * sizeof(int16_t);
    
    audio_error_t result = decode_opus(&audio_system_, 
                                      const_cast<uint8_t*>(opus_data.data()), 
                                      opus_data.size(), 
                                      reinterpret_cast<uint8_t*>(pcm_data.data()), 
                                      &pcm_size);
    
    if (result == AUDIO_ERROR_NONE) {
        pcm_data.resize(pcm_size / sizeof(int16_t));
        return true;
    }
    
    return false;
}

std::vector<uint8_t> Chatbot::packBinaryFrame(const std::vector<uint8_t>& opus_data) {
    BinProtocol* packed_frame = pack_bin_frame(&audio_system_, 
                                              opus_data.data(), 
                                              opus_data.size(), 
                                              ws_protocolVersion_);
    
    if (packed_frame) {
        size_t total_size = sizeof(BinProtocol) + opus_data.size();
        std::vector<uint8_t> result(reinterpret_cast<uint8_t*>(packed_frame), 
                                   reinterpret_cast<uint8_t*>(packed_frame) + total_size);
        
        // 释放pack_bin_frame分配的内存
        free(packed_frame);
        
        return result;
    }
    
    return std::vector<uint8_t>();
}

bool Chatbot::unpackBinaryFrame(const std::vector<uint8_t>& packed_data, 
                                std::vector<uint8_t>& opus_data, 
                                BinProtocolInfo& protocol_info) {
    return unpack_bin_frame(&audio_system_, 
                           packed_data.data(), 
                           packed_data.size(), 
                           protocol_info, 
                           opus_data);
}

// ==================== Snowboy 唤醒检测实现 ====================

bool Chatbot::initSnowboyDetector() {
    USER_LOG_INFO("Initializing Snowboy detector...");
    
    const std::string resource_file = "third_party/snowboy/resources/common.res";
    const std::string model_file = "third_party/snowboy/resources/models/echo.pmdl";
    
    snowboy_detector_ = SnowboyDetectConstructor(resource_file.c_str(), model_file.c_str());
    
    if (!snowboy_detector_) {
        USER_LOG_ERROR("Failed to create Snowboy detector");
        return false;
    }
    
    // 设置检测参数
    SnowboyDetectSetSensitivity(snowboy_detector_, "0.5");
    SnowboyDetectSetAudioGain(snowboy_detector_, 1.0);
    SnowboyDetectApplyFrontend(snowboy_detector_, false); // 个人模型建议设置为false
    
    USER_LOG_INFO("Snowboy detector initialized successfully");
    USER_LOG_INFO("Sample rate: %d, Channels: %d, Bits per sample: %d", 
                  SnowboyDetectSampleRate(snowboy_detector_),
                  SnowboyDetectNumChannels(snowboy_detector_),
                  SnowboyDetectBitsPerSample(snowboy_detector_));
    USER_LOG_INFO("Number of hotwords: %d", SnowboyDetectNumHotwords(snowboy_detector_));
    
    return true;
}

void Chatbot::deinitSnowboyDetector() {
    if (snowboy_detector_) {
        USER_LOG_INFO("Deinitializing Snowboy detector...");
        SnowboyDetectDestructor(snowboy_detector_);
        snowboy_detector_ = nullptr;
        USER_LOG_INFO("Snowboy detector deinitialized");
    }
}

void Chatbot::startWakewordDetection() {
    if (!snowboy_detector_) {
        USER_LOG_ERROR("Snowboy detector not initialized");
        return;
    }
    
    USER_LOG_INFO("Starting wakeword detection...");
    wakeword_detection_running_.store(true);
    wakeword_detection_thread_ = std::thread([this]() {
        wakewordDetectionLoop();
    });
}

void Chatbot::stopWakewordDetection() {
    if (wakeword_detection_running_.load()) {
        USER_LOG_INFO("Stopping wakeword detection...");
        wakeword_detection_running_.store(false);
        
        if (wakeword_detection_thread_.joinable()) {
            wakeword_detection_thread_.join();
        }
        
        USER_LOG_INFO("Wakeword detection stopped");
    }
}

void Chatbot::wakewordDetectionLoop() {
    USER_LOG_INFO("Wakeword detection loop started");
    
    if (!snowboy_detector_) {
        USER_LOG_ERROR("Snowboy detector is null in detection loop");
        return;
    }
    
    std::vector<int16_t> audio_data;
    
    while (wakeword_detection_running_.load() && !threads_stop_flag_.load()) {
        // 从音频队列获取数据
        if (getRecordedAudio(audio_data) && !audio_data.empty()) {
            // 使用Snowboy进行唤醒检测
            int result = SnowboyDetectRunDetection(snowboy_detector_, 
                                                  audio_data.data(), 
                                                  audio_data.size(), 
                                                  false);
            
            if (result > 0) {
                USER_LOG_INFO("Wakeword detected! (hotword index: %d)", result);
                
                // 播放唤醒提示音
                std::thread([this]() {
                    std::string waked_sound_path = "third_party/audio/waked.pcm";
                    auto audioQueue = load_audio_from_file(&audio_system_, waked_sound_path, frame_duration_);
                    while (!audioQueue.empty()) {
                        const auto& frame = audioQueue.front();
                        addFrameToPlaybackQueue(frame);
                        audioQueue.pop();
                    }
                    // 短暂等待播放完成
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }).detach();
                
                // 发送唤醒事件
                enqueueEvent(AppEvent::wake_detected);
                break; // 检测到唤醒词后退出循环
                
            } else if (result == -1) {
                USER_LOG_WARN("Snowboy detection error");
            } else if (result == -2) {
                // 静音，正常情况，不记录日志
            }
        }
        
        // 短暂休眠避免过度消耗CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    USER_LOG_INFO("Wakeword detection loop ended");
}
