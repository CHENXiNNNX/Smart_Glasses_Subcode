#include "Idle.h"
#include "../../Utils/user_log.h"
#include "../../third_party/snowboy/include/snowboy-detect-c-wrapper.h"
#include "../../Events/AppEvents.h"

// 静态成员变量定义
std::atomic<bool> IdleState::state_running_{false};
std::thread IdleState::state_running_thread_;

void IdleState::Enter(Application* app) {
    std::string json_message = R"({"type": "state", "state": "idle"})";
    app->ws_client_.SendText(json_message);
    // clear recorded audio queue
    app->audio_processor_.clearRecordedAudioQueue();
    // 开启录音
    app->audio_processor_.startRecording();
    // start state running
    state_running_.store(true);
    state_running_thread_ = std::thread([app]() { Run(app); });
    USER_LOG_INFO("Into Idle state.");
}

void IdleState::Run(Application* app) {
    USER_LOG_INFO("Idle state run.");
    SnowboyDetect* detector = SnowboyDetectConstructor("third_party/snowboy/resources/common.res",
                                                     "third_party/snowboy/resources/models/echo.pmdl");
    SnowboyDetectSetSensitivity(detector, "0.5");
    SnowboyDetectSetAudioGain(detector, 1);
    SnowboyDetectApplyFrontend(detector, false);
    std::vector<int16_t> data;
    while (state_running_.load() == true) {
        if(app->audio_processor_.recordedQueueIsEmpty() == false) {
            app->audio_processor_.getRecordedAudio(data);
            // 检测唤醒词
            int result = SnowboyDetectRunDetection(detector, data.data(), data.size(), false);
            if (result > 0) {
                // 发生唤醒事件
                USER_LOG_INFO("Wake detected.");
                app->eventQueue_.Enqueue(static_cast<int>(AppEvent::wake_detected));
                break;
            }
        }
    }
    SnowboyDetectDestructor(detector);
}

void IdleState::Exit(Application* app) {
    // stop录音
    app->audio_processor_.stopRecording();
    // stop running
    state_running_.store(false);
    state_running_thread_.join();

    // playing waked up sound
    std::string waked_sound_path = "third_party/audio/waked.pcm";
    auto audioQueue = app->audio_processor_.loadAudioFromFile(waked_sound_path, 40);
    while (!audioQueue.empty()) {
        const auto& frame = audioQueue.front();
        app->audio_processor_.addFrameToPlaybackQueue(frame);
        audioQueue.pop();
    }
    app->set_tts_completed(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    USER_LOG_INFO("Idle State exit.");
}