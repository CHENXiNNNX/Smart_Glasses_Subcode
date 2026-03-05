/*
 * protocol_handle.cc - xiaozhi 协议处理
 */

#include "protocol_handle.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/time/time.hpp"

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#else
#include "../../../third_party/libdatachannel/deps/json/single_include/nlohmann/json.hpp"
#endif

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <string_view>
#include <thread>

using json = nlohmann::json;

namespace
{
    constexpr const char*               LOG_TAG = "PROTOCOL_HANDLE";
    constexpr std::chrono::milliseconds QUEUE_WAIT_TIMEOUT{100};
    constexpr int                       MIN_SAMPLE_RATE               = 8000;
    constexpr int                       MAX_SAMPLE_RATE               = 48000;
    constexpr int                       MIN_CHANNEL_COUNT             = 1;
    constexpr int                       MAX_CHANNEL_COUNT             = 2;
    constexpr int                       MIN_FRAME_DURATION_MS         = 10;
    constexpr int                       MAX_FRAME_DURATION_MS         = 60;
    constexpr int                       PERFORMANCE_WARN_THRESHOLD_US = 1000;
    constexpr int                       HEALTH_CHECK_SAMPLE_MIN       = 100;
    constexpr double                    ERROR_RATE_WARN_THRESHOLD     = 1.0;
    constexpr double                    EXCEPTION_RATE_WARN_THRESHOLD = 0.1;
} // namespace

namespace app
{
    namespace chatbot
    {
        namespace protocol_handle
        {

            using namespace tool::log;

            static const std::unordered_map<std::string, MessageType> MESSAGE_TYPE_MAP = {
                {"hello", MessageType::HELLO}, {"listen", MessageType::LISTEN},
                {"stt", MessageType::STT},     {"llm", MessageType::LLM},
                {"tts", MessageType::TTS},     {"mcp", MessageType::MCP},
                {"error", MessageType::ERROR}};

            static const std::unordered_map<MessageType, std::string> MESSAGE_TYPE_TO_STRING = {
                {MessageType::HELLO, "hello"}, {MessageType::LISTEN, "listen"},
                {MessageType::STT, "stt"},     {MessageType::LLM, "llm"},
                {MessageType::TTS, "tts"},     {MessageType::MCP, "mcp"},
                {MessageType::ERROR, "error"}, {MessageType::UNKNOWN, "unknown"}};

            static const std::unordered_map<std::string, EmotionType> EMOTION_MAP = {
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
                {"confused", EmotionType::CONFUSED}};

            static const std::unordered_map<EmotionType, std::string> EMOTION_TO_STRING = {
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
                {EmotionType::CONFUSED, "confused"}};

            static const std::unordered_map<std::string, ListenMode> LISTEN_MODE_MAP = {
                {"auto", ListenMode::AUTO},
                {"manual", ListenMode::MANUAL},
                {"realtime", ListenMode::REALTIME}};

            static const std::unordered_map<ListenMode, std::string> LISTEN_MODE_TO_STRING = {
                {ListenMode::AUTO, "auto"},
                {ListenMode::MANUAL, "manual"},
                {ListenMode::REALTIME, "realtime"}};

            class ProtocolHandler::Impl
            {
            public:
                // 配置
                ProtocolConfig config;

                // 会话管理
                std::string        session_id;
                mutable std::mutex session_mutex;

                // 回调函数
                HelloCallback         hello_callback;
                ListenCallback        listen_callback;
                STTCallback           stt_callback;
                LLMCallback           llm_callback;
                TTSCallback           tts_callback;
                MCPCallback           mcp_callback;
                ErrorCallback         error_callback;
                ProtocolErrorCallback protocol_error_callback;
                mutable std::mutex    callback_mutex;

                // 异步消息处理队列
                std::queue<std::vector<char>> message_queue; // 消息缓冲队列
                std::mutex                    queue_mutex;
                std::condition_variable       queue_cv;
                std::unique_ptr<std::thread>  processor_thread;
                std::atomic<bool>             should_stop{false};

                // 统计信息
                Stats stats;

                explicit Impl(ProtocolConfig cfg) : config(std::move(cfg)) {}

                ~Impl()
                {
                    stopProcessorThread();
                }

                void startProcessorThread()
                {
                    if (!config.enable_async_processing)
                    {
                        return;
                    }

                    should_stop.store(false, std::memory_order_release);

                    processor_thread = std::make_unique<std::thread>(
                        [this]()
                        {
                            while (!should_stop.load(std::memory_order_acquire))
                            {
                                std::unique_lock<std::mutex> lock(queue_mutex);

                                // 等待消息或停止信号
                                queue_cv.wait_for(lock, QUEUE_WAIT_TIMEOUT,
                                                  [this]() {
                                                      return !message_queue.empty() ||
                                                             should_stop.load(
                                                                 std::memory_order_acquire);
                                                  });

                                if (should_stop.load(std::memory_order_acquire) &&
                                    message_queue.empty())
                                {
                                    break;
                                }

                                if (message_queue.empty())
                                {
                                    continue;
                                }

                                std::vector<char> buffer = std::move(message_queue.front());
                                message_queue.pop();
                                lock.unlock();

                                // 处理消息
                                processMessageInternal(buffer.data(), buffer.size());
                            }
                        });
                }

                void stopProcessorThread()
                {
                    should_stop.store(true, std::memory_order_release);
                    queue_cv.notify_all();

                    if (processor_thread && processor_thread->joinable())
                    {
                        processor_thread->join();
                    }
                    processor_thread.reset();
                }

                ProtocolError enqueueMessage(const char* buffer, size_t size)
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);

                    // 检查队列是否满
                    if (message_queue.size() >= config.message_queue_size)
                    {
                        stats.queue_overflows.fetch_add(1, std::memory_order_relaxed);
                        LOG_WARN(LOG_TAG, "消息队列满 %u，丢弃",
                                 static_cast<unsigned>(config.message_queue_size));
                        message_queue.pop(); // 丢弃最旧的消息
                    }

                    // 拷贝消息到队列
                    std::vector<char> msg_buffer(size);
                    std::copy_n(buffer, size, msg_buffer.begin());
                    message_queue.push(std::move(msg_buffer));
                    queue_cv.notify_one();

                    return ProtocolError::NONE;
                }

                MessageType processMessageInternal(const char* buffer, size_t size)
                {
                    uint64_t parse_start = static_cast<uint64_t>(app::tool::time::uptime_us());

                    try
                    {
                        json j = json::parse(std::string_view(buffer, size));

                        // 验证基本结构
                        if (!j.contains("type"))
                        {
                            invokeProtocolError(ProtocolError::MISSING_FIELD,
                                                "Message missing 'type' field");
                            stats.invalid_messages.fetch_add(1, std::memory_order_relaxed);
                            return MessageType::UNKNOWN;
                        }

                        if (!j["type"].is_string())
                        {
                            invokeProtocolError(ProtocolError::TYPE_MISMATCH,
                                                "'type' field is not string");
                            stats.invalid_messages.fetch_add(1, std::memory_order_relaxed);
                            return MessageType::UNKNOWN;
                        }

                        std::string type_str = j["type"];
                        MessageType type     = ProtocolHandler::stringToMessageType(type_str);

                        // 提取session_id（如果有）
                        if (j.contains("session_id") && j["session_id"].is_string())
                        {
                            std::lock_guard<std::mutex> lock(session_mutex);
                            session_id = j["session_id"];
                        }

                        // 根据类型分发消息
                        bool success = dispatchMessage(type, j);

                        // 更新统计
                        if (success)
                        {
                            stats.messages_parsed.fetch_add(1, std::memory_order_relaxed);
                            updateMessageTypeStats(type);
                        }

                        // 计算解析时间
                        uint64_t parse_time =
                            static_cast<uint64_t>(app::tool::time::uptime_us()) - parse_start;
                        stats.total_parse_time_us.fetch_add(parse_time, std::memory_order_relaxed);

                        uint64_t total = stats.messages_parsed.load(std::memory_order_relaxed);
                        if (total > 0)
                        {
                            stats.avg_parse_time_us.store(stats.total_parse_time_us.load() / total,
                                                          std::memory_order_relaxed);
                        }

                        return type;
                    }
                    catch (const json::parse_error& e)
                    {
                        LOG_ERROR(LOG_TAG, "JSON解析失败: %s", e.what());
                        stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                        invokeErrorCallback(std::string("JSON parse error: ") + e.what());
                        return MessageType::ERROR;
                    }
                    catch (const json::type_error& e)
                    {
                        LOG_ERROR(LOG_TAG, "JSON类型错误: %s", e.what());
                        stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                        invokeErrorCallback(std::string("JSON type error: ") + e.what());
                        return MessageType::ERROR;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "异常: %s", e.what());
                        stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                        invokeErrorCallback(std::string("Exception: ") + e.what());
                        return MessageType::ERROR;
                    }
                }

                bool dispatchMessage(MessageType type, const json& j)
                {
                    switch (type)
                    {
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
                        LOG_WARN(LOG_TAG, "未知消息类型 %d", static_cast<int>(type));
                        return false;
                    }
                }

                bool handleHelloMessage(const json& j)
                {
                    try
                    {
                        HelloMessage msg;

                        msg.session_id = getStringOrDefault(j, "session_id", "");
                        msg.version =
                            getValidatedInt(j, "version", config.protocol_version, 0,
                                            std::numeric_limits<int>::max(), "hello.version");
                        msg.transport = getStringOrDefault(j, "transport", "websocket");

                        auto audio_it = j.find("audio_params");
                        if (audio_it != j.end() && audio_it->is_object() && !audio_it->is_null())
                        {
                            applyAudioParams(*audio_it, msg);
                        }
                        else
                        {
                            applyDefaultAudioParams(msg);
                            LOG_WARN(LOG_TAG, "audio_params缺失，用默认值");
                        }

                        LOG_INFO(LOG_TAG, "Hello session=%s %dHz/%dch/%dms", msg.session_id.c_str(),
                                 msg.audio_params.sample_rate, msg.audio_params.channels,
                                 msg.audio_params.frame_duration);

                        invokeHelloCallback(msg);
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "Hello处理失败: %s", e.what());
                        return false;
                    }
                }

                bool handleListenMessage(const json& j)
                {
                    try
                    {
                        ListenMessage msg;

                        // 提取基本字段
                        msg.session_id = j.value("session_id", "");

                        // 解析监听状态
                        if (j.contains("state") && j["state"].is_string())
                        {
                            std::string state_str = j["state"];
                            if (state_str == "start")
                            {
                                msg.state = ListenState::START;
                            }
                            else if (state_str == "stop")
                            {
                                msg.state = ListenState::STOP;
                            }
                            else
                            {
                                LOG_WARN(LOG_TAG, "未知listen状态 %s", state_str.c_str());
                                msg.state = ListenState::START;
                            }
                        }
                        else
                        {
                            msg.state = ListenState::START;
                        }

                        // 解析监听模式
                        if (j.contains("mode") && j["mode"].is_string())
                        {
                            std::string mode_str = j["mode"];
                            msg.mode             = stringToListenMode(mode_str);
                        }
                        else
                        {
                            msg.mode = ListenMode::AUTO;
                        }

                        // 调用回调
                        invokeListenCallback(msg);

                        LOG_DEBUG(LOG_TAG, "<- Listen: state=%s, mode=%s",
                                  (msg.state == ListenState::START) ? "start" : "stop",
                                  listenModeToString(msg.mode).c_str());

                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "Listen处理失败: %s", e.what());
                        return false;
                    }
                }

                bool handleSTTMessage(const json& j)
                {
                    try
                    {
                        STTMessage msg;

                        msg.session_id = j.value("session_id", "");
                        msg.text       = j.value("text", "");
                        msg.is_final   = j.value("is_final", true);

                        LOG_DEBUG(LOG_TAG, "-> STT: \"%s\" (final: %s)", msg.text.c_str(),
                                  msg.is_final ? "true" : "false");

                        // 触发回调
                        invokeSTTCallback(msg);
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "STT处理失败: %s", e.what());
                        return false;
                    }
                }

                bool handleLLMMessage(const json& j)
                {
                    try
                    {
                        LLMMessage msg;

                        msg.session_id = j.value("session_id", "");
                        msg.text       = j.value("text", "");
                        msg.is_final   = j.value("is_final", true);

                        // 解析情感类型
                        std::string emotion_str = j.value("emotion", "neutral");
                        msg.emotion             = ProtocolHandler::stringToEmotionType(emotion_str);

                        LOG_DEBUG(LOG_TAG, "<- LLM: \"%s\" (emotion: %s, final: %s)",
                                  msg.text.c_str(),
                                  ProtocolHandler::emotionTypeToString(msg.emotion).c_str(),
                                  msg.is_final ? "true" : "false");

                        // 触发回调
                        invokeLLMCallback(msg);
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "LLM处理失败: %s", e.what());
                        return false;
                    }
                }

                bool handleTTSMessage(const json& j)
                {
                    try
                    {
                        // 解析TTS状态
                        std::string state_str = j.value("state", "start");

                        // 只处理已知的状态，其他状态（如 sentence_end）静默忽略
                        if (state_str == "start")
                        {
                            TTSMessage msg;
                            msg.session_id = j.value("session_id", "");
                            msg.text       = j.value("text", "");
                            msg.state      = TTSState::START;

                            LOG_DEBUG(LOG_TAG, "<- TTS: state=start");
                            invokeTTSCallback(msg);
                        }
                        else if (state_str == "sentence_start")
                        {
                            TTSMessage msg;
                            msg.session_id = j.value("session_id", "");
                            msg.text       = j.value("text", "");
                            msg.state      = TTSState::SENTENCE_START;

                            LOG_DEBUG(LOG_TAG, "<- TTS: state=sentence_start, text=\"%s\"",
                                      msg.text.c_str());
                            invokeTTSCallback(msg);
                        }
                        else if (state_str == "stop")
                        {
                            TTSMessage msg;
                            msg.session_id = j.value("session_id", "");
                            msg.text       = j.value("text", "");
                            msg.state      = TTSState::STOP;

                            LOG_DEBUG(LOG_TAG, "<- TTS: state=stop");
                            invokeTTSCallback(msg);
                        }
                        else
                        {
                            // 未知状态
                            LOG_DEBUG(LOG_TAG, "<- TTS: state=%s (ignored)", state_str.c_str());
                        }

                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "TTS处理失败: %s", e.what());
                        return false;
                    }
                }

                bool handleMCPMessage(const json& j)
                {
                    try
                    {
                        if (!j.contains("payload"))
                        {
                            LOG_ERROR(LOG_TAG, "MCP缺少payload");
                            return false;
                        }

                        std::string mcp_payload = j["payload"].dump();

                        LOG_DEBUG(LOG_TAG, "MCP payload %u",
                                  static_cast<unsigned>(mcp_payload.size()));

                        // 触发回调并获取响应
                        std::string response = invokeMCPCallback(mcp_payload);

                        // 记录响应（但发送由外部处理）
                        if (!response.empty())
                        {
                            LOG_DEBUG(LOG_TAG, "MCP响应 %u",
                                      static_cast<unsigned>(response.size()));
                        }

                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "MCP处理失败: %s", e.what());
                        return false;
                    }
                }

                bool handleErrorMessage(const json& j)
                {
                    try
                    {
                        std::string error_msg = j.value("message", "Unknown error");

                        LOG_ERROR(LOG_TAG, "<- Error: %s", error_msg.c_str());

                        // 触发错误回调
                        invokeErrorCallback(error_msg);
                        return true;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "Error处理失败: %s", e.what());
                        return false;
                    }
                }

                void invokeHelloCallback(const HelloMessage& msg)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (hello_callback)
                    {
                        try
                        {
                            hello_callback(msg);
                        }
                        catch (const std::runtime_error& e)
                        {
                            LOG_ERROR(LOG_TAG, "Hello回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                        catch (const std::logic_error& e)
                        {
                            LOG_ERROR(LOG_TAG, "Hello回调逻辑错误: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "Hello回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                void invokeListenCallback(const ListenMessage& msg)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (listen_callback)
                    {
                        try
                        {
                            listen_callback(msg);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "Listen回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                void invokeSTTCallback(const STTMessage& msg)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (stt_callback)
                    {
                        try
                        {
                            stt_callback(msg);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "STT回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                void invokeLLMCallback(const LLMMessage& msg)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (llm_callback)
                    {
                        try
                        {
                            llm_callback(msg);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "LLM回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                void invokeTTSCallback(const TTSMessage& msg)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (tts_callback)
                    {
                        try
                        {
                            tts_callback(msg);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "TTS回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                std::string invokeMCPCallback(const std::string& payload)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (mcp_callback)
                    {
                        try
                        {
                            return mcp_callback(payload);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "MCP回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                            return "";
                        }
                    }
                    return "";
                }

                void invokeErrorCallback(const std::string& error)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (error_callback)
                    {
                        try
                        {
                            error_callback(error);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "Error回调异常: %s", e.what());
                        }
                    }
                }

                void invokeProtocolError(ProtocolError error, const std::string& detail)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (protocol_error_callback)
                    {
                        try
                        {
                            protocol_error_callback(error, detail);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "协议错误回调异常: %s", e.what());
                        }
                    }
                }

                void updateMessageTypeStats(MessageType type)
                {
                    switch (type)
                    {
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

                static std::string getStringOrDefault(const json& node, std::string_view key,
                                                      const std::string& fallback)
                {
                    auto it = node.find(std::string(key));
                    if (it != node.end() && it->is_string())
                    {
                        return it->get<std::string>();
                    }
                    return fallback;
                }

                int getValidatedInt(const json& node, std::string_view key, int fallback,
                                    int min_value, int max_value,
                                    std::string_view warn_context) const
                {
                    auto it = node.find(std::string(key));
                    if (it != node.end() && it->is_number_integer())
                    {
                        int value = it->get<int>();
                        if (value >= min_value && value <= max_value)
                        {
                            return value;
                        }

                        std::string key_str(key);
                        LOG_WARN(LOG_TAG, "%.*s %s=%d 越界[%d,%d]，用%d",
                                 static_cast<int>(warn_context.size()), warn_context.data(),
                                 key_str.c_str(), value, min_value, max_value, fallback);
                    }
                    return fallback;
                }

                void applyDefaultAudioParams(HelloMessage& msg) const
                {
                    msg.audio_params.format         = config.default_audio_format;
                    msg.audio_params.sample_rate    = config.default_sample_rate;
                    msg.audio_params.channels       = config.default_channels;
                    msg.audio_params.frame_duration = config.default_frame_duration;
                }

                void applyAudioParams(const json& audio_node, HelloMessage& msg) const
                {
                    applyDefaultAudioParams(msg);

                    msg.audio_params.format =
                        getStringOrDefault(audio_node, "format", msg.audio_params.format);
                    msg.audio_params.sample_rate =
                        getValidatedInt(audio_node, "sample_rate", msg.audio_params.sample_rate,
                                        MIN_SAMPLE_RATE, MAX_SAMPLE_RATE, "audio.sample_rate");
                    msg.audio_params.channels =
                        getValidatedInt(audio_node, "channels", msg.audio_params.channels,
                                        MIN_CHANNEL_COUNT, MAX_CHANNEL_COUNT, "audio.channels");
                    msg.audio_params.frame_duration = getValidatedInt(
                        audio_node, "frame_duration", msg.audio_params.frame_duration,
                        MIN_FRAME_DURATION_MS, MAX_FRAME_DURATION_MS, "audio.frame_duration");
                }
            };

            ProtocolHandler::ProtocolHandler(const ProtocolConfig& config)
                : pImpl_(std::make_unique<Impl>(config))
            {

                // 启动异步处理线程
                if (pImpl_->config.enable_async_processing)
                {
                    pImpl_->startProcessorThread();
                }
            }

            ProtocolHandler::~ProtocolHandler()
            {

                // 输出最终统计
                logStats();
            }

            ProtocolError ProtocolHandler::parseMessage(const char* buffer, size_t size)
            {
                if (!buffer || size == 0)
                {
                    return ProtocolError::INVALID_MESSAGE;
                }

                if (pImpl_->config.enable_async_processing)
                {
                    // 异步处理
                    return pImpl_->enqueueMessage(buffer, size);
                }

                // 同步处理
                pImpl_->processMessageInternal(buffer, size);
                return ProtocolError::NONE;
            }

            MessageType ProtocolHandler::parseMessageSync(const std::string& json_str)
            {
                return pImpl_->processMessageInternal(json_str.data(), json_str.size());
            }

            std::string ProtocolHandler::generateHelloMessage(int sample_rate, int channels,
                                                              int frame_duration)
            {
                // 使用配置的默认值（如果参数为-1）
                if (sample_rate < 0)
                {
                    sample_rate = pImpl_->config.default_sample_rate;
                }
                if (channels < 0)
                {
                    channels = pImpl_->config.default_channels;
                }
                if (frame_duration < 0)
                {
                    frame_duration = pImpl_->config.default_frame_duration;
                }

                // 参数验证
                if (sample_rate < MIN_SAMPLE_RATE || sample_rate > MAX_SAMPLE_RATE)
                {
                    LOG_WARN(LOG_TAG, "sample_rate %d 无效，用默认", sample_rate);
                    sample_rate = pImpl_->config.default_sample_rate;
                }

                if (channels < MIN_CHANNEL_COUNT || channels > MAX_CHANNEL_COUNT)
                {
                    LOG_WARN(LOG_TAG, "channels %d 无效，用默认", channels);
                    channels = pImpl_->config.default_channels;
                }

                if (frame_duration < MIN_FRAME_DURATION_MS ||
                    frame_duration > MAX_FRAME_DURATION_MS)
                {
                    LOG_WARN(LOG_TAG, "frame_duration %d 无效，用默认", frame_duration);
                    frame_duration = pImpl_->config.default_frame_duration;
                }

                json j;
                j["type"]                           = "hello";
                j["version"]                        = pImpl_->config.protocol_version;
                j["transport"]                      = "websocket";
                j["features"]["aec"]                = pImpl_->config.enable_aec;
                j["features"]["mcp"]                = pImpl_->config.enable_mcp;
                j["audio_params"]["format"]         = pImpl_->config.default_audio_format;
                j["audio_params"]["sample_rate"]    = sample_rate;
                j["audio_params"]["channels"]       = channels;
                j["audio_params"]["frame_duration"] = frame_duration;

                return j.dump();
            }

            std::string ProtocolHandler::generateListenMessage(ListenState state, ListenMode mode)
            {
                std::lock_guard<std::mutex> lock(pImpl_->session_mutex);

                json j;
                j["session_id"] = pImpl_->session_id;
                j["type"]       = "listen";
                j["state"]      = (state == ListenState::START) ? "start" : "stop";
                j["mode"]       = listenModeToString(mode);

                return j.dump();
            }

            void ProtocolHandler::setHelloCallback(HelloCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->hello_callback = std::move(callback);
            }

            void ProtocolHandler::setListenCallback(ListenCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->listen_callback = std::move(callback);
            }

            void ProtocolHandler::setSTTCallback(STTCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->stt_callback = std::move(callback);
            }

            void ProtocolHandler::setLLMCallback(LLMCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->llm_callback = std::move(callback);
            }

            void ProtocolHandler::setTTSCallback(TTSCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->tts_callback = std::move(callback);
            }

            void ProtocolHandler::setMCPCallback(MCPCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->mcp_callback = std::move(callback);
            }

            void ProtocolHandler::setErrorCallback(ErrorCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->error_callback = std::move(callback);
            }

            void ProtocolHandler::setProtocolErrorCallback(ProtocolErrorCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->protocol_error_callback = std::move(callback);
            }

            void ProtocolHandler::setSessionId(const std::string& session_id)
            {
                std::lock_guard<std::mutex> lock(pImpl_->session_mutex);
                pImpl_->session_id = session_id;
            }

            std::string ProtocolHandler::getSessionId() const
            {
                std::lock_guard<std::mutex> lock(pImpl_->session_mutex);
                return pImpl_->session_id;
            }

            bool ProtocolHandler::hasActiveSession() const
            {
                std::lock_guard<std::mutex> lock(pImpl_->session_mutex);
                return !pImpl_->session_id.empty();
            }

            MessageType ProtocolHandler::stringToMessageType(const std::string& type_str)
            {
                auto it = MESSAGE_TYPE_MAP.find(type_str);
                return (it != MESSAGE_TYPE_MAP.end()) ? it->second : MessageType::UNKNOWN;
            }

            std::string ProtocolHandler::messageTypeToString(MessageType type)
            {
                auto it = MESSAGE_TYPE_TO_STRING.find(type);
                return (it != MESSAGE_TYPE_TO_STRING.end()) ? it->second : "unknown";
            }

            EmotionType ProtocolHandler::stringToEmotionType(const std::string& emotion_str)
            {
                auto it = EMOTION_MAP.find(emotion_str);
                return (it != EMOTION_MAP.end()) ? it->second : EmotionType::NEUTRAL;
            }

            std::string ProtocolHandler::emotionTypeToString(EmotionType emotion)
            {
                auto it = EMOTION_TO_STRING.find(emotion);
                return (it != EMOTION_TO_STRING.end()) ? it->second : "neutral";
            }

            ListenMode ProtocolHandler::stringToListenMode(const std::string& mode_str)
            {
                auto it = LISTEN_MODE_MAP.find(mode_str);
                return (it != LISTEN_MODE_MAP.end()) ? it->second : ListenMode::AUTO;
            }

            std::string ProtocolHandler::listenModeToString(ListenMode mode)
            {
                auto it = LISTEN_MODE_TO_STRING.find(mode);
                return (it != LISTEN_MODE_TO_STRING.end()) ? it->second : "auto";
            }

            void ProtocolHandler::getStats(Stats& out_stats) const
            {
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

            void ProtocolHandler::resetStats()
            {
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
            }

            void ProtocolHandler::logStats() const
            {
                uint64_t total      = pImpl_->stats.messages_parsed.load();
                uint64_t errors     = pImpl_->stats.parse_errors.load();
                uint64_t exceptions = pImpl_->stats.callback_exceptions.load();
                uint64_t invalid    = pImpl_->stats.invalid_messages.load();
                uint64_t overflows  = pImpl_->stats.queue_overflows.load();

                LOG_INFO(LOG_TAG, "统计 解析=%llu 错误=%llu 异常=%llu 无效=%llu 溢出=%llu", total,
                         errors, exceptions, invalid, overflows);

                if (total > static_cast<uint64_t>(HEALTH_CHECK_SAMPLE_MIN))
                {
                    double er  = static_cast<double>(errors + invalid) / total * 100.0;
                    double exr = static_cast<double>(exceptions) / total * 100.0;
                    if (er > ERROR_RATE_WARN_THRESHOLD)
                        LOG_WARN(LOG_TAG, "错误率 %.1f%%", er);
                    if (exr > EXCEPTION_RATE_WARN_THRESHOLD)
                        LOG_WARN(LOG_TAG, "异常率 %.1f%%", exr);
                    if (overflows > 0)
                        LOG_WARN(LOG_TAG, "队列溢出 %llu", overflows);
                }
            }

        } // namespace protocol_handle
    }     // namespace chatbot
} // namespace app
