#include "Application.h"
#include "../Utils/user_log.h"
#include "StateConfig.h"
#include "WS_Handler.h"

WSHandler app_handler;

Application::Application(const std::string& address, int port, const std::string& token, const std::string& deviceId, const std::string& aliyun_api_key, 
                         int protocolVersion, int sample_rate, int channels, int frame_duration)
    : ws_client_(address, port, token, deviceId, protocolVersion),
      aliyun_api_key_(aliyun_api_key),
      ws_protocolVersion_(protocolVersion),
      client_state_(static_cast<int>(AppState::startup)),
      audio_processor_(sample_rate, channels, frame_duration) {
        // 设置接收到消息的回调函数
        ws_client_.SetMessageCallback([this](const std::string& message, bool is_binary) {
            app_handler.ws_msg_handle(message, is_binary, this);
        });
        ws_client_.SetCloseCallback([this]() {
            // 断开连接时的回调
            eventQueue_.Enqueue(static_cast<int>(AppEvent::fault_happen));
        });
}

Application::~Application() {
    threads_stop_flag_.store(true);
    if (ws_msg_thread_.joinable()) ws_msg_thread_.join();
    if (state_trans_thread_.joinable()) state_trans_thread_.join();
    USER_LOG_WARN("Application destruct.");
}

void Application::Run() {

    // 启动状态机事件处理线程
    state_trans_thread_ = std::thread([this]() {
        // initialize state machine and user states
        StateConfig::Configure(client_state_, this);
        // 主要执行state事件处理, 状态切换
        while(threads_stop_flag_.load() == false) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(eventQueue_.IsEmpty() == false) {
                // 事件queue处理
                if (auto event_opt = eventQueue_.Dequeue(); event_opt) {
                    client_state_.HandleEvent(event_opt.value());
                }
            } 
        }
    });

    // 等待 state 切换事件线程结束
    state_trans_thread_.join();
    if(ws_client_.IsConnected()) {
        ws_client_.Close();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    USER_LOG_WARN("ai chat app run end");
}