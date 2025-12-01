#include "chatbot.hpp"
#include "../tool/log/log.hpp"
#include "../tool/mac/mac.hpp"
#include "../tool/uuid/uuid.hpp"
#include "../media/audio/audio.hpp"
#include "../media/camera/camera.hpp"
#include "../protocol/webrtc/signaling.hpp"
#include "../protocol/webrtc/webrtc.hpp"
#include "mcp/mcp_tool/mcp_tool.hpp"
#include "activation/activation.hpp"
#include "../protocol/websocket/websocket.hpp"

#include <thread>
#include <chrono>

using namespace app::tool::log;
namespace activation      = app::chatbot::activation;
namespace mcp             = app::chatbot::mcp;
namespace mcp_tool        = app::chatbot::mcp::mcp_tool;
namespace protocol_handle = app::chatbot::protocol_handle;
namespace websocket       = app::protocol::websocket;
namespace wakeword        = app::chatbot::wakeword;

namespace app
{
    namespace chatbot
    {

            namespace
            {
                constexpr const char* LOG_TAG = "CHATBOT";
            } // namespace

        ChatbotSystem::ChatbotSystem(const ChatbotConfig& config) : config_(config)
        {
            LOG_INFO(LOG_TAG, "ChatbotSystem created");
        }

        ChatbotSystem::~ChatbotSystem()
        {
            close();
            LOG_INFO(LOG_TAG, "ChatbotSystem destroyed");
        }

        // 设置外部音频系统
        void ChatbotSystem::setAudioSystem(app::media::audio::AudioSystem* audio_system)
        {
            audio_system_ = audio_system;
        }

        // 设置外部视频系统
        void ChatbotSystem::setVideoSystem(app::media::camera::VideoSystem* video_system)
        {
            video_system_ = video_system;
        }

        // 设置外部WiFi管理器
        void ChatbotSystem::setWifiManager(app::network::wifi::WifiManager* wifi_manager)
        {
            wifi_manager_ = wifi_manager;
        }

        void ChatbotSystem::setSignaling(app::protocol::webrtc::Signaling* signaling)
        {
            signaling_ = signaling;
        }

        void ChatbotSystem::setWebRTCSystem(app::protocol::webrtc::WebRTCSystem* webrtc_system)
        {
            webrtc_system_ = webrtc_system;
        }

        ChatbotError ChatbotSystem::getDeviceId()
        {
            // 获取设备ID
            if (config_.device_id.empty())
            {
                config_.device_id = app::tool::mac::getWirelessMacAddress();
                if (config_.device_id.empty())
                {
                    LOG_ERROR(LOG_TAG, "获取MAC地址失败");
                    return ChatbotError::INITIALIZATION_FAILED;
                }
            }
            LOG_INFO(LOG_TAG, "MAC地址: %s", config_.device_id.c_str());

            // 获取客户端ID（UUID），如果配置为空则自动生成
            if (config_.client_id.empty())
            {
                config_.client_id = app::tool::uuid::generateUUID(config_.config_file_path);
                if (config_.client_id.empty())
                {
                    LOG_ERROR(LOG_TAG, "生成UUID失败");
                    return ChatbotError::INITIALIZATION_FAILED;
                }
            }
            LOG_INFO(LOG_TAG, "UUID: %s", config_.client_id.c_str());

            return ChatbotError::NONE;
        }

        // ============================================================================
        // 激活管理器初始化
        // ============================================================================

        ChatbotError ChatbotSystem::initializeActivation()
        {
            // 获取设备信息
            ChatbotError err = getDeviceId();
            if (err != ChatbotError::NONE)
            {
                return err;
            }

            try
            {
                activation::ActivationConfig act_cfg;
                act_cfg.api_url           = config_.activation_api_url;
                act_cfg.activation_url    = "https://xiaozhi.me";
                act_cfg.poll_interval_sec = 5;
                act_cfg.poll_timeout_sec  = config_.activation_timeout_sec;
                act_cfg.verify_ssl        = false;

                activation_manager_ = std::make_unique<activation::DeviceActivation>(act_cfg);

                return ChatbotError::NONE;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(LOG_TAG, "激活管理器初始化失败: %s", e.what());
                return ChatbotError::ACTIVATION_FAILED;
            }
        }

        ChatbotError ChatbotSystem::deinitializeActivation()
        {
            if (activation_manager_)
            {
                activation_manager_.reset();
                LOG_INFO(LOG_TAG, "激活管理器已释放");
            }
            return ChatbotError::NONE;
        }

        ChatbotError ChatbotSystem::checkActivation()
        {
            if (!activation_manager_)
            {
                LOG_ERROR(LOG_TAG, "激活管理器未初始化");
                return ChatbotError::ACTIVATION_FAILED;
            }

            if (config_.device_id.empty() || config_.client_id.empty())
            {
                LOG_ERROR(LOG_TAG, "设备信息未初始化");
                return ChatbotError::INITIALIZATION_FAILED;
            }

            // 设置状态为激活中
            state_.store(ChatbotState::ACTIVATING);

            // 循环直到激活成功
            while (true)
            {
                // 检查激活状态
                activation::ActivationResult result =
                    activation_manager_->checkActivation(config_.device_id, config_.client_id);

                if (result.isActivated())
                {
                    // 激活成功
                    LOG_INFO(LOG_TAG, "========================================");
                    LOG_INFO(LOG_TAG, "  设备激活成功");
                    LOG_INFO(LOG_TAG, "========================================");

                    state_.store(ChatbotState::ACTIVATED);
                    return ChatbotError::NONE;
                }

                if (result.hasError())
                {
                    // 检查失败，记录错误并重试
                    LOG_WARN(LOG_TAG, "激活检查失败: %s，%d秒后重试...",
                             result.error_message.c_str(), config_.activation_timeout_sec);
                }
                else if (result.status == activation::ActivationStatus::NOT_ACTIVATED)
                {
                    // 未激活，显示激活码
                    LOG_INFO(LOG_TAG, "========================================");
                    LOG_INFO(LOG_TAG, "  ⚠ 设备未激活");
                    LOG_INFO(LOG_TAG, "  激活URL: https://xiaozhi.me");
                    LOG_INFO(LOG_TAG, "  激活码: %s", result.activation_code.c_str());
                    LOG_INFO(LOG_TAG, "  请使用激活码完成设备激活");
                    LOG_INFO(LOG_TAG, "========================================");
                }

                // 等待5秒后重试
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }

            return ChatbotError::NONE;
        }

        // ============================================================================
        // MCP工具注册
        // ============================================================================

        ChatbotError ChatbotSystem::initializeMCP()
        {
            try
            {
                // 创建MCP配置
                mcp::McpConfig mcp_cfg;
                mcp_cfg.server_name      = "Smart_Glasses";
                mcp_cfg.server_version   = "1.0.0";
                mcp_cfg.protocol_version = "2025-11-06";

                // 创建MCP服务器
                mcp_server_ = std::make_unique<mcp::McpServer>(mcp_cfg);

                // 设置vision配置回调
                if (video_system_)
                {
                    mcp_server_->setVisionConfigCallback(
                        [this](const std::string& url, const std::string& token)
                        { video_system_->setExplainUrl(url, token); });
                }

                // 注册所有MCP工具
                int tool_count = mcp_tool::McpToolManager::registerAllTools(
                    *mcp_server_, audio_system_, video_system_, wifi_manager_,
                    signaling_, webrtc_system_, this);
                LOG_INFO(LOG_TAG, "MCP工具注册完成，共注册 %d 个工具", tool_count);

                return ChatbotError::NONE;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(LOG_TAG, "MCP服务器初始化失败: %s", e.what());
                mcp_server_.reset();
                return ChatbotError::MCP_ERROR;
            }
        }

        ChatbotError ChatbotSystem::deinitializeMCP()
        {
            if (mcp_server_)
            {
                mcp_server_.reset();
                LOG_INFO(LOG_TAG, "MCP服务器已释放");
            }
            return ChatbotError::NONE;
        }

        // ============================================================================
        // 协议处理器初始化
        // ============================================================================

        ChatbotError ChatbotSystem::initializeProtocol()
        {
            try
            {
                // 创建协议配置
                protocol_handle::ProtocolConfig proto_cfg;
                proto_cfg.enable_async_processing = true;
                proto_cfg.message_queue_size      = 100;
                proto_cfg.enable_mcp              = true;

                // 创建协议处理器
                protocol_handler_ = std::make_unique<protocol_handle::ProtocolHandler>(proto_cfg);

                // 设置协议回调
                setupProtocolCallbacks();

                return ChatbotError::NONE;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(LOG_TAG, "协议处理器初始化失败: %s", e.what());
                protocol_handler_.reset();
                return ChatbotError::PROTOCOL_ERROR;
            }
        }

        ChatbotError ChatbotSystem::deinitializeProtocol()
        {
            if (protocol_handler_)
            {
                protocol_handler_.reset();
                LOG_INFO(LOG_TAG, "协议处理器已释放");
            }
            return ChatbotError::NONE;
        }

        void ChatbotSystem::setupProtocolCallbacks()
        {
            if (!protocol_handler_)
            {
                return;
            }

            // 设置Hello消息回调
            protocol_handler_->setHelloCallback(
                [this](const protocol_handle::HelloMessage& msg)
                {
                    LOG_INFO(LOG_TAG, "收到Hello响应，会话ID: %s", msg.session_id.c_str());
                    if (protocol_handler_)
                    {
                        protocol_handler_->setSessionId(msg.session_id);
                    }
                });

            // 设置STT消息回调
            protocol_handler_->setSTTCallback(
                [this](const protocol_handle::STTMessage& msg)
                {
                    (void)msg;
                });

            // 设置LLM消息回调
            protocol_handler_->setLLMCallback(
                [this](const protocol_handle::LLMMessage& msg)
                {
                    LOG_INFO(LOG_TAG, "收到LLM回复: %s", msg.text.c_str());
                    // LLM回复后，状态转为SPEAKING
                    state_.store(ChatbotState::SPEAKING);
                });

            // 设置TTS消息回调
            protocol_handler_->setTTSCallback(
                [this](const protocol_handle::TTSMessage& msg)
                {
                    if (msg.state == protocol_handle::TTSState::START)
                    {
                        // TTS开始：确保播放已启动
                        if (audio_system_ && !audio_system_->isStreamRunning(app::media::audio::StreamDirection::OUTPUT))
                        {
                            app::media::audio::AudioError err = audio_system_->startStream(app::media::audio::StreamDirection::OUTPUT);
                            if (err != app::media::audio::AudioError::NONE)
                            {
                                LOG_ERROR(LOG_TAG, "启动播放失败: %d", static_cast<int>(err));
                            }
                        }
                    }
                    else if (msg.state == protocol_handle::TTSState::SENTENCE_START &&
                             config_.enable_interrupt_conversation)
                    {
                        // 发送Listen消息，继续监听
                        if (protocol_handler_ && ws_client_ && ws_client_->isHandshaked() &&
                            audio_system_)
                        {
                            // 确保AI音频流正在运行
                            if (!audio_system_->isStreamActive(app::media::audio::StreamType::AI))
                            {
                                app::media::audio::AudioError err = audio_system_->startStream(app::media::audio::StreamType::AI);
                                if (err != app::media::audio::AudioError::NONE)
                                {
                                    LOG_ERROR(LOG_TAG, "启动AI音频流失败: %d",
                                              static_cast<int>(err));
                                }
                            }

                            // 发送Listen消息
                            std::string listen_msg = protocol_handler_->generateListenMessage(
                                protocol_handle::ListenState::START,
                                protocol_handle::ListenMode::AUTO);

                            websocket::WebSocketError ws_err = ws_client_->sendText(listen_msg);
                            if (ws_err == websocket::WebSocketError::NONE)
                            {
                                state_.store(ChatbotState::LISTENING);
                            }
                        }
                    }
                    else if (msg.state == protocol_handle::TTSState::STOP)
                    {
                        // TTS停止：停止播放
                        if (audio_system_)
                        {
                            audio_system_->stopStream(app::media::audio::StreamDirection::OUTPUT);
                        }

                        // 使用异步任务
                        std::thread(
                            [this]()
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds(
                                    config_.delay_conversation_sec * 1000));

                                // 检查WebSocket是否还连接
                                if (!ws_client_ || !ws_client_->isHandshaked())
                                {
                                    state_.store(ChatbotState::READY);
                                    return;
                                }

                                // 检查当前状态
                                ChatbotState current_state = state_.load();
                                if (current_state == ChatbotState::CLOSED ||
                                    current_state == ChatbotState::ERROR)
                                {
                                    return;
                                }

                                // 检查AI音频流是否还在运行
                                if (!audio_system_ || !audio_system_->isStreamActive(app::media::audio::StreamType::AI))
                                {
                                    state_.store(ChatbotState::READY);
                                    return;
                                }

                                // 重新发送Listen消息
                                if (protocol_handler_ && ws_client_)
                                {
                                    std::string listen_msg =
                                        protocol_handler_->generateListenMessage(
                                            protocol_handle::ListenState::START,
                                            protocol_handle::ListenMode::AUTO);

                                    websocket::WebSocketError ws_err =
                                        ws_client_->sendText(listen_msg);
                                    if (ws_err == websocket::WebSocketError::NONE)
                                    {
                                        state_.store(ChatbotState::LISTENING);
                                    }
                                    else
                                    {
                                        LOG_ERROR(LOG_TAG, "发送Listen消息失败: %d",
                                                  static_cast<int>(ws_err));
                                        state_.store(ChatbotState::READY);
                                    }
                                }
                            })
                            .detach();
                    }
                });

            // 设置MCP消息回调
            if (mcp_server_)
            {
                protocol_handler_->setMCPCallback(
                    [this](const std::string& mcp_payload) -> std::string
                    {
                        if (!mcp_server_)
                        {
                            return "{\"error\": \"MCP server not available\"}";
                        }

                        // 调用MCP服务器处理消息，返回响应
                        std::string response = mcp_server_->handle_message(mcp_payload);

                        // 将响应发送回服务器
                        if (!response.empty() && ws_client_ && ws_client_->isHandshaked() &&
                            protocol_handler_)
                        {
                            try
                            {
                                std::string session_id = protocol_handler_->getSessionId();
                                std::string mcp_response_msg =
                                    "{\"session_id\":\"" + session_id +
                                    "\",\"type\":\"mcp\",\"payload\":" + response + "}";

                                // 发送MCP响应
                                websocket::WebSocketError err =
                                    ws_client_->sendText(mcp_response_msg);
                                if (err != websocket::WebSocketError::NONE)
                                {
                                    LOG_ERROR(LOG_TAG, "发送MCP响应失败: %d",
                                              static_cast<int>(err));
                                }
                            }
                            catch (const std::exception& e)
                            {
                                LOG_ERROR(LOG_TAG, "构建MCP响应消息失败: %s", e.what());
                            }
                        }
                        else
                        {
                            if (!ws_client_ || !ws_client_->isHandshaked())
                            {
                                LOG_WARN(LOG_TAG, "WebSocket未连接，无法发送MCP响应");
                            }
                        }

                        // 返回响应（虽然已经发送，但保持接口一致性）
                        return response;
                    });
            }

            // 设置错误回调
            protocol_handler_->setErrorCallback(
                [this](const std::string& error)
                { LOG_ERROR(LOG_TAG, "协议错误: %s", error.c_str()); });

            LOG_INFO(LOG_TAG, "协议回调已设置");
        }

        // ============================================================================
        // WebSocket连接初始化
        // ============================================================================

        ChatbotError ChatbotSystem::initializeWebSocket()
        {
            ChatbotError err = initializeProtocol();
            if (err != ChatbotError::NONE)
            {
                LOG_ERROR(LOG_TAG, "协议处理器初始化失败");
                return err;
            }

            if (config_.device_id.empty() || config_.client_id.empty())
            {
                LOG_ERROR(LOG_TAG, "设备信息未初始化，无法初始化WebSocket");
                return ChatbotError::INITIALIZATION_FAILED;
            }

            try
            {
                // 生成Hello消息
                std::string hello_msg = protocol_handler_->generateHelloMessage(16000, // 16kHz
                                                                                1,     // Mono
                                                                                20     // 20ms frame
                );

                // 创建WebSocket配置
                websocket::WebSocketConfig ws_cfg;
                ws_cfg.url                = config_.api_url;
                ws_cfg.hello_message      = hello_msg;
                ws_cfg.auto_reconnect     = false;
                ws_cfg.connect_timeout_ms = config_.connection_timeout_sec * 1000;
                ws_cfg.verify_ssl         = false; // 不验证SSL证书

                // 添加HTTP Headers
                ws_cfg.headers["Device-Id"]  = config_.device_id;
                ws_cfg.headers["Client-Id"]  = config_.client_id;
                ws_cfg.headers["User-Agent"] = "SmartGlasses/1.0";

                // 创建WebSocket客户端
                ws_client_ = std::make_unique<websocket::WebSocketClient>(ws_cfg);
                LOG_INFO(LOG_TAG, "WebSocket客户端创建成功");

                // 设置WebSocket回调
                setupWebSocketCallbacks();

                return ChatbotError::NONE;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(LOG_TAG, "WebSocket客户端初始化失败: %s", e.what());
                ws_client_.reset();
                return ChatbotError::CONNECTION_FAILED;
            }
        }

        ChatbotError ChatbotSystem::deinitializeWebSocket()
        {
            if (ws_client_)
            {
                ws_client_->disconnect();
                ws_client_.reset();
                LOG_INFO(LOG_TAG, "WebSocket客户端已释放");
                deinitializeProtocol();
            }

            return ChatbotError::NONE;
        }

        void ChatbotSystem::setupWebSocketCallbacks()
        {
            if (!ws_client_ || !protocol_handler_)
            {
                return;
            }

            // 设置文本消息回调
            ws_client_->setTextCallback(
                [this](const char* data, size_t size) -> bool
                {
                    if (protocol_handler_)
                    {
                        protocol_handler_->parseMessage(data, size);
                    }
                    return true;
                });

            // 设置二进制消息回调
            ws_client_->setBinaryCallback(
                [this](const char* data, size_t size) -> bool
                {
                    if (!audio_system_)
                    {
                        LOG_WARN(LOG_TAG, "音频系统未初始化，无法处理TTS音频");
                        return false;
                    }

                    // 跳过16字节头部
                    if (size <= 16)
                    {
                        LOG_WARN(LOG_TAG, "TTS音频包太小: %zu bytes", size);
                        return false;
                    }

                    const uint8_t* opus_data =
                        reinterpret_cast<const uint8_t*>(data) + 16;
                    size_t opus_size = size - 16;

                    // 解码Opus音频数据
                    app::media::audio::AudioFramePtr frame =
                        audio_system_->decodeOpus(opus_data, opus_size);

                    if (!frame)
                    {
                        LOG_WARN(LOG_TAG, "Opus解码失败，音频数据: %zu bytes", opus_size);
                        return false;
                    }

                    // 推送播放帧
                    audio_system_->pushPlaybackFrame(frame);

                    // 如果播放未启动，自动启动播放
                    if (!audio_system_->isStreamRunning(app::media::audio::StreamDirection::OUTPUT))
                    {
                        app::media::audio::AudioError err = audio_system_->startStream(app::media::audio::StreamDirection::OUTPUT);
                        if (err != app::media::audio::AudioError::NONE)
                        {
                            LOG_ERROR(LOG_TAG, "启动播放失败: %d", static_cast<int>(err));
                        }
                    }

                    return true;
                });

            // 设置连接状态回调
            ws_client_->setStateCallback(
                [this](websocket::ConnectionState old_state, websocket::ConnectionState new_state)
                {
                    LOG_INFO(LOG_TAG, "WebSocket状态: %d -> %d", static_cast<int>(old_state),
                             static_cast<int>(new_state));

                    if (new_state == websocket::ConnectionState::HANDSHAKED)
                    {
                        // 握手成功
                        LOG_INFO(LOG_TAG, "WebSocket握手成功");
                    }
                    else if (new_state == websocket::ConnectionState::CLOSED ||
                             new_state == websocket::ConnectionState::DISCONNECTED)
                    {
                        // 连接断开，回到READY状态
                        ChatbotState current_state = state_.load();
                        if (current_state != ChatbotState::CLOSED &&
                            current_state != ChatbotState::READY)
                        {
                            // 停止AI音频流
                            if (audio_system_)
                            {
                                audio_system_->stopStream(app::media::audio::StreamType::AI);
                            }
                            state_.store(ChatbotState::READY);
                            LOG_INFO(LOG_TAG, "WebSocket连接断开，回到就绪状态");
                        }
                    }
                });

            // 设置错误回调
            ws_client_->setErrorCallback(
                [this](websocket::WebSocketError error, const std::string& message)
                {
                    (void)error;
                    LOG_ERROR(LOG_TAG, "WebSocket错误: %s", message.c_str());
                });

            LOG_INFO(LOG_TAG, "WebSocket回调已设置");
        }

        ChatbotError ChatbotSystem::connectAIServer()
        {
            if (!ws_client_)
            {
                LOG_ERROR(LOG_TAG, "WebSocket客户端未初始化");
                return ChatbotError::CONNECTION_FAILED;
            }

            // 设置状态为连接中
            state_.store(ChatbotState::CONNECTING);

            // 循环重连直到成功
            while (true)
            {
                // 尝试连接
                websocket::WebSocketError ws_err = ws_client_->connect();
                if (ws_err != websocket::WebSocketError::NONE)
                {
                    LOG_WARN(LOG_TAG, "WebSocket连接失败: %d，%d秒后重试...",
                             static_cast<int>(ws_err), config_.connection_timeout_sec);
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    continue;
                }

                // 等待连接和握手完成
                int       wait_count = 0;
                const int max_wait   = config_.connection_timeout_sec * 10;

                while (wait_count < max_wait)
                {
                    if (ws_client_->isHandshaked())
                    {
                        // 连接成功
                        LOG_INFO(LOG_TAG, "========================================");
                        LOG_INFO(LOG_TAG, "  WebSocket连接成功");
                        LOG_INFO(LOG_TAG, "========================================");

                        state_.store(ChatbotState::READY);
                        return ChatbotError::NONE;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    wait_count++;
                }

                // 连接超时，断开后重试
                LOG_WARN(LOG_TAG, "WebSocket连接超时，%d秒后重试...",
                         config_.connection_timeout_sec);
                ws_client_->disconnect();
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }

            return ChatbotError::NONE;
        }

        // ============================================================================
        // 唤醒词检测器初始化
        // ============================================================================

        ChatbotError ChatbotSystem::initializeWakeword()
        {
            if (!audio_system_)
            {
                LOG_ERROR(LOG_TAG, "音频系统未初始化，无法初始化唤醒词检测器");
                return ChatbotError::AUDIO_SYSTEM_ERROR;
            }

            try
            {
                // 创建唤醒词配置
                wakeword::WakewordConfig ww_cfg;
                ww_cfg.resource_file  = config_.wakeword_resource_file;
                ww_cfg.model_file     = config_.wakeword_model_file;
                ww_cfg.sensitivity    = config_.wakeword_sensitivity;
                ww_cfg.audio_gain     = config_.wakeword_audio_gain;
                ww_cfg.apply_frontend = false;

                // 创建唤醒词检测器
                wakeword_detector_ = std::make_unique<wakeword::WakewordDetector>(ww_cfg);

                // 初始化唤醒词检测器
                wakeword::WakewordError ww_err = wakeword_detector_->initialize();
                if (ww_err != wakeword::WakewordError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "唤醒词检测器初始化失败");
                    wakeword_detector_.reset();
                    return ChatbotError::WAKEWORD_ERROR;
                }

                // 设置状态为就绪
                state_.store(ChatbotState::READY);

                // 启用唤醒词检测
                wakeword_detector_->setEnabled(true);

                // 设置唤醒词回调
                setupWakewordCallbacks();

                // 设置音频系统的唤醒词回调
                setupWakewordAudioCallback();

                // 设置AI音频流回调
                setupAIAudioCallback();

                return ChatbotError::NONE;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(LOG_TAG, "唤醒词检测器初始化异常: %s", e.what());
                wakeword_detector_.reset();
                return ChatbotError::WAKEWORD_ERROR;
            }
        }

        ChatbotError ChatbotSystem::deinitializeWakeword()
        {
            if (wakeword_detector_)
            {
                // 禁用唤醒词检测
                wakeword_detector_->setEnabled(false);
                wakeword_detector_.reset();
                LOG_INFO(LOG_TAG, "唤醒词检测器已释放");
            }

            return ChatbotError::NONE;
        }

        void ChatbotSystem::setupWakewordCallbacks()
        {
            if (!wakeword_detector_)
            {
                return;
            }

            // 设置唤醒词检测回调
            wakeword_detector_->setWakewordCallback(
                [this](wakeword::WakewordResult result, int hotword_index)
                {
                    if (result == wakeword::WakewordResult::HOTWORD_1 ||
                        result == wakeword::WakewordResult::HOTWORD_2 ||
                        result == wakeword::WakewordResult::HOTWORD_3)
                    {

                        LOG_INFO(LOG_TAG, "========================================");
                        LOG_INFO(LOG_TAG, "  🎙️ 唤醒词检测到！Hotword %d",
                                 hotword_index);
                        LOG_INFO(LOG_TAG, "========================================");

                        // 检查WebRTC是否正在连接或已连接，如果是则忽略唤醒词
                        if (webrtc_system_)
                        {
                            auto webrtc_state = webrtc_system_->getState();
                            if (webrtc_state != app::protocol::webrtc::WebRTCState::UNINITIALIZED &&
                                webrtc_state != app::protocol::webrtc::WebRTCState::DISCONNECTED &&
                                webrtc_state != app::protocol::webrtc::WebRTCState::FAILED)
                            {
                                LOG_WARN(LOG_TAG, "WebRTC正在连接或已连接，忽略唤醒词。WebRTC状态: %d",
                                         static_cast<int>(webrtc_state));
                                return;
                            }
                        }

                        // 检查当前状态，只有在READY状态才处理唤醒词
                        ChatbotState current_state = state_.load();
                        if (current_state != ChatbotState::READY)
                        {
                            LOG_WARN(LOG_TAG, "当前状态不是READY，忽略唤醒词。当前状态: %s",
                                     stateToString(current_state));
                            return;
                        }

                        // 设置状态为连接中
                        state_.store(ChatbotState::CONNECTING);

                        // 初始化WebSocket客户端
                        ChatbotError err = initializeWebSocket();
                        if (err != ChatbotError::NONE)
                        {
                            LOG_ERROR(LOG_TAG, "WebSocket客户端初始化失败");
                            state_.store(ChatbotState::READY); // 回到就绪状态
                            return;
                        }

                        // 连接AI服务器
                        err = connectAIServer();
                        if (err != ChatbotError::NONE)
                        {
                            LOG_ERROR(LOG_TAG, "AI服务器连接失败");
                            state_.store(ChatbotState::READY); // 回到就绪状态
                            return;
                        }

                        // 连接成功后，启动AI音频流并发送Listen消息
                        if (protocol_handler_ && ws_client_ && ws_client_->isHandshaked() &&
                            audio_system_)
                        {
                            // 启动AI音频流
                            app::media::audio::AudioError audio_err =
                                audio_system_->startStream(app::media::audio::StreamType::AI);
                            if (audio_err != app::media::audio::AudioError::NONE)
                            {
                                LOG_ERROR(LOG_TAG, "启动AI音频流失败: %d",
                                          static_cast<int>(audio_err));
                                state_.store(ChatbotState::READY);
                                return;
                            }

                            // 设置状态为监听中
                            state_.store(ChatbotState::LISTENING);
                            LOG_INFO(LOG_TAG, "开始监听用户语音");

                            // 生成并发送Listen消息（开始监听，自动模式）
                            std::string listen_msg = protocol_handler_->generateListenMessage(
                                protocol_handle::ListenState::START,
                                protocol_handle::ListenMode::AUTO);

                            // 发送Listen消息
                            websocket::WebSocketError ws_err = ws_client_->sendText(listen_msg);
                            if (ws_err != websocket::WebSocketError::NONE)
                            {
                                LOG_ERROR(LOG_TAG, "发送Listen消息失败: %d",
                                          static_cast<int>(ws_err));
                                // 发送失败，停止AI音频流并回到就绪状态
                                audio_system_->stopStream(app::media::audio::StreamType::AI);
                                state_.store(ChatbotState::READY);
                                return;
                            }
                        }
                    }
                });

            // 设置错误回调
            wakeword_detector_->setErrorCallback(
                [this](wakeword::WakewordError error, const std::string& message)
                {
                    (void)error;
                    (void)message;
                });
        }

        void ChatbotSystem::setupWakewordAudioCallback()
        {
            if (!audio_system_ || !wakeword_detector_)
            {
                return;
            }

            // 设置音频系统的唤醒词回调，将音频数据传递给唤醒词检测器
            audio_system_->setWakewordCallback(
                [this](const int16_t* data, size_t length)
                {
                    if (wakeword_detector_ && wakeword_detector_->isEnabled())
                    {
                        wakeword_detector_->processAudioFrame(data, static_cast<int>(length));
                    }
                });
        }

        void ChatbotSystem::setupAIAudioCallback()
        {
            if (!audio_system_)
            {
                return;
            }

            // 设置AI音频流回调
            audio_system_->setAIAudioCallback(
                [this](app::media::audio::AudioFramePtr frame)
                {
                    if (!frame || !frame->data || frame->size == 0)
                    {
                        return;
                    }

                    // 检查WebSocket连接状态
                    if (!ws_client_ || !ws_client_->isHandshaked())
                    {
                        // WebSocket未连接，跳过音频发送（不打印日志，避免日志过多）
                        return;
                    }

                    // 检查是否在监听状态
                    ChatbotState current_state = state_.load();
                    if (current_state != ChatbotState::LISTENING)
                    {
                        // 不在监听状态，不发送音频
                        return;
                    }

                    // 发送二进制音频数据
                    websocket::WebSocketError err = ws_client_->sendBinary(
                        reinterpret_cast<const char*>(frame->data), frame->size);

                    if (err != websocket::WebSocketError::NONE)
                    {
                        LOG_WARN(LOG_TAG, "发送音频数据失败: %d", static_cast<int>(err));
                    }
                });

            LOG_INFO(LOG_TAG, "AI音频流回调已设置");
        }

        // 初始化ChatbotSystem
        ChatbotError ChatbotSystem::open()
        {
            // 检查音频系统是否已设置
            if (!audio_system_)
            {
                LOG_ERROR(LOG_TAG, "音频系统未设置，请先调用 setAudioSystem()");
                return ChatbotError::AUDIO_SYSTEM_ERROR;
            }

            // 设置状态为初始化中
            state_.store(ChatbotState::INITIALIZING);

            // 初始化激活管理器
            ChatbotError err = initializeActivation();
            if (err != ChatbotError::NONE)
            {
                return err;
            }

            // 激活检测
            err = checkActivation();
            if (err != ChatbotError::NONE)
            {
                return err;
            }

            // 注册MCP工具
            err = initializeMCP();
            if (err != ChatbotError::NONE)
            {
                LOG_ERROR(LOG_TAG, "MCP工具注册失败");
                return err;
            }

            // 初始化唤醒词检测器
            err = initializeWakeword();
            if (err != ChatbotError::NONE)
            {
                LOG_ERROR(LOG_TAG, "唤醒词检测器初始化失败");
                return err;
            }

            return ChatbotError::NONE;
        }

        void ChatbotSystem::close()
        {
            if (state_.load() == ChatbotState::CLOSED)
            {
                return;
            }
            LOG_INFO(LOG_TAG, "正在关闭ChatbotSystem...");
            state_.store(ChatbotState::CLOSED);

            deinitializeWakeword();   // 关闭唤醒词检测器
            deinitializeWebSocket();  // 关闭WebSocket客户端
            deinitializeMCP();        // 关闭MCP服务器
            deinitializeActivation(); // 关闭激活管理器

            audio_system_ = nullptr;

            LOG_INFO(LOG_TAG, "ChatbotSystem已关闭");
        }

        ChatbotState ChatbotSystem::getState() const
        {
            return state_.load();
        }

        bool ChatbotSystem::isReady() const
        {
            return state_.load() == ChatbotState::READY;
        }

        void ChatbotSystem::disconnectWebSocket()
        {
            if (ws_client_)
            {
                LOG_INFO(LOG_TAG, "断开AI服务器连接");
                ws_client_->disconnect();
                // WebSocket断开后，状态回调会自动重置状态为READY
            }
        }

    } // namespace chatbot
} // namespace app
