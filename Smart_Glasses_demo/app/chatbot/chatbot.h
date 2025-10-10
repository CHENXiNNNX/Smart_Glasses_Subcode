/**
 * @file chatbot.h
 * @brief xiaozhi AI主控制器
 * @details 整合WebSocket、协议处理、状态机、MCP和音频系统
 *          实现完整的AI对话功能
 * 
 * @author Smart_Glasses Team
 * @date 2025-10-10
 */

#ifndef CHATBOT_H
#define CHATBOT_H

#include <string>
#include <memory>
#include <functional>
#include "../media/audio/audio.h"

namespace glasses {
namespace chatbot {

// 前向声明
namespace protocol { 
    class ProtocolHandler;
    struct IoTDescriptor;
}
namespace statemachine { class AIStateMachine; }
namespace mcp { 
    class MCPManager;
    using MethodHandler = std::function<bool(const std::string&, const std::string&, const std::map<std::string, std::string>&)>;
    using StateGetter = std::function<std::map<std::string, std::string>(const std::string&)>;
}

namespace websocket = glasses::protocol::websocket;

// ============================================================================
// AI管理器配置
// ============================================================================

/**
 * @brief AI管理器配置
 */
struct AIConfig {
    std::string device_id;              // 设备ID (MAC地址)
    std::string client_id;              // 客户端ID (UUID)
    std::string server_url;             // 服务器地址
    std::string config_file_path;       // 配置文件路径
    
    // 音频配置
    int audio_sample_rate;              // 音频采样率
    int audio_channels;                 // 音频声道数
    int audio_frame_duration;           // 帧时长(ms)
    
    // 是否自动重连
    bool auto_reconnect;
    
    AIConfig()
        : server_url("wss://api.tenclass.net/xiaozhi/v1/")
        , config_file_path("./system_para.conf")
        , audio_sample_rate(48000)
        , audio_channels(1)
        , audio_frame_duration(20)
        , auto_reconnect(true) {}
};

// ============================================================================
// AI管理器状态
// ============================================================================

/**
 * @brief AI管理器状态
 */
enum class AIManagerState {
    UNINITIALIZED,      // 未初始化
    INITIALIZED,        // 已初始化
    CONNECTING,         // 连接中
    CONNECTED,          // 已连接
    ACTIVE,             // 活跃中（可以对话）
    ERROR,              // 错误状态
    SHUTDOWN            // 已关闭
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
 * @brief TTS音频回调
 */
using TTSAudioCallback = std::function<void(const uint8_t* data, size_t size)>;

/**
 * @brief 状态变化回调
 */
using StateChangedCallback = std::function<void(AIManagerState state)>;

/**
 * @brief 错误回调
 */
using ErrorOccurredCallback = std::function<void(const std::string& error)>;

// ============================================================================
// AI管理器类
// ============================================================================

/**
 * @brief xiaozhi AI主控制器
 * @details 负责整合所有AI相关模块:
 *          - WebSocket通信
 *          - 协议处理
 *          - 状态机管理
 *          - MCP工具调用
 *          - 音频系统集成
 */
class AIManager {
public:
    /**
     * @brief 构造函数
     * @param config AI配置
     */
    explicit AIManager(const AIConfig& config = AIConfig());
    
    /**
     * @brief 析构函数
     */
    ~AIManager();

    // ========================================================================
    // 初始化和关闭
    // ========================================================================

    /**
     * @brief 初始化AI管理器
     * @param audio_system 音频系统指针
     * @return true 初始化成功
     */
    bool initialize(audio_system_t* audio_system);

    /**
     * @brief 启动AI服务
     * @return true 启动成功
     */
    bool start();

    /**
     * @brief 停止AI服务
     */
    void stop();

    /**
     * @brief 关闭AI管理器
     */
    void shutdown();

    // ========================================================================
    // AI交互控制
    // ========================================================================

    /**
     * @brief 开始监听用户语音
     * @param mode 监听模式（auto/manual/realtime）
     * @return true 成功
     */
    bool startListening(const std::string& mode = "auto");

    /**
     * @brief 停止监听
     * @return true 成功
     */
    bool stopListening();

    /**
     * @brief 发送文本消息（用于测试）
     * @param text 文本内容
     * @return true 成功
     */
    bool sendTextMessage(const std::string& text);

    // ========================================================================
    // MCP设备注册
    // ========================================================================

    /**
     * @brief 注册IoT设备
     * @param descriptor 设备描述符
     * @param handler 方法处理函数
     * @param getter 状态获取函数
     * @return true 注册成功
     */
    bool registerDevice(
        const protocol::IoTDescriptor& descriptor,
        mcp::MethodHandler handler,
        mcp::StateGetter getter
    );

    /**
     * @brief 注销IoT设备
     * @param device_name 设备名称
     * @return true 注销成功
     */
    bool unregisterDevice(const std::string& device_name);

    // ========================================================================
    // 状态查询
    // ========================================================================

    /**
     * @brief 获取管理器状态
     */
    AIManagerState getState() const;

    /**
     * @brief 检查是否已连接
     */
    bool isConnected() const;

    /**
     * @brief 检查是否处于活跃状态
     */
    bool isActive() const;

    /**
     * @brief 获取当前会话ID
     */
    std::string getSessionId() const;

    // ========================================================================
    // 回调设置
    // ========================================================================

    /**
     * @brief 设置STT文本回调
     */
    void onSTTText(STTTextCallback callback);

    /**
     * @brief 设置LLM文本回调
     */
    void onLLMText(LLMTextCallback callback);

    /**
     * @brief 设置TTS音频回调
     */
    void onTTSAudio(TTSAudioCallback callback);

    /**
     * @brief 设置状态变化回调
     */
    void onStateChanged(StateChangedCallback callback);

    /**
     * @brief 设置错误回调
     */
    void onError(ErrorOccurredCallback callback);

    // 禁止拷贝和赋值
    AIManager(const AIManager&) = delete;
    AIManager& operator=(const AIManager&) = delete;

private:
    class Impl;  // Pimpl惯用法
    std::unique_ptr<Impl> pImpl_;
    
    // 允许音频回调访问pImpl_
    friend void audioDataCallback(void* data, int len, uint64_t timestamp);
};

// 音频回调函数（C函数指针兼容）
void audioDataCallback(void* data, int len, uint64_t timestamp);

} // namespace chatbot
} // namespace glasses

#endif // CHATBOT_H

