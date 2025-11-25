#ifndef APP_HPP
#define APP_HPP

#include <string>
#include <memory>

namespace app {
namespace network {
namespace wifi {
class WifiManager;
}
}

namespace media {
namespace audio {
class AudioSystem;
}
namespace camera {
class VideoSystem;
}
}

namespace chatbot {
class ChatbotSystem;
}

class App {
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
    
    // WiFi相关方法
    bool initWiFi();
    bool deinitWiFi();
    
    // 蓝牙相关方法
    bool initBluetooth();
    bool deinitBluetooth();
    
    // 网络检查方法
    bool checkNetwork();
    
    // 网络连接方法
    bool connectNetwork();
    bool disconnectNetwork();
    bool searchSavedNetwork();
    bool forgetNetwork(const std::string& ssid);

    // 音频相关
    bool initAudio();
    bool deinitAudio();

    // 视频相关
    bool initVideo();
    bool deinitVideo();

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

    // 聊天机器人系统
    std::unique_ptr<app::chatbot::ChatbotSystem> chatbot_system_;
};

} // namespace app

#endif // APP_HPP