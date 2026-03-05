/* protocol_handle.hpp - xiaozhi AI协议处理 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <queue>
#include <deque>
#include <chrono>

namespace app
{
    namespace chatbot
    {
        namespace protocol_handle
        {

            class ProtocolHandler;
            struct ProtocolMessage;

            enum class MessageType
            {
                HELLO = 0, // 握手消息
                LISTEN,    // 监听控制
                STT,       // 语音识别结果
                LLM,       // 大语言模型回复
                TTS,       // 文本转语音
                MCP,       // MCP工具调用
                ERROR,     // 错误消息
                UNKNOWN    // 未知类型
            };

            enum class ListenMode
            {
                AUTO = 0, // 自动停止（检测到静音后停止）
                MANUAL,   // 手动停止（需要显式调用stop）
                REALTIME  // 实时模式（需要AEC支持）
            };

            enum class ListenState
            {
                START = 0, // 开始监听
                STOP       // 停止监听
            };

            enum class TTSState
            {
                START = 0,      // TTS开始
                SENTENCE_START, // 句子开始
                STOP            // TTS停止
            };

            enum class EmotionType
            {
                NEUTRAL = 0, // 中性
                HAPPY,       // 开心
                LAUGHING,    // 大笑
                FUNNY,       // 有趣
                SAD,         // 悲伤
                ANGRY,       // 生气
                CRYING,      // 哭泣
                LOVING,      // 爱意
                EMBARRASSED, // 尴尬
                SURPRISED,   // 惊讶
                SHOCKED,     // 震惊
                THINKING,    // 思考
                WINKING,     // 眨眼
                COOL,        // 酷
                RELAXED,     // 放松
                DELICIOUS,   // 美味
                KISSY,       // 亲吻
                CONFIDENT,   // 自信
                SLEEPY,      // 困倦
                SILLY,       // 傻笑
                CONFUSED     // 困惑
            };

            enum class ProtocolError
            {
                NONE = 0,
                PARSE_ERROR,        // 解析错误
                INVALID_MESSAGE,    // 无效消息
                MISSING_FIELD,      // 缺失字段
                TYPE_MISMATCH,      // 类型不匹配
                OUT_OF_RANGE,       // 超出范围
                CALLBACK_EXCEPTION, // 回调异常
                QUEUE_FULL,         // 队列满
                TIMEOUT,            // 超时
                UNKNOWN             // 未知错误
            };

            struct AudioParams
            {
                std::string format;         // 音频格式（opus）
                int         sample_rate;    // 采样率
                int         channels;       // 声道数
                int         frame_duration; // 帧时长（ms）

                AudioParams() : format("opus"), sample_rate(16000), channels(1), frame_duration(20)
                {
                }
            };

            struct HelloMessage
            {
                std::string session_id;   // 会话ID
                int         version;      // 协议版本
                std::string transport;    // 传输方式
                AudioParams audio_params; // 音频参数

                HelloMessage() : version(1), transport("websocket") {}
            };

            struct ListenMessage
            {
                std::string session_id; // 会话ID
                ListenState state;      // 监听状态
                ListenMode  mode;       // 监听模式

                ListenMessage() : state(ListenState::START), mode(ListenMode::AUTO) {}
            };

            struct STTMessage
            {
                std::string session_id; // 会话ID
                std::string text;       // 识别文本
                bool        is_final;   // 是否最终结果

                STTMessage() : is_final(true) {}
            };

            struct LLMMessage
            {
                std::string session_id; // 会话ID
                std::string text;       // 回复文本
                EmotionType emotion;    // 情感类型
                bool        is_final;   // 是否最终结果

                LLMMessage() : emotion(EmotionType::NEUTRAL), is_final(true) {}
            };

            struct TTSMessage
            {
                std::string session_id; // 会话ID
                TTSState    state;      // TTS状态
                std::string text;       // TTS文本（sentence_start时有效）

                TTSMessage() : state(TTSState::START) {}
            };

            struct ProtocolConfig
            {
                // 协议版本
                int protocol_version = 1;

                // 音频默认参数
                std::string default_audio_format   = "opus";
                int         default_sample_rate    = 16000;
                int         default_channels       = 1;
                int         default_frame_duration = 20;

                // 协议特性
                bool enable_aec = false; // 回声消除
                bool enable_mcp = true;  // MCP工具支持

                // 性能配置
                size_t message_queue_size      = 100;  // 异步队列大小
                size_t message_pool_size       = 200;  // 消息池大小
                bool   enable_async_processing = true; // 启用异步处理

                // 验证配置
                bool enable_message_validation = true;  // 启用消息验证
                bool strict_mode               = false; // 严格模式（字段缺失即报错）

                // 超时配置
                int parse_timeout_ms = 5000; // 解析超时
            };

            using HelloCallback  = std::function<void(const HelloMessage& msg)>;
            using ListenCallback = std::function<void(const ListenMessage& msg)>;
            using STTCallback    = std::function<void(const STTMessage& msg)>;
            using LLMCallback    = std::function<void(const LLMMessage& msg)>;
            using TTSCallback    = std::function<void(const TTSMessage& msg)>;
            using MCPCallback    = std::function<std::string(const std::string& mcp_payload)>;
            using ErrorCallback  = std::function<void(const std::string& error)>;
            using ProtocolErrorCallback =
                std::function<void(ProtocolError error, const std::string& detail)>;

            class ProtocolHandler
            {
            public:
                explicit ProtocolHandler(const ProtocolConfig& config = ProtocolConfig());
                ~ProtocolHandler();

                ProtocolError parseMessage(const char* buffer, size_t size);
                MessageType   parseMessageSync(const std::string& json_str);

                std::string generateHelloMessage(int sample_rate = -1, int channels = -1,
                                                 int frame_duration = -1);
                std::string generateListenMessage(ListenState state,
                                                  ListenMode  mode = ListenMode::AUTO);

                void setHelloCallback(HelloCallback callback);
                void setListenCallback(ListenCallback callback);
                void setSTTCallback(STTCallback callback);
                void setLLMCallback(LLMCallback callback);
                void setTTSCallback(TTSCallback callback);
                void setMCPCallback(MCPCallback callback);
                void setErrorCallback(ErrorCallback callback);
                void setProtocolErrorCallback(ProtocolErrorCallback callback);

                void        setSessionId(const std::string& session_id);
                std::string getSessionId() const;
                bool        hasActiveSession() const;

                static MessageType stringToMessageType(const std::string& type_str);
                static std::string messageTypeToString(MessageType type);
                static EmotionType stringToEmotionType(const std::string& emotion_str);
                static std::string emotionTypeToString(EmotionType emotion);
                static ListenMode  stringToListenMode(const std::string& mode_str);
                static std::string listenModeToString(ListenMode mode);

                struct Stats
                {
                    std::atomic<uint64_t> messages_parsed{0};     // 总解析消息数
                    std::atomic<uint64_t> parse_errors{0};        // 解析错误数
                    std::atomic<uint64_t> callback_exceptions{0}; // 回调异常数
                    std::atomic<uint64_t> invalid_messages{0};    // 无效消息数
                    std::atomic<uint64_t> queue_overflows{0};     // 队列溢出数

                    // 各类型消息统计
                    std::atomic<uint64_t> hello_count{0};
                    std::atomic<uint64_t> stt_count{0};
                    std::atomic<uint64_t> llm_count{0};
                    std::atomic<uint64_t> tts_count{0};
                    std::atomic<uint64_t> mcp_count{0};
                    std::atomic<uint64_t> error_count{0};

                    // 性能统计
                    std::atomic<uint64_t> total_parse_time_us{0}; // 总解析时间
                    std::atomic<uint64_t> avg_parse_time_us{0};   // 平均解析时间
                };

                void getStats(Stats& out_stats) const;
                void resetStats();
                void logStats() const;

                // 禁止拷贝和赋值
                ProtocolHandler(const ProtocolHandler&)            = delete;
                ProtocolHandler& operator=(const ProtocolHandler&) = delete;

            private:
                class Impl;
                std::unique_ptr<Impl> pImpl_;
            };

        } // namespace protocol_handle
    }     // namespace chatbot
} // namespace app
