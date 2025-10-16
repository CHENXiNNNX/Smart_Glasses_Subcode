/**
 * @file handlev2.cc
 * @brief xiaozhi AI协议处理模块V2实现
 */

#include "handlev2.h"
#include "../../tool/log/log.h"
#include "../../../common/common.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <condition_variable>

using json = nlohmann::json;

namespace glasses {
namespace chatbot {
namespace protocol {

using namespace tool::logger;

// ============================================================================
// 静态哈希表（O(1)查找优化）
// ============================================================================

// 消息类型哈希表
static const std::unordered_map<std::string, MessageType> g_type_map = {
    {"hello", MessageType::HELLO},
    {"listen", MessageType::LISTEN},
    {"stt", MessageType::STT},
    {"llm", MessageType::LLM},
    {"tts", MessageType::TTS},
    {"mcp", MessageType::MCP},
    {"error", MessageType::ERROR}
};

static const std::unordered_map<MessageType, std::string> g_type_to_string = {
    {MessageType::HELLO, "hello"},
    {MessageType::LISTEN, "listen"},
    {MessageType::STT, "stt"},
    {MessageType::LLM, "llm"},
    {MessageType::TTS, "tts"},
    {MessageType::MCP, "mcp"},
    {MessageType::ERROR, "error"},
    {MessageType::UNKNOWN, "unknown"}
};

// 情感类型哈希表
static const std::unordered_map<std::string, EmotionType> g_emotion_map = {
    {"neutral", EmotionType::NEUTRAL},
    {"happy", EmotionType::HAPPY},
    {"laughing", EmotionType::LAUGHING},
    {"funny", EmotionType::FUNNY},
    {"sad", EmotionType::SAD},
    {"angry", EmotionType::ANGRY},
    {"crying", EmotionType::CRYING},
    {"loving", EmotionType::LOVING},
    {"embarrassed", EmotionType::EMBARRASSED},
    {"surprised", EmotionType::SURPRISED},
    {"shocked", EmotionType::SHOCKED},
    {"thinking", EmotionType::THINKING},
    {"winking", EmotionType::WINKING},
    {"cool", EmotionType::COOL},
    {"relaxed", EmotionType::RELAXED},
    {"delicious", EmotionType::DELICIOUS},
    {"kissy", EmotionType::KISSY},
    {"confident", EmotionType::CONFIDENT},
    {"sleepy", EmotionType::SLEEPY},
    {"silly", EmotionType::SILLY},
    {"confused", EmotionType::CONFUSED}
};

static const std::unordered_map<EmotionType, std::string> g_emotion_to_string = {
    {EmotionType::NEUTRAL, "neutral"},
    {EmotionType::HAPPY, "happy"},
    {EmotionType::LAUGHING, "laughing"},
    {EmotionType::FUNNY, "funny"},
    {EmotionType::SAD, "sad"},
    {EmotionType::ANGRY, "angry"},
    {EmotionType::CRYING, "crying"},
    {EmotionType::LOVING, "loving"},
    {EmotionType::EMBARRASSED, "embarrassed"},
    {EmotionType::SURPRISED, "surprised"},
    {EmotionType::SHOCKED, "shocked"},
    {EmotionType::THINKING, "thinking"},
    {EmotionType::WINKING, "winking"},
    {EmotionType::COOL, "cool"},
    {EmotionType::RELAXED, "relaxed"},
    {EmotionType::DELICIOUS, "delicious"},
    {EmotionType::KISSY, "kissy"},
    {EmotionType::CONFIDENT, "confident"},
    {EmotionType::SLEEPY, "sleepy"},
    {EmotionType::SILLY, "silly"},
    {EmotionType::CONFUSED, "confused"}
};

// 监听模式哈希表
static const std::unordered_map<std::string, ListenMode> g_listen_mode_map = {
    {"auto", ListenMode::AUTO},
    {"manual", ListenMode::MANUAL},
    {"realtime", ListenMode::REALTIME}
};

static const std::unordered_map<ListenMode, std::string> g_listen_mode_to_string = {
    {ListenMode::AUTO, "auto"},
    {ListenMode::MANUAL, "manual"},
    {ListenMode::REALTIME, "realtime"}
};

// ============================================================================
// ProtocolHandlerV2::Impl 内部实现（Pimpl惯用法）
// ============================================================================

class ProtocolHandlerV2::Impl {
public:
    // 配置
    ProtocolConfig config;
    
    // 会话管理
    std::string session_id;
    mutable std::mutex session_mutex;
    
    // 回调函数
    HelloCallback hello_callback;
    ListenCallback listen_callback;
    STTCallback stt_callback;
    LLMCallback llm_callback;
    TTSCallback tts_callback;
    MCPCallback mcp_callback;
    ErrorCallback error_callback;
    ProtocolErrorCallback protocol_error_callback;
    mutable std::mutex callback_mutex;
    
    // 异步消息处理队列
    std::queue<std::pair<std::vector<char>, uint64_t>> message_queue;  // (buffer, timestamp)
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::unique_ptr<std::thread> processor_thread;
    std::atomic<bool> should_stop{false};
    
    // 统计信息
    Stats stats;
    
    explicit Impl(const ProtocolConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("ProtocolV2", "Impl created");
    }
    
    ~Impl() {
        LOG_DEBUG("ProtocolV2", "Impl destroying...");
        stopProcessorThread();
        LOG_DEBUG("ProtocolV2", "Impl destroyed");
    }
    
    // ========================================================================
    // 异步消息处理
    // ========================================================================
    
    void startProcessorThread() {
        if (!config.enable_async_processing) {
            return;
        }
        
        should_stop.store(false, std::memory_order_release);
        
        processor_thread = std::make_unique<std::thread>([this]() {
            LOG_DEBUG("ProtocolV2", "Message processor thread started");
            
            while (!should_stop.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> lock(queue_mutex);
                
                // 等待消息或停止信号
                queue_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !message_queue.empty() || should_stop.load(std::memory_order_acquire);
                });
                
                if (should_stop.load(std::memory_order_acquire) && message_queue.empty()) {
                    break;
                }
                
                if (message_queue.empty()) {
                    continue;
                }
                
                // 取出消息
                auto [buffer, timestamp] = std::move(message_queue.front());
                message_queue.pop();
                lock.unlock();
                
                // 处理消息
                processMessageInternal(buffer.data(), buffer.size(), timestamp);
            }
            
            LOG_DEBUG("ProtocolV2", "Message processor thread stopped");
        });
    }
    
    void stopProcessorThread() {
        should_stop.store(true, std::memory_order_release);
        queue_cv.notify_all();
        
        if (processor_thread && processor_thread->joinable()) {
            processor_thread->join();
        }
        processor_thread.reset();
    }
    
    ProtocolError enqueueMessage(const char* buffer, size_t size) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        
        // 检查队列是否满
        if (message_queue.size() >= config.message_queue_size) {
            stats.queue_overflows.fetch_add(1, std::memory_order_relaxed);
            LOG_WARN("ProtocolV2", "Message queue full (%zu), dropping oldest message", 
                    config.message_queue_size);
            message_queue.pop();  // 丢弃最旧的消息
        }
        
        // 拷贝消息到队列（必要的拷贝，因为buffer可能被复用）
        std::vector<char> msg_buffer(buffer, buffer + size);
        message_queue.push({std::move(msg_buffer), get_nowus()});
        queue_cv.notify_one();
        
        return ProtocolError::NONE;
    }
    
    // ========================================================================
    // 消息解析核心逻辑
    // ========================================================================
    
    MessageType processMessageInternal(const char* buffer, size_t size, uint64_t recv_timestamp) {
        uint64_t parse_start = get_nowus();
        
        try {
            // 解析JSON（使用nlohmann的in-place解析，减少拷贝）
            json j = json::parse(buffer, buffer + size);
            
            // 验证基本结构
            if (!j.contains("type")) {
                invokeProtocolError(ProtocolError::MISSING_FIELD, "Message missing 'type' field");
                stats.invalid_messages.fetch_add(1, std::memory_order_relaxed);
                return MessageType::UNKNOWN;
            }
            
            if (!j["type"].is_string()) {
                invokeProtocolError(ProtocolError::TYPE_MISMATCH, "'type' field is not string");
                stats.invalid_messages.fetch_add(1, std::memory_order_relaxed);
                return MessageType::UNKNOWN;
            }
            
            std::string type_str = j["type"];
            MessageType type = ProtocolHandlerV2::stringToMessageType(type_str);
            
            // 提取session_id（如果有）
            if (j.contains("session_id") && j["session_id"].is_string()) {
                std::lock_guard<std::mutex> lock(session_mutex);
                session_id = j["session_id"];
            }
            
            // 根据类型分发消息
            bool success = dispatchMessage(type, j);
            
            // 更新统计
            if (success) {
                stats.messages_parsed.fetch_add(1, std::memory_order_relaxed);
                updateMessageTypeStats(type);
            }
            
            // 计算解析时间
            uint64_t parse_time = get_nowus() - parse_start;
            stats.total_parse_time_us.fetch_add(parse_time, std::memory_order_relaxed);
            
            uint64_t total = stats.messages_parsed.load(std::memory_order_relaxed);
            if (total > 0) {
                stats.avg_parse_time_us.store(
                    stats.total_parse_time_us.load() / total, 
                    std::memory_order_relaxed
                );
            }
            
            return type;
            
        } catch (const json::parse_error& e) {
            LOG_ERROR("ProtocolV2", "JSON parse error: %s", e.what());
            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
            invokeErrorCallback(std::string("JSON parse error: ") + e.what());
            return MessageType::ERROR;
            
        } catch (const json::type_error& e) {
            LOG_ERROR("ProtocolV2", "JSON type error: %s", e.what());
            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
            invokeErrorCallback(std::string("JSON type error: ") + e.what());
            return MessageType::ERROR;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Exception: %s", e.what());
            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
            invokeErrorCallback(std::string("Exception: ") + e.what());
            return MessageType::ERROR;
        }
    }
    
    bool dispatchMessage(MessageType type, const json& j) {
        switch (type) {
            case MessageType::HELLO:
                return handleHelloMessage(j);
                
            case MessageType::LISTEN:
                return handleListenMessage(j);
                
            case MessageType::STT:
                return handleSTTMessage(j);
                
            case MessageType::LLM:
                return handleLLMMessage(j);
                
            case MessageType::TTS:
                return handleTTSMessage(j);
                
            case MessageType::MCP:
                return handleMCPMessage(j);
                
            case MessageType::ERROR:
                return handleErrorMessage(j);
                
            default:
                LOG_WARN("ProtocolV2", "Unknown message type: %d", static_cast<int>(type));
                return false;
        }
    }
    
    // ========================================================================
    // 各类型消息处理（异常安全）
    // ========================================================================
    
    bool handleHelloMessage(const json& j) {
        try {
            HelloMessage msg;
            
            // 提取基本字段（带null检查）
            if (j.contains("session_id") && !j["session_id"].is_null()) {
                msg.session_id = j["session_id"].get<std::string>();
            } else {
                msg.session_id = "";
            }
            
            if (j.contains("version") && j["version"].is_number()) {
                msg.version = j["version"].get<int>();
            } else {
                msg.version = 1;
            }
            
            if (j.contains("transport") && !j["transport"].is_null()) {
                msg.transport = j["transport"].get<std::string>();
            } else {
                msg.transport = "websocket";
            }
            
            // 解析音频参数（带验证+null检查）
            if (j.contains("audio_params") && j["audio_params"].is_object() && !j["audio_params"].is_null()) {
                const auto& ap = j["audio_params"];
                
                // format字段
                if (ap.contains("format") && !ap["format"].is_null()) {
                    msg.audio_params.format = ap["format"].get<std::string>();
                } else {
                    msg.audio_params.format = "opus";
                }
                
                // 采样率验证
                if (ap.contains("sample_rate") && ap["sample_rate"].is_number()) {
                    int sr = ap["sample_rate"];
                    if (sr >= 8000 && sr <= 48000) {
                        msg.audio_params.sample_rate = sr;
                    } else {
                        LOG_WARN("ProtocolV2", "Invalid sample_rate: %d, using default", sr);
                        msg.audio_params.sample_rate = config.default_sample_rate;
                    }
                } else {
                    msg.audio_params.sample_rate = config.default_sample_rate;
                }
                
                // 声道数验证
                if (ap.contains("channels") && ap["channels"].is_number()) {
                    int ch = ap["channels"];
                    if (ch >= 1 && ch <= 2) {
                        msg.audio_params.channels = ch;
                    } else {
                        LOG_WARN("ProtocolV2", "Invalid channels: %d, using default", ch);
                        msg.audio_params.channels = config.default_channels;
                    }
                } else {
                    msg.audio_params.channels = config.default_channels;
                }
                
                // 帧时长验证
                if (ap.contains("frame_duration") && ap["frame_duration"].is_number()) {
                    int fd = ap["frame_duration"];
                    if (fd >= 10 && fd <= 60) {
                        msg.audio_params.frame_duration = fd;
                    } else {
                        LOG_WARN("ProtocolV2", "Invalid frame_duration: %d, using default", fd);
                        msg.audio_params.frame_duration = config.default_frame_duration;
                    }
                } else {
                    msg.audio_params.frame_duration = config.default_frame_duration;
                }
            } else {
                // audio_params不存在或为null，使用全部默认值
                msg.audio_params.format = "opus";
                msg.audio_params.sample_rate = config.default_sample_rate;
                msg.audio_params.channels = config.default_channels;
                msg.audio_params.frame_duration = config.default_frame_duration;
                LOG_WARN("ProtocolV2", "audio_params missing or null, using defaults");
            }
            
            LOG_INFO("ProtocolV2", "← Hello: session=%s, audio=%dHz/%dch/%dms",
                    msg.session_id.c_str(),
                    msg.audio_params.sample_rate,
                    msg.audio_params.channels,
                    msg.audio_params.frame_duration);
            
            // 触发回调（异常安全）
            invokeHelloCallback(msg);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Failed to handle Hello message: %s", e.what());
            return false;
        }
    }
    
    bool handleListenMessage(const json& j) {
        try {
            ListenMessage msg;
            
            // 提取基本字段
            msg.session_id = j.value("session_id", "");
            
            // 解析监听状态
            if (j.contains("state") && j["state"].is_string()) {
                std::string state_str = j["state"];
                if (state_str == "start") {
                    msg.state = ListenState::START;
                } else if (state_str == "stop") {
                    msg.state = ListenState::STOP;
                } else {
                    LOG_WARN("ProtocolV2", "Unknown listen state: %s", state_str.c_str());
                    msg.state = ListenState::START;
                }
            } else {
                msg.state = ListenState::START;
            }
            
            // 解析监听模式
            if (j.contains("mode") && j["mode"].is_string()) {
                std::string mode_str = j["mode"];
                msg.mode = stringToListenMode(mode_str);
            } else {
                msg.mode = ListenMode::AUTO;
            }
            
            // 调用回调
            invokeListenCallback(msg);
            
            LOG_DEBUG("ProtocolV2", "← Listen: state=%s, mode=%s", 
                     (msg.state == ListenState::START) ? "start" : "stop",
                     listenModeToString(msg.mode).c_str());
            
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Failed to handle Listen message: %s", e.what());
            return false;
        }
    }
    
    bool handleSTTMessage(const json& j) {
        try {
            STTMessage msg;
            
            msg.session_id = j.value("session_id", "");
            msg.text = j.value("text", "");
            msg.is_final = j.value("is_final", true);
            
            LOG_DEBUG("ProtocolV2", "← STT: \"%s\" (final: %s)",
                     msg.text.c_str(), msg.is_final ? "true" : "false");
            
            // 触发回调（异常安全）
            invokeSTTCallback(msg);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Failed to handle STT message: %s", e.what());
            return false;
        }
    }
    
    bool handleLLMMessage(const json& j) {
        try {
            LLMMessage msg;
            
            msg.session_id = j.value("session_id", "");
            msg.text = j.value("text", "");
            msg.is_final = j.value("is_final", true);
            
            // 解析情感类型（使用哈希表）
            std::string emotion_str = j.value("emotion", "neutral");
            msg.emotion = ProtocolHandlerV2::stringToEmotionType(emotion_str);
            
            LOG_DEBUG("ProtocolV2", "← LLM: \"%s\" (emotion: %s, final: %s)",
                     msg.text.c_str(), 
                     ProtocolHandlerV2::emotionTypeToString(msg.emotion).c_str(),
                     msg.is_final ? "true" : "false");
            
            // 触发回调（异常安全）
            invokeLLMCallback(msg);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Failed to handle LLM message: %s", e.what());
            return false;
        }
    }
    
    bool handleTTSMessage(const json& j) {
        try {
            TTSMessage msg;
            
            msg.session_id = j.value("session_id", "");
            msg.text = j.value("text", "");
            
            // 解析TTS状态
            std::string state_str = j.value("state", "start");
            if (state_str == "start") {
                msg.state = TTSState::START;
            } else if (state_str == "sentence_start") {
                msg.state = TTSState::SENTENCE_START;
            } else if (state_str == "stop") {
                msg.state = TTSState::STOP;
            } else {
                LOG_WARN("ProtocolV2", "Unknown TTS state: %s, using START", state_str.c_str());
                msg.state = TTSState::START;
            }
            
            LOG_DEBUG("ProtocolV2", "← TTS: state=%s, text=\"%s\"",
                     state_str.c_str(), msg.text.c_str());
            
            // 触发回调（异常安全）
            invokeTTSCallback(msg);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Failed to handle TTS message: %s", e.what());
            return false;
        }
    }
    
    bool handleMCPMessage(const json& j) {
        try {
            if (!j.contains("payload")) {
                LOG_ERROR("ProtocolV2", "MCP message missing 'payload' field");
                return false;
            }
            
            std::string mcp_payload = j["payload"].dump();
            
            LOG_DEBUG("ProtocolV2", "← MCP: payload size=%zu", mcp_payload.size());
            
            // 触发回调并获取响应（异常安全）
            std::string response = invokeMCPCallback(mcp_payload);
            
            // V2改进：在这里记录响应（但发送由外部处理）
            if (!response.empty()) {
                LOG_DEBUG("ProtocolV2", "MCP response size: %zu", response.size());
            }
            
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Failed to handle MCP message: %s", e.what());
            return false;
        }
    }
    
    bool handleErrorMessage(const json& j) {
        try {
            std::string error_msg = j.value("message", "Unknown error");
            
            LOG_ERROR("ProtocolV2", "← Error: %s", error_msg.c_str());
            
            // 触发错误回调
            invokeErrorCallback(error_msg);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("ProtocolV2", "Failed to handle Error message: %s", e.what());
            return false;
        }
    }
    
    // ========================================================================
    // 回调调用（异常安全，参考StateMachineV2）
    // ========================================================================
    
    void invokeHelloCallback(const HelloMessage& msg) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (hello_callback) {
            try {
                hello_callback(msg);
            } catch (const std::runtime_error& e) {
                LOG_ERROR("ProtocolV2", "Hello callback runtime_error: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::logic_error& e) {
                LOG_ERROR("ProtocolV2", "Hello callback logic_error: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "Hello callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeListenCallback(const ListenMessage& msg) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (listen_callback) {
            try {
                listen_callback(msg);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "Listen callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeSTTCallback(const STTMessage& msg) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (stt_callback) {
            try {
                stt_callback(msg);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "STT callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeLLMCallback(const LLMMessage& msg) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (llm_callback) {
            try {
                llm_callback(msg);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "LLM callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeTTSCallback(const TTSMessage& msg) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (tts_callback) {
            try {
                tts_callback(msg);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "TTS callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    std::string invokeMCPCallback(const std::string& payload) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (mcp_callback) {
            try {
                return mcp_callback(payload);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "MCP callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                return "";
            }
        }
        return "";
    }
    
    void invokeErrorCallback(const std::string& error) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (error_callback) {
            try {
                error_callback(error);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "Error callback exception: %s", e.what());
                // 错误回调的异常不再统计，避免递归
            }
        }
    }
    
    void invokeProtocolError(ProtocolError error, const std::string& detail) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (protocol_error_callback) {
            try {
                protocol_error_callback(error, detail);
            } catch (const std::exception& e) {
                LOG_ERROR("ProtocolV2", "Protocol error callback exception: %s", e.what());
            }
        }
    }
    
    // ========================================================================
    // 统计更新
    // ========================================================================
    
    void updateMessageTypeStats(MessageType type) {
        switch (type) {
            case MessageType::HELLO:
                stats.hello_count.fetch_add(1, std::memory_order_relaxed);
                break;
            case MessageType::STT:
                stats.stt_count.fetch_add(1, std::memory_order_relaxed);
                break;
            case MessageType::LLM:
                stats.llm_count.fetch_add(1, std::memory_order_relaxed);
                break;
            case MessageType::TTS:
                stats.tts_count.fetch_add(1, std::memory_order_relaxed);
                break;
            case MessageType::MCP:
                stats.mcp_count.fetch_add(1, std::memory_order_relaxed);
                break;
            case MessageType::ERROR:
                stats.error_count.fetch_add(1, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
};

// ============================================================================
// ProtocolHandlerV2 公共接口实现
// ============================================================================

ProtocolHandlerV2::ProtocolHandlerV2(const ProtocolConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_INFO("ProtocolV2", "Protocol Handler V2 created");
    
    // 启动异步处理线程
    if (pImpl_->config.enable_async_processing) {
        pImpl_->startProcessorThread();
    }
}

ProtocolHandlerV2::~ProtocolHandlerV2() {
    LOG_INFO("ProtocolV2", "Protocol Handler V2 destroying...");
    
    // 输出最终统计
    logStats();
    
    // RAII自动清理（pImpl_析构会停止线程并清理资源）
    LOG_INFO("ProtocolV2", "Protocol Handler V2 destroyed");
}

// ========================================================================
// 消息解析
// ========================================================================

ProtocolError ProtocolHandlerV2::parseMessage(const char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return ProtocolError::INVALID_MESSAGE;
    }
    
    if (pImpl_->config.enable_async_processing) {
        // 异步处理：快速入队，不阻塞调用线程
        return pImpl_->enqueueMessage(buffer, size);
    } else {
        // 同步处理
        pImpl_->processMessageInternal(buffer, size, get_nowus());
        return ProtocolError::NONE;
    }
}

MessageType ProtocolHandlerV2::parseMessageSync(const std::string& json_str) {
    return pImpl_->processMessageInternal(json_str.data(), json_str.size(), get_nowus());
}

// ========================================================================
// 消息生成
// ========================================================================

std::string ProtocolHandlerV2::generateHelloMessage(int sample_rate, 
                                                    int channels, 
                                                    int frame_duration) {
    // 使用配置的默认值（如果参数为-1）
    if (sample_rate < 0) {
        sample_rate = pImpl_->config.default_sample_rate;
    }
    if (channels < 0) {
        channels = pImpl_->config.default_channels;
    }
    if (frame_duration < 0) {
        frame_duration = pImpl_->config.default_frame_duration;
    }
    
    // 参数验证
    if (sample_rate < 8000 || sample_rate > 48000) {
        LOG_WARN("ProtocolV2", "Invalid sample_rate: %d, using default", sample_rate);
        sample_rate = pImpl_->config.default_sample_rate;
    }
    
    if (channels < 1 || channels > 2) {
        LOG_WARN("ProtocolV2", "Invalid channels: %d, using default", channels);
        channels = pImpl_->config.default_channels;
    }
    
    if (frame_duration < 10 || frame_duration > 60) {
        LOG_WARN("ProtocolV2", "Invalid frame_duration: %d, using default", frame_duration);
        frame_duration = pImpl_->config.default_frame_duration;
    }
    
    json j;
    j["type"] = "hello";
    j["version"] = pImpl_->config.protocol_version;
    j["transport"] = "websocket";
    j["audio_params"]["format"] = pImpl_->config.default_audio_format;
    j["audio_params"]["sample_rate"] = sample_rate;
    j["audio_params"]["channels"] = channels;
    j["audio_params"]["frame_duration"] = frame_duration;
    
    return j.dump();
}

std::string ProtocolHandlerV2::generateListenMessage(ListenState state, ListenMode mode) {
    std::lock_guard<std::mutex> lock(pImpl_->session_mutex);
    
    json j;
    j["session_id"] = pImpl_->session_id;
    j["type"] = "listen";
    j["state"] = (state == ListenState::START) ? "start" : "stop";
    j["mode"] = listenModeToString(mode);
    
    return j.dump();
}

// ========================================================================
// 回调设置
// ========================================================================

void ProtocolHandlerV2::setHelloCallback(HelloCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->hello_callback = callback;
}

void ProtocolHandlerV2::setListenCallback(ListenCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->listen_callback = callback;
}

void ProtocolHandlerV2::setSTTCallback(STTCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->stt_callback = callback;
}

void ProtocolHandlerV2::setLLMCallback(LLMCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->llm_callback = callback;
}

void ProtocolHandlerV2::setTTSCallback(TTSCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->tts_callback = callback;
}

void ProtocolHandlerV2::setMCPCallback(MCPCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->mcp_callback = callback;
}

void ProtocolHandlerV2::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->error_callback = callback;
}

void ProtocolHandlerV2::setProtocolErrorCallback(ProtocolErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->protocol_error_callback = callback;
}

// ========================================================================
// 会话管理
// ========================================================================

void ProtocolHandlerV2::setSessionId(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(pImpl_->session_mutex);
    pImpl_->session_id = session_id;
    LOG_INFO("ProtocolV2", "Session ID set: %s", session_id.c_str());
}

std::string ProtocolHandlerV2::getSessionId() const {
    std::lock_guard<std::mutex> lock(pImpl_->session_mutex);
    return pImpl_->session_id;
}

bool ProtocolHandlerV2::hasActiveSession() const {
    std::lock_guard<std::mutex> lock(pImpl_->session_mutex);
    return !pImpl_->session_id.empty();
}

// ========================================================================
// 工具函数（静态，使用哈希表）
// ========================================================================

MessageType ProtocolHandlerV2::stringToMessageType(const std::string& type_str) {
    auto it = g_type_map.find(type_str);
    return (it != g_type_map.end()) ? it->second : MessageType::UNKNOWN;
}

std::string ProtocolHandlerV2::messageTypeToString(MessageType type) {
    auto it = g_type_to_string.find(type);
    return (it != g_type_to_string.end()) ? it->second : "unknown";
}

EmotionType ProtocolHandlerV2::stringToEmotionType(const std::string& emotion_str) {
    auto it = g_emotion_map.find(emotion_str);
    return (it != g_emotion_map.end()) ? it->second : EmotionType::NEUTRAL;
}

std::string ProtocolHandlerV2::emotionTypeToString(EmotionType emotion) {
    auto it = g_emotion_to_string.find(emotion);
    return (it != g_emotion_to_string.end()) ? it->second : "neutral";
}

ListenMode ProtocolHandlerV2::stringToListenMode(const std::string& mode_str) {
    auto it = g_listen_mode_map.find(mode_str);
    return (it != g_listen_mode_map.end()) ? it->second : ListenMode::AUTO;
}

std::string ProtocolHandlerV2::listenModeToString(ListenMode mode) {
    auto it = g_listen_mode_to_string.find(mode);
    return (it != g_listen_mode_to_string.end()) ? it->second : "auto";
}

// ========================================================================
// 统计信息
// ========================================================================

void ProtocolHandlerV2::getStats(Stats& out_stats) const {
    out_stats.messages_parsed.store(pImpl_->stats.messages_parsed.load());
    out_stats.parse_errors.store(pImpl_->stats.parse_errors.load());
    out_stats.callback_exceptions.store(pImpl_->stats.callback_exceptions.load());
    out_stats.invalid_messages.store(pImpl_->stats.invalid_messages.load());
    out_stats.queue_overflows.store(pImpl_->stats.queue_overflows.load());
    out_stats.hello_count.store(pImpl_->stats.hello_count.load());
    out_stats.stt_count.store(pImpl_->stats.stt_count.load());
    out_stats.llm_count.store(pImpl_->stats.llm_count.load());
    out_stats.tts_count.store(pImpl_->stats.tts_count.load());
    out_stats.mcp_count.store(pImpl_->stats.mcp_count.load());
    out_stats.error_count.store(pImpl_->stats.error_count.load());
    out_stats.total_parse_time_us.store(pImpl_->stats.total_parse_time_us.load());
    out_stats.avg_parse_time_us.store(pImpl_->stats.avg_parse_time_us.load());
}

void ProtocolHandlerV2::resetStats() {
    pImpl_->stats.messages_parsed.store(0);
    pImpl_->stats.parse_errors.store(0);
    pImpl_->stats.callback_exceptions.store(0);
    pImpl_->stats.invalid_messages.store(0);
    pImpl_->stats.queue_overflows.store(0);
    pImpl_->stats.hello_count.store(0);
    pImpl_->stats.stt_count.store(0);
    pImpl_->stats.llm_count.store(0);
    pImpl_->stats.tts_count.store(0);
    pImpl_->stats.mcp_count.store(0);
    pImpl_->stats.error_count.store(0);
    pImpl_->stats.total_parse_time_us.store(0);
    pImpl_->stats.avg_parse_time_us.store(0);
    
    LOG_INFO("ProtocolV2", "Stats reset");
}

void ProtocolHandlerV2::logStats() const {
    uint64_t total = pImpl_->stats.messages_parsed.load();
    uint64_t errors = pImpl_->stats.parse_errors.load();
    uint64_t exceptions = pImpl_->stats.callback_exceptions.load();
    uint64_t invalid = pImpl_->stats.invalid_messages.load();
    uint64_t overflows = pImpl_->stats.queue_overflows.load();
    
    LOG_INFO("ProtocolV2", "=== Protocol Handler V2 Statistics ===");
    LOG_INFO("ProtocolV2", "  Messages parsed:     %llu", total);
    LOG_INFO("ProtocolV2", "  Parse errors:        %llu", errors);
    LOG_INFO("ProtocolV2", "  Callback exceptions: %llu", exceptions);
    LOG_INFO("ProtocolV2", "  Invalid messages:    %llu", invalid);
    LOG_INFO("ProtocolV2", "  Queue overflows:     %llu", overflows);
    
    LOG_INFO("ProtocolV2", "Message type breakdown:");
    LOG_INFO("ProtocolV2", "  HELLO: %llu", pImpl_->stats.hello_count.load());
    LOG_INFO("ProtocolV2", "  STT:   %llu", pImpl_->stats.stt_count.load());
    LOG_INFO("ProtocolV2", "  LLM:   %llu", pImpl_->stats.llm_count.load());
    LOG_INFO("ProtocolV2", "  TTS:   %llu", pImpl_->stats.tts_count.load());
    LOG_INFO("ProtocolV2", "  MCP:   %llu", pImpl_->stats.mcp_count.load());
    LOG_INFO("ProtocolV2", "  ERROR: %llu", pImpl_->stats.error_count.load());
    
    // 性能指标
    uint64_t avg_time = pImpl_->stats.avg_parse_time_us.load();
    if (total > 0) {
        LOG_INFO("ProtocolV2", "  Avg parse time: %llu μs", avg_time);
        
        if (avg_time > 1000) {
            LOG_WARN("ProtocolV2", "Average parse time > 1ms, consider optimization");
        }
    }
    
    // 健康度评估
    if (total > 100) {
        double error_rate = (double)(errors + invalid) / total * 100.0;
        double exception_rate = (double)exceptions / total * 100.0;
        
        if (error_rate > 1.0) {
            LOG_WARN("ProtocolV2", "Error rate: %.2f%% (review message format)", error_rate);
        }
        
        if (exception_rate > 0.1) {
            LOG_WARN("ProtocolV2", "Exception rate: %.2f%% (review callbacks)", exception_rate);
        }
        
        if (overflows > 0) {
            LOG_WARN("ProtocolV2", "Queue overflows: %llu (consider increasing queue size)", 
                    overflows);
        }
    }
}

// ========================================================================
// 静态工具函数实现（已在类内联实现）
// ========================================================================

} // namespace protocol
} // namespace chatbot
} // namespace glasses

