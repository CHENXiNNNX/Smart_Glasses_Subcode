#ifndef APP_HPP
#define APP_HPP

#include <string>
#include <memory>
#include "media/sync.hpp"

namespace app
{
    namespace network
    {
        namespace wifi
        {
            class WifiManager;
        }
    } // namespace network

    namespace media
    {
        namespace audio
        {
            class AudioSystem;
        }
        namespace camera
        {
            class VideoSystem;
        }
    } // namespace media

    namespace protocol
    {
        namespace webrtc
        {
            class Signaling;
            class WebRTCSystem;
        }
    } // namespace protocol

    namespace chatbot
    {
        class ChatbotSystem;
    }

    class App
    {
    public:
        App();
        ~App();

        void init();
        void run();

    private:
        // 日志相关
        bool initLog();
        bool deinitLog();

        // 网络相关
        bool initNetwork();
        bool deinitNetwork();

        // WiFi相关
        bool initWiFi();
        bool deinitWiFi();

        // 蓝牙相关
        bool initBluetooth();
        bool deinitBluetooth();

        // 网络检查相关
        bool checkNetwork();

        // 网络连接相关
        bool connectNetwork();
        bool disconnectNetwork();
        bool searchSavedNetwork();
        bool forgetNetwork(const std::string& ssid);

        // 时间同步相关
        bool initSync();
        bool deinitSync();

        // 音频系统相关
        bool initAudio(int sample_rate, int channels, int frame_duration_ms);
        bool deinitAudio();

        // 视频系统相关
        bool initVideo(int width, int height);
        bool deinitVideo();

        // Webrtc系统相关
        bool initSignaling(std::string device_id, std::string server_url);
        bool deinitSignaling();
        bool initWebrtc();
        bool deinitWebrtc();

        // 聊天机器人相关
        bool initChatbot();
        bool deinitChatbot();

    private:
        // WiFi管理器
        std::unique_ptr<app::network::wifi::WifiManager> wifi_manager_;

        // 音频系统
        std::unique_ptr<app::media::audio::AudioSystem> audio_system_;

        // 视频系统
        std::unique_ptr<app::media::camera::VideoSystem> video_system_;

        // 同步上下文（音视频时间同步）
        std::shared_ptr<sync_context_t> sync_ctx_;

        // 信令系统
        std::shared_ptr<app::protocol::webrtc::Signaling> signaling_;

        // WebRTC系统
        std::shared_ptr<app::protocol::webrtc::WebRTCSystem> webrtc_;

        // 聊天机器人系统
        std::unique_ptr<app::chatbot::ChatbotSystem> chatbot_system_;
    };

} // namespace app

#endif // APP_HPP