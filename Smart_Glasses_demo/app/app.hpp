/*
 * app.hpp - 应用组装层
 */

#pragma once

#include "media/sync.hpp"

#include <atomic>
#include <memory>

namespace app
{
    namespace media
    {
        namespace audio
        {
            class AudioDrv;
            struct AudioCfg;
        } // namespace audio
        namespace camera
        {
            class CameraDrv;
            struct CameraCfg;
        } // namespace camera
    }     // namespace media
    namespace network
    {
        namespace wifi
        {
            class WifiManager;
            struct WifiConfig;
        } // namespace wifi
    }     // namespace network
    namespace chatbot
    {
        class ChatbotSystem;
        struct ChatbotConfig;
    } // namespace chatbot

    class IAudioService;
    class IVideoService;
    class INetworkService;
    class IHttpClient;

    class AudioServiceImpl;
    class VideoServiceImpl;
    class NetworkServiceImpl;
    class HttpClientImpl;

    class App
    {
    public:
        App();
        ~App();

        App(const App&)            = delete;
        App& operator=(const App&) = delete;

        bool init();
        void run();
        void stop();

    private:
        bool initLog();
        void deinitLog();

        bool initSync();
        void deinitSync();

        bool initAudio();
        void deinitAudio();

        bool initCamera();
        void deinitCamera();

        bool initNetwork();
        void deinitNetwork();

        bool initChatbot();
        void deinitChatbot();

        void cleanup();

    private:
        std::atomic<bool> running_{true};

        std::shared_ptr<sync_context_t> sync_ctx_;

        std::unique_ptr<media::audio::AudioDrv>     audio_drv_;
        std::unique_ptr<media::camera::CameraDrv>   camera_drv_;
        std::unique_ptr<network::wifi::WifiManager> wifi_mgr_;

        std::unique_ptr<AudioServiceImpl>   audio_svc_;
        std::unique_ptr<VideoServiceImpl>   video_svc_;
        std::unique_ptr<NetworkServiceImpl> network_svc_;
        std::unique_ptr<HttpClientImpl>     http_svc_;

        std::unique_ptr<chatbot::ChatbotSystem> chatbot_;
    };

} // namespace app
