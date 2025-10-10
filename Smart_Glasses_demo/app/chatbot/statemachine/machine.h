/**
 * @file machine.h
 * @brief AI状态机模块
 * @details 管理xiaozhi AI的状态转换和音频上传控制
 *          状态流转: idle → listening → thinking → speaking → idle
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-10
 */

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
    UNKNOWN,            // 未知状态
    STARTING,           // 启动中
    IDLE,               // 空闲（已连接但未激活）
    LISTENING,          // 监听中（正在接收用户语音）
    THINKING,           // 思考中（AI处理中）
    SPEAKING,           // 说话中（TTS播放中）
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
 *          1. 状态转换管理
 *          2. 音频上传开关控制（TTS时停止上传，避免回声）
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
     * @details 状态转换: STARTING → IDLE
     */
    void onHello();

    /**
     * @brief Listen开始（开始监听用户语音）
     * @details 状态转换: IDLE/SPEAKING → LISTENING
     *          开启音频上传
     */
    void onListenStart();

    /**
     * @brief STT消息接收（收到语音识别结果）
     * @details 状态转换: LISTENING → THINKING
     */
    void onSTT(const std::string& text, bool is_final);

    /**
     * @brief LLM消息接收（收到AI回复）
     * @details 状态保持: THINKING
     */
    void onLLM(const std::string& text, bool is_final);

    /**
     * @brief TTS开始（AI开始说话）
     * @details 状态转换: THINKING → SPEAKING
     *          关闭音频上传（避免AI听到自己的声音）
     */
    void onTTS_start();

    /**
     * @brief TTS句子开始
     * @details 状态保持: SPEAKING
     */
    void onTTS_sentenceStart(const std::string& text);

    /**
     * @brief TTS结束（AI说完了）
     * @details 状态转换: SPEAKING → LISTENING
     *          等待指定时间后恢复音频上传
     * @param delay_ms 延迟时间（毫秒），避免回声
     */
    void onTTS_stop(int delay_ms = 2000);

    /**
     * @brief 错误发生
     * @details 状态转换: 任意 → ERROR
     */
    void onError(const std::string& error_msg);

    /**
     * @brief 重置状态机
     * @details 状态转换: 任意 → IDLE
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

