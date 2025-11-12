#include "chatbot.hpp"
#include "../tool/log/log.hpp"
#include "../tool/mac/mac.hpp"
#include "../tool/uuid/uuid.hpp"
#include "mcp/mcp_tool/mcp_tool.hpp"
#include "activation/activation.hpp"
#include "protocol_handle/handle.hpp"
#include "../protocol/websocket/websocket.hpp"

#include <thread>

#include <chrono>

using namespace app::tool::log;
namespace audio = app::media::audio;
namespace wifi = app::network::wifi;
namespace activation = app::chatbot::activation;
namespace mcp = app::chatbot::mcp;
namespace mcp_tool = app::chatbot::mcp::mcp_tool;
namespace protocol_handle = app::chatbot::protocol_handle;
namespace websocket = app::protocol::websocket;
namespace wakeword = app::chatbot::wakeword;


namespace app {
namespace chatbot {

ChatbotSystem::ChatbotSystem(const ChatbotConfig& config)
    : config_(config) {
    LOG_INFO("Chatbot", "ChatbotSystem created");
}

ChatbotSystem::~ChatbotSystem() {
    close();
    LOG_INFO("Chatbot", "ChatbotSystem destroyed");
}

// 初始化音频系统
ChatbotError ChatbotSystem::initializeAudio() {
    // 创建音频配置
    audio::AudioConfig audio_config;
    audio_config.sample_rate = config_.sample_rate;
    audio_config.channels = config_.channels;
    audio_config.frame_duration_ms = config_.frame_duration_ms;
    
    // 创建音频系统
    audio_system_ = std::make_unique<audio::AudioSystem>(audio_config);
    
    // 初始化音频系统
    audio::AudioError audio_err = audio_system_->initialize();
    if (audio_err != audio::AudioError::NONE) {
        LOG_ERROR("Chatbot", "  音频系统初始化失败");
        audio_system_.reset();
        state_.store(ChatbotState::ERROR);
        return ChatbotError::AUDIO_SYSTEM_ERROR;
    }
    LOG_INFO("Chatbot", "  音频系统初始化成功");

    // 启动录音
    audio_err = audio_system_->startRecord();
    if (audio_err != audio::AudioError::NONE) {
        LOG_ERROR("Chatbot", "启动录音失败");
        return ChatbotError::AUDIO_SYSTEM_ERROR;
    }
    
    return ChatbotError::NONE;
}

// 释放音频系统
ChatbotError ChatbotSystem::deinitializeAudio() {
    if (!audio_system_) {
        return ChatbotError::NONE;
    }

    // 停止录音（如果正在录音）
    if (audio_system_->isRecording()) {
        audio_system_->stopRecord();
    }

    // 关闭音频系统
    audio_system_->shutdown();
    audio_system_.reset();

    return ChatbotError::NONE;
}


// 初始化WiFi管理器
ChatbotError ChatbotSystem::initializeWiFi() {
    // 创建WiFi配置
    wifi::wifiConfig wifi_config;
    wifi_config.auto_connect_on_init = true;  // 自动连接已保存的WiFi
    
    // 创建WiFi管理器
    wifi_manager_ = std::make_unique<wifi::wifiManager>(wifi_config);
    
    // 初始化WiFi管理器
    wifi::wifiError wifi_err = wifi_manager_->initialize();
    if (wifi_err != wifi::wifiError::NONE) {
        LOG_ERROR("Chatbot", "  WiFi管理器初始化失败");
        wifi_manager_.reset();
        state_.store(ChatbotState::ERROR);
        return ChatbotError::WIFI_MANAGER_ERROR;
    }
    LOG_INFO("Chatbot", "  WiFi管理器初始化成功");
    return ChatbotError::NONE;
}

// 释放WiFi管理器
ChatbotError ChatbotSystem::deinitializeWiFi() {
    if (wifi_manager_) {
        wifi_manager_->shutdown();
        wifi_manager_.reset();
    }
    return ChatbotError::NONE;
}

ChatbotError ChatbotSystem::checkNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR("Chatbot", "WiFi管理器未初始化");
        return ChatbotError::WIFI_MANAGER_ERROR;
    }
    
    // 检查WiFi是否已连接
    if (wifi_manager_->isConnected()) {
        std::string ssid = wifi_manager_->getCurrentSSID();
        std::string ip = wifi_manager_->getIPAddress();
        LOG_INFO("Chatbot", "WiFi已连接: %s (IP: %s)", ssid.c_str(), ip.c_str());
        state_.store(ChatbotState::NETWORK_CONNECTED);
        return ChatbotError::NONE;
    }
    
    LOG_INFO("Chatbot", "WiFi未连接");
    return ChatbotError::NETWORK_ERROR;
}

ChatbotError ChatbotSystem::connectNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR("Chatbot", "WiFi管理器未初始化");
        return ChatbotError::WIFI_MANAGER_ERROR;
    }

    state_.store(ChatbotState::NETWORK_CONFIGURING);
    
    // 先尝试连接已保存的WiFi
    LOG_INFO("Chatbot", "尝试连接已保存的WiFi...");
    wifi::wifiError wifi_err = wifi_manager_->connectSavedNetwork();
    if (wifi_err == wifi::wifiError::NONE) {
        std::string ssid = wifi_manager_->getCurrentSSID();
        std::string ip = wifi_manager_->getIPAddress();
        LOG_INFO("Chatbot", "  已保存WiFi连接成功: %s (IP: %s)", ssid.c_str(), ip.c_str());
        state_.store(ChatbotState::NETWORK_CONNECTED);
        return ChatbotError::NONE;
    }
    
    // 连接已保存WiFi失败，返回错误
    LOG_WARN("Chatbot", "已保存WiFi连接失败");
    state_.store(ChatbotState::ERROR);
    return ChatbotError::NETWORK_ERROR;
}

ChatbotError ChatbotSystem::disconnectNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR("Chatbot", "WiFi管理器未初始化");
        return ChatbotError::WIFI_MANAGER_ERROR;
    }
    
    if (!wifi_manager_->isConnected()) {
        LOG_INFO("Chatbot", "WiFi未连接");
        return ChatbotError::NONE;
    }
    wifi::wifiError wifi_err = wifi_manager_->disconnect();
    if (wifi_err != wifi::wifiError::NONE) {
        LOG_ERROR("Chatbot", "WiFi断开失败");
        return ChatbotError::NETWORK_ERROR;
    }
    LOG_INFO("Chatbot", "WiFi已断开");
    state_.store(ChatbotState::NETWORK_CHECKING);
    return ChatbotError::NONE;
}

// 初始化网络（检测+配网，阻塞直到成功）
ChatbotError ChatbotSystem::initializeNetwork() {
    // 初始化wifi系统
    initializeWiFi();

    // 检查WiFi连接状态
    state_.store(ChatbotState::NETWORK_CHECKING);
    ChatbotError err = checkNetwork();
    if (err == ChatbotError::NONE) {
        // 已连接，直接返回成功
        return ChatbotError::NONE;
    }
    
    // 未连接，进入配网流程（阻塞直到成功）
    LOG_INFO("Chatbot", "进入配网流程...");
    
    // 阻塞循环，直到连接成功
    while (true) {
        err = connectNetwork();
        if (err == ChatbotError::NONE) {
            // 连接成功
            return ChatbotError::NONE;
        }
        
        // 连接失败，等待后重试
        LOG_WARN("Chatbot", "WiFi连接失败，10秒后重试...");
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

ChatbotError ChatbotSystem::searchSavedNetwork() {
    if (!wifi_manager_) {
        LOG_ERROR("Chatbot", "WiFi管理器未初始化");
        return ChatbotError::WIFI_MANAGER_ERROR;
    }
    
    // 获取已保存的网络列表
    std::vector<wifi::savedNetworkInfo> saved = wifi_manager_->getSavedNetworks();
    
    if (saved.empty()) {
        LOG_INFO("Chatbot", "没有已保存的WiFi网络");
        return ChatbotError::NONE;
    }
    
    LOG_INFO("Chatbot", "找到 %zu 个已保存的WiFi网络:", saved.size());
    
    // 输出每个网络的详细信息
    for (size_t i = 0; i < saved.size(); ++i) {
        const auto& net = saved[i];
        LOG_INFO("Chatbot", "  [%zu] SSID: %s", i + 1, net.ssid.c_str());
        LOG_INFO("Chatbot", "       网络ID: %d", net.network_id);
        
        if (net.is_current) {
            LOG_INFO("Chatbot", "       状态: 当前连接");
        } else {
            LOG_INFO("Chatbot", "       状态: 未连接");
        }
        
        if (net.is_enabled_auto) {
            LOG_INFO("Chatbot", "       自动连接: 启用");
        } else {
            LOG_INFO("Chatbot", "       自动连接: 禁用");
        }
        
        if (net.priority > 0) {
            LOG_INFO("Chatbot", "       优先级: %d", net.priority);
        }
    }
    
    return ChatbotError::NONE;
}

ChatbotError ChatbotSystem::forgetNetwork(const std::string& ssid) {
    if (!wifi_manager_) {
        LOG_ERROR("Chatbot", "WiFi管理器未初始化");
        return ChatbotError::WIFI_MANAGER_ERROR;
    }
    
    if (ssid.empty()) {
        LOG_ERROR("Chatbot", "SSID不能为空");
        return ChatbotError::NETWORK_ERROR;
    }
    
    // 检查网络是否已保存
    if (!wifi_manager_->isNetworkSaved(ssid)) {
        LOG_WARN("Chatbot", "WiFi \"%s\" 未在已保存列表中", ssid.c_str());
        return ChatbotError::NETWORK_ERROR;
    }
    
    // 执行删除
    wifi::wifiError err = wifi_manager_->forgetNetwork(ssid);
    if (err != wifi::wifiError::NONE) {
        LOG_ERROR("Chatbot", "删除WiFi \"%s\" 失败: %d", ssid.c_str(), static_cast<int>(err));
        return ChatbotError::NETWORK_ERROR;
    }
    
    LOG_INFO("Chatbot", "WiFi \"%s\" 已删除", ssid.c_str());
    return ChatbotError::NONE;
}

ChatbotError ChatbotSystem::getDeviceId() {
    // 获取设备ID（MAC地址），如果配置为空则自动获取
    if (config_.device_id.empty()) {
        config_.device_id = app::tool::mac::getWirelessMacAddress();
        if (config_.device_id.empty()) {
            LOG_ERROR("Chatbot", "获取MAC地址失败");
            return ChatbotError::INITIALIZATION_FAILED;
        }
    }
    LOG_INFO("Chatbot", "MAC地址: %s", config_.device_id.c_str());
    
    // 获取客户端ID（UUID），如果配置为空则自动生成
    if (config_.client_id.empty()) {
        config_.client_id = app::tool::uuid::generateUUID(config_.config_file_path);
        if (config_.client_id.empty()) {
            LOG_ERROR("Chatbot", "生成UUID失败");
            return ChatbotError::INITIALIZATION_FAILED;
        }
    }
    LOG_INFO("Chatbot", "UUID: %s", config_.client_id.c_str());
    
    return ChatbotError::NONE;
}

// ============================================================================
// 激活管理器初始化
// ============================================================================

ChatbotError ChatbotSystem::initializeActivation() {
    // 获取设备信息（如果配置为空则自动获取）
    ChatbotError err = getDeviceId();
    if (err != ChatbotError::NONE) {
        return err;
    }

    try {
        activation::ActivationConfig act_cfg;
        act_cfg.api_url = config_.activation_api_url;
        act_cfg.activation_url = "https://xiaozhi.me";  // 激活页面URL
        act_cfg.poll_interval_sec = 5;  // 每5秒检测一次
        act_cfg.poll_timeout_sec = config_.activation_timeout_sec;
        act_cfg.verify_ssl = false;  // 不验证SSL证书
        
        activation_manager_ = std::make_unique<activation::DeviceActivation>(act_cfg);
        
        return ChatbotError::NONE;
    } catch (const std::exception& e) {
        LOG_ERROR("Chatbot", "激活管理器初始化失败: %s", e.what());
        return ChatbotError::ACTIVATION_FAILED;
    }
}

ChatbotError ChatbotSystem::deinitializeActivation() {
    if (activation_manager_) {
        activation_manager_.reset();
        LOG_INFO("Chatbot", "激活管理器已释放");
    }
    return ChatbotError::NONE;
}

ChatbotError ChatbotSystem::checkActivation() {
    if (!activation_manager_) {
        LOG_ERROR("Chatbot", "激活管理器未初始化");
        return ChatbotError::ACTIVATION_FAILED;
    }
    
    if (config_.device_id.empty() || config_.client_id.empty()) {
        LOG_ERROR("Chatbot", "设备信息未初始化");
        return ChatbotError::INITIALIZATION_FAILED;
    }
    
    // 设置状态为激活中
    state_.store(ChatbotState::ACTIVATING);
    
    // 阻塞循环直到激活成功
    while (true) {
        // 检查激活状态
        activation::ActivationResult result = 
            activation_manager_->checkActivation(config_.device_id, config_.client_id);
        
        if (result.isActivated()) {
            // 激活成功
            LOG_INFO("Chatbot", "========================================");
            LOG_INFO("Chatbot", "  设备激活成功");
            LOG_INFO("Chatbot", "========================================");
            
            state_.store(ChatbotState::ACTIVATED);
            return ChatbotError::NONE;
        }
        
        if (result.hasError()) {
            // 检查失败，记录错误并重试
            LOG_WARN("Chatbot", "激活检查失败: %s，%d秒后重试...", 
                     result.error_message.c_str(), config_.activation_timeout_sec);
        } else if (result.status == activation::ActivationStatus::NOT_ACTIVATED) {
            // 未激活，显示激活码
            LOG_INFO("Chatbot", "========================================");
            LOG_INFO("Chatbot", "  ⚠ 设备未激活");
            LOG_INFO("Chatbot", "  激活URL: https://xiaozhi.me");
            LOG_INFO("Chatbot", "  激活码: %s", result.activation_code.c_str());
            LOG_INFO("Chatbot", "  请使用激活码完成设备激活");
            LOG_INFO("Chatbot", "========================================");
        }
        
        // 等待5秒后重试
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return ChatbotError::NONE;
}

// ============================================================================
// MCP工具注册
// ============================================================================

ChatbotError ChatbotSystem::initializeMCP() {
    try {
        // 创建MCP配置
        mcp::McpConfig mcp_cfg;
        mcp_cfg.server_name = "Smart_Glasses";
        mcp_cfg.server_version = "1.0.0";
        mcp_cfg.protocol_version = "2025-11-06";
        
        // 创建MCP服务器
        mcp_server_ = std::make_unique<mcp::McpServer>(mcp_cfg);
        
        // 注册所有MCP工具
        int tool_count = mcp_tool::McpToolManager::register_all_tools(*mcp_server_);
        LOG_INFO("Chatbot", "MCP工具注册完成，共注册 %d 个工具", tool_count);
        
        return ChatbotError::NONE;
    } catch (const std::exception& e) {
        LOG_ERROR("Chatbot", "MCP服务器初始化失败: %s", e.what());
        mcp_server_.reset();
        return ChatbotError::MCP_ERROR;
    }
}

ChatbotError ChatbotSystem::deinitializeMCP() {
    if (mcp_server_) {
        mcp_server_.reset();
        LOG_INFO("Chatbot", "MCP服务器已释放");
    }
    return ChatbotError::NONE;
}

// ============================================================================
// 协议处理器初始化
// ============================================================================

ChatbotError ChatbotSystem::initializeProtocol() {
    try {
        // 创建协议配置
        protocol_handle::ProtocolConfig proto_cfg;
        proto_cfg.enable_async_processing = true;
        proto_cfg.message_queue_size = 100;
        proto_cfg.enable_mcp = true;  // 启用MCP工具支持
        
        // 创建协议处理器
        protocol_handler_ = std::make_unique<protocol_handle::ProtocolHandler>(proto_cfg);
        
        // 设置协议回调
        setupProtocolCallbacks();
        
        return ChatbotError::NONE;
    } catch (const std::exception& e) {
        LOG_ERROR("Chatbot", "协议处理器初始化失败: %s", e.what());
        protocol_handler_.reset();
        return ChatbotError::PROTOCOL_ERROR;
    }
}

ChatbotError ChatbotSystem::deinitializeProtocol() {
    if (protocol_handler_) {
        protocol_handler_.reset();
        LOG_INFO("Chatbot", "协议处理器已释放");
    }
    return ChatbotError::NONE;
}

void ChatbotSystem::setupProtocolCallbacks() {
    if (!protocol_handler_) {
        return;
    }
    
    // 设置Hello消息回调
    protocol_handler_->setHelloCallback([this](const protocol_handle::HelloMessage& msg) {
        LOG_INFO("Chatbot", "收到Hello响应，会话ID: %s", msg.session_id.c_str());
        // 保存session_id，后续消息需要使用
        if (protocol_handler_) {
            protocol_handler_->setSessionId(msg.session_id);
        }
    });
    
    // 设置STT消息回调（语音识别结果）
    protocol_handler_->setSTTCallback([this](const protocol_handle::STTMessage& msg) {
        (void)msg;  // STT结果通常用于日志记录，实际对话由LLM处理
    });
    
    // 设置LLM消息回调（AI回复）
    protocol_handler_->setLLMCallback([this](const protocol_handle::LLMMessage& msg) {
        LOG_INFO("Chatbot", "收到LLM回复: %s", msg.text.c_str());
        // LLM回复后，状态转为SPEAKING（等待TTS音频）
        state_.store(ChatbotState::SPEAKING);
    });
    
    // 设置TTS消息回调（TTS状态）
    protocol_handler_->setTTSCallback([this](const protocol_handle::TTSMessage& msg) {
        if (msg.state == protocol_handle::TTSState::START) {
            // TTS开始：确保播放已启动
            if (audio_system_ && !audio_system_->isPlaying()) {
                audio::AudioError err = audio_system_->startPlayback();
                if (err != audio::AudioError::NONE) {
                    LOG_ERROR("Chatbot", "启动播放失败: %d", static_cast<int>(err));
                }
            }
        } 
        else if (msg.state == protocol_handle::TTSState::SENTENCE_START && config_.enable_interrupt_conversation) {
            // 每个句子开始时发送Listen消息，继续监听用户可能的打断
            if (protocol_handler_ && ws_client_ && ws_client_->isHandshaked() && audio_system_) {
                // 确保AI音频流正在运行
                if (!audio_system_->isAIStreamActive()) {
                    audio::AudioError err = audio_system_->startAIStream();
                    if (err != audio::AudioError::NONE) {
                        LOG_ERROR("Chatbot", "启动AI音频流失败: %d", static_cast<int>(err));
                    }
                }
                
                // 发送Listen消息（继续监听）
                std::string listen_msg = protocol_handler_->generateListenMessage(
                    protocol_handle::ListenState::START, 
                    protocol_handle::ListenMode::AUTO);
                
                websocket::WebSocketError ws_err = ws_client_->sendText(listen_msg);
                if (ws_err == websocket::WebSocketError::NONE) {
                    state_.store(ChatbotState::LISTENING);
                }
            }
        } 
        else if (msg.state == protocol_handle::TTSState::STOP) {
            // TTS停止：停止播放
            if (audio_system_) {
                audio_system_->stopPlayback();
            }
            
            // 使用异步任务，避免阻塞
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(config_.delay_conversation_sec * 1000));
                
                // 检查WebSocket是否还连接
                if (!ws_client_ || !ws_client_->isHandshaked()) {
                    state_.store(ChatbotState::READY);
                    return;
                }
                
                // 检查当前状态
                ChatbotState current_state = state_.load();
                if (current_state == ChatbotState::CLOSED || current_state == ChatbotState::ERROR) {
                    return;
                }
                
                // 检查AI音频流是否还在运行
                if (!audio_system_ || !audio_system_->isAIStreamActive()) {
                    state_.store(ChatbotState::READY);
                    return;
                }
                
                // 重新发送Listen消息，继续监听
                if (protocol_handler_ && ws_client_) {
                    std::string listen_msg = protocol_handler_->generateListenMessage(
                        protocol_handle::ListenState::START, 
                        protocol_handle::ListenMode::AUTO);
                    
                    websocket::WebSocketError ws_err = ws_client_->sendText(listen_msg);
                    if (ws_err == websocket::WebSocketError::NONE) {
                        state_.store(ChatbotState::LISTENING);
                    } else {
                        LOG_ERROR("Chatbot", "发送Listen消息失败: %d", static_cast<int>(ws_err));
                        state_.store(ChatbotState::READY);
                    }
                }
            }).detach();
        }
    });
    
    // 设置MCP消息回调（转发给MCP服务器并发送响应）
    if (mcp_server_) {
        protocol_handler_->setMCPCallback([this](const std::string& mcp_payload) -> std::string {
            if (!mcp_server_) {
                return "{\"error\": \"MCP server not available\"}";
            }
            
            // 调用MCP服务器处理消息，返回响应
            std::string response = mcp_server_->handle_message(mcp_payload);
            
            // 将响应发送回服务器
            if (!response.empty() && ws_client_ && ws_client_->isHandshaked() && protocol_handler_) {
                try {
                    // 构建完整的MCP响应消息：{"session_id":"...", "type":"mcp", "payload":{...}}
                    // response已经是JSON字符串，需要解析后作为payload
                    std::string session_id = protocol_handler_->getSessionId();
                    
                    // 构建完整的MCP响应消息
                    std::string mcp_response_msg = "{\"session_id\":\"" + session_id + 
                                                   "\",\"type\":\"mcp\",\"payload\":" + response + "}";
                    
                    // 发送MCP响应
                    websocket::WebSocketError err = ws_client_->sendText(mcp_response_msg);
                    if (err != websocket::WebSocketError::NONE) {
                        LOG_ERROR("Chatbot", "发送MCP响应失败: %d", static_cast<int>(err));
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Chatbot", "构建MCP响应消息失败: %s", e.what());
                }
            } else {
                if (!ws_client_ || !ws_client_->isHandshaked()) {
                    LOG_WARN("Chatbot", "WebSocket未连接，无法发送MCP响应");
                }
            }
            
            // 返回响应（虽然已经发送，但保持接口一致性）
            return response;
        });
    }
    
    // 设置错误回调
    protocol_handler_->setErrorCallback([this](const std::string& error) {
        LOG_ERROR("Chatbot", "协议错误: %s", error.c_str());
    });
    
    LOG_INFO("Chatbot", "协议回调已设置");
}

// ============================================================================
// WebSocket连接初始化
// ============================================================================

ChatbotError ChatbotSystem::initializeWebSocket() {
    ChatbotError err = initializeProtocol();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("Chatbot", "协议处理器初始化失败");
        return err;
    }
    
    if (config_.device_id.empty() || config_.client_id.empty()) {
        LOG_ERROR("Chatbot", "设备信息未初始化，无法初始化WebSocket");
        return ChatbotError::INITIALIZATION_FAILED;
    }
    
    try {
        // 生成Hello消息
        std::string hello_msg = protocol_handler_->generateHelloMessage(
            16000,  // 16kHz
            1,      // Mono
            20      // 20ms frame
        );
        
        // 创建WebSocket配置
        websocket::WebSocketConfig ws_cfg;
        ws_cfg.url = config_.api_url;
        ws_cfg.hello_message = hello_msg;
        ws_cfg.auto_reconnect = false;  // 手动控制重连
        ws_cfg.connect_timeout_ms = config_.connection_timeout_sec * 1000;
        ws_cfg.verify_ssl = false;  // 不验证SSL证书
        
        // 添加HTTP Headers
        ws_cfg.headers["Device-Id"] = config_.device_id;
        ws_cfg.headers["Client-Id"] = config_.client_id;
        ws_cfg.headers["User-Agent"] = "SmartGlasses/1.0";
        
        // 创建WebSocket客户端
        ws_client_ = std::make_unique<websocket::WebSocketClient>(ws_cfg);
        LOG_INFO("Chatbot", "WebSocket客户端创建成功");
        
        // 设置WebSocket回调
        setupWebSocketCallbacks();
        
        return ChatbotError::NONE;
    } catch (const std::exception& e) {
        LOG_ERROR("Chatbot", "WebSocket客户端初始化失败: %s", e.what());
        ws_client_.reset();
        return ChatbotError::CONNECTION_FAILED;
    }
}

ChatbotError ChatbotSystem::deinitializeWebSocket() {
    if (ws_client_) {
        ws_client_->disconnect();
        ws_client_.reset();
        LOG_INFO("Chatbot", "WebSocket客户端已释放");
        deinitializeProtocol();
    }
    
    return ChatbotError::NONE;
}

void ChatbotSystem::setupWebSocketCallbacks() {
    if (!ws_client_ || !protocol_handler_) {
        return;
    }
    
    // 设置文本消息回调（JSON协议）
    ws_client_->setTextCallback([this](const char* data, size_t size) -> bool {
        if (protocol_handler_) {
            protocol_handler_->parseMessage(data, size);
        }
        return true;
    });
    
    // 设置二进制消息回调（TTS音频）
    ws_client_->setBinaryCallback([this](const char* data, size_t size) -> bool {
        if (!audio_system_) {
            LOG_WARN("Chatbot", "音频系统未初始化，无法处理TTS音频");
            return false;
        }
        
        // 跳过16字节头部
        if (size <= 16) {
            LOG_WARN("Chatbot", "TTS音频包太小: %zu bytes", size);
            return false;
        }
        
        const uint8_t* opus_data = reinterpret_cast<const uint8_t*>(data) + 16;  // 跳过16字节头部
        size_t opus_size = size - 16;
        
        // 解码Opus音频数据
        audio::AudioFramePtr frame = audio_system_->decodeOpus(opus_data, opus_size);
        
        if (!frame) {
            LOG_WARN("Chatbot", "Opus解码失败，音频数据: %zu bytes", opus_size);
            return false;
        }
        
        // 推送播放帧（零拷贝）
        audio_system_->pushPlaybackFrame(frame);
        
        // 如果播放未启动，自动启动播放
        if (!audio_system_->isPlaying()) {
            audio::AudioError err = audio_system_->startPlayback();
            if (err != audio::AudioError::NONE) {
                LOG_ERROR("Chatbot", "启动播放失败: %d", static_cast<int>(err));
            }
        }
        
        return true;
    });
    
    // 设置连接状态回调
    ws_client_->setStateCallback([this](websocket::ConnectionState old_state, 
                                        websocket::ConnectionState new_state) {
        LOG_INFO("Chatbot", "WebSocket状态: %d → %d", 
                 static_cast<int>(old_state), static_cast<int>(new_state));
        
        if (new_state == websocket::ConnectionState::HANDSHAKED) {
            // 握手成功，但状态由connectAIServer()或唤醒词回调管理
            // 这里不改变状态，避免覆盖正在进行的连接流程
            LOG_INFO("Chatbot", "WebSocket握手成功");
        } else if (new_state == websocket::ConnectionState::CLOSED ||
                   new_state == websocket::ConnectionState::DISCONNECTED) {
            // 连接断开，回到READY状态（等待下次唤醒）
            ChatbotState current_state = state_.load();
            if (current_state != ChatbotState::CLOSED && 
                current_state != ChatbotState::READY) {
                // 停止AI音频流
                if (audio_system_) {
                    audio_system_->stopAIStream();
                }
                state_.store(ChatbotState::READY);
                LOG_INFO("Chatbot", "WebSocket连接断开，回到就绪状态");
            }
        }
    });
    
    // 设置错误回调
    ws_client_->setErrorCallback([this](websocket::WebSocketError error, 
                                        const std::string& message) {
        (void)error;
        LOG_ERROR("Chatbot", "WebSocket错误: %s", message.c_str());
    });
    
    LOG_INFO("Chatbot", "WebSocket回调已设置");
}

ChatbotError ChatbotSystem::connectAIServer() {
    if (!ws_client_) {
        LOG_ERROR("Chatbot", "WebSocket客户端未初始化");
        return ChatbotError::CONNECTION_FAILED;
    }
    
    // 设置状态为连接中
    state_.store(ChatbotState::CONNECTING);
    
    // 阻塞循环，无限重连直到成功
    while (true) {
        // 尝试连接
        websocket::WebSocketError ws_err = ws_client_->connect();
        if (ws_err != websocket::WebSocketError::NONE) {
            LOG_WARN("Chatbot", "WebSocket连接失败: %d，%d秒后重试...", 
                     static_cast<int>(ws_err), config_.connection_timeout_sec);
            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
        }
        
        // 等待连接和握手完成
        int wait_count = 0;
        const int max_wait = config_.connection_timeout_sec * 10;  // 10秒 = 100 * 100ms
        
        while (wait_count < max_wait) {
            if (ws_client_->isHandshaked()) {
                // 连接成功
                LOG_INFO("Chatbot", "========================================");
                LOG_INFO("Chatbot", "  WebSocket连接成功");
                LOG_INFO("Chatbot", "========================================");
                
                state_.store(ChatbotState::READY);
                return ChatbotError::NONE;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_count++;
        }
        
        // 连接超时，断开后重试
        LOG_WARN("Chatbot", "WebSocket连接超时，%d秒后重试...", config_.connection_timeout_sec);
        ws_client_->disconnect();
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    return ChatbotError::NONE;
}

// ============================================================================
// 唤醒词检测器初始化
// ============================================================================

ChatbotError ChatbotSystem::initializeWakeword() {
    if (!audio_system_) {
        LOG_ERROR("Chatbot", "音频系统未初始化，无法初始化唤醒词检测器");
        return ChatbotError::AUDIO_SYSTEM_ERROR;
    }
    
    try {
        // 创建唤醒词配置
        wakeword::WakewordConfig ww_cfg;
        ww_cfg.resource_file = config_.wakeword_resource_file;
        ww_cfg.model_file = config_.wakeword_model_file;
        ww_cfg.sensitivity = config_.wakeword_sensitivity;
        ww_cfg.audio_gain = config_.wakeword_audio_gain;
        ww_cfg.apply_frontend = false;  // 已有3A算法，不需要前端处理
        
        // 创建唤醒词检测器
        wakeword_detector_ = std::make_unique<wakeword::WakewordDetector>(ww_cfg);
        
        // 初始化唤醒词检测器
        wakeword::WakewordError ww_err = wakeword_detector_->initialize();
        if (ww_err != wakeword::WakewordError::NONE) {
            LOG_ERROR("Chatbot", "唤醒词检测器初始化失败");
            wakeword_detector_.reset();
            return ChatbotError::WAKEWORD_ERROR;
        }
        
        // 设置状态为就绪
        state_.store(ChatbotState::READY);

        // 启用唤醒词检测
        wakeword_detector_->setEnabled(true);
        
        // 设置唤醒词回调
        setupWakewordCallbacks();
        
        // 设置音频系统的唤醒词回调，将音频数据传递给唤醒词检测器
        setupWakewordAudioCallback();
        
        // 设置AI音频流回调（用于发送音频到服务器）
        // 注意：此时WebSocket可能还未初始化，但回调会在WebSocket初始化后生效
        setupAIAudioCallback();
        
        return ChatbotError::NONE;

    } catch (const std::exception& e) {
        LOG_ERROR("Chatbot", "唤醒词检测器初始化异常: %s", e.what());
        wakeword_detector_.reset();
        return ChatbotError::WAKEWORD_ERROR;
    }
}

ChatbotError ChatbotSystem::deinitializeWakeword() {
    if (wakeword_detector_) {
        // 禁用唤醒词检测
        wakeword_detector_->setEnabled(false);
        wakeword_detector_.reset();
        LOG_INFO("Chatbot", "唤醒词检测器已释放");
    }
    
    return ChatbotError::NONE;
}

void ChatbotSystem::setupWakewordCallbacks() {
    if (!wakeword_detector_) {
        return;
    }
    
    // 设置唤醒词检测回调
    wakeword_detector_->setWakewordCallback([this](wakeword::WakewordResult result, int hotword_index) {
        if (result == wakeword::WakewordResult::HOTWORD_1 || 
            result == wakeword::WakewordResult::HOTWORD_2 || 
            result == wakeword::WakewordResult::HOTWORD_3) {
            
            LOG_INFO("Chatbot", "========================================");
            LOG_INFO("Chatbot", "  🎙️ 唤醒词检测到！Hotword %d", hotword_index);
            LOG_INFO("Chatbot", "========================================");
            
            // 检查当前状态，只有在READY状态才处理唤醒词
            ChatbotState current_state = state_.load();
            if (current_state != ChatbotState::READY) {
                LOG_WARN("Chatbot", "当前状态不是READY，忽略唤醒词。当前状态: %s", 
                         stateToString(current_state));
                return;
            }
            
            // 设置状态为连接中
            state_.store(ChatbotState::CONNECTING);
            
            // 初始化WebSocket客户端
            ChatbotError err = initializeWebSocket();
            if (err != ChatbotError::NONE) {
                LOG_ERROR("Chatbot", "WebSocket客户端初始化失败");
                state_.store(ChatbotState::READY);  // 回到就绪状态
                return;
            }
            
            // 连接AI服务器
            err = connectAIServer();
            if (err != ChatbotError::NONE) {
                LOG_ERROR("Chatbot", "AI服务器连接失败");
                state_.store(ChatbotState::READY);  // 回到就绪状态
                return;
            }
            
            // 连接成功后，启动AI音频流并发送Listen消息
            if (protocol_handler_ && ws_client_ && ws_client_->isHandshaked() && audio_system_) {
                // 启动AI音频流
                audio::AudioError audio_err = audio_system_->startAIStream();
                if (audio_err != audio::AudioError::NONE) {
                    LOG_ERROR("Chatbot", "启动AI音频流失败: %d", static_cast<int>(audio_err));
                    state_.store(ChatbotState::READY);
                    return;
                }
                
                // 设置状态为监听中
                state_.store(ChatbotState::LISTENING);
                LOG_INFO("Chatbot", "开始监听用户语音");
                
                // 生成并发送Listen消息（开始监听，自动模式）
                std::string listen_msg = protocol_handler_->generateListenMessage(
                    protocol_handle::ListenState::START, 
                    protocol_handle::ListenMode::AUTO);
                
                // 发送Listen消息
                websocket::WebSocketError ws_err = ws_client_->sendText(listen_msg);
                if (ws_err != websocket::WebSocketError::NONE) {
                    LOG_ERROR("Chatbot", "发送Listen消息失败: %d", static_cast<int>(ws_err));
                    // 发送失败，停止AI音频流并回到就绪状态
                    audio_system_->stopAIStream();
                    state_.store(ChatbotState::READY);
                    return;
                }
            }
        }
    });
    
    // 设置错误回调
    wakeword_detector_->setErrorCallback([this](wakeword::WakewordError error, const std::string& message) {
        (void)error;
        (void)message;
    });
}

void ChatbotSystem::setupWakewordAudioCallback() {
    if (!audio_system_ || !wakeword_detector_) {
        return;
    }
    
    // 设置音频系统的唤醒词回调，将音频数据传递给唤醒词检测器
    audio_system_->setWakewordCallback([this](const int16_t* data, size_t length) {
        if (wakeword_detector_ && wakeword_detector_->isEnabled()) {
            wakeword_detector_->processAudioFrame(data, static_cast<int>(length));
        }
    });
}

void ChatbotSystem::setupAIAudioCallback() {
    if (!audio_system_) {
        return;
    }
    
    // 设置AI音频流回调，当AI音频流激活时，将Opus编码后的音频帧发送到服务器
    // 注意：WebSocket可能在此时还未初始化，回调中会检查WebSocket状态
    audio_system_->setAIAudioCallback([this](audio::AudioFramePtr frame) {
        if (!frame || !frame->data || frame->size == 0) {
            return;
        }
        
        // 检查WebSocket连接状态
        if (!ws_client_ || !ws_client_->isHandshaked()) {
            // WebSocket未连接，跳过音频发送（不打印日志，避免日志过多）
            return;
        }
        
        // 检查是否在监听状态
        ChatbotState current_state = state_.load();
        if (current_state != ChatbotState::LISTENING) {
            // 不在监听状态，不发送音频
            return;
        }
        
        // 发送二进制音频数据（Opus编码）
        websocket::WebSocketError err = ws_client_->sendBinary(
            reinterpret_cast<const char*>(frame->data), frame->size);
        
        if (err != websocket::WebSocketError::NONE) {
            LOG_WARN("Chatbot", "发送音频数据失败: %d", static_cast<int>(err));
        }
    });
    
    LOG_INFO("Chatbot", "AI音频流回调已设置");
}

// 初始化ChatbotSystem
ChatbotError ChatbotSystem::open() {
    // 设置状态为初始化中
    state_.store(ChatbotState::INITIALIZING);

    // 初始化音频
    ChatbotError err = initializeAudio();
    if (err != ChatbotError::NONE) {
        return err;
    }
    
    // 初始化网络
    err = initializeNetwork();
    if (err != ChatbotError::NONE) {
        return err;
    }
    
    // 初始化激活管理器
    err = initializeActivation();
    if (err != ChatbotError::NONE) {
        return err;
    }
    
    // 激活检测
    err = checkActivation();
    if (err != ChatbotError::NONE) {
        return err;
    }
    
    // 注册MCP工具
    err = initializeMCP();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("Chatbot", "MCP工具注册失败");
        return err;
    }
    
    // 初始化唤醒词检测器
    err = initializeWakeword();
    if (err != ChatbotError::NONE) {
        LOG_ERROR("Chatbot", "唤醒词检测器初始化失败");
        return err;
    }
    
    return ChatbotError::NONE;
}

void ChatbotSystem::close() {
    if (state_.load() == ChatbotState::CLOSED) {
        return;
    }
    LOG_INFO("Chatbot", "正在关闭ChatbotSystem...");
    state_.store(ChatbotState::CLOSED);
    
    deinitializeWakeword();     // 关闭唤醒词检测器
    deinitializeWebSocket();    // 关闭WebSocket客户端
    deinitializeMCP();          // 关闭MCP服务器
    deinitializeActivation();   // 关闭激活管理器
    deinitializeWiFi();         // 关闭WiFi管理器
    deinitializeAudio();        // 关闭音频系统
    
    LOG_INFO("Chatbot", "ChatbotSystem已关闭");
}

ChatbotState ChatbotSystem::getState() const {
    return state_.load();
}

bool ChatbotSystem::isReady() const {
    return state_.load() == ChatbotState::READY;
}

} // namespace chatbot
} // namespace app