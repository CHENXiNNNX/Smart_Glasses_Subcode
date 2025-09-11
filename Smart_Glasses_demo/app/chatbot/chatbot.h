#ifndef CHATBOT_H
#define CHATBOT_H

#include "event/app_event.h"
#include "event/eventqueue.h"
#include "intent/intent_handler.h"
#include "statemachine/state_machine.h"
#include "utils/user_log.h"
#include "../protocol/websocket/websocket.h"
#include "../media/audio/audio.h"

#include <thread>
#include <atomic>
#include <string>
#include <memory>

// 应用状态枚举
enum class AppState {
    fault,
    startup,
    stopping,
    idle,
    listening,
    thinking,
    speaking,
};

class Chatbot {
public:
    Chatbot(const std::string& address, int port, const std::string& token, const std::string& deviceId, 
            const std::string& aliyun_api_key, int protocolVersion, 
            int sample_rate, int channels, int frame_duration);
    ~Chatbot();

    void Run();
    void Stop();

    // 状态管理方法
    int GetCurrentState() const;
    void TransitionToState(int state);

    // 配置状态机
    void ConfigureStateMachine();

    // WebSocket消息处理
    void HandleWebSocketMessage(const std::string& message, bool is_binary);
    void HandleJsonMessage(const std::string& message);
    void HandleBinaryMessage(const std::string& message);

    // 事件处理
    void EnqueueEvent(int event);

    // 音频控制
    audio_error_t StartRecording();
    audio_error_t StopRecording();
    audio_error_t StartPlayback();
    audio_error_t StopPlayback();

    // WebSocket通信
    bool IsWebSocketConnected() const;
    void SendTextMessage(const std::string& message);
    void SendBinaryMessage(const uint8_t* data, size_t size);

    // 获取配置参数
    std::string GetAliyunApiKey() const { return aliyun_api_key_; }
    int GetProtocolVersion() const { return ws_protocolVersion_; }

    // 状态标志设置
    void SetFirstAudioMsgReceived(bool flag) { first_audio_msg_received_ = flag; }
    bool GetFirstAudioMsgReceived() const { return first_audio_msg_received_; }
    
    void SetTTSCompleted(bool flag) { tts_completed_ = flag; }
    bool GetTTSCompleted() const { return tts_completed_; }
    
    void SetDialogueCompleted(bool flag) { dialogue_completed_ = flag; }
    bool GetDialogueCompleted() const { return dialogue_completed_; }

private:
    // 状态管理
    std::unique_ptr<StateMachine> state_machine_;
    
    // 音频系统
    audio_system_t audio_system_;
    
    // WebSocket客户端
    std::unique_ptr<WebSocketClient> ws_client_;
    
    // 意图处理器
    IntentHandler intent_handler_;
    
    // 事件队列
    EventQueue<int> event_queue_;
    EventQueue<Json::Value> intent_queue_;
    
    // 配置参数
    std::string aliyun_api_key_;
    int ws_protocolVersion_;
    
    // 状态标志
    bool first_audio_msg_received_ = false;
    bool tts_completed_ = false;
    bool dialogue_completed_ = false;
    
    // 线程管理
    std::atomic<bool> threads_stop_flag_ = false;
    std::thread ws_msg_thread_;
    std::thread state_trans_thread_;
    
    // 私有方法
    void InitializeAudioSystem(int sample_rate, int channels, int frame_duration);
    void InitializeWebSocketClient(const std::string& address, int port, const std::string& token, 
                                 const std::string& deviceId, int protocolVersion);
    void InitializeStateMachine();
};

#endif // CHATBOT_H