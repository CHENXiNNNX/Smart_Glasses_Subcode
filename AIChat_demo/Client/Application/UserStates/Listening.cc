#include "Listening.h"
#include "../../Utils/user_log.h"
#include "../../Events/AppEvents.h"

// 静态成员变量定义
std::atomic<bool> ListeningState::state_running_{false};
std::thread ListeningState::state_running_thread_;

void ListeningState::Enter(Application* app) {
    std::string json_message = R"({"type": "state", "state": "listening"})";
    app->ws_client_.SendText(json_message);
    // clear首次接收server音频消息
    app->set_first_audio_msg_received(true);
    // start录音
    app->audio_processor_.startRecording();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // clear recorded audio queue
    app->audio_processor_.clearRecordedAudioQueue();
    // running
    state_running_.store(true);
    state_running_thread_ = std::thread([app]() { Run(app); });
    USER_LOG_INFO("Into listening state.");
}

void ListeningState::Run(Application* app) {
    while (state_running_.load() == true) {
        // need a time out
        std::vector<int16_t> audio_frame;
        if(app->audio_processor_.recordedQueueIsEmpty() == false) {
            if(app->audio_processor_.getRecordedAudio(audio_frame)) {
                // 编码
                uint8_t opus_data[1536];
                size_t opus_data_size;
                if (app->audio_processor_.encode(audio_frame, opus_data, opus_data_size)) {
                    // 打包
                    BinProtocol* packed_frame = app->audio_processor_.PackBinFrame(opus_data, opus_data_size, app->get_ws_protocolVersion());
                    if (packed_frame) {
                        // 发送
                        app->ws_client_.SendBinary(reinterpret_cast<uint8_t*>(packed_frame), sizeof(BinProtocol) + opus_data_size);
                    } else {
                        USER_LOG_WARN("Audio Packing failed");
                    }
                } else {
                    USER_LOG_WARN("Audio Encoding failed");
                }
            }
        }
    }
}

void ListeningState::Exit(Application* app) {
    // stop录音
    app->audio_processor_.stopRecording();
    // stop running
    state_running_.store(false);
    state_running_thread_.join();
    USER_LOG_INFO("Listening exit.");
}
