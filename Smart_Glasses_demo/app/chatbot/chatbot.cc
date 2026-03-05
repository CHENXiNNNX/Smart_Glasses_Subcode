/* chatbot.cc - Chatbot 编排 */

#include "chatbot.hpp"
#include "activation/activation.hpp"
#include "mcp/mcp.hpp"
#include "mcp/mcp_tool/mcp_tool.hpp"
#include "protocol_handle/protocol_handle.hpp"
#include "wakeword/wakeword.hpp"
#include "../interfaces/iaudio_service.hpp"
#include "../interfaces/ihttp_client.hpp"
#include "../protocol/websocket/websocket.hpp"
#include "../tool/log/log.hpp"
#include "../tool/mac/mac.hpp"
#include "../tool/uuid/uuid.hpp"

#include <chrono>
#include <thread>

#define TAG "CHATBOT"

namespace app::chatbot
{

    using namespace app::tool::log;
    namespace activation      = app::chatbot::activation;
    namespace protocol_handle = app::chatbot::protocol_handle;
    namespace websocket       = app::protocol::websocket;
    namespace wakeword_ns     = app::chatbot::wakeword;

    struct ChatbotSystem::Impl
    {
        ChatbotConfig               config;
        mcp::mcp_tool::MediaHandles media_handles;
        app::IHttpClient*           http_client = nullptr;
        app::IAudioService*         audio_svc   = nullptr;

        std::atomic<ChatbotState> state{ChatbotState::UNINITIALIZED};
        std::atomic<bool>         shutdown{false};

        std::unique_ptr<activation::DeviceActivation>     activation;
        std::unique_ptr<mcp::McpServer>                   mcp_server;
        std::unique_ptr<protocol_handle::ProtocolHandler> protocol;
        std::unique_ptr<websocket::WebSocketClient>       ws_client;
        std::unique_ptr<wakeword_ns::WakewordDetector>    wakeword;
    };

    ChatbotSystem::ChatbotSystem(const ChatbotConfig& config) : impl_(std::make_shared<Impl>())
    {
        impl_->config = config;
    }

    ChatbotSystem::~ChatbotSystem()
    {
        deinit();
    }

    void ChatbotSystem::set_media_handles(const mcp::mcp_tool::MediaHandles& handles)
    {
        impl_->media_handles = handles;
    }

    void ChatbotSystem::set_http_client(app::IHttpClient* client)
    {
        impl_->http_client = client;
    }

    void ChatbotSystem::set_audio_service(app::IAudioService* svc)
    {
        impl_->audio_svc = svc;
    }

    ChatbotError ChatbotSystem::init()
    {
        Impl& i = *impl_;
        i.state.store(ChatbotState::INITIALIZING);

        /* 1. 设备 ID */
        if (i.config.device_id.empty())
        {
            i.config.device_id = app::tool::mac::getWirelessMacAddress();
            if (i.config.device_id.empty())
            {
                LOG_ERROR(TAG, "获取MAC失败");
                i.state.store(ChatbotState::ERROR);
                return ChatbotError::INITIALIZATION_FAILED;
            }
        }
        if (i.config.client_id.empty())
        {
            i.config.client_id = app::tool::uuid::generateUUID(i.config.config_file_path);
            if (i.config.client_id.empty())
            {
                LOG_ERROR(TAG, "生成UUID失败");
                i.state.store(ChatbotState::ERROR);
                return ChatbotError::INITIALIZATION_FAILED;
            }
        }
        LOG_INFO(TAG, "设备 %s 客户端 %s", i.config.device_id.c_str(), i.config.client_id.c_str());

        /* 激活 */
        if (i.http_client && i.http_client->valid())
        {
            try
            {
                activation::ActivationConfig act_cfg;
                act_cfg.api_url           = i.config.activation_api_url;
                act_cfg.activation_url    = "https://xiaozhi.me";
                act_cfg.poll_interval_sec = 5;
                act_cfg.poll_timeout_sec  = i.config.activation_timeout_sec;
                act_cfg.verify_ssl        = false;

                i.activation =
                    std::make_unique<activation::DeviceActivation>(act_cfg, i.http_client);
                i.state.store(ChatbotState::ACTIVATING);

                while (true)
                {
                    activation::ActivationResult r =
                        i.activation->checkActivation(i.config.device_id, i.config.client_id);
                    if (r.isActivated())
                    {
                        LOG_INFO(TAG, "已激活");
                        i.state.store(ChatbotState::ACTIVATED);
                        break;
                    }
                    if (r.status == activation::ActivationStatus::NOT_ACTIVATED)
                        LOG_INFO(TAG, "未激活 %s", r.activation_code.c_str());
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(TAG, "激活失败: %s", e.what());
                i.state.store(ChatbotState::ERROR);
                return ChatbotError::ACTIVATION_FAILED;
            }
        }
        else
        {
            LOG_WARN(TAG, "HTTP未设置，跳过激活");
            i.state.store(ChatbotState::ACTIVATED);
        }

        /* 3. MCP */
        try
        {
            mcp::McpConfig mcp_cfg;
            i.mcp_server = std::make_unique<mcp::McpServer>(mcp_cfg);

            if (i.media_handles.set_explain_url)
            {
                i.mcp_server->setVisionConfigCallback(
                    [h = i.media_handles](const std::string& url, const std::string& token)
                    {
                        if (h.set_explain_url)
                            h.set_explain_url(url, token);
                    });
            }

            int n = mcp::mcp_tool::register_tools(*i.mcp_server, i.media_handles);
            LOG_INFO(TAG, "MCP工具 %d 个", n);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR(TAG, "MCP失败 %s", e.what());
            i.state.store(ChatbotState::ERROR);
            return ChatbotError::INITIALIZATION_FAILED;
        }

        /* 协议 */
        try
        {
            protocol_handle::ProtocolConfig proto_cfg;
            proto_cfg.enable_mcp = true;
            i.protocol           = std::make_unique<protocol_handle::ProtocolHandler>(proto_cfg);

            i.protocol->setHelloCallback(
                [this](const protocol_handle::HelloMessage& msg)
                {
                    if (impl_->protocol)
                        impl_->protocol->setSessionId(msg.session_id);
                });

            i.protocol->setMCPCallback(
                [this](const std::string& payload) -> std::string
                {
                    if (!impl_->mcp_server)
                        return "{\"error\":\"MCP not available\"}";

                    std::string response = impl_->mcp_server->handle_message(payload);
                    if (!response.empty() && impl_->ws_client && impl_->ws_client->isHandshaked() &&
                        impl_->protocol)
                    {
                        std::string session_id = impl_->protocol->getSessionId();
                        std::string msg        = "{\"session_id\":\"" + session_id +
                                          "\",\"type\":\"mcp\",\"payload\":" + response + "}";
                        if (impl_->ws_client->sendText(msg) != websocket::WebSocketError::NONE)
                            LOG_ERROR(TAG, "MCP响应发送失败");
                    }
                    return response;
                });

            i.protocol->setSTTCallback([](const protocol_handle::STTMessage&) {});

            i.protocol->setLLMCallback(
                [this](const protocol_handle::LLMMessage& msg)
                {
                    (void)msg;
                    impl_->state.store(ChatbotState::SPEAKING);
                });

            i.protocol->setTTSCallback(
                [this](const protocol_handle::TTSMessage& msg)
                {
                    Impl& ii = *impl_;
                    if (msg.state == protocol_handle::TTSState::START)
                    {
                        if (ii.audio_svc && !ii.audio_svc->isPlaybackRunning())
                            ii.audio_svc->startPlayback();
                    }
                    else if (msg.state == protocol_handle::TTSState::STOP)
                    {
                        if (ii.audio_svc)
                            ii.audio_svc->stopPlayback();

                        int delay_sec = ii.config.delay_conversation_sec;
                        std::thread(
                            [impl = impl_, delay_sec]()
                            {
                                std::this_thread::sleep_for(std::chrono::seconds(delay_sec));
                                if (impl->shutdown.load())
                                    return;
                                Impl& ii = *impl;
                                if (!ii.ws_client || !ii.ws_client->isHandshaked())
                                    return;
                                if (ii.state.load() == ChatbotState::CLOSED ||
                                    ii.state.load() == ChatbotState::ERROR)
                                    return;
                                if (!ii.audio_svc || !ii.audio_svc->isCaptureRunning())
                                {
                                    ii.state.store(ChatbotState::READY);
                                    return;
                                }
                                if (ii.protocol && ii.ws_client)
                                {
                                    std::string listen_msg = ii.protocol->generateListenMessage(
                                        protocol_handle::ListenState::START,
                                        protocol_handle::ListenMode::AUTO);
                                    if (ii.ws_client->sendText(listen_msg) ==
                                        websocket::WebSocketError::NONE)
                                        ii.state.store(ChatbotState::LISTENING);
                                }
                            })
                            .detach();
                    }
                });
        }
        catch (const std::exception& e)
        {
            LOG_ERROR(TAG, "协议失败 %s", e.what());
            i.state.store(ChatbotState::ERROR);
            return ChatbotError::INITIALIZATION_FAILED;
        }

        /* WebSocket */
        try
        {
            std::string                hello = i.protocol->generateHelloMessage(16000, 1, 20);
            websocket::WebSocketConfig ws_cfg;
            ws_cfg.url                   = i.config.api_url;
            ws_cfg.hello_message         = hello;
            ws_cfg.auto_reconnect        = false;
            ws_cfg.connect_timeout_ms    = i.config.connection_timeout_sec * 1000;
            ws_cfg.verify_ssl            = false;
            ws_cfg.headers["Device-Id"]  = i.config.device_id;
            ws_cfg.headers["Client-Id"]  = i.config.client_id;
            ws_cfg.headers["User-Agent"] = "SmartGlasses/1.0";

            i.ws_client = std::make_unique<websocket::WebSocketClient>(ws_cfg);
            i.ws_client->setTextCallback(
                [this](const char* data, size_t size) -> bool
                {
                    if (impl_->protocol)
                        impl_->protocol->parseMessage(data, size);
                    return true;
                });

            i.ws_client->setBinaryCallback(
                [this](const char* data, size_t size) -> bool
                {
                    if (!impl_->audio_svc)
                        return false;
                    if (size <= 16)
                        return false;
                    const uint8_t* opus_data = reinterpret_cast<const uint8_t*>(data) + 16;
                    size_t         opus_len  = size - 16;
                    if (!impl_->audio_svc->decodeAndPlay(opus_data, opus_len))
                        return false;
                    if (!impl_->audio_svc->isPlaybackRunning())
                        impl_->audio_svc->startPlayback();
                    return true;
                });

            i.ws_client->setStateCallback(
                [this](websocket::ConnectionState, websocket::ConnectionState new_state)
                {
                    if (new_state == websocket::ConnectionState::CLOSED ||
                        new_state == websocket::ConnectionState::DISCONNECTED)
                    {
                        ChatbotState cur = impl_->state.load();
                        if (cur != ChatbotState::CLOSED && cur != ChatbotState::READY)
                        {
                            impl_->state.store(ChatbotState::READY);
                            LOG_INFO(TAG, "断开");
                        }
                    }
                });

            i.ws_client->setErrorCallback([](websocket::WebSocketError, const std::string& msg)
                                          { LOG_ERROR(TAG, "WS %s", msg.c_str()); });
        }
        catch (const std::exception& e)
        {
            LOG_ERROR(TAG, "WS失败 %s", e.what());
            i.state.store(ChatbotState::ERROR);
            return ChatbotError::INITIALIZATION_FAILED;
        }

        /* 唤醒词 */
        try
        {
            wakeword_ns::WakewordConfig ww_cfg;
            ww_cfg.resource_file = i.config.wakeword_resource_file;
            ww_cfg.model_file    = i.config.wakeword_model_file;
            ww_cfg.sensitivity   = i.config.wakeword_sensitivity;
            ww_cfg.audio_gain    = i.config.wakeword_audio_gain;
            i.wakeword           = std::make_unique<wakeword_ns::WakewordDetector>(ww_cfg);
            if (i.wakeword->init() == wakeword_ns::WakewordError::NONE)
            {
                i.wakeword->setEnabled(true);
                setupWakewordCallbacks();
                setupWakewordAudioCallback();
                setupAIAudioCallback();
            }
        }
        catch (...)
        {
            LOG_WARN(TAG, "唤醒词初始化失败，跳过");
        }

        i.state.store(ChatbotState::READY);
        LOG_INFO(TAG, "就绪");
        return ChatbotError::NONE;
    }

    void ChatbotSystem::setupWakewordCallbacks()
    {
        Impl& i = *impl_;
        if (!i.wakeword)
            return;

        i.wakeword->setWakewordCallback(
            [this](wakeword_ns::WakewordResult result, int hotword_index)
            {
                if (result != wakeword_ns::WakewordResult::HOTWORD_1 &&
                    result != wakeword_ns::WakewordResult::HOTWORD_2 &&
                    result != wakeword_ns::WakewordResult::HOTWORD_3)
                    return;

                if (impl_->state.load() != ChatbotState::READY)
                    return;

                impl_->state.store(ChatbotState::CONNECTING);
                LOG_INFO(TAG, "唤醒 %d", hotword_index);

                ChatbotError err = connectAIServer();
                if (err != ChatbotError::NONE)
                {
                    LOG_ERROR(TAG, "连接失败");
                    impl_->state.store(ChatbotState::READY);
                    return;
                }

                if (!impl_->protocol || !impl_->ws_client || !impl_->ws_client->isHandshaked() ||
                    !impl_->audio_svc)
                {
                    impl_->state.store(ChatbotState::READY);
                    return;
                }

                if (!impl_->audio_svc->isCaptureRunning() && !impl_->audio_svc->startCapture())
                {
                    LOG_ERROR(TAG, "采集启动失败");
                    impl_->state.store(ChatbotState::READY);
                    return;
                }

                std::string listen_msg = impl_->protocol->generateListenMessage(
                    protocol_handle::ListenState::START, protocol_handle::ListenMode::AUTO);

                if (impl_->ws_client->sendText(listen_msg) != websocket::WebSocketError::NONE)
                {
                    impl_->audio_svc->stopCapture();
                    impl_->state.store(ChatbotState::READY);
                    return;
                }

                impl_->state.store(ChatbotState::LISTENING);
                LOG_INFO(TAG, "监听");
            });

        i.wakeword->setErrorCallback([](wakeword_ns::WakewordError, const std::string&) {});
    }

    void ChatbotSystem::setupWakewordAudioCallback()
    {
        Impl& i = *impl_;
        if (!i.audio_svc || !i.wakeword)
            return;

        i.audio_svc->setWakewordCallback(
            [this](const int16_t* data, size_t length)
            {
                if (impl_->state.load() != ChatbotState::READY)
                    return;
                if (impl_->wakeword && impl_->wakeword->isEnabled())
                    impl_->wakeword->processAudioFrame(data, static_cast<int>(length));
            });
    }

    void ChatbotSystem::setupAIAudioCallback()
    {
        Impl& i = *impl_;
        if (!i.audio_svc)
            return;

        i.audio_svc->setCaptureCallback(
            [this](const app::AudioFrameView& frame)
            {
                if (!frame.data || frame.size == 0)
                    return;
                if (!impl_->ws_client || !impl_->ws_client->isHandshaked())
                    return;
                if (impl_->state.load() != ChatbotState::LISTENING)
                    return;

                websocket::WebSocketError err = impl_->ws_client->sendBinary(
                    reinterpret_cast<const char*>(frame.data), frame.size);
                if (err != websocket::WebSocketError::NONE)
                    LOG_WARN(TAG, "音频发送失败");
            });
    }

    ChatbotError ChatbotSystem::connectAIServer()
    {
        Impl& i = *impl_;
        if (!i.ws_client)
            return ChatbotError::CONNECTION_FAILED;

        int retry = 0;
        while (true)
        {
            if (i.shutdown.load())
                return ChatbotError::CONNECTION_FAILED;

            websocket::WebSocketError err = i.ws_client->connect();
            if (err == websocket::WebSocketError::NONE)
            {
                int wait_cnt = 0;
                while (!i.ws_client->isHandshaked() && wait_cnt < 100)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    wait_cnt++;
                }
                if (i.ws_client->isHandshaked())
                {
                    LOG_INFO(TAG, "已连接");
                    return ChatbotError::NONE;
                }
            }

            retry++;
            LOG_WARN(TAG, "连接失败 %d", static_cast<int>(err));
            std::this_thread::sleep_for(std::chrono::seconds(i.config.connection_timeout_sec));
        }
    }

    void ChatbotSystem::deinit()
    {
        Impl& i = *impl_;
        i.shutdown.store(true);
        if (i.audio_svc)
            i.audio_svc->stopCapture();
        if (i.wakeword)
        {
            i.wakeword->setEnabled(false);
            i.wakeword.reset();
        }
        if (i.ws_client)
        {
            i.ws_client->disconnect();
            i.ws_client.reset();
        }
        i.protocol.reset();
        i.mcp_server.reset();
        i.activation.reset();
        i.state.store(ChatbotState::CLOSED);
    }

    ChatbotState ChatbotSystem::get_state() const
    {
        return impl_->state.load();
    }

    bool ChatbotSystem::is_ready() const
    {
        return impl_->state.load() == ChatbotState::READY;
    }

    void ChatbotSystem::disconnect()
    {
        if (impl_->ws_client)
            impl_->ws_client->disconnect();
    }

} // namespace app::chatbot
