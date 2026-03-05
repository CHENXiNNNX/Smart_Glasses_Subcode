/* chatbot.hpp - Chatbot 编排 */

#pragma once

#include "mcp/mcp_tool/mcp_tool.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace app
{
    class IAudioService;
    class IHttpClient;
} // namespace app

namespace app::chatbot
{

    enum class ChatbotState
    {
        UNINITIALIZED = 0,
        INITIALIZING,
        ACTIVATING,
        ACTIVATED,
        READY,
        CONNECTING,
        LISTENING,
        SPEAKING,
        ERROR,
        CLOSED
    };

    enum class ChatbotError
    {
        NONE = 0,
        INITIALIZATION_FAILED,
        ACTIVATION_FAILED,
        CONNECTION_FAILED,
        INVALID_STATE,
        UNKNOWN
    };

    struct ChatbotConfig
    {
        std::string api_url            = "wss://api.tenclass.net/xiaozhi/v1/";
        std::string activation_api_url = "https://api.tenclass.net/xiaozhi/ota/";
        std::string device_id;
        std::string client_id;
        std::string config_file_path = "./system_para.conf";

        int activation_timeout_sec = 300;
        int connection_timeout_sec = 10;
        int delay_conversation_sec = 1; /* TTS 结束后延迟再发 Listen */

        std::string wakeword_resource_file = "./third_party/snowboy/resources/common.res";
        std::string wakeword_model_file    = "./third_party/snowboy/resources/models/echo.pmdl";
        float       wakeword_sensitivity   = 0.8f;
        float       wakeword_audio_gain    = 1.0f;
    };

    class ChatbotSystem
    {
    public:
        explicit ChatbotSystem(const ChatbotConfig& config = ChatbotConfig());
        ~ChatbotSystem();

        ChatbotSystem(const ChatbotSystem&)            = delete;
        ChatbotSystem& operator=(const ChatbotSystem&) = delete;

        /* 依赖注入 */
        void set_media_handles(const mcp::mcp_tool::MediaHandles& handles);
        void set_http_client(app::IHttpClient* client);
        void set_audio_service(app::IAudioService* svc);

        /* 生命周期 */
        ChatbotError init();
        void         deinit();

        /* 状态 */
        ChatbotState get_state() const;
        bool         is_ready() const;

        /* 断开连接 */
        void disconnect();

    private:
        void         setupWakewordCallbacks();
        void         setupWakewordAudioCallback();
        void         setupAIAudioCallback();
        ChatbotError connectAIServer();

        struct Impl;
        std::shared_ptr<Impl> impl_;
    };

    inline const char* state_to_string(ChatbotState s)
    {
        switch (s)
        {
        case ChatbotState::UNINITIALIZED:
            return "UNINITIALIZED";
        case ChatbotState::INITIALIZING:
            return "INITIALIZING";
        case ChatbotState::ACTIVATING:
            return "ACTIVATING";
        case ChatbotState::ACTIVATED:
            return "ACTIVATED";
        case ChatbotState::READY:
            return "READY";
        case ChatbotState::CONNECTING:
            return "CONNECTING";
        case ChatbotState::LISTENING:
            return "LISTENING";
        case ChatbotState::SPEAKING:
            return "SPEAKING";
        case ChatbotState::ERROR:
            return "ERROR";
        case ChatbotState::CLOSED:
            return "CLOSED";
        default:
            return "UNKNOWN";
        }
    }

    inline const char* error_to_string(ChatbotError e)
    {
        switch (e)
        {
        case ChatbotError::NONE:
            return "NONE";
        case ChatbotError::INITIALIZATION_FAILED:
            return "INITIALIZATION_FAILED";
        case ChatbotError::ACTIVATION_FAILED:
            return "ACTIVATION_FAILED";
        case ChatbotError::CONNECTION_FAILED:
            return "CONNECTION_FAILED";
        case ChatbotError::INVALID_STATE:
            return "INVALID_STATE";
        case ChatbotError::UNKNOWN:
            return "UNKNOWN";
        default:
            return "INVALID";
        }
    }

} // namespace app::chatbot
