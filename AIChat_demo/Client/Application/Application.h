#ifndef APPLICATION_H
#define APPLICATION_H

#include "../Audio/AudioProcess.h"
#include "../StateMachine/StateMachine.h"
#include "../Events/EventQueue.h"
#include "../Events/AppEvents.h"
#include "../WebSocket/WebsocketClient.h"
#include "../Intent/IntentHandler.h"

#include <thread>
#include <atomic>
#include <string>

class Application {
public:
    Application(const std::string& address, int port, const std::string& token, const std::string& deviceId, 
                const std::string& aliyun_api_key, int protocolVersion, 
                int sample_rate, int channels, int frame_duration);
    ~Application();

    void Run();

    void Stop(void) {
        eventQueue_.Enqueue(static_cast<int>(AppEvent::to_stop));
    }

    AudioProcess audio_processor_;
    StateMachine client_state_;
    EventQueue<int> eventQueue_;
    EventQueue<Json::Value> IntentQueue_;
    // EventQueue<>;
    WebSocketClient ws_client_;
    IntentHandler intent_handler_;
    
    void set_first_audio_msg_received(bool flag) {
        first_audio_msg_received_ = flag;
    }
    bool get_first_audio_msg_received() {
        return first_audio_msg_received_;
    }

    void set_tts_completed(bool flag) {
        tts_completed_ = flag;
    }
    bool get_tts_completed() {
        return tts_completed_;
    }

    void set_dialogue_completed(bool flag) {
        dialogue_completed_ = flag;
    }
    bool get_dialogue_completed() {
        return dialogue_completed_;
    }

    void set_aliyun_api_key(const std::string& key) {
        aliyun_api_key_ = key;
    }
    std::string get_aliyun_api_key() {
        return aliyun_api_key_;
    }

    void set_threads_stop_sig(bool flag) {
        threads_stop_flag_.store(flag);
    }
    bool get_threads_stop_sig() {
        return threads_stop_flag_.load();
    }
    
    void set_ws_protocolVersion(int version) {
        ws_protocolVersion_ = version;
    }
    int get_ws_protocolVersion() {
        return ws_protocolVersion_;
    }

    int getState() {
        return client_state_.GetCurrentState();
    }

private:
    bool first_audio_msg_received_ = false;
    bool tts_completed_ = false;
    bool dialogue_completed_ = false;
    std::string aliyun_api_key_;
    int ws_protocolVersion_;
    // 原子变量用于通知线程退出
    std::atomic<bool> threads_stop_flag_ = false;
    std::thread ws_msg_thread_;
    std::thread state_trans_thread_;
};

#endif // APPLICATION_H