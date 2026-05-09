/*
 * app.cc - 应用组装层
 */

#include "app.hpp"
#include "tool/log/log.hpp"
#include "chatbot/chatbot.hpp"
#include "chatbot/mcp/mcp_tool/mcp_tool.hpp"
#include "impl/audio_service_impl.hpp"
#include "impl/http_client_impl.hpp"
#include "impl/network_service_impl.hpp"
#include "impl/video_service_impl.hpp"
#include "media/audio/audio.hpp"
#include "media/camera/camera.hpp"
#include "media/media_config.hpp"
#include "network/wifi/wifi.hpp"
#include "tool/log/log.hpp"

#include <chrono>
#include <thread>

namespace app
{

    namespace
    {
        constexpr const char* LOG_TAG = "APP";
    }

    App::App() = default;

    App::~App()
    {
        cleanup();
    }

    void App::stop()
    {
        running_.store(false);
    }

    bool App::initLog()
    {
        return tool::log::Logger::inst().init(tool::log::LogConfig());
    }

    void App::deinitLog()
    {
        tool::log::Logger::inst().deinit();
    }

    bool App::initSync()
    {
        sync_ctx_ = std::make_shared<sync_context_t>();
        if (sync_init(sync_ctx_.get()) != 0)
        {
            LOG_ERROR(LOG_TAG, "同步初始化失败");
            return false;
        }
        return true;
    }

    void App::deinitSync()
    {
        if (sync_ctx_)
        {
            sync_deinit(sync_ctx_.get());
            sync_ctx_.reset();
        }
    }

    bool App::initAudio()
    {
        media::audio::AudioCfg cfg;
        cfg.capture.rate           = AUDIO_SAMPLE_RATE;
        cfg.capture.channels       = AUDIO_CHANNELS;
        cfg.capture.frame_ms       = AUDIO_FRAME_DURATION_MS;
        cfg.playback.rate          = AUDIO_SAMPLE_RATE;
        cfg.playback.channels      = AUDIO_CHANNELS;
        cfg.playback.frame_ms      = AUDIO_FRAME_DURATION_MS;
        cfg.playback.volume        = 70;
        cfg.opus.rate              = AUDIO_SAMPLE_RATE;
        cfg.opus.channels          = AUDIO_CHANNELS;
        cfg.opus.bitrate           = AUDIO_BIT_RATE;
        cfg.opus.vbr               = true;
        cfg.proc.denoise           = AUDIO_DENOISE_ENABLED;
        cfg.proc.agc               = AUDIO_AGC_ENABLED;
        cfg.proc.vad               = AUDIO_VAD_ENABLED;
        cfg.proc.dereverb          = AUDIO_DEREVERB_ENABLED;
        cfg.proc.agc_level         = AUDIO_AGC_LEVEL;
        cfg.proc.noise_suppress_db = AUDIO_NOISE_SUPPRESS_LEVEL;
        cfg.proc.echo_suppress_db  = AUDIO_ECHO_SUPPRESS_LEVEL;
        cfg.proc.agc_increment     = AUDIO_AGC_INCREMENT;
        cfg.proc.agc_decrement     = AUDIO_AGC_DECREMENT;
        cfg.proc.agc_max_gain_db   = AUDIO_AGC_MAX_GAIN;
        cfg.enable_capture         = true;
        cfg.enable_playback        = true;
        cfg.enable_opus            = true;
        cfg.enable_proc            = true;

        audio_drv_ = std::make_unique<media::audio::AudioDrv>();
        if (audio_drv_->init(cfg) != media::audio::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "音频初始化失败");
            return false;
        }
        if (audio_drv_->start() != media::audio::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "音频启动失败");
            return false;
        }

        audio_svc_ = std::make_unique<AudioServiceImpl>(audio_drv_.get());
        LOG_INFO(LOG_TAG, "音频就绪");
        return true;
    }

    void App::deinitAudio()
    {
        audio_svc_.reset();
        if (audio_drv_)
        {
            audio_drv_->stop();
            audio_drv_->deinit();
            audio_drv_.reset();
        }
    }

    bool App::initCamera()
    {
        media::camera::CameraCfg cfg;
        cfg.h264.width   = CAMERA_WIDTH;
        cfg.h264.height  = CAMERA_HEIGHT;
        cfg.h264.fps     = CAMERA_FPS;
        cfg.h264.bitrate = H264_Default_Bitrate;
        cfg.iq_file_dir  = ISP_PATH;

        camera_drv_ = std::make_unique<media::camera::CameraDrv>();
        if (camera_drv_->init(cfg, sync_ctx_) != media::camera::Error::OK ||
            camera_drv_->start() != media::camera::Error::OK)
        {
            LOG_WARN(LOG_TAG, "摄像头初始化失败，跳过");
            camera_drv_.reset();
            return true; /* 非致命 */
        }

        video_svc_ = std::make_unique<VideoServiceImpl>(camera_drv_.get());
        LOG_INFO(LOG_TAG, "视频就绪");
        return true;
    }

    void App::deinitCamera()
    {
        if (camera_drv_)
        {
            camera_drv_->stop();
            camera_drv_->deinit();
            camera_drv_.reset();
        }
        video_svc_.reset();
    }

    bool App::initNetwork()
    {
        network::wifi::WifiConfig cfg;
        cfg.auto_connect_on_init = true;

        wifi_mgr_ = std::make_unique<network::wifi::WifiManager>(cfg);
        if (wifi_mgr_->init() != network::wifi::WifiError::NONE)
        {
            LOG_ERROR(LOG_TAG, "WiFi初始化失败");
            return false;
        }

        if (wifi_mgr_->connectSavedNetwork() == network::wifi::WifiError::NONE)
        {
            LOG_INFO(LOG_TAG, "WiFi %s %s", wifi_mgr_->getCurrentSSID().c_str(),
                     wifi_mgr_->getIPAddress().c_str());
        }
        else
        {
            LOG_WARN(LOG_TAG, "WiFi未连接");
        }

        network_svc_ = std::make_unique<NetworkServiceImpl>(wifi_mgr_.get());
        LOG_INFO(LOG_TAG, "网络就绪");
        return true;
    }

    void App::deinitNetwork()
    {
        if (wifi_mgr_)
        {
            wifi_mgr_->deinit();
            wifi_mgr_.reset();
        }
        network_svc_.reset();
    }

    bool App::initChatbot()
    {
        http_svc_ = std::make_unique<HttpClientImpl>();

        chatbot::ChatbotConfig cfg;
        chatbot_ = std::make_unique<chatbot::ChatbotSystem>(cfg);

        /* MediaHandles: MCP 工具回调 */
        chatbot::mcp::mcp_tool::MediaHandles handles;

        if (audio_drv_ && audio_drv_->is_init())
        {
            handles.set_volume = [this](int v)
            { audio_drv_->setVolume(static_cast<uint8_t>(v)); };
            handles.get_volume = [this]()
            { return static_cast<int>(audio_drv_->volume()); };
        }

        if (camera_drv_ && camera_drv_->is_init())
        {
            handles.set_explain_url = [this](const std::string& u, const std::string& t)
            { camera_drv_->set_explain_url(u, t); };
            handles.explain_image = [this](const std::string& q)
            { return camera_drv_->explain_image(q); };
            handles.save_photo = [this](const std::string& path, std::function<void(bool)> cb)
            {
                camera_drv_->jpeg().save(path,
                                         [cb](const std::string&, media::camera::Error e)
                                         {
                                             if (cb)
                                                 cb(e == media::camera::Error::OK);
                                         });
                return true;
            };
            handles.start_record = [this](const std::string& path, int dur)
            { return camera_drv_->recorder().start(path, dur) == media::camera::Error::OK; };
            handles.stop_record  = [this]() { camera_drv_->recorder().stop(); };
            handles.is_recording = [this]() { return camera_drv_->recorder().is_recording(); };
            handles.is_running   = [this]() { return camera_drv_->is_running(); };
            handles.start_stream = [this]()
            { return camera_drv_->start() == media::camera::Error::OK; };
            handles.stop_stream = [this]() { camera_drv_->stop(); };
        }

        chatbot_->set_media_handles(handles);
        chatbot_->set_http_client(http_svc_.get());
        chatbot_->set_audio_service(audio_svc_.get());

        chatbot::ChatbotError err = chatbot_->init();
        if (err != chatbot::ChatbotError::NONE)
        {
            LOG_ERROR(LOG_TAG, "Chatbot初始化失败: %s", chatbot::error_to_string(err));
            return false;
        }

        LOG_INFO(LOG_TAG, "Chatbot就绪");
        return true;
    }

    void App::deinitChatbot()
    {
        if (chatbot_)
        {
            chatbot_->deinit();
            chatbot_.reset();
        }
        http_svc_.reset();
    }

    void App::cleanup()
    {
        deinitChatbot();
        deinitNetwork();
        deinitCamera();
        deinitAudio();
        deinitSync();
        deinitLog();
    }

    bool App::init()
    {
        if (!initLog())
            return false;

        LOG_INFO(LOG_TAG, "启动");

        if (!initSync())
        {
            deinitLog();
            return false;
        }

        if (!initAudio())
        {
            cleanup();
            return false;
        }

        if (!initCamera())
        {
            cleanup();
            return false;
        }

        if (!initNetwork())
        {
            cleanup();
            return false;
        }

        if (!initChatbot())
        {
            cleanup();
            return false;
        }

        return true;
    }

    void App::run()
    {
        LOG_INFO(LOG_TAG, "就绪，等待唤醒");

        while (running_.load() && chatbot_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            chatbot::ChatbotState state = chatbot_->get_state();
            if (state == chatbot::ChatbotState::ERROR || state == chatbot::ChatbotState::CLOSED)
            {
                LOG_WARN(LOG_TAG, "状态 %s，退出", chatbot::state_to_string(state));
                break;
            }
        }

        LOG_INFO(LOG_TAG, "关闭");
    }

} // namespace app
