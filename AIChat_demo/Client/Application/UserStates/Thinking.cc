#include "Thinking.h"
#include "../../Utils/user_log.h"

void ThinkingState::Enter(Application* app) {
    std::string json_message = R"({"type": "state", "state": "thinking"})";
    app->ws_client_.SendText(json_message);
    USER_LOG_INFO("Into thinking state.");
}

void ThinkingState::Exit(Application* app) {
    USER_LOG_INFO("thinking state exit.");
}
