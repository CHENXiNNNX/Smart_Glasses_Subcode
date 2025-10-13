/**
 * @file machinev2.cc
 * @brief AI状态机V2实现
 */

#include "machinev2.h"
#include "../../tool/log/log.h"
#include "../../../common/common.h"
#include <thread>
#include <condition_variable>

namespace glasses {
namespace chatbot {
namespace statemachine {

using namespace tool::logger;

// ============================================================================
// AIStateMachineV2::Impl 内部实现（Pimpl惯用法）
// ============================================================================

class AIStateMachineV2::Impl {
public:
    // 配置
    StateMachineConfig config;
    
    // 状态
    std::atomic<AIState> current_state{AIState::IDLE};
    std::atomic<bool> audio_upload_enabled{false};
    uint64_t state_enter_time{0};  // 进入当前状态的时间戳
    
    // 回调函数
    StateChangeCallback state_change_callback;
    AudioUploadCallback audio_upload_callback;
    ErrorCallback error_callback;
    TimeoutCallback timeout_callback;
    
    // 状态历史（环形缓冲区）
    std::deque<StateTransition> state_history;
    mutable std::mutex history_mutex;
    
    // 延迟任务管理（可中断）
    std::unique_ptr<std::thread> delay_thread;
    std::atomic<bool> should_cancel_delay{false};
    std::condition_variable delay_cv;
    std::mutex delay_mutex;
    
    // 超时监控线程
    std::unique_ptr<std::thread> watchdog_thread;
    std::atomic<bool> should_stop_watchdog{false};
    
    // 统计信息
    Stats stats;
    
    // 线程安全（回调保护）
    mutable std::mutex callback_mutex;
    
    // 状态转换验证表
    static const std::unordered_map<AIState, std::unordered_set<AIState>> valid_transitions;
    
    explicit Impl(const StateMachineConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("StateMachineV2", "Impl created");
        state_enter_time = get_nowus();
    }
    
    ~Impl() {
        LOG_DEBUG("StateMachineV2", "Impl destroying...");
        
        // 停止所有线程
        stopDelayThread();
        stopWatchdogThread();
        
        LOG_DEBUG("StateMachineV2", "Impl destroyed");
    }
    
    // ========================================================================
    // 状态转换核心逻辑
    // ========================================================================
    
    bool changeState(AIState new_state, const std::string& event, const std::string& detail = "") {
        AIState old_state = current_state.load(std::memory_order_acquire);
        
        // 1. 检查状态是否改变
        if (old_state == new_state) {
            return true;  // 状态未改变
        }
        
        // 2. 验证转换合法性
        if (config.enable_transition_validation) {
            if (!isTransitionValid(old_state, new_state)) {
                LOG_ERROR("StateMachineV2", "Invalid transition: %s → %s (event: %s)",
                         AIStateMachineV2::stateToString(old_state).c_str(),
                         AIStateMachineV2::stateToString(new_state).c_str(),
                         event.c_str());
                
                stats.invalid_transitions.fetch_add(1, std::memory_order_relaxed);
                
                // 触发错误回调
                invokeErrorCallback(StateMachineError::INVALID_TRANSITION,
                                   "Invalid state transition");
                return false;
            }
        }
        
        // 3. 更新状态时间统计
        uint64_t now = get_nowus();
        uint64_t time_spent = now - state_enter_time;
        updateStateTimeStats(old_state, time_spent);
        
        // 4. 执行状态切换
        current_state.store(new_state, std::memory_order_release);
        state_enter_time = now;
        stats.total_transitions.fetch_add(1, std::memory_order_relaxed);
        
        LOG_INFO("StateMachineV2", "State: %s → %s (event: %s)",
                 AIStateMachineV2::stateToString(old_state).c_str(),
                 AIStateMachineV2::stateToString(new_state).c_str(),
                 event.c_str());
        
        // 5. 记录状态历史
        if (config.enable_history_tracking) {
            recordTransition(old_state, new_state, now, event, detail);
        }
        
        // 6. 触发状态变化回调（异常安全）
        invokeStateChangeCallback(old_state, new_state);
        
        return true;
    }
    
    void setAudioUpload(bool enable) {
        bool old_value = audio_upload_enabled.exchange(enable, std::memory_order_acq_rel);
        
        if (old_value != enable) {
            LOG_INFO("StateMachineV2", "Audio upload: %s", enable ? "ENABLED" : "DISABLED");
            
            // 触发音频上传控制回调（异常安全）
            invokeAudioUploadCallback(enable);
        }
    }
    
    // ========================================================================
    // 状态转换验证
    // ========================================================================
    
    bool isTransitionValid(AIState from, AIState to) const {
        // ERROR状态可以转到任何状态（恢复机制）
        if (from == AIState::ERROR) {
            return true;
        }
        
        // 查找转换表
        auto it = valid_transitions.find(from);
        if (it == valid_transitions.end()) {
            return false;
        }
        
        return it->second.count(to) > 0;
    }
    
    // ========================================================================
    // 状态历史管理
    // ========================================================================
    
    void recordTransition(AIState from, AIState to, uint64_t timestamp,
                         const std::string& event, const std::string& detail) {
        std::lock_guard<std::mutex> lock(history_mutex);
        
        state_history.emplace_back(from, to, timestamp, event, detail);
        
        // 限制历史大小（环形缓冲）
        if (state_history.size() > config.max_state_history) {
            state_history.pop_front();
        }
    }
    
    std::vector<StateTransition> getHistory() const {
        std::lock_guard<std::mutex> lock(history_mutex);
        return {state_history.begin(), state_history.end()};
    }
    
    StateTransition getLastTransition() const {
        std::lock_guard<std::mutex> lock(history_mutex);
        if (state_history.empty()) {
            return StateTransition();
        }
        return state_history.back();
    }
    
    void clearHistory() {
        std::lock_guard<std::mutex> lock(history_mutex);
        state_history.clear();
    }
    
    // ========================================================================
    // 延迟任务管理（安全的线程管理）
    // ========================================================================
    
    void scheduleDelayedStateChange(AIState target_state, int delay_ms,
                                    const std::string& event) {
        // 取消旧的延迟任务
        cancelDelayedTask();
        
        should_cancel_delay.store(false, std::memory_order_release);
        
        // 创建新的延迟线程（使用智能指针）
        delay_thread = std::make_unique<std::thread>([this, target_state, delay_ms, event]() {
            std::unique_lock<std::mutex> lock(delay_mutex);
            
            LOG_DEBUG("StateMachineV2", "Delay task started: %dms → %s",
                     delay_ms, AIStateMachineV2::stateToString(target_state).c_str());
            
            // ✅ 可中断的等待
            bool cancelled = delay_cv.wait_for(lock, std::chrono::milliseconds(delay_ms),
                [this]() { return should_cancel_delay.load(std::memory_order_acquire); });
            
            if (cancelled) {
                LOG_DEBUG("StateMachineV2", "Delay task cancelled");
                return;
            }
            
            // ✅ 再次检查取消标志（双重保险）
            if (should_cancel_delay.load(std::memory_order_acquire)) {
                LOG_DEBUG("StateMachineV2", "Delay task cancelled (double check)");
                return;
            }
            
            LOG_DEBUG("StateMachineV2", "Delay task executing state change");
            changeState(target_state, event);
        });
    }
    
    void cancelDelayedTask() {
        should_cancel_delay.store(true, std::memory_order_release);
        delay_cv.notify_all();
        
        if (delay_thread && delay_thread->joinable()) {
            delay_thread->join();
        }
        delay_thread.reset();
    }
    
    void stopDelayThread() {
        cancelDelayedTask();
    }
    
    // ========================================================================
    // 超时监控线程
    // ========================================================================
    
    void startWatchdogThread() {
        if (!config.enable_state_timeout) {
            return;
        }
        
        should_stop_watchdog.store(false, std::memory_order_release);
        
        watchdog_thread = std::make_unique<std::thread>([this]() {
            LOG_DEBUG("StateMachineV2", "Watchdog thread started");
            
            while (!should_stop_watchdog.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                checkStateTimeout();
            }
            
            LOG_DEBUG("StateMachineV2", "Watchdog thread stopped");
        });
    }
    
    void stopWatchdogThread() {
        should_stop_watchdog.store(true, std::memory_order_release);
        
        if (watchdog_thread && watchdog_thread->joinable()) {
            watchdog_thread->join();
        }
        watchdog_thread.reset();
    }
    
    void checkStateTimeout() {
        AIState current = current_state.load(std::memory_order_acquire);
        uint64_t now = get_nowus();
        uint64_t elapsed_ms = (now - state_enter_time) / 1000;
        
        int timeout_ms = 0;
        bool is_timeout = false;
        
        switch (current) {
            case AIState::THINKING:
                timeout_ms = config.thinking_timeout_ms;
                is_timeout = (elapsed_ms > timeout_ms);
                break;
                
            case AIState::SPEAKING:
                timeout_ms = config.speaking_timeout_ms;
                is_timeout = (elapsed_ms > timeout_ms);
                break;
                
            case AIState::LISTENING:
                timeout_ms = config.listening_timeout_ms;
                is_timeout = (elapsed_ms > timeout_ms);
                break;
                
            default:
                break;
        }
        
        if (is_timeout) {
            LOG_WARN("StateMachineV2", "State timeout: %s (elapsed: %llu ms, timeout: %d ms)",
                    AIStateMachineV2::stateToString(current).c_str(),
                    elapsed_ms, timeout_ms);
            
            stats.state_timeouts.fetch_add(1, std::memory_order_relaxed);
            
            // 触发超时回调
            invokeTimeoutCallback(current, timeout_ms);
            
            // 超时后转到ERROR状态
            changeState(AIState::ERROR, "timeout", 
                       "State " + AIStateMachineV2::stateToString(current) + " timeout");
        }
    }
    
    // ========================================================================
    // 回调调用（异常安全）
    // ========================================================================
    
    void invokeStateChangeCallback(AIState old_state, AIState new_state) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (state_change_callback) {
            try {
                state_change_callback(old_state, new_state);
            } catch (const std::runtime_error& e) {
                LOG_ERROR("StateMachineV2", "State callback runtime_error: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::logic_error& e) {
                LOG_ERROR("StateMachineV2", "State callback logic_error: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                LOG_ERROR("StateMachineV2", "State callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
            // 不捕获系统异常（bad_alloc等），让它们正确传播
        }
    }
    
    void invokeAudioUploadCallback(bool enable) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (audio_upload_callback) {
            try {
                audio_upload_callback(enable);
            } catch (const std::exception& e) {
                LOG_ERROR("StateMachineV2", "Audio callback exception: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    void invokeErrorCallback(StateMachineError error, const std::string& message) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (error_callback) {
            try {
                error_callback(error, message);
            } catch (const std::exception& e) {
                LOG_ERROR("StateMachineV2", "Error callback exception: %s", e.what());
            }
        }
    }
    
    void invokeTimeoutCallback(AIState state, int timeout_ms) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (timeout_callback) {
            try {
                timeout_callback(state, timeout_ms);
            } catch (const std::exception& e) {
                LOG_ERROR("StateMachineV2", "Timeout callback exception: %s", e.what());
            }
        }
    }
    
    // ========================================================================
    // 统计信息更新
    // ========================================================================
    
    void updateStateTimeStats(AIState state, uint64_t time_us) {
        switch (state) {
            case AIState::IDLE:
                stats.time_in_idle.fetch_add(time_us, std::memory_order_relaxed);
                break;
            case AIState::LISTENING:
                stats.time_in_listening.fetch_add(time_us, std::memory_order_relaxed);
                break;
            case AIState::THINKING:
                stats.time_in_thinking.fetch_add(time_us, std::memory_order_relaxed);
                break;
            case AIState::SPEAKING:
                stats.time_in_speaking.fetch_add(time_us, std::memory_order_relaxed);
                break;
            case AIState::ERROR:
                stats.time_in_error.fetch_add(time_us, std::memory_order_relaxed);
                break;
        }
    }
};

// ============================================================================
// 状态转换验证表（静态初始化）
// ============================================================================

const std::unordered_map<AIState, std::unordered_set<AIState>> 
    AIStateMachineV2::Impl::valid_transitions = {
    // IDLE可以转到LISTENING（唤醒词触发）或ERROR
    {AIState::IDLE, 
        {AIState::LISTENING, AIState::ERROR}},
    
    // LISTENING可以转到THINKING（STT完成）、IDLE（取消）或ERROR
    {AIState::LISTENING, 
        {AIState::THINKING, AIState::IDLE, AIState::ERROR}},
    
    // THINKING可以转到SPEAKING（TTS开始）、IDLE（取消）或ERROR
    {AIState::THINKING, 
        {AIState::SPEAKING, AIState::IDLE, AIState::ERROR}},
    
    // SPEAKING可以转到LISTENING（连续对话）、IDLE（会话结束）或ERROR
    {AIState::SPEAKING, 
        {AIState::LISTENING, AIState::IDLE, AIState::ERROR}},
    
    // ERROR可以转到任何状态（恢复机制）
    {AIState::ERROR, 
        {AIState::IDLE, AIState::LISTENING, AIState::THINKING, 
         AIState::SPEAKING, AIState::ERROR}}
};

// ============================================================================
// AIStateMachineV2 公共接口实现
// ============================================================================

AIStateMachineV2::AIStateMachineV2(const StateMachineConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_INFO("StateMachineV2", "AI State Machine V2 created");
    
    // 启动超时监控线程
    if (pImpl_->config.enable_state_timeout) {
        pImpl_->startWatchdogThread();
    }
}

AIStateMachineV2::~AIStateMachineV2() {
    LOG_INFO("StateMachineV2", "AI State Machine V2 destroying...");
    
    // 输出最终统计
    logStats();
    
    // RAII自动清理（pImpl_的析构函数会清理所有资源）
    LOG_INFO("StateMachineV2", "AI State Machine V2 destroyed");
}

// ========================================================================
// 状态查询
// ========================================================================

AIState AIStateMachineV2::getState() const {
    return pImpl_->current_state.load(std::memory_order_acquire);
}

bool AIStateMachineV2::isState(AIState state) const {
    return getState() == state;
}

bool AIStateMachineV2::isAudioUploadEnabled() const {
    return pImpl_->audio_upload_enabled.load(std::memory_order_acquire);
}

// ========================================================================
// 事件触发接口
// ========================================================================

void AIStateMachineV2::onHello() {
    LOG_INFO("StateMachineV2", "Event: Hello received");
    
    // Hello握手成功 → 进入IDLE状态（等待唤醒词）
    pImpl_->changeState(AIState::IDLE, "onHello");
    
    // IDLE状态下禁用音频上传（仅本地唤醒词检测）
    pImpl_->setAudioUpload(false);
}

void AIStateMachineV2::onWakewordDetected() {
    LOG_INFO("StateMachineV2", "Event: Wakeword detected!");
    
    // 记录唤醒词事件（实际状态切换在onListenStart()中）
    AIState current = getState();
    if (current != AIState::IDLE) {
        LOG_WARN("StateMachineV2", "Wakeword detected but not in IDLE state: %s",
                stateToString(current).c_str());
    }
}

void AIStateMachineV2::onListenStart() {
    LOG_INFO("StateMachineV2", "Event: Listen start");
    
    // 开始监听 → 进入LISTENING状态
    if (pImpl_->changeState(AIState::LISTENING, "onListenStart")) {
        // 启用音频上传
        pImpl_->setAudioUpload(true);
    }
}

void AIStateMachineV2::onSTT(const std::string& text, bool is_final) {
    LOG_DEBUG("StateMachineV2", "Event: STT - \"%s\" (final: %s)",
             text.c_str(), is_final ? "true" : "false");
    
    if (is_final) {
        // 收到最终识别结果 → AI开始思考
        if (pImpl_->changeState(AIState::THINKING, "onSTT", "text: " + text)) {
            // 停止音频上传（用户已说完）
            pImpl_->setAudioUpload(false);
        }
    }
    // 否则保持LISTENING状态，继续上传音频
}

void AIStateMachineV2::onLLM(const std::string& text, bool is_final) {
    LOG_DEBUG("StateMachineV2", "Event: LLM - \"%s\" (final: %s)",
             text.c_str(), is_final ? "true" : "false");
    
    // LLM回复期间，状态保持THINKING
    // LLM是流式返回，会多次调用此函数
}

void AIStateMachineV2::onTTS_start() {
    LOG_INFO("StateMachineV2", "Event: TTS start");
    
    // TTS开始 → 进入SPEAKING状态
    pImpl_->changeState(AIState::SPEAKING, "onTTS_start");
    
    // 音频上传已在THINKING时禁用，保持禁用
}

void AIStateMachineV2::onTTS_sentenceStart(const std::string& text) {
    LOG_DEBUG("StateMachineV2", "Event: TTS sentence start - \"%s\"", text.c_str());
    
    // 句子开始 → 状态保持SPEAKING
    // 音频上传保持禁用
}

void AIStateMachineV2::onTTS_stop(int delay_ms) {
    // 使用配置值（如果传入-1）
    if (delay_ms < 0) {
        delay_ms = pImpl_->config.tts_finish_delay_ms;
    }
    
    LOG_INFO("StateMachineV2", "Event: TTS stop (delay: %dms)", delay_ms);
    
    // ✅ 安全的延迟状态切换
    pImpl_->scheduleDelayedStateChange(AIState::LISTENING, delay_ms, "onTTS_stop_delayed");
    
    LOG_DEBUG("StateMachineV2", "Scheduled return to LISTENING after %dms (continuous conversation)", 
             delay_ms);
}

void AIStateMachineV2::onWebSocketClosed() {
    LOG_INFO("StateMachineV2", "Event: WebSocket closed");
    
    AIState current = getState();
    
    // 取消所有延迟任务
    pImpl_->cancelDelayedTask();
    
    // 如果在对话中，回到IDLE
    if (current == AIState::LISTENING || 
        current == AIState::THINKING || 
        current == AIState::SPEAKING) {
        
        LOG_INFO("StateMachineV2", "Session ended, returning to IDLE...");
        
        if (pImpl_->changeState(AIState::IDLE, "onWebSocketClosed")) {
            pImpl_->setAudioUpload(false);
        }
    }
}

void AIStateMachineV2::onError(const std::string& error_msg) {
    LOG_ERROR("StateMachineV2", "Event: Error - %s", error_msg.c_str());
    
    // 取消所有延迟任务
    pImpl_->cancelDelayedTask();
    
    // 错误 → 进入ERROR状态
    if (pImpl_->changeState(AIState::ERROR, "onError", error_msg)) {
        // 禁用音频上传
        pImpl_->setAudioUpload(false);
    }
}

void AIStateMachineV2::reset() {
    LOG_INFO("StateMachineV2", "Reset to IDLE");
    
    // 取消所有延迟任务
    pImpl_->cancelDelayedTask();
    
    // 重置到IDLE状态
    if (pImpl_->changeState(AIState::IDLE, "reset")) {
        // 禁用音频上传
        pImpl_->setAudioUpload(false);
    }
}

// ========================================================================
// 回调设置
// ========================================================================

void AIStateMachineV2::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->state_change_callback = callback;
}

void AIStateMachineV2::setAudioUploadCallback(AudioUploadCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->audio_upload_callback = callback;
}

void AIStateMachineV2::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->error_callback = callback;
}

void AIStateMachineV2::setTimeoutCallback(TimeoutCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->timeout_callback = callback;
}

// ========================================================================
// 状态历史和调试
// ========================================================================

std::vector<StateTransition> AIStateMachineV2::getStateHistory() const {
    return pImpl_->getHistory();
}

void AIStateMachineV2::clearHistory() {
    pImpl_->clearHistory();
}

StateTransition AIStateMachineV2::getLastTransition() const {
    return pImpl_->getLastTransition();
}

uint64_t AIStateMachineV2::getTimeInCurrentState() const {
    uint64_t now = get_nowus();
    return (now - pImpl_->state_enter_time) / 1000;  // 返回毫秒
}

// ========================================================================
// 统计信息
// ========================================================================

void AIStateMachineV2::getStats(Stats& out_stats) const {
    out_stats.total_transitions.store(pImpl_->stats.total_transitions.load());
    out_stats.invalid_transitions.store(pImpl_->stats.invalid_transitions.load());
    out_stats.callback_exceptions.store(pImpl_->stats.callback_exceptions.load());
    out_stats.state_timeouts.store(pImpl_->stats.state_timeouts.load());
    out_stats.time_in_idle.store(pImpl_->stats.time_in_idle.load());
    out_stats.time_in_listening.store(pImpl_->stats.time_in_listening.load());
    out_stats.time_in_thinking.store(pImpl_->stats.time_in_thinking.load());
    out_stats.time_in_speaking.store(pImpl_->stats.time_in_speaking.load());
    out_stats.time_in_error.store(pImpl_->stats.time_in_error.load());
}

void AIStateMachineV2::resetStats() {
    pImpl_->stats.total_transitions.store(0);
    pImpl_->stats.invalid_transitions.store(0);
    pImpl_->stats.callback_exceptions.store(0);
    pImpl_->stats.state_timeouts.store(0);
    pImpl_->stats.time_in_idle.store(0);
    pImpl_->stats.time_in_listening.store(0);
    pImpl_->stats.time_in_thinking.store(0);
    pImpl_->stats.time_in_speaking.store(0);
    pImpl_->stats.time_in_error.store(0);
    
    LOG_INFO("StateMachineV2", "Stats reset");
}

void AIStateMachineV2::logStats() const {
    uint64_t total = pImpl_->stats.total_transitions.load();
    uint64_t invalid = pImpl_->stats.invalid_transitions.load();
    uint64_t exceptions = pImpl_->stats.callback_exceptions.load();
    uint64_t timeouts = pImpl_->stats.state_timeouts.load();
    
    LOG_INFO("StateMachineV2", "=== State Machine V2 Statistics ===");
    LOG_INFO("StateMachineV2", "  Total transitions:   %llu", total);
    LOG_INFO("StateMachineV2", "  Invalid transitions: %llu", invalid);
    LOG_INFO("StateMachineV2", "  Callback exceptions: %llu", exceptions);
    LOG_INFO("StateMachineV2", "  State timeouts:      %llu", timeouts);
    
    // 各状态停留时间（转换为秒）
    LOG_INFO("StateMachineV2", "Time spent in each state:");
    LOG_INFO("StateMachineV2", "  IDLE:      %.2f s", 
             pImpl_->stats.time_in_idle.load() / 1000000.0);
    LOG_INFO("StateMachineV2", "  LISTENING: %.2f s", 
             pImpl_->stats.time_in_listening.load() / 1000000.0);
    LOG_INFO("StateMachineV2", "  THINKING:  %.2f s", 
             pImpl_->stats.time_in_thinking.load() / 1000000.0);
    LOG_INFO("StateMachineV2", "  SPEAKING:  %.2f s", 
             pImpl_->stats.time_in_speaking.load() / 1000000.0);
    LOG_INFO("StateMachineV2", "  ERROR:     %.2f s", 
             pImpl_->stats.time_in_error.load() / 1000000.0);
    
    // 健康度评分
    if (total > 0) {
        double error_rate = (double)invalid / total * 100.0;
        double exception_rate = (double)exceptions / total * 100.0;
        
        if (error_rate > 1.0) {
            LOG_WARN("StateMachineV2", "Invalid transition rate: %.2f%% (consider reviewing state logic)", 
                    error_rate);
        }
        
        if (exception_rate > 0.1) {
            LOG_WARN("StateMachineV2", "Callback exception rate: %.2f%% (review callback implementations)", 
                    exception_rate);
        }
    }
}

// ========================================================================
// 工具函数
// ========================================================================

std::string AIStateMachineV2::stateToString(AIState state) {
    switch (state) {
        case AIState::IDLE:         return "IDLE";
        case AIState::LISTENING:    return "LISTENING";
        case AIState::THINKING:     return "THINKING";
        case AIState::SPEAKING:     return "SPEAKING";
        case AIState::ERROR:        return "ERROR";
        default:                    return "INVALID";
    }
}

AIState AIStateMachineV2::stringToState(const std::string& str) {
    if (str == "IDLE") return AIState::IDLE;
    if (str == "LISTENING") return AIState::LISTENING;
    if (str == "THINKING") return AIState::THINKING;
    if (str == "SPEAKING") return AIState::SPEAKING;
    if (str == "ERROR") return AIState::ERROR;
    return AIState::IDLE;  // 默认返回IDLE
}

} // namespace statemachine
} // namespace chatbot
} // namespace glasses

