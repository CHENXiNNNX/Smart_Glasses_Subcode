#include "chatbot.h"
#include <json/json.h>

Chatbot::Chatbot(const std::string& address, int port, const std::string& token, const std::string& deviceId, 
                 const std::string& aliyun_api_key, int protocolVersion, 
                 int sample_rate, int channels, int frame_duration)
    : aliyun_api_key_(aliyun_api_key),
      ws_protocolVersion_(protocolVersion) {
    
    // 初始化各个子系统
    InitializeAudioSystem(sample_rate, channels, frame_duration);
    InitializeWebSocketClient(address, port, token, deviceId, protocolVersion);
    InitializeStateMachine();
    
    USER_LOG_INFO("Chatbot initialized successfully");
}

Chatbot::~Chatbot() {
    // 停止所有线程
    threads_stop_flag_.store(true);
    
    // 等待线程结束
    if (ws_msg_thread_.joinable()) {
        ws_msg_thread_.join();
    }
    if (state_trans_thread_.joinable()) {
        state_trans_thread_.join();
    }
    
    // 停止WebSocket连接
    if (ws_client_ && ws_client_->IsConnected()) {
        ws_client_->Close();
    }
    
    // 释放音频系统
    audio_system_deinit(&audio_system_);
    
    USER_LOG_INFO("Chatbot destroyed");
}

void Chatbot::Run() {
    // 启动WebSocket客户端
    ws_client_->Run();
    
    // 启动状态机事件处理线程
    state_trans_thread_ = std::thread([this]() {
        // 配置状态机
        ConfigureStateMachine();
        
        // 进入启动状态
        EnqueueEvent(static_cast<int>(AppEvent::startup_done));
        
        // 处理事件循环
        while (threads_stop_flag_.load() == false) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (!event_queue_.IsEmpty()) {
                if (auto event_opt = event_queue_.Dequeue(); event_opt) {
                    state_machine_->HandleEvent(event_opt.value());
                }
            }
            
            // 处理意图队列
            if (!intent_queue_.IsEmpty()) {
                if (auto intent_opt = intent_queue_.Dequeue(); intent_opt) {
                    IntentHandler::HandleIntent(intent_opt.value());
                }
            }
        }
    });
    
    // 等待状态转换线程结束
    state_trans_thread_.join();
    
    USER_LOG_INFO("Chatbot run ended");
}

void Chatbot::Stop() {
    USER_LOG_INFO("Stopping chatbot...");
    EnqueueEvent(static_cast<int>(AppEvent::to_stop));
    threads_stop_flag_.store(true);
}

int Chatbot::GetCurrentState() const {
    if (state_machine_) {
        return state_machine_->GetCurrentState();
    }
    return static_cast<int>(AppState::fault);
}

void Chatbot::TransitionToState(int state) {
    if (state_machine_) {
        // 这个方法可能需要根据状态机的实际实现进行调整
        USER_LOG_INFO("Transitioning to state: %d", state);
    }
}

void Chatbot::ConfigureStateMachine() {
    if (!state_machine_) {
        USER_LOG_ERROR("State machine not initialized");
        return;
    }
    
    // 注册状态和状态转换
    state_machine_->RegisterState(static_cast<int>(AppState::fault), 
        [this]() { USER_LOG_INFO("Entering fault state"); }, 
        [this]() { USER_LOG_INFO("Exiting fault state"); });
    
    state_machine_->RegisterState(static_cast<int>(AppState::startup), 
        [this]() { USER_LOG_INFO("Entering startup state"); }, 
        [this]() { USER_LOG_INFO("Exiting startup state"); });
    
    state_machine_->RegisterState(static_cast<int>(AppState::stopping), 
        [this]() { USER_LOG_INFO("Entering stopping state"); }, 
        [this]() { USER_LOG_INFO("Exiting stopping state"); });
    
    state_machine_->RegisterState(static_cast<int>(AppState::idle), 
        [this]() {
            USER_LOG_INFO("Entering idle state");
            // 在空闲状态下停止录音和播放
            StopRecording();
            StopPlayback();
        }, 
        [this]() { USER_LOG_INFO("Exiting idle state"); });
    
    state_machine_->RegisterState(static_cast<int>(AppState::listening), 
        [this]() {
            USER_LOG_INFO("Entering listening state");
            // 在监听状态下开始录音
            StartRecording();
        }, 
        [this]() {
            USER_LOG_INFO("Exiting listening state");
            // 退出监听状态时停止录音
            StopRecording();
        });
    
    state_machine_->RegisterState(static_cast<int>(AppState::thinking), 
        [this]() { USER_LOG_INFO("Entering thinking state"); }, 
        [this]() { USER_LOG_INFO("Exiting thinking state"); });
    
    state_machine_->RegisterState(static_cast<int>(AppState::speaking), 
        [this]() {
            USER_LOG_INFO("Entering speaking state");
            // 在说话状态下开始播放
            StartPlayback();
        }, 
        [this]() {
            USER_LOG_INFO("Exiting speaking state");
            // 退出说话状态时停止播放
            StopPlayback();
        });
    
    // 注册状态转换
    state_machine_->RegisterTransition(static_cast<int>(AppState::startup), static_cast<int>(AppEvent::startup_done), static_cast<int>(AppState::idle));
    state_machine_->RegisterTransition(static_cast<int>(AppState::idle), static_cast<int>(AppEvent::wake_detected), static_cast<int>(AppState::speaking));
    state_machine_->RegisterTransition(static_cast<int>(AppState::listening), static_cast<int>(AppEvent::vad_no_speech), static_cast<int>(AppState::idle));
    state_machine_->RegisterTransition(static_cast<int>(AppState::listening), static_cast<int>(AppEvent::asr_received), static_cast<int>(AppState::thinking));
    state_machine_->RegisterTransition(static_cast<int>(AppState::thinking), static_cast<int>(AppEvent::speaking_msg_received), static_cast<int>(AppState::speaking));
    state_machine_->RegisterTransition(static_cast<int>(AppState::speaking), static_cast<int>(AppEvent::speaking_end), static_cast<int>(AppState::listening));
    state_machine_->RegisterTransition(static_cast<int>(AppState::speaking), static_cast<int>(AppEvent::dialogue_end), static_cast<int>(AppState::idle));
    state_machine_->RegisterTransition(-1, static_cast<int>(AppEvent::fault_happen), static_cast<int>(AppState::fault));
    state_machine_->RegisterTransition(-1, static_cast<int>(AppEvent::to_stop), static_cast<int>(AppState::stopping));
    state_machine_->RegisterTransition(static_cast<int>(AppState::fault), static_cast<int>(AppEvent::fault_solved), static_cast<int>(AppState::idle));
    
    // 初始化状态机
    state_machine_->Initialize();
}

void Chatbot::HandleWebSocketMessage(const std::string& message, bool is_binary) {
    if (is_binary) {
        HandleBinaryMessage(message);
    } else {
        HandleJsonMessage(message);
    }
}

void Chatbot::HandleJsonMessage(const std::string& message) {
    Json::Value root;
    Json::Reader reader;
    
    // 解析JSON字符串
    bool parsingSuccessful = reader.parse(message, root);
    if (!parsingSuccessful) {
        USER_LOG_ERROR("Error parsing JSON message: %s", reader.getFormattedErrorMessages().c_str());
        EnqueueEvent(static_cast<int>(AppEvent::fault_happen));
        return;
    }
    
    USER_LOG_INFO("Received JSON message: %s", message.c_str());
    
    // 处理不同类型的消息
    const Json::Value type = root["type"];
    if (type.isString()) {
        std::string typeStr = type.asString();
        if (typeStr == "vad") {
            // 处理VAD消息
            const Json::Value state = root["state"];
            if (state.isString() && state.asString() == "no_speech") {
                EnqueueEvent(static_cast<int>(AppEvent::vad_no_speech));
            }
        } else if (typeStr == "asr") {
            // 处理ASR消息
            const Json::Value text = root["text"];
            if (text.isString()) {
                USER_LOG_INFO("Received ASR text: %s", text.asString().c_str());
            }
            EnqueueEvent(static_cast<int>(AppEvent::asr_received));
        } else if (typeStr == "chat") {
            // 处理聊天消息
            USER_LOG_INFO("Received chat message");
        } else if (typeStr == "tts") {
            // 处理TTS消息
            const Json::Value state = root["state"];
            if (state.isString()) {
                if (state.asString() == "end") {
                    SetTTSCompleted(true);
                    EnqueueEvent(static_cast<int>(AppEvent::speaking_end));
                }
            }
        } else if (typeStr == "error") {
            // 处理错误消息
            USER_LOG_ERROR("Server error message: %s", message.c_str());
            EnqueueEvent(static_cast<int>(AppEvent::fault_happen));
        } else {
            USER_LOG_WARN("Unknown message type: %s", typeStr.c_str());
        }
    }
    
    // 处理意图调用
    if (root.isMember("function_call") && root["function_call"].isObject()) {
        intent_queue_.Enqueue(root);
    }
}

void Chatbot::HandleBinaryMessage(const std::string& message) {
    // 处理二进制音频数据
    if (GetFirstAudioMsgReceived()) {
        SetFirstAudioMsgReceived(false);
        EnqueueEvent(static_cast<int>(AppEvent::speaking_msg_received));
    }
    
    BinProtocolInfo protocol_info;
    std::vector<uint8_t> opus_data;
    std::vector<int16_t> pcm_data;
    
    // 解包二进制数据
    // 这里需要实现UnpackBinFrame函数，或者根据实际API进行调整
    // 假设audio_system_t中有相应的方法
    
    // 简单示例：假设我们只是将数据添加到播放队列
    // 实际实现需要根据音频系统的API进行调整
    std::vector<int16_t> audio_data(message.begin(), message.end());
    // 这里需要添加到播放队列的代码
}

void Chatbot::EnqueueEvent(int event) {
    event_queue_.Enqueue(event);
}

audio_error_t Chatbot::StartRecording() {
    audio_error_t result = start_recording(&audio_system_);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to start recording");
    }
    return result;
}

audio_error_t Chatbot::StopRecording() {
    audio_error_t result = stop_recording(&audio_system_);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to stop recording");
    }
    return result;
}

audio_error_t Chatbot::StartPlayback() {
    audio_error_t result = start_playback(&audio_system_);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to start playback");
    }
    return result;
}

audio_error_t Chatbot::StopPlayback() {
    audio_error_t result = stop_playback(&audio_system_);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to stop playback");
    }
    return result;
}

bool Chatbot::IsWebSocketConnected() const {
    if (ws_client_) {
        return ws_client_->IsConnected();
    }
    return false;
}

void Chatbot::SendTextMessage(const std::string& message) {
    if (ws_client_ && ws_client_->IsConnected()) {
        ws_client_->SendText(message);
    } else {
        USER_LOG_WARN("Cannot send text message: WebSocket not connected");
    }
}

void Chatbot::SendBinaryMessage(const uint8_t* data, size_t size) {
    if (ws_client_ && ws_client_->IsConnected()) {
        ws_client_->SendBinary(data, size);
    } else {
        USER_LOG_WARN("Cannot send binary message: WebSocket not connected");
    }
}

void Chatbot::InitializeAudioSystem(int sample_rate, int channels, int frame_duration) {
    // 初始化音频系统参数
    audio_system_.sample_rate = sample_rate;
    audio_system_.channels = channels;
    audio_system_.frame_duration_ms = frame_duration;
    audio_system_.current_mode = AUDIO_MODE_AI;
    audio_system_.isRecording = false;
    audio_system_.isPlaying = false;
    
    // 初始化音频系统
    audio_error_t result = audio_system_init(&audio_system_);
    if (result != AUDIO_ERROR_NONE) {
        USER_LOG_ERROR("Failed to initialize audio system");
    } else {
        USER_LOG_INFO("Audio system initialized successfully");
    }
}

void Chatbot::InitializeWebSocketClient(const std::string& address, int port, const std::string& token, 
                                       const std::string& deviceId, int protocolVersion) {
    // 创建WebSocket客户端
    ws_client_ = std::make_unique<WebSocketClient>(address, port, token, deviceId, protocolVersion);
    
    // 设置回调函数
    ws_client_->SetMessageCallback([this](const std::string& message, bool is_binary) {
        this->HandleWebSocketMessage(message, is_binary);
    });
    
    ws_client_->SetCloseCallback([this]() {
        USER_LOG_INFO("WebSocket connection closed");
        this->EnqueueEvent(static_cast<int>(AppEvent::fault_happen));
    });
    
    USER_LOG_INFO("WebSocket client initialized");
}

void Chatbot::InitializeStateMachine() {
    // 创建并初始化状态机
    state_machine_ = std::make_unique<StateMachine>(static_cast<int>(AppState::startup));
    USER_LOG_INFO("State machine initialized");
}