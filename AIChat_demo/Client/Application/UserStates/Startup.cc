#include "Startup.h"
#include "../../Utils/user_log.h"
#include "../../Application/Application.h"
#include "../../Events/AppEvents.h"
#include "../IntentsRegistry.h"

void StartupState::Enter(Application* app) {
    USER_LOG_INFO("Into startup state.");
    app->ws_client_.Run(); // 会开一个thread
    app->ws_client_.Connect();
    // 等待连接建立, 尝试3次
    int try_count = 1;
    while(!app->ws_client_.IsConnected() && try_count && !app->get_threads_stop_sig()) {
        try_count--;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        USER_LOG_INFO("Try to connect to server.");
        app->ws_client_.Connect();
    }
    
    if (app->ws_client_.IsConnected()) {
        // hello消息
        std::string json_message = 
        R"({
            "type": "hello",
            "api_key": ")" + app->get_aliyun_api_key() + R"(",
            "audio_params": {
                "format": "opus",
                "sample_rate": )" + std::to_string(app->audio_processor_.get_sample_rate()) + R"(,
                "channels": )" + std::to_string(app->audio_processor_.get_channels()) + R"(,
                "frame_duration": )" + std::to_string(app->audio_processor_.get_frame_duration()) + R"(
            }
        })";
        app->ws_client_.SendText(json_message);
        // 注册意图处理函数
        // 注册所有函数到 IntentHandler
        IntentsRegistry::RegisterAllFunctions(app->intent_handler_);
        // 生成注册消息并发送给服务器
        auto register_message = IntentsRegistry::GenerateRegisterMessage();
        Json::StreamWriterBuilder writer;
        std::string serialized_message = Json::writeString(writer, register_message);
        app->ws_client_.SendText(serialized_message);
        // start up done
        app->eventQueue_.Enqueue(static_cast<int>(AppEvent::startup_done));
        USER_LOG_INFO("Startup done.");
    }
    else {
        USER_LOG_ERROR("Startup failed.");
        app->eventQueue_.Enqueue(static_cast<int>(AppEvent::to_stop));
    }
}

void StartupState::Exit(Application* app) {
    USER_LOG_INFO("Startup exit.");
}