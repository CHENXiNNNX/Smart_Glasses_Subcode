#include "Stop.h"
#include "../../Utils/user_log.h"

void StopState::Enter(Application* app) {
    USER_LOG_INFO("Into stopping state.");
    app->ws_client_.Close();
    // 设置标志，通知线程退出
    app->set_threads_stop_sig(true);
}

void StopState::Exit(Application* app) {
    USER_LOG_INFO("Stopping exit.");
}