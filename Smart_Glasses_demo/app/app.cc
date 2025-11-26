#include "app.hpp"
#include "tool/log/log.hpp"
#include "network/wifi/wifi.hpp"
#include "media/audio/audio.hpp"
#include "media/camera/camera.hpp"
#include "chatbot/chatbot.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace app::tool::log;
namespace wifi = app::network::wifi;
namespace audio = app::media::audio;
namespace video = app::media::camera;
namespace chatbot = app::chatbot;

#define TAG "App"

namespace app {

App::App() {

}

App::~App() {
    deinitChatbot();  
    deinitVideo();
    deinitAudio();
    deinitNetwork();
    deinitLog();
}

// 初始化日志
bool App::initLog() {
    bool log_err = Logger::getInstance().initialize(LogConfig());
    if (!log_err) {
        LOG_ERROR(TAG, "初始化日志失败");
        return false;
    }
    return true;
}

// 释放日志系统
bool App::deinitLog() {
    bool log_err = Logger::getInstance().shutdown();
    if (!log_err) {
        LOG_ERROR(TAG, "释放日志失败");
        return false;
    }
    return true;
}

// 初始化网络
bool App::initNetwork() {
    // 初始化WiFi系统
    if (!initWiFi()) {
        return false;
    }

    // 检查WiFi连接状态
    bool err = checkNetwork();
    if (err) {
        // 已连接，直接返回成功
        return true;
    }

    // 未连接，进入配网流程（阻塞直到成功）
    LOG_INFO(TAG, "进入配网流程...");

    // 阻塞循环，直到连接成功
    while (true) {
        err = connectNetwork();
        if (err) {
            // 连接成功
            return true;
        }

        // 连接失败，等待后重试
        LOG_WARN(TAG, "WiFi连接失败，10秒后重试...");
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

// 释放网络
bool App::deinitNetwork() {
    return deinitWiFi();
}

// 初始化WiFi
bool App::initWiFi() {
    // 创建WiFi配置
    wifi::WifiConfig wifi_config;
    wifi_config.auto_connect_on_init = true; // 自动连接已保存的WiFi

    // 创建WiFi管理器
    wifi_manager_ = std::make_unique<wifi::WifiManager>(wifi_config);

    // 初始化WiFi管理器
    wifi::WifiError wifi_err = wifi_manager_->initialize();
    if (wifi_err != wifi::WifiError::NONE) {
        LOG_ERROR(TAG, "  WiFi管理器初始化失败");
        wifi_manager_.reset();
        return false;
    }
    LOG_INFO(TAG, "  WiFi管理器初始化成功");
    return true;
}

// 释放WiFi
bool App::deinitWiFi() {
    if (wifi_manager_) {
        wifi_manager_->shutdown();
        wifi_manager_.reset();
    }
    return true;
}

// 初始化蓝牙
bool App::initBluetooth() {
    // TODO: 初始化蓝牙
    return true;
}

// 释放蓝牙
bool App::deinitBluetooth() {
    // TODO: 释放蓝牙
    return true;
}

// 检查网络连接状态
bool App::checkNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR(TAG, "WiFi管理器未初始化");
        return false;
    }

    // 检查WiFi是否已连接
    if (wifi_manager_->isConnected()) {
        std::string ssid = wifi_manager_->getCurrentSSID();
        std::string ip   = wifi_manager_->getIPAddress();
        LOG_INFO(TAG, "WiFi已连接: %s (IP: %s)", ssid.c_str(), ip.c_str());
        return true;
    }

    LOG_INFO(TAG, "WiFi未连接");
    return false;
}

// 连接网络
bool App::connectNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR(TAG, "WiFi管理器未初始化");
        return false;
    }

    // 先尝试连接已保存的WiFi
    LOG_INFO(TAG, "尝试连接已保存的WiFi...");
    wifi::WifiError wifi_err = wifi_manager_->connectSavedNetwork();
    if (wifi_err == wifi::WifiError::NONE) {
        std::string ssid = wifi_manager_->getCurrentSSID();
        std::string ip   = wifi_manager_->getIPAddress();
        LOG_INFO(TAG, "  已保存WiFi连接成功: %s (IP: %s)", ssid.c_str(), ip.c_str());
        return true;
    }

    // 连接已保存WiFi失败，返回错误
    LOG_WARN(TAG, "已保存WiFi连接失败");
    return false;
}

// 断开网络
bool App::disconnectNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR(TAG, "WiFi管理器未初始化");
        return false;
    }

    if (!wifi_manager_->isConnected()) {
        LOG_INFO(TAG, "WiFi未连接");
        return true;
    }
    
    wifi::WifiError wifi_err = wifi_manager_->disconnect();
    if (wifi_err != wifi::WifiError::NONE) {
        LOG_ERROR(TAG, "WiFi断开失败");
        return false;
    }
    
    LOG_INFO(TAG, "WiFi已断开");
    return true;
}

// 搜索已保存的网络
bool App::searchSavedNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR(TAG, "WiFi管理器未初始化");
        return false;
    }

    // 获取已保存的网络列表
    std::vector<wifi::SavedNetworkInfo> saved = wifi_manager_->getSavedNetworks();

    if (saved.empty()) {
        LOG_INFO(TAG, "没有已保存的WiFi网络");
        return true;
    }

    LOG_INFO(TAG, "找到 %zu 个已保存的WiFi网络:", saved.size());

    // 输出每个网络的详细信息
    for (size_t i = 0; i < saved.size(); ++i) {
        const auto& net = saved[i];
        LOG_INFO(TAG, "  [%zu] SSID: %s", i + 1, net.ssid.c_str());
        LOG_INFO(TAG, "       网络ID: %d", net.network_id);

        if (net.is_current) {
            LOG_INFO(TAG, "       状态: 当前连接");
        } else {
            LOG_INFO(TAG, "       状态: 未连接");
        }

        if (net.is_enabled_auto) {
            LOG_INFO(TAG, "       自动连接: 启用");
        } else {
            LOG_INFO(TAG, "       自动连接: 禁用");
        }

        if (net.priority > 0) {
            LOG_INFO(TAG, "       优先级: %d", net.priority);
        }
    }

    return true;
}

// 忘记网络
bool App::forgetNetwork(const std::string& ssid) {
    if (!wifi_manager_) {
        LOG_ERROR(TAG, "WiFi管理器未初始化");
        return false;
    }

    if (ssid.empty()) {
        LOG_ERROR(TAG, "SSID不能为空");
        return false;
    }

    // 检查网络是否已保存
    if (!wifi_manager_->isNetworkSaved(ssid)) {
        LOG_WARN(TAG, "WiFi \"%s\" 未在已保存列表中", ssid.c_str());
        return false;
    }

    // 执行删除
    wifi::WifiError err = wifi_manager_->forgetNetwork(ssid);
    if (err != wifi::WifiError::NONE) {
        LOG_ERROR(TAG, "删除WiFi \"%s\" 失败: %d", ssid.c_str(), static_cast<int>(err));
        return false;
    }

    LOG_INFO(TAG, "WiFi \"%s\" 已删除", ssid.c_str());
    return true;
}

// 初始化音频系统
bool App::initAudio() {
    // 创建音频配置
    audio::AudioConfig audio_config;
    audio_config.sample_rate       = 48000; // 采样率
    audio_config.channels          = 1;     // 单声道
    audio_config.frame_duration_ms = 20;    // 帧时长（毫秒）

    // 创建音频系统
    audio_system_ = std::make_unique<audio::AudioSystem>(audio_config);

    // 初始化音频系统
    audio::AudioError audio_err = audio_system_->initialize();
    if (audio_err != audio::AudioError::NONE) {
        LOG_ERROR(TAG, "  音频系统初始化失败");
        audio_system_.reset();
        return false;
    }
    LOG_INFO(TAG, "  音频系统初始化成功");

    // 启动录音
    audio_err = audio_system_->startRecord();
    if (audio_err != audio::AudioError::NONE) {
        LOG_ERROR(TAG, "启动录音失败");
        return false;
    }

    return true;
}

// 释放音频系统
bool App::deinitAudio() {
    if (!audio_system_) {
        return true;
    }

    // 停止录音（如果正在录音）
    if (audio_system_->isRecording()) {
        audio_system_->stopRecord();
    }

    // 关闭音频系统
    audio_system_->shutdown();
    audio_system_.reset();

    return true;
}

// 初始化视频系统
bool App::initVideo() {
    // 创建视频配置
    video::VideoConfig video_config;
    video_config.width = 1920;            // 分辨率宽度
    video_config.height = 1080;           // 分辨率高度

    // 创建视频系统
    video_system_ = std::make_unique<video::VideoSystem>(video_config);

    // 初始化视频系统
    video::VideoError video_err = video_system_->initialize();
    if (video_err != video::VideoError::NONE) {
        LOG_ERROR(TAG, "  视频系统初始化失败");
        video_system_.reset();
        return false;
    }
    LOG_INFO(TAG, "  视频系统初始化成功");
    return true;
}

// 释放视频系统
bool App::deinitVideo() {
    if (!video_system_) {
        return true;
    }

    video_system_->shutdown();
    video_system_.reset();
    return true;
}

// 初始化聊天机器人
bool App::initChatbot() {
    // 确保音频系统已初始化
    if (!audio_system_) {
        LOG_ERROR(TAG, "音频系统未初始化，无法初始化聊天机器人");
        return false;
    }
    
    // 创建聊天机器人配置
    chatbot::ChatbotConfig chatbot_config;
    
    // 创建聊天机器人系统
    chatbot_system_ = std::make_unique<chatbot::ChatbotSystem>(chatbot_config);
    
    // 注入音频系统（必须在 open() 之前）
    chatbot_system_->setAudioSystem(audio_system_.get());
    
    // 注入视频系统（必须在 open() 之前）
    if (video_system_) {
        chatbot_system_->setVideoSystem(video_system_.get());
    }
    
    // 注入WiFi管理器（必须在 open() 之前）
    if (wifi_manager_) {
        chatbot_system_->setWifiManager(wifi_manager_.get());
    }
    
    // 打开聊天机器人系统
    chatbot::ChatbotError chatbot_err = chatbot_system_->open();
    if (chatbot_err != chatbot::ChatbotError::NONE) {
        LOG_ERROR(TAG, "  聊天机器人系统初始化失败: %s", 
                  chatbot::errorToString(chatbot_err));
        chatbot_system_.reset();
        return false;
    }
    
    LOG_INFO(TAG, "  聊天机器人系统初始化成功");
    return true;
}

// 释放聊天机器人
bool App::deinitChatbot() {
    if (!chatbot_system_) {
        return true;
    }
    
    // 关闭聊天机器人系统
    chatbot_system_->close();
    chatbot_system_.reset();
    
    LOG_INFO(TAG, "  聊天机器人系统已释放");
    return true;
}



// 初始化
void App::init() {
    // 初始化日志
    if (!initLog()) {
        return;
    }
    
    // 初始化音频系统
    if (!initAudio()) {
        return;
    }

    // 初始化视频系统
    if (!initVideo()) {
        return;
    }
    
    // 初始化网络
    if (!initNetwork()) {
        return;
    }

    // 初始化聊天机器人
    if (!initChatbot()) {
        return;
    }
    
    // 初始化完成
    LOG_INFO(TAG, "初始化完成");
}

// 运行
void App::run() {
    init();
    
    // 主循环保持程序运行，等待用户输入退出命令
    LOG_INFO(TAG, "========================================");
    LOG_INFO(TAG, "  系统运行中，等待唤醒词...");
    LOG_INFO(TAG, "  输入 'q' 或 'Q' 退出程序");
    LOG_INFO(TAG, "========================================");
    
    std::string input;
    while (std::cin >> input) {
        if (input == "q" || input == "Q") {
            LOG_INFO(TAG, "收到退出指令，正在关闭系统...");
            break;
        } else {
            LOG_INFO(TAG, "未知命令: %s ", input.c_str());
        }
    }
}

} // namespace app