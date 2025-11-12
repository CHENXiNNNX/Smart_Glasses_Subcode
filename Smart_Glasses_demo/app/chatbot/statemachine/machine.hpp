#ifndef MACHINE_HPP
#define MACHINE_HPP

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

namespace app {
namespace chatbot {
namespace statemachine {

// ============================================================================
// 前向声明
// ============================================================================
class AIStateMachine;

// ============================================================================
// AI状态枚举
// ============================================================================

/**
 * @brief AI对话状态
 * @note STT完成后保持LISTENING，直到TTS开始才切换到SPEAKING
 */
enum class AIState {
    IDLE = 0,           // 空闲/待机（唤醒词检测）
    CONNECTING,         // 连接中（打开音频通道）
    LISTENING,          // 监听中（用户说话 + STT处理 + LLM处理）
    SPEAKING,           // 说话中（AI正在说话/TTS播放）
    ERROR               // 错误状态
};

/**
 * @brief 状态机错误类型
 */
enum class StateMachineError {
    NONE = 0,
    INVALID_TRANSITION,     // 非法状态转换
    CALLBACK_EXCEPTION,     // 回调函数异常
    TIMEOUT,                // 状态超时
    INVALID_STATE,          // 无效状态
    THREAD_ERROR,           // 线程错误
    CONNECTION_FAILED       // 连接失败
};

// ============================================================================
// 状态转换历史记录
// ============================================================================

/**
 * @brief 状态转换记录
 */
struct StateTransition {
    AIState from_state;         // 源状态
    AIState to_state;           // 目标状态
    uint64_t timestamp;         // 时间戳（微秒）
    std::string trigger_event;  // 触发事件（如"onSTT", "onTTS_stop"）
    std::string detail;         // 额外信息（如STT文本）
    
    StateTransition()
        : from_state(AIState::IDLE)
        , to_state(AIState::IDLE)
        , timestamp(0) {}
    
    StateTransition(AIState from, AIState to, uint64_t ts, 
                   const std::string& event, const std::string& info = "")
        : from_state(from)
        , to_state(to)
        , timestamp(ts)
        , trigger_event(event)
        , detail(info) {}
};

// ============================================================================
// 状态机配置
// ============================================================================

/**
 * @brief 状态机配置
 */
struct StateMachineConfig {
    // 超时配置
    int connecting_timeout_ms = 10000;      // CONNECTING状态超时（10秒）
    int listening_timeout_ms = 300000;      // LISTENING状态超时（5分钟）
    int speaking_timeout_ms = 60000;        // SPEAKING状态超时（60秒）
    
    // 延迟配置
    int tts_finish_delay_ms = 2000;         // TTS结束延迟（2秒，避免回声）
    
    // 历史记录配置
    size_t max_state_history = 100;         // 最大历史记录数
    
    // 功能开关
    bool enable_transition_validation = true;   // 启用状态转换验证
    bool enable_state_timeout = true;           // 启用状态超时检查
    bool enable_history_tracking = true;        // 启用历史追踪
};

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * @brief 状态变化回调
 */
using StateChangeCallback = std::function<void(AIState old_state, AIState new_state)>;

/**
 * @brief 音频上传控制回调
 */
using AudioUploadCallback = std::function<void(bool enable)>;

/**
 * @brief 错误回调
 */
using ErrorCallback = std::function<void(StateMachineError error, const std::string& message)>;

/**
 * @brief 状态超时回调
 */
using TimeoutCallback = std::function<void(AIState state, int timeout_ms)>;

// ============================================================================
// AI状态机类
// ============================================================================

/**
 * @brief AI状态机
 * 
 * 状态流转：
 *   IDLE (唤醒词检测)
 *     ↓ onWakewordDetected
 *   CONNECTING (打开音频通道)
 *     ↓ onConnectionSuccess
 *   LISTENING (用户说话 + STT + LLM)
 *     ↓ onTTS_start
 *   SPEAKING (AI说话/TTS播放)
 *     ↓ onTTS_stop + 延迟
 *   LISTENING (连续对话) 或 IDLE (会话结束)
 * 
 * 音频上传控制：
 *   - IDLE:       禁用（仅本地唤醒词检测）
 *   - CONNECTING: 禁用
 *   - LISTENING:  启用（上传给服务器进行STT）
 *   - SPEAKING:   禁用（避免回声）
 *   - ERROR:      禁用
 */
class AIStateMachine {
public:
    /**
     * @brief 构造函数
     * @param config 状态机配置
     */
    explicit AIStateMachine(const StateMachineConfig& config = StateMachineConfig());
    
    /**
     * @brief 析构函数（RAII自动清理所有资源）
     */
    ~AIStateMachine();
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取当前状态
     * @return AIState 当前状态
     */
    AIState getState() const;
    
    /**
     * @brief 检查是否处于某个状态
     * @param state 要检查的状态
     * @return true 当前处于该状态
     */
    bool isState(AIState state) const;
    
    /**
     * @brief 获取当前音频上传是否启用
     * @return true 音频上传已启用
     */
    bool isAudioUploadEnabled() const;
    
    // ========================================================================
    // 事件触发接口（协议消息驱动）
    // ========================================================================
    
    /**
     * @brief Hello消息接收（握手成功）
     * @details 状态转换: 任意 → IDLE
     *          音频控制: 禁用上传（仅唤醒词检测）
     */
    void onHello();
    
    /**
     * @brief 唤醒词检测到（开始连接音频通道）
     * @details 状态转换: IDLE → CONNECTING
     *          音频控制: 保持禁用
     */
    void onWakewordDetected();
    
    /**
     * @brief 音频通道连接成功
     * @details 状态转换: CONNECTING → LISTENING
     *          音频控制: 启用音频上传
     */
    void onConnectionSuccess();
    
    /**
     * @brief 音频通道连接失败
     * @details 状态转换: CONNECTING → IDLE/ERROR
     *          音频控制: 禁用上传
     * @param reason 失败原因
     */
    void onConnectionFailed(const std::string& reason);
    
    /**
     * @brief STT消息接收
     * @details 状态保持: LISTENING
     *          说明: STT partial/final都保持在LISTENING状态
     * @param text 识别文本
     * @param is_final 是否为最终结果
     */
    void onSTT(const std::string& text, bool is_final);
    
    /**
     * @brief LLM消息接收
     * @details 状态保持: LISTENING
     *          说明: LLM处理期间仍然保持LISTENING状态
     * @param text LLM文本
     * @param is_final 是否为最终结果
     */
    void onLLM(const std::string& text, bool is_final);
    
    /**
     * @brief TTS开始
     * @details 状态转换: LISTENING → SPEAKING
     *          音频控制: 禁用上传（避免回声）
     */
    void onTTS_start();
    
    /**
     * @brief TTS句子开始
     * @details 状态保持: SPEAKING
     * @param text TTS句子文本
     */
    void onTTS_sentenceStart(const std::string& text);
    
    /**
     * @brief TTS结束
     * @details 状态转换: SPEAKING → 延迟后 → LISTENING（连续对话）
     *          音频控制: 延迟后启用上传
     * @param delay_ms 延迟时间（避免回声，-1使用配置值）
     */
    void onTTS_stop(int delay_ms = -1);
    
    /**
     * @brief 停止监听（手动停止）
     * @details 状态转换: LISTENING → IDLE
     *          音频控制: 禁用上传
     */
    void onStopListening();
    
    /**
     * @brief WebSocket连接关闭
     * @details 状态转换: 任意 → IDLE
     *          音频控制: 禁用上传
     */
    void onWebSocketClosed();
    
    /**
     * @brief 错误发生
     * @details 状态转换: 任意 → ERROR
     *          音频控制: 禁用上传
     * @param error_msg 错误消息
     */
    void onError(const std::string& error_msg);
    
    /**
     * @brief 重置状态机
     * @details 状态转换: 任意 → IDLE
     *          音频控制: 禁用上传
     */
    void reset();
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /**
     * @brief 设置状态变化回调
     * @param callback 回调函数
     */
    void setStateChangeCallback(StateChangeCallback callback);
    
    /**
     * @brief 设置音频上传控制回调
     * @param callback 回调函数
     */
    void setAudioUploadCallback(AudioUploadCallback callback);
    
    /**
     * @brief 设置错误回调
     * @param callback 回调函数
     */
    void setErrorCallback(ErrorCallback callback);
    
    /**
     * @brief 设置超时回调
     * @param callback 回调函数
     */
    void setTimeoutCallback(TimeoutCallback callback);
    
    // ========================================================================
    // 状态历史和调试
    // ========================================================================
    
    /**
     * @brief 获取状态历史记录
     * @return 状态转换历史列表
     */
    std::vector<StateTransition> getStateHistory() const;
    
    /**
     * @brief 清空状态历史
     */
    void clearHistory();
    
    /**
     * @brief 获取最近一次状态转换
     * @return 最近的转换记录
     */
    StateTransition getLastTransition() const;
    
    /**
     * @brief 获取在当前状态停留的时间（毫秒）
     * @return 停留时间
     */
    uint64_t getTimeInCurrentState() const;
    
    // ========================================================================
    // 统计信息
    // ========================================================================
    
    /**
     * @brief 状态机统计信息
     */
    struct Stats {
        std::atomic<uint64_t> total_transitions{0};     // 总转换次数
        std::atomic<uint64_t> invalid_transitions{0};   // 非法转换次数
        std::atomic<uint64_t> callback_exceptions{0};   // 回调异常次数
        std::atomic<uint64_t> state_timeouts{0};        // 状态超时次数
        std::atomic<uint64_t> connection_failures{0};   // 连接失败次数
        
        // 各状态停留时间（累计，微秒）
        std::atomic<uint64_t> time_in_idle{0};
        std::atomic<uint64_t> time_in_connecting{0};
        std::atomic<uint64_t> time_in_listening{0};
        std::atomic<uint64_t> time_in_speaking{0};
        std::atomic<uint64_t> time_in_error{0};
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
    
    // ========================================================================
    // 工具函数
    // ========================================================================
    
    /**
     * @brief 状态转字符串
     * @param state AI状态
     * @return 状态名称
     */
    static std::string stateToString(AIState state);
    
    /**
     * @brief 字符串转状态
     * @param str 状态字符串
     * @return AI状态
     */
    static AIState stringToState(const std::string& str);
    
    // 禁止拷贝和赋值
    AIStateMachine(const AIStateMachine&) = delete;
    AIStateMachine& operator=(const AIStateMachine&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;  // Pimpl模式 + 智能指针管理
};

} // namespace statemachine
} // namespace chatbot
} // namespace app

#endif // MACHINE_HPP

