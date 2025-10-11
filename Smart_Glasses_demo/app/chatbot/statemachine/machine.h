#ifndef MACHINE_H
#define MACHINE_H

#include <functional>
#include <string>
#include <atomic>
#include <mutex>

namespace glasses {
namespace chatbot {
namespace statemachine {

/**
 * @brief AI状态枚举
 */
enum class AIState {
    IDLE,               // 空闲/待机（唤醒词检测）
    LISTENING,          // 监听中（用户正在说话）
    THINKING,           // 思考中（AI处理：STT完成 → LLM → TTS准备）
    SPEAKING,           // 说话中（AI正在说话/TTS播放）
    ERROR               // 错误状态
};

/**
 * @brief 状态变化回调函数类型
 * @param old_state 旧状态
 * @param new_state 新状态
 */
using StateChangeCallback = std::function<void(AIState old_state, AIState new_state)>;

/**
 * @brief 音频上传控制回调函数类型
 * @param enable true启用上传，false禁用上传
 */
using AudioUploadCallback = std::function<void(bool enable)>;

/**
 * @brief AI状态机类
 * @details 负责管理AI对话过程中的状态转换
 *          核心功能：
 *          1. 状态转换管理（5个状态）
 *          2. 音频上传开关控制（避免回声）
 *          3. 状态变化通知
 */
class AIStateMachine {
public:
    /**
     * @brief 构造函数
     */
    AIStateMachine();
    
    /**
     * @brief 析构函数
     */
    ~AIStateMachine();

    // ========================================================================
    // 状态转换接口
    // ========================================================================

    /**
     * @brief 设置状态（内部使用）
     * @param state 新状态
     */
    void setState(AIState state);

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

    // ========================================================================
    // 事件触发接口（协议消息驱动）
    // ========================================================================

    /**
     * @brief Hello消息接收（握手成功）
     * @details 状态转换: UNKNOWN → IDLE
     *          音频控制: 禁用AI流，启用唤醒词检测
     */
    void onHello();
    
    /**
     * @brief 唤醒词检测到（用户说了唤醒词）
     * @details 状态转换: IDLE → 准备进入 LISTENING
     *          说明: 实际状态切换在 onListenStart() 中完成
     */
    void onWakewordDetected();

    /**
     * @brief Listen开始（开始监听用户语音）
     * @details 状态转换: IDLE → LISTENING
     *          音频控制: 启用AI音频流上传
     */
    void onListenStart();

    /**
     * @brief STT消息接收（收到语音识别结果）
     * @details 状态转换: 
     *          - STT partial: LISTENING（保持）
     *          - STT final: LISTENING → THINKING
     *          音频控制: STT final后停止上传
     */
    void onSTT(const std::string& text, bool is_final);

    /**
     * @brief LLM消息接收（收到AI回复）
     * @details 状态保持: THINKING
     *          说明: LLM是流式返回，会多次调用
     */
    void onLLM(const std::string& text, bool is_final);

    /**
     * @brief TTS开始（AI开始说话）
     * @details 状态转换: THINKING → SPEAKING
     *          音频控制: 保持禁用上传
     */
    void onTTS_start();

    /**
     * @brief TTS句子开始
     * @details 状态保持: SPEAKING
     *          音频控制: 保持禁用上传
     */
    void onTTS_sentenceStart(const std::string& text);

    /**
     * @brief TTS结束（AI说完了）
     * @details 状态转换: SPEAKING → 延迟2秒 → LISTENING（继续监听）
     *          音频控制: 延迟后启用音频上传
     *          说明: 进入连续对话模式，直到WebSocket关闭
     * @param delay_ms 延迟时间（毫秒），避免回声，默认2000ms
     */
    void onTTS_stop(int delay_ms = 2000);
    
    /**
     * @brief WebSocket连接关闭（由服务器超时或网络断开触发）
     * @details 状态转换: LISTENING/THINKING/SPEAKING → IDLE
     *          音频控制: 禁用AI音频流，启用唤醒词检测
     */
    void onWebSocketClosed();

    /**
     * @brief 错误发生
     * @details 状态转换: 任意 → ERROR
     *          音频控制: 禁用所有音频处理
     */
    void onError(const std::string& error_msg);

    /**
     * @brief 重置状态机
     * @details 状态转换: 任意 → IDLE
     *          音频控制: 禁用AI音频流
     */
    void reset();

    // ========================================================================
    // 音频上传控制
    // ========================================================================

    /**
     * @brief 启用音频上传
     */
    void enableAudioUpload();

    /**
     * @brief 禁用音频上传
     */
    void disableAudioUpload();

    /**
     * @brief 检查音频上传是否启用
     * @return true 音频上传已启用
     */
    bool isAudioUploadEnabled() const;

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

    // ========================================================================
    // 工具函数
    // ========================================================================

    /**
     * @brief 将状态转换为字符串（用于日志）
     * @param state AI状态
     * @return std::string 状态名称
     */
    static std::string stateToString(AIState state);

    // 禁止拷贝和赋值
    AIStateMachine(const AIStateMachine&) = delete;
    AIStateMachine& operator=(const AIStateMachine&) = delete;

private:
    class Impl;  // 前向声明，使用Pimpl惯用法
    Impl* pimpl_;
};

} // namespace statemachine
} // namespace chatbot
} // namespace glasses

#endif // MACHINE_H

