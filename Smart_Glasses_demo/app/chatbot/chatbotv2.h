/**
 * @file chatbotv2.h
 * @brief xiaozhi AI系统主控制器V2 - 现代C++重写版本
 * @details 特性：
 *          - RAII资源管理（智能指针）
 *          - 整合所有V2模块（StateMachine, Protocol, Activation, WebSocket, UDP, MCP, Wakeword）
 *          - 消除全局静态指针
 *          - 安全的线程管理（可中断延迟任务）
 *          - 异常安全的回调
 *          - 统一日志系统
 *          - 配置化设计
 *          - 完整统计监控
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-11
 */

#ifndef CHATBOTV2_H
#define CHATBOTV2_H

#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <cstdint>

// 前向声明（避免头文件依赖）
namespace glasses {
    namespace media {
        namespace audio {
            class AudioSystemV2;
            struct AudioFrame;
            using AudioFramePtr = std::shared_ptr<AudioFrame>;
        }
    }
}

namespace glasses {
namespace chatbot {

// ============================================================================
// 前向声明
// ============================================================================
class ChatbotSystemV2;

// ============================================================================
// Chatbot系统状态枚举
// ============================================================================

/**
 * @brief Chatbot系统状态
 */
enum class ChatbotState {
    UNINITIALIZED = 0,  // 未初始化
    INITIALIZED,        // 已初始化
    ACTIVATING,         // 激活中
    ACTIVATED,          // 已激活
    CONNECTING,         // 连接AI服务器中
    CONNECTED,          // 已连接
    READY,              // 就绪（可接受唤醒词）
    ACTIVE,             // 活跃（对话中）
    ERROR,              // 错误状态
    SHUTDOWN            // 已关闭
};

/**
 * @brief Chatbot错误类型
 */
enum class ChatbotError {
    NONE = 0,
    INITIALIZATION_FAILED,  // 初始化失败
    ACTIVATION_FAILED,      // 激活失败
    CONNECTION_FAILED,      // 连接失败
    AUDIO_SYSTEM_ERROR,     // 音频系统错误
    NETWORK_ERROR,          // 网络错误
    CALLBACK_EXCEPTION,     // 回调异常
    INVALID_STATE,          // 无效状态
    TIMEOUT,                // 超时
    UNKNOWN                 // 未知错误
};

// ============================================================================
// Chatbot配置
// ============================================================================

/**
 * @brief Chatbot系统配置
 */
struct ChatbotConfig {
    // 设备信息（空字符串表示自动获取）
    std::string device_id;              // 设备ID（MAC地址）
    std::string client_id;              // 客户端ID（UUID）
    std::string config_file_path = "./system_para.conf";  // UUID配置文件路径
    
    // 服务器配置
    std::string api_url = "wss://api.tenclass.net/xiaozhi/v1/";
    std::string activation_api_url = "https://api.tenclass.net/xiaozhi/ota/";
    
    // 唤醒词配置
    std::string wakeword_resource_file = "./third_party/snowboy/resources/common.res";
    std::string wakeword_model_file = "./third_party/snowboy/resources/models/echo.pmdl";
    float wakeword_sensitivity = 0.5f;
    float wakeword_audio_gain = 1.0f;
    
    // 功能开关
    bool auto_activate = true;          // 自动检查激活状态
    bool auto_connect = true;           // 自动连接AI服务器
    bool enable_wakeword = true;        // 启用唤醒词检测
    bool enable_mcp_tools = true;       // 启用MCP工具
    bool enable_ipc = false;            // 启用UDP IPC（多进程通信）
    
    // 超时配置
    int activation_timeout_sec = 300;   // 激活超时（5分钟）
    int connection_timeout_ms = 10000;  // 连接超时（10秒）
    
    // 日志配置
    bool enable_detailed_logging = false;  // 详细日志
};

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * @brief STT文本回调
 */
using STTTextCallback = std::function<void(const std::string& text, bool is_final)>;

/**
 * @brief LLM文本回调
 */
using LLMTextCallback = std::function<void(const std::string& text, bool is_final)>;

/**
 * @brief TTS音频回调（Opus编码）
 */
using TTSAudioCallback = std::function<void(const uint8_t* data, size_t size)>;

/**
 * @brief 状态变化回调
 */
using ChatbotStateCallback = std::function<void(ChatbotState old_state, ChatbotState new_state)>;

/**
 * @brief 错误回调
 */
using ChatbotErrorCallback = std::function<void(ChatbotError error, const std::string& message)>;

/**
 * @brief 唤醒词检测回调
 */
using WakewordDetectedCallback = std::function<void(int hotword_index)>;

/**
 * @brief 激活状态回调
 */
using ActivationStateCallback = std::function<void(bool is_activated, const std::string& activation_code)>;

// ============================================================================
// ChatbotSystemV2 主控制器
// ============================================================================

/**
 * @brief xiaozhi AI系统主控制器V2
 * @details 现代C++重写的AI系统，集成所有V2模块，特性：
 *          - RAII自动资源管理
 *          - 智能指针，无裸指针，无全局静态指针
 *          - 安全的线程管理（可中断延迟任务）
 *          - 异常安全的回调
 *          - 模块化初始化（失败自动回滚）
 *          - 完整的统计监控
 */
class ChatbotSystemV2 {
public:
    /**
     * @brief 构造函数
     * @param config Chatbot配置
     */
    explicit ChatbotSystemV2(const ChatbotConfig& config = ChatbotConfig());
    
    /**
     * @brief 析构函数（RAII自动清理所有资源）
     */
    ~ChatbotSystemV2();
    
    // ========================================================================
    // 初始化和关闭
    // ========================================================================
    
    /**
     * @brief 初始化Chatbot系统
     * @param audio_system 音频系统V2实例（智能指针）
     * @return ChatbotError::NONE 成功
     */
    ChatbotError initialize(std::shared_ptr<media::audio::AudioSystemV2> audio_system);
    
    /**
     * @brief 启动Chatbot服务（激活 → 连接 → 就绪）
     * @return ChatbotError::NONE 成功
     */
    ChatbotError start();
    
    /**
     * @brief 停止Chatbot服务
     */
    void stop();
    
    /**
     * @brief 关闭Chatbot系统
     */
    void shutdown();
    
    // ========================================================================
    // AI交互控制
    // ========================================================================
    
    /**
     * @brief 手动开始监听用户语音
     * @param mode 监听模式（"auto", "manual", "realtime"）
     * @return ChatbotError::NONE 成功
     */
    ChatbotError startListening(const std::string& mode = "auto");
    
    /**
     * @brief 停止监听
     */
    ChatbotError stopListening();
    
    /**
     * @brief 发送文本消息（测试用）
     * @param text 文本内容
     * @return ChatbotError::NONE 成功
     */
    ChatbotError sendTextMessage(const std::string& text);
    
    /**
     * @brief 手动触发唤醒词（测试用）
     */
    void triggerWakeword();
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取Chatbot系统状态
     */
    ChatbotState getState() const;
    
    /**
     * @brief 检查是否已就绪
     */
    bool isReady() const;
    
    /**
     * @brief 检查是否已激活
     */
    bool isActivated() const;
    
    /**
     * @brief 检查是否已连接
     */
    bool isConnected() const;
    
    /**
     * @brief 获取当前会话ID
     */
    std::string getSessionId() const;
    
    /**
     * @brief 获取设备ID
     */
    std::string getDeviceId() const;
    
    /**
     * @brief 获取客户端ID
     */
    std::string getClientId() const;
    
    // ========================================================================
    // MCP工具注册接口
    // ========================================================================
    
    /**
     * @brief 注册MCP工具（便捷方法）
     * @param name 工具名称（格式：self.module.function）
     * @param description 工具描述
     * @param callback 工具回调函数
     * @return ChatbotError::NONE 成功
     */
    template<typename CallbackType>
    ChatbotError registerMCPTool(const std::string& name,
                                 const std::string& description,
                                 CallbackType callback);
    
    /**
     * @brief 获取已注册工具数量
     */
    size_t getMCPToolCount() const;
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /**
     * @brief 设置STT文本回调
     */
    void setSTTCallback(STTTextCallback callback);
    
    /**
     * @brief 设置LLM文本回调
     */
    void setLLMCallback(LLMTextCallback callback);
    
    /**
     * @brief 设置TTS音频回调
     */
    void setTTSCallback(TTSAudioCallback callback);
    
    /**
     * @brief 设置状态变化回调
     */
    void setStateCallback(ChatbotStateCallback callback);
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(ChatbotErrorCallback callback);
    
    /**
     * @brief 设置唤醒词检测回调
     */
    void setWakewordCallback(WakewordDetectedCallback callback);
    
    /**
     * @brief 设置激活状态回调
     */
    void setActivationCallback(ActivationStateCallback callback);
    
    // ========================================================================
    // 统计信息
    // ========================================================================
    
    /**
     * @brief Chatbot系统统计信息
     */
    struct Stats {
        std::atomic<uint64_t> messages_sent{0};         // 发送消息数
        std::atomic<uint64_t> messages_received{0};     // 接收消息数
        std::atomic<uint64_t> wakeword_detected{0};     // 唤醒词检测次数
        std::atomic<uint64_t> conversations{0};         // 对话次数
        std::atomic<uint64_t> stt_received{0};          // STT消息数
        std::atomic<uint64_t> llm_received{0};          // LLM消息数
        std::atomic<uint64_t> tts_received{0};          // TTS消息数
        std::atomic<uint64_t> mcp_calls{0};             // MCP调用次数
        std::atomic<uint64_t> errors{0};                // 错误次数
        std::atomic<uint64_t> total_uptime_us{0};       // 总运行时间
    };
    
    /**
     * @brief 获取统计信息
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
    
    /**
     * @brief 输出所有子模块统计日志
     */
    void logAllStats() const;
    
    // 禁止拷贝和赋值
    ChatbotSystemV2(const ChatbotSystemV2&) = delete;
    ChatbotSystemV2& operator=(const ChatbotSystemV2&) = delete;

private:
    class Impl;  // Pimpl惯用法
    std::unique_ptr<Impl> pImpl_;
};

} // namespace chatbot
} // namespace glasses

#endif // CHATBOTV2_H

