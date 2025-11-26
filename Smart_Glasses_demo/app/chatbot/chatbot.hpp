#ifndef CHATBOT_HPP
#define CHATBOT_HPP

#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <cstdint>

#include "activation/activation.hpp"
#include "wakeword/wakeword.hpp"
#include "mcp/mcp.hpp"
#include "protocol_handle/protocol_handle.hpp"
#include "../protocol/websocket/websocket.hpp"

// 前向声明（避免循环依赖）
namespace app {
namespace media {
namespace audio {
class AudioSystem;
}
namespace camera {
class VideoSystem;
}
}
namespace network {
namespace wifi {
class WifiManager;
}
}
}

namespace app
{
    namespace chatbot
    {

        /**
         * @brief Chatbot系统状态
         * 
         * 状态转换流程：
         * UNINITIALIZED -> INITIALIZING -> ACTIVATING -> ACTIVATED -> READY 
         *   -> CONNECTING -> LISTENING -> SPEAKING -> READY (循环)
         *   -> ERROR (任何阶段) / CLOSED (关闭时)
         */
        enum class ChatbotState
        {
            UNINITIALIZED = 0,   // 未初始化（初始状态）
            INITIALIZING,        // 正在初始化（激活管理器、MCP、唤醒词检测器）
            ACTIVATING,          // 激活中（检查激活状态+轮询）
            ACTIVATED,           // 已激活（设备激活成功）
            READY,               // 就绪（等待唤醒词，可以开始AI对话）
            CONNECTING,          // 连接AI服务器中（唤醒词触发后）
            LISTENING,           // 监听用户语音中
            SPEAKING,            // AI回复中（TTS播放）
            ERROR,               // 错误状态
            CLOSED,              // 已关闭
        };

        /**
         * @brief Chatbot错误类型
         */
        enum class ChatbotError
        {
            NONE = 0, // 无错误

            // ============================================================
            // 初始化阶段错误
            // ============================================================
            INITIALIZATION_FAILED, // 初始化失败
            AUDIO_SYSTEM_ERROR,    // 音频系统错误（未设置或操作失败）

            // ============================================================
            // 激活阶段错误
            // ============================================================
            ACTIVATION_FAILED, // 激活失败

            // ============================================================
            // 服务准备阶段错误
            // ============================================================
            PROTOCOL_ERROR, // 协议错误（初始化/处理失败）
            WAKEWORD_ERROR, // 唤醒词错误（初始化/检测失败）
            MCP_ERROR,      // MCP工具错误（注册/调用失败）

            // ============================================================
            // 运行时错误
            // ============================================================
            CONNECTION_FAILED, // AI服务器连接失败

            // ============================================================
            // 通用错误
            // ============================================================
            INVALID_STATE,      // 无效状态（非法状态转换）
            TIMEOUT,            // 超时
            CALLBACK_EXCEPTION, // 回调异常
            UNKNOWN             // 未知错误
        };

        /**
         * @brief Chatbot系统配置
         */
        struct ChatbotConfig
        {
            // ============================================================
            // 服务器配置
            // ============================================================
            // std::string api_url = "ws://192.168.50.127:8000/xiaozhi/v1/"; // AI服务器URL
            // std::string activation_api_url ="http://192.168.50.127:8002/xiaozhi/ota/"; // 激活API URL

            std::string api_url = "wss://api.tenclass.net/xiaozhi/v1/";
            std::string activation_api_url = "https://api.tenclass.net/xiaozhi/ota/";

            // ============================================================
            // 设备信息（空字符串表示自动获取）
            // ============================================================
            std::string device_id;                               // 设备ID
            std::string client_id;                               // 客户端ID
            std::string config_file_path = "./system_para.conf"; // UUID配置文件路径

            // ============================================================
            // 超时配置（可选，有默认值）
            // ============================================================
            int activation_timeout_sec = 300; // 激活超时（5分钟）
            int connection_timeout_sec = 10;  // AI服务器连接超时（10秒）
            int network_timeout_sec    = 30;  // 网络连接超时（30秒）
            int delay_conversation_sec = 1;   // 延迟对话时间（2秒）

            // ============================================================
            // 唤醒词配置
            // ============================================================
            std::string wakeword_resource_file = "./third_party/snowboy/resources/common.res";
            std::string wakeword_model_file    = "./third_party/snowboy/resources/models/echo.pmdl";
            float       wakeword_sensitivity   = 0.5f;
            float       wakeword_audio_gain    = 1.0f;

            // ============================================================
            // 功能开关
            // ============================================================
            bool enable_interrupt_conversation = false; // 对话打断功能开关
        };

        class ChatbotSystem
        {
        private:
            // 配置
            ChatbotConfig config_;

            // 核心模块（智能指针管理）
            std::unique_ptr<activation::DeviceActivation>                   activation_manager_;
            std::unique_ptr<mcp::McpServer>                                 mcp_server_;
            std::unique_ptr<app::chatbot::protocol_handle::ProtocolHandler> protocol_handler_;
            std::unique_ptr<app::protocol::websocket::WebSocketClient>      ws_client_;
            std::unique_ptr<wakeword::WakewordDetector>                     wakeword_detector_;

            // 外部注入的音频系统（不由 ChatbotSystem 管理生命周期）
            app::media::audio::AudioSystem* audio_system_{nullptr};

            // 外部注入的视频系统（不由 ChatbotSystem 管理生命周期）
            app::media::camera::VideoSystem* video_system_{nullptr};

            // 外部注入的WiFi管理器（不由 ChatbotSystem 管理生命周期）
            app::network::wifi::WifiManager* wifi_manager_{nullptr};

            // 状态
            std::atomic<ChatbotState> state_{ChatbotState::UNINITIALIZED};

            // 激活管理
            ChatbotError getDeviceId(); // 初始化设备信息（如果配置为空则自动获取）
            ChatbotError initializeActivation();   // 初始化激活管理器
            ChatbotError deinitializeActivation(); // 释放激活管理器
            ChatbotError checkActivation();        // 激活检测

            // MCP工具管理
            ChatbotError initializeMCP();   // 初始化MCP服务器并注册工具
            ChatbotError deinitializeMCP(); // 释放MCP服务器

            // 协议处理器管理
            ChatbotError initializeProtocol();     // 初始化协议处理器
            ChatbotError deinitializeProtocol();   // 释放协议处理器
            void         setupProtocolCallbacks(); // 设置协议回调

            // WebSocket连接管理
            ChatbotError initializeWebSocket();     // 初始化WebSocket客户端
            ChatbotError deinitializeWebSocket();   // 释放WebSocket客户端
            ChatbotError connectAIServer();         // 连接AI服务器（阻塞，无限重连）
            void         setupWebSocketCallbacks(); // 设置WebSocket回调

            // 唤醒词管理
            ChatbotError initializeWakeword();         // 初始化唤醒词检测器
            ChatbotError deinitializeWakeword();       // 释放唤醒词检测器
            void         setupWakewordCallbacks();     // 设置唤醒词回调
            void         setupWakewordAudioCallback(); // 设置音频系统的唤醒词回调
            void setupAIAudioCallback(); // 设置AI音频流回调（发送音频到服务器）

        public:
            explicit ChatbotSystem(const ChatbotConfig& config = ChatbotConfig());
            ~ChatbotSystem();

            // AI聊天机器人系统的打开和关闭
            ChatbotError open();
            void         close();

            // 设置外部音频系统（必须在 open() 之前调用）
            void setAudioSystem(app::media::audio::AudioSystem* audio_system);

            // 设置外部视频系统（必须在 open() 之前调用）
            void setVideoSystem(app::media::camera::VideoSystem* video_system);

            // 设置外部WiFi管理器（必须在 open() 之前调用）
            void setWifiManager(app::network::wifi::WifiManager* wifi_manager);

            // 状态查询
            ChatbotState getState() const;
            bool         isReady() const;
        };

        // ============================================================================
        // 辅助函数
        // ============================================================================

        /**
         * @brief 状态转字符串
         */
        inline const char* stateToString(ChatbotState state)
        {
            switch (state)
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

        /**
         * @brief 错误转字符串
         */
        inline const char* errorToString(ChatbotError error)
        {
            switch (error)
            {
            case ChatbotError::NONE:
                return "NONE";
            case ChatbotError::INITIALIZATION_FAILED:
                return "INITIALIZATION_FAILED";
            case ChatbotError::AUDIO_SYSTEM_ERROR:
                return "AUDIO_SYSTEM_ERROR";
            case ChatbotError::ACTIVATION_FAILED:
                return "ACTIVATION_FAILED";
            case ChatbotError::PROTOCOL_ERROR:
                return "PROTOCOL_ERROR";
            case ChatbotError::WAKEWORD_ERROR:
                return "WAKEWORD_ERROR";
            case ChatbotError::MCP_ERROR:
                return "MCP_ERROR";
            case ChatbotError::CONNECTION_FAILED:
                return "CONNECTION_FAILED";
            case ChatbotError::INVALID_STATE:
                return "INVALID_STATE";
            case ChatbotError::TIMEOUT:
                return "TIMEOUT";
            case ChatbotError::CALLBACK_EXCEPTION:
                return "CALLBACK_EXCEPTION";
            case ChatbotError::UNKNOWN:
                return "UNKNOWN";
            default:
                return "INVALID";
            }
        }

    } // namespace chatbot
} // namespace app

#endif // CHATBOT_HPP