/**
 * @file protocol_handle.hpp
 * @brief xiaozhi AI协议处理模块
 */

#ifndef PROTOCOL_HANDLE_HPP
#define PROTOCOL_HANDLE_HPP

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

            // ============================================================================
            // 前向声明
            // ============================================================================
            class ProtocolHandler;
            struct ProtocolMessage;

            // ============================================================================
            // 消息类型枚举
            // ============================================================================

            /**
             * @brief 消息类型
             */
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

            /**
             * @brief 监听模式
             */
            enum class ListenMode
            {
                AUTO = 0, // 自动停止（检测到静音后停止）
                MANUAL,   // 手动停止（需要显式调用stop）
                REALTIME  // 实时模式（需要AEC支持）
            };

            /**
             * @brief 监听状态
             */
            enum class ListenState
            {
                START = 0, // 开始监听
                STOP       // 停止监听
            };

            /**
             * @brief TTS状态
             */
            enum class TTSState
            {
                START = 0,      // TTS开始
                SENTENCE_START, // 句子开始
                STOP            // TTS停止
            };

            /**
             * @brief LLM情感类型（21种）
             */
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

            /**
             * @brief 协议错误类型
             */
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

            // ============================================================================
            // 消息结构体（使用智能指针管理）
            // ============================================================================

            /**
             * @brief 音频参数
             */
            struct AudioParams
            {
                std::string format;         // 音频格式（opus）
                int         sample_rate;    // 采样率
                int         channels;       // 声道数
                int         frame_duration; // 帧时长（ms）

                AudioParams() : format("opus"), sample_rate(48000), channels(1), frame_duration(20)
                {
                }
            };

            /**
             * @brief Hello消息
             */
            struct HelloMessage
            {
                std::string session_id;   // 会话ID
                int         version;      // 协议版本
                std::string transport;    // 传输方式
                AudioParams audio_params; // 音频参数

                HelloMessage() : version(1), transport("websocket") {}
            };

            /**
             * @brief Listen消息
             */
            struct ListenMessage
            {
                std::string session_id; // 会话ID
                ListenState state;      // 监听状态
                ListenMode  mode;       // 监听模式

                ListenMessage() : state(ListenState::START), mode(ListenMode::AUTO) {}
            };

            /**
             * @brief STT消息
             */
            struct STTMessage
            {
                std::string session_id; // 会话ID
                std::string text;       // 识别文本
                bool        is_final;   // 是否最终结果

                STTMessage() : is_final(true) {}
            };

            /**
             * @brief LLM消息
             */
            struct LLMMessage
            {
                std::string session_id; // 会话ID
                std::string text;       // 回复文本
                EmotionType emotion;    // 情感类型
                bool        is_final;   // 是否最终结果

                LLMMessage() : emotion(EmotionType::NEUTRAL), is_final(true) {}
            };

            /**
             * @brief TTS消息
             */
            struct TTSMessage
            {
                std::string session_id; // 会话ID
                TTSState    state;      // TTS状态
                std::string text;       // TTS文本（sentence_start时有效）

                TTSMessage() : state(TTSState::START) {}
            };

            // ============================================================================
            // 协议配置
            // ============================================================================

            /**
             * @brief 协议处理器配置
             */
            struct ProtocolConfig
            {
                // 协议版本
                int protocol_version = 1;

                // 音频默认参数
                std::string default_audio_format   = "opus";
                int         default_sample_rate    = 48000;
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

            // ============================================================================
            // 回调函数类型
            // ============================================================================

            /**
             * @brief Hello消息回调
             */
            using HelloCallback = std::function<void(const HelloMessage& msg)>;

            /**
             * @brief Listen消息回调
             */
            using ListenCallback = std::function<void(const ListenMessage& msg)>;

            /**
             * @brief STT消息回调
             */
            using STTCallback = std::function<void(const STTMessage& msg)>;

            /**
             * @brief LLM消息回调
             */
            using LLMCallback = std::function<void(const LLMMessage& msg)>;

            /**
             * @brief TTS消息回调
             */
            using TTSCallback = std::function<void(const TTSMessage& msg)>;

            /**
             * @brief MCP消息回调（返回响应）
             */
            using MCPCallback = std::function<std::string(const std::string& mcp_payload)>;

            /**
             * @brief 错误消息回调
             */
            using ErrorCallback = std::function<void(const std::string& error)>;

            /**
             * @brief 协议错误回调
             */
            using ProtocolErrorCallback =
                std::function<void(ProtocolError error, const std::string& detail)>;

            // ============================================================================
            // 协议处理器类
            // ============================================================================

            /**
             * @brief xiaozhi协议处理器
             * @details 协议处理器，特性：
             *          - RAII自动资源管理
             *          - 智能指针，无裸指针
             *          - 消息异步队列，解耦接收和处理
             *          - 哈希表O(1)查找
             *          - 异常安全的回调
             *          - 完整的统计和监控
             */
            class ProtocolHandler
            {
            public:
                /**
                 * @brief 构造函数
                 * @param config 协议配置
                 */
                explicit ProtocolHandler(const ProtocolConfig& config = ProtocolConfig());

                /**
                 * @brief 析构函数（RAII自动清理所有资源）
                 */
                ~ProtocolHandler();

                // ========================================================================
                // 消息解析（接收方向）
                // ========================================================================

                /**
                 * @brief 解析JSON消息（异步）
                 * @param buffer 消息缓冲区
                 * @param size 消息大小
                 * @return ProtocolError::NONE 成功
                 */
                ProtocolError parseMessage(const char* buffer, size_t size);

                /**
                 * @brief 解析JSON消息（同步，用于测试）
                 * @param json_str JSON字符串
                 * @return MessageType 消息类型
                 */
                MessageType parseMessageSync(const std::string& json_str);

                // ========================================================================
                // 消息生成（发送方向）
                // ========================================================================

                /**
                 * @brief 生成Hello消息
                 * @param sample_rate 采样率（默认使用配置）
                 * @param channels 声道数（默认使用配置）
                 * @param frame_duration 帧时长（默认使用配置）
                 * @return JSON字符串
                 */
                std::string generateHelloMessage(int sample_rate = -1, int channels = -1,
                                                 int frame_duration = -1);

                /**
                 * @brief 生成Listen消息
                 * @param state 监听状态
                 * @param mode 监听模式
                 * @return JSON字符串
                 */
                std::string generateListenMessage(ListenState state,
                                                  ListenMode  mode = ListenMode::AUTO);

                // ========================================================================
                // 回调设置（线程安全）
                // ========================================================================

                /**
                 * @brief 设置Hello消息回调
                 */
                void setHelloCallback(HelloCallback callback);

                /**
                 * @brief 设置Listen消息回调
                 */
                void setListenCallback(ListenCallback callback);

                /**
                 * @brief 设置STT消息回调
                 */
                void setSTTCallback(STTCallback callback);

                /**
                 * @brief 设置LLM消息回调
                 */
                void setLLMCallback(LLMCallback callback);

                /**
                 * @brief 设置TTS消息回调
                 */
                void setTTSCallback(TTSCallback callback);

                /**
                 * @brief 设置MCP消息回调
                 */
                void setMCPCallback(MCPCallback callback);

                /**
                 * @brief 设置错误消息回调
                 */
                void setErrorCallback(ErrorCallback callback);

                /**
                 * @brief 设置协议错误回调
                 */
                void setProtocolErrorCallback(ProtocolErrorCallback callback);

                // ========================================================================
                // 会话管理
                // ========================================================================

                /**
                 * @brief 设置会话ID
                 * @param session_id 会话ID
                 */
                void setSessionId(const std::string& session_id);

                /**
                 * @brief 获取会话ID
                 * @return 当前会话ID
                 */
                std::string getSessionId() const;

                /**
                 * @brief 检查是否有活跃会话
                 * @return true 有会话
                 */
                bool hasActiveSession() const;

                // ========================================================================
                // 工具函数（静态，使用哈希表优化）
                // ========================================================================

                /**
                 * @brief 字符串转消息类型（O(1)）
                 */
                static MessageType stringToMessageType(const std::string& type_str);

                /**
                 * @brief 消息类型转字符串
                 */
                static std::string messageTypeToString(MessageType type);

                /**
                 * @brief 字符串转情感类型（O(1)）
                 */
                static EmotionType stringToEmotionType(const std::string& emotion_str);

                /**
                 * @brief 情感类型转字符串
                 */
                static std::string emotionTypeToString(EmotionType emotion);

                /**
                 * @brief 字符串转监听模式
                 */
                static ListenMode stringToListenMode(const std::string& mode_str);

                /**
                 * @brief 监听模式转字符串
                 */
                static std::string listenModeToString(ListenMode mode);

                // ========================================================================
                // 统计信息
                // ========================================================================

                /**
                 * @brief 协议处理器统计信息
                 */
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

                /**
                 * @brief 获取统计信息
                 * @param out_stats 输出统计信息
                 */
                void getStats(Stats& out_stats) const;

                /**
                 * @brief 重置统计信息
                 */
                void resetStats();

                /**
                 * @brief 输出统计日志
                 */
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

#endif // PROTOCOL_HANDLE_HPP
