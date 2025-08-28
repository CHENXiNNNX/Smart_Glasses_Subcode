#include "Speaking.h"
#include "../../Utils/user_log.h"
#include "../../Events/AppEvents.h"

// 静态成员变量定义
std::atomic<bool> SpeakingState::state_running_{false};
std::thread SpeakingState::state_running_thread_;

void SpeakingState::Enter(Application* app) {
    std::string json_message = R"({"type": "state", "state": "speaking"})";
    app->ws_client_.SendText(json_message);
    // start播放
    app->audio_processor_.startPlaying();
    // running
    state_running_.store(true);
    state_running_thread_ = std::thread([app]() { Run(app); });
    USER_LOG_INFO("Into speaking state.");
}

void SpeakingState::Run(Application* app) {
    USER_LOG_INFO("Speaking state run.");
    while(state_running_.load() == true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if(app->get_tts_completed() && app->audio_processor_.playbackQueueIsEmpty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            USER_LOG_INFO("Speaking end.");
            if(app->get_dialogue_completed() == false) {
                app->eventQueue_.Enqueue(static_cast<int>(AppEvent::speaking_end));
            } else {
                app->eventQueue_.Enqueue(static_cast<int>(AppEvent::dialogue_end));
            }
            app->set_tts_completed(false);
            app->set_dialogue_completed(false);
            break;
        }
    }
}

void SpeakingState::Exit(Application* app) {
    // clear playback audio queue
    app->audio_processor_.clearPlaybackAudioQueue();
    // stop播放
    app->audio_processor_.stopPlaying();
    // stop state running
    state_running_.store(false);
    state_running_thread_.join();
    USER_LOG_INFO("Speaking exit.");
}
