/**
 * @file machine.cc
 * @brief AI状态机实现
 */

#include "machine.hpp"
#include "../../tool/log/log.hpp"
#include "../../../common/common.hpp"
#include <thread>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace app {
namespace chatbot {
namespace statemachine {

using namespace tool::log;

// ============================================================================
// AIStateMachine::Impl 内部实现（Pimpl惯用法）
// ============================================================================

class AIStateMachine::Impl {
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
        LOG_DEBUG("StateMachine", "Impl created");
        state_enter_time = get_nowus();
    }
    
    ~Impl() {
        LOG_DEBUG("StateMachine", "Impl destroying...");
        
        // 停止所有线程
        stopDelayThread();
        stopWatchdogThread();
        
        LOG_DEBUG("StateMachine", "Impl destroyed");
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
                LOG_ERROR("StateMachine", "Invalid transition: %s → %s (event: %s)",
                         AIStateMachine::stateToString(old_state).c_str(),
                         AIStateMachine::stateToString(new_state).c_str(),
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
        
        LOG_INFO("StateMachine", "State: %s → %s (event: %s)",
                 AIStateMachine::stateToString(old_state).c_str(),
                 AIStateMachine::stateToString(new_state).c_str(),
                 event.c_str());
        
        // 5. 记录状态历史
        if (config.enable_history_tracking) {
            recordTransition(old_state, new_state, now, event, detail);
        }
        
        // 6. 根据新状态处理音频上传
        switch (new_state) {
            case AIState::LISTENING:
                // LISTENING状态：启用音频上传（包含STT+LLM阶段）
                setAudioUpload(true);
                break;
            case AIState::IDLE:
            case AIState::CONNECTING:
            case AIState::SPEAKING:
            case AIState::ERROR:
                // 其他状态：禁用音频上传
                setAudioUpload(false);
                break;
        }
        
        // 7. 触发状态变化回调（异常安全）
        invokeStateChangeCallback(old_state, new_state);
        
        return true;
    }
    
    void setAudioUpload(bool enable) {
        bool old_value = audio_upload_enabled.exchange(enable, std::memory_order_acq_rel);
        
        if (old_value != enable) {
            LOG_INFO("StateMachine", "Audio upload: %s", enable ? "ENABLED" : "DISABLED");
            
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
    // 延迟任务管理
    // ========================================================================
    
    void scheduleDelayedStateChange(AIState target_state, int delay_ms,
                                    const std::string& event) {
        // 取消旧的延迟任务
        cancelDelayedTask();
        
        should_cancel_delay.store(false, std::memory_order_release);
        
        // 创建新的延迟线程（使用智能指针）
        delay_thread = std::make_unique<std::thread>([this, target_state, delay_ms, event]() {
            std::unique_lock<std::mutex> lock(delay_mutex);
            
            LOG_DEBUG("StateMachine", "Delay task started: %dms → %s",
                     delay_ms, AIStateMachine::stateToString(target_state).c_str());
            
            // 可中断的等待
            bool cancelled = delay_cv.wait_for(lock, std::chrono::milliseconds(delay_ms),
                [this]() { return should_cancel_delay.load(std::memory_order_acquire); });
            
            if (cancelled) {
                LOG_DEBUG("StateMachine", "Delay task cancelled");
                return;
            }
            
            // 再次检查取消标志
            if (should_cancel_delay.load(std::memory_order_acquire)) {
                LOG_DEBUG("StateMachine", "Delay task cancelled (double check)");
                return;
            }
            
            LOG_DEBUG("StateMachine", "Delay task executing state change");
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
            LOG_DEBUG("StateMachine", "Watchdog thread started");
            
            while (!should_stop_watchdog.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                
                checkStateTimeout();
            }
            
            LOG_DEBUG("StateMachine", "Watchdog thread stopped");
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
        
        uint64_t timeout_ms = 0;
        bool is_timeout = false;
        
        switch (current) {
            case AIState::CONNECTING:
                timeout_ms = static_cast<uint64_t>(config.connecting_timeout_ms);
                is_timeout = (elapsed_ms > timeout_ms);
                break;
                
            case AIState::LISTENING:
                timeout_ms = static_cast<uint64_t>(config.listening_timeout_ms);
                is_timeout = (elapsed_ms > timeout_ms);
                break;
                
            case AIState::SPEAKING:
                timeout_ms = static_cast<uint64_t>(config.speaking_timeout_ms);
                is_timeout = (elapsed_ms > timeout_ms);
                break;
                
            default:
                break;
        }
        
        if (is_timeout) {
            LOG_WARN("StateMachine", "State timeout: %s (elapsed: %llu ms, timeout: %llu ms)",
                    AIStateMachine::stateToString(current).c_str(),
                    elapsed_ms, timeout_ms);
            
            stats.state_timeouts.fetch_add(1, std::memory_order_relaxed);
            
            // 触发超时回调
            invokeTimeoutCallback(current, static_cast<int>(timeout_ms));
            
            // 超时后转到ERROR状态
            changeState(AIState::ERROR, "timeout", 
                       "State " + AIStateMachine::stateToString(current) + " timeout");
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
                LOG_ERROR("StateMachine", "State callback runtime_error: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::logic_error& e) {
                LOG_ERROR("StateMachine", "State callback logic_error: %s", e.what());
                stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                LOG_ERROR("StateMachine", "State callback exception: %s", e.what());
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
                LOG_ERROR("StateMachine", "Audio callback exception: %s", e.what());
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
                LOG_ERROR("StateMachine", "Error callback exception: %s", e.what());
            }
        }
    }
    
    void invokeTimeoutCallback(AIState state, int timeout_ms) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (timeout_callback) {
            try {
                timeout_callback(state, timeout_ms);
            } catch (const std::exception& e) {
                LOG_ERROR("StateMachine", "Timeout callback exception: %s", e.what());
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
            case AIState::CONNECTING:
                stats.time_in_connecting.fetch_add(time_us, std::memory_order_relaxed);
                break;
            case AIState::LISTENING:
                stats.time_in_listening.fetch_add(time_us, std::memory_order_relaxed);
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
    AIStateMachine::Impl::valid_transitions = {
    // IDLE可以转到CONNECTING（唤醒词触发）或ERROR
    {AIState::IDLE, 
        {AIState::CONNECTING, AIState::ERROR}},
    
    // CONNECTING可以转到LISTENING（连接成功）、IDLE（取消）或ERROR
    {AIState::CONNECTING,
        {AIState::LISTENING, AIState::IDLE, AIState::ERROR}},
    
    // LISTENING可以转到SPEAKING（TTS开始）、IDLE（手动停止）或ERROR
    // 注意：STT完成后仍保持LISTENING
    {AIState::LISTENING, 
        {AIState::SPEAKING, AIState::IDLE, AIState::ERROR}},
    
    // SPEAKING可以转到LISTENING（连续对话）、IDLE（会话结束）或ERROR
    {AIState::SPEAKING, 
        {AIState::LISTENING, AIState::IDLE, AIState::ERROR}},
    
    // ERROR可以转到任何状态（恢复机制）
    {AIState::ERROR, 
        {AIState::IDLE, AIState::CONNECTING, AIState::LISTENING, 
         AIState::SPEAKING, AIState::ERROR}}
};

// ============================================================================
// AIStateMachine 公共接口实现
// ============================================================================

AIStateMachine::AIStateMachine(const StateMachineConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_INFO("StateMachine", "AI State Machine created");
    
    // 启动超时监控线程
    if (pImpl_->config.enable_state_timeout) {
        pImpl_->startWatchdogThread();
    }
}

AIStateMachine::~AIStateMachine() {
    LOG_INFO("StateMachine", "AI State Machine destroying...");
    
    // 输出最终统计
    logStats();
    
    // RAII自动清理（pImpl_的析构函数会清理所有资源）
    LOG_INFO("StateMachine", "AI State Machine destroyed");
}

// ========================================================================
// 状态查询
// ========================================================================

AIState AIStateMachine::getState() const {
    return pImpl_->current_state.load(std::memory_order_acquire);
}

bool AIStateMachine::isState(AIState state) const {
    return getState() == state;
}

bool AIStateMachine::isAudioUploadEnabled() const {
    return pImpl_->audio_upload_enabled.load(std::memory_order_acquire);
}

// ========================================================================
// 事件触发接口
// ========================================================================

void AIStateMachine::onHello() {
    LOG_INFO("StateMachine", "Event: Hello received");
    
    // Hello握手成功 → 进入IDLE状态（等待唤醒词）
    pImpl_->changeState(AIState::IDLE, "onHello");
    
    // IDLE状态下禁用音频上传（仅本地唤醒词检测）
    pImpl_->setAudioUpload(false);
}

void AIStateMachine::onWakewordDetected() {
    LOG_INFO("StateMachine", "Event: Wakeword detected!");
    
    // 唤醒词检测到 → 开始连接音频通道
    if (pImpl_->changeState(AIState::CONNECTING, "onWakewordDetected")) {
        // 音频上传保持禁用（连接阶段）
        pImpl_->setAudioUpload(false);
    }
}

void AIStateMachine::onConnectionSuccess() {
    LOG_INFO("StateMachine", "Event: Connection success");
    
    // 音频通道连接成功 → 进入LISTENING状态
    if (pImpl_->changeState(AIState::LISTENING, "onConnectionSuccess")) {
        // 启用音频上传（开始STT）
        pImpl_->setAudioUpload(true);
    }
}

void AIStateMachine::onConnectionFailed(const std::string& reason) {
    LOG_ERROR("StateMachine", "Event: Connection failed - %s", reason.c_str());
    
    pImpl_->stats.connection_failures.fetch_add(1, std::memory_order_relaxed);
    
    // 连接失败 → 返回IDLE或ERROR
    if (pImpl_->changeState(AIState::IDLE, "onConnectionFailed", reason)) {
        pImpl_->setAudioUpload(false);
    }
}

void AIStateMachine::onSTT(const std::string& text, bool is_final) {
    LOG_DEBUG("StateMachine", "Event: STT - \"%s\" (final: %s)",
             text.c_str(), is_final ? "true" : "false");
    
    // STT消息不改变状态
    // STT partial: 保持LISTENING
    // STT final: 仍然保持LISTENING（等待TTS开始）
    
    // 仅记录日志，状态保持LISTENING
    if (is_final) {
        LOG_INFO("StateMachine", "STT final: \"%s\", waiting for TTS start...", text.c_str());
    }
}

void AIStateMachine::onLLM(const std::string& text, bool is_final) {
    LOG_DEBUG("StateMachine", "Event: LLM - \"%s\" (final: %s)",
             text.c_str(), is_final ? "true" : "false");
    
    // LLM消息不改变状态
    // LLM处理期间保持LISTENING状态
    
    // 仅记录日志，状态保持LISTENING
}

void AIStateMachine::onTTS_start() {
    LOG_INFO("StateMachine", "Event: TTS start");
    
    // TTS开始 → LISTENING → SPEAKING
    if (pImpl_->changeState(AIState::SPEAKING, "onTTS_start")) {
        // 禁用音频上传（避免回声）
        pImpl_->setAudioUpload(false);
    }
}

void AIStateMachine::onTTS_sentenceStart(const std::string& text) {
    LOG_DEBUG("StateMachine", "Event: TTS sentence start - \"%s\"", text.c_str());
    
    // 句子开始 → 状态保持SPEAKING
    // 音频上传保持禁用
}

void AIStateMachine::onTTS_stop(int delay_ms) {
    // 使用配置值（如果传入-1）
    if (delay_ms < 0) {
        delay_ms = pImpl_->config.tts_finish_delay_ms;
    }
    
    LOG_INFO("StateMachine", "Event: TTS stop (delay: %dms)", delay_ms);
    
    // 安全的延迟状态切换
    // SPEAKING → 延迟后 → LISTENING（连续对话）
    pImpl_->scheduleDelayedStateChange(AIState::LISTENING, delay_ms, "onTTS_stop_delayed");
    
    LOG_DEBUG("StateMachine", "Scheduled return to LISTENING after %dms (continuous conversation)", 
             delay_ms);
}

void AIStateMachine::onStopListening() {
    LOG_INFO("StateMachine", "Event: Stop listening");
    
    // 取消所有延迟任务
    pImpl_->cancelDelayedTask();
    
    // 手动停止 → 返回IDLE
    if (pImpl_->changeState(AIState::IDLE, "onStopListening")) {
        pImpl_->setAudioUpload(false);
    }
}

void AIStateMachine::onWebSocketClosed() {
    LOG_INFO("StateMachine", "Event: WebSocket closed");
    
    AIState current = getState();
    
    // 取消所有延迟任务
    pImpl_->cancelDelayedTask();
    
    // 如果在对话中，回到IDLE
    if (current == AIState::CONNECTING || 
        current == AIState::LISTENING || 
        current == AIState::SPEAKING) {
        
        LOG_INFO("StateMachine", "Session ended, returning to IDLE...");
        
        if (pImpl_->changeState(AIState::IDLE, "onWebSocketClosed")) {
            pImpl_->setAudioUpload(false);
        }
    }
}

void AIStateMachine::onError(const std::string& error_msg) {
    LOG_ERROR("StateMachine", "Event: Error - %s", error_msg.c_str());
    
    // 取消所有延迟任务
    pImpl_->cancelDelayedTask();
    
    // 错误 → 进入ERROR状态
    if (pImpl_->changeState(AIState::ERROR, "onError", error_msg)) {
        // 禁用音频上传
        pImpl_->setAudioUpload(false);
    }
}

void AIStateMachine::reset() {
    LOG_INFO("StateMachine", "Reset to IDLE");
    
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

void AIStateMachine::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->state_change_callback = callback;
}

void AIStateMachine::setAudioUploadCallback(AudioUploadCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->audio_upload_callback = callback;
}

void AIStateMachine::setErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->error_callback = callback;
}

void AIStateMachine::setTimeoutCallback(TimeoutCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->timeout_callback = callback;
}

// ========================================================================
// 状态历史和调试
// ========================================================================

std::vector<StateTransition> AIStateMachine::getStateHistory() const {
    return pImpl_->getHistory();
}

void AIStateMachine::clearHistory() {
    pImpl_->clearHistory();
}

StateTransition AIStateMachine::getLastTransition() const {
    return pImpl_->getLastTransition();
}

uint64_t AIStateMachine::getTimeInCurrentState() const {
    uint64_t now = get_nowus();
    return (now - pImpl_->state_enter_time) / 1000;  // 返回毫秒
}

// ========================================================================
// 统计信息
// ========================================================================

void AIStateMachine::getStats(Stats& out_stats) const {
    out_stats.total_transitions.store(pImpl_->stats.total_transitions.load());
    out_stats.invalid_transitions.store(pImpl_->stats.invalid_transitions.load());
    out_stats.callback_exceptions.store(pImpl_->stats.callback_exceptions.load());
    out_stats.state_timeouts.store(pImpl_->stats.state_timeouts.load());
    out_stats.connection_failures.store(pImpl_->stats.connection_failures.load());
    out_stats.time_in_idle.store(pImpl_->stats.time_in_idle.load());
    out_stats.time_in_connecting.store(pImpl_->stats.time_in_connecting.load());
    out_stats.time_in_listening.store(pImpl_->stats.time_in_listening.load());
    out_stats.time_in_speaking.store(pImpl_->stats.time_in_speaking.load());
    out_stats.time_in_error.store(pImpl_->stats.time_in_error.load());
}

void AIStateMachine::resetStats() {
    pImpl_->stats.total_transitions.store(0);
    pImpl_->stats.invalid_transitions.store(0);
    pImpl_->stats.callback_exceptions.store(0);
    pImpl_->stats.state_timeouts.store(0);
    pImpl_->stats.connection_failures.store(0);
    pImpl_->stats.time_in_idle.store(0);
    pImpl_->stats.time_in_connecting.store(0);
    pImpl_->stats.time_in_listening.store(0);
    pImpl_->stats.time_in_speaking.store(0);
    pImpl_->stats.time_in_error.store(0);
    
    LOG_INFO("StateMachine", "Stats reset");
}

void AIStateMachine::logStats() const {
    uint64_t total = pImpl_->stats.total_transitions.load();
    uint64_t invalid = pImpl_->stats.invalid_transitions.load();
    uint64_t exceptions = pImpl_->stats.callback_exceptions.load();
    uint64_t timeouts = pImpl_->stats.state_timeouts.load();
    uint64_t conn_failures = pImpl_->stats.connection_failures.load();
    
    LOG_INFO("StateMachine", "=== AI State Machine Statistics ===");
    LOG_INFO("StateMachine", "  Total transitions:   %llu", total);
    LOG_INFO("StateMachine", "  Invalid transitions: %llu", invalid);
    LOG_INFO("StateMachine", "  Callback exceptions: %llu", exceptions);
    LOG_INFO("StateMachine", "  State timeouts:      %llu", timeouts);
    LOG_INFO("StateMachine", "  Connection failures: %llu", conn_failures);
    
    // 各状态停留时间（转换为秒）
    LOG_INFO("StateMachine", "Time spent in each state:");
    LOG_INFO("StateMachine", "  IDLE:       %.2f s", 
             pImpl_->stats.time_in_idle.load() / 1000000.0);
    LOG_INFO("StateMachine", "  CONNECTING: %.2f s", 
             pImpl_->stats.time_in_connecting.load() / 1000000.0);
    LOG_INFO("StateMachine", "  LISTENING:  %.2f s", 
             pImpl_->stats.time_in_listening.load() / 1000000.0);
    LOG_INFO("StateMachine", "  SPEAKING:   %.2f s", 
             pImpl_->stats.time_in_speaking.load() / 1000000.0);
    LOG_INFO("StateMachine", "  ERROR:      %.2f s", 
             pImpl_->stats.time_in_error.load() / 1000000.0);
    
    // 健康度评分
    if (total > 0) {
        double error_rate = (double)invalid / total * 100.0;
        double exception_rate = (double)exceptions / total * 100.0;
        
        if (error_rate > 1.0) {
            LOG_WARN("StateMachine", "Invalid transition rate: %.2f%% (consider reviewing state logic)", 
                    error_rate);
        }
        
        if (exception_rate > 0.1) {
            LOG_WARN("StateMachine", "Callback exception rate: %.2f%% (review callback implementations)", 
                    exception_rate);
        }
    }
}

// ========================================================================
// 工具函数
// ========================================================================

std::string AIStateMachine::stateToString(AIState state) {
    switch (state) {
        case AIState::IDLE:         return "IDLE";
        case AIState::CONNECTING:   return "CONNECTING";
        case AIState::LISTENING:    return "LISTENING";
        case AIState::SPEAKING:     return "SPEAKING";
        case AIState::ERROR:        return "ERROR";
        default:                    return "INVALID";
    }
}

AIState AIStateMachine::stringToState(const std::string& str) {
    if (str == "IDLE") return AIState::IDLE;
    if (str == "CONNECTING") return AIState::CONNECTING;
    if (str == "LISTENING") return AIState::LISTENING;
    if (str == "SPEAKING") return AIState::SPEAKING;
    if (str == "ERROR") return AIState::ERROR;
    return AIState::IDLE;  // 默认返回IDLE
}

} // namespace statemachine
} // namespace chatbot
} // namespace app

