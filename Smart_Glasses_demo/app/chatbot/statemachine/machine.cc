#include "machine.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace glasses {
namespace chatbot {
namespace statemachine {

/**
 * @brief AI状态机的内部实现（Pimpl惯用法）
 */
class AIStateMachine::Impl {
public:
    std::atomic<AIState> current_state;
    std::atomic<bool> audio_upload_enabled;
    
    StateChangeCallback state_change_callback;
    AudioUploadCallback audio_upload_callback;
    
    std::mutex mutex;
    std::thread delay_thread;  // TTS结束延迟线程

    Impl()
        : current_state(AIState::IDLE)
        , audio_upload_enabled(false)
        , state_change_callback(nullptr)
        , audio_upload_callback(nullptr) {
    }

    ~Impl() {
        // 等待延迟线程结束
        if (delay_thread.joinable()) {
            delay_thread.join();
        }
    }

    void changeState(AIState new_state) {
        AIState old_state = current_state.exchange(new_state);
        
        if (old_state == new_state) {
            return;  // 状态未改变，不需要通知
        }

        current_state = new_state;
        
        std::cout << "[StateMachine] State: " 
                  << AIStateMachine::stateToString(old_state) << " → " 
                  << AIStateMachine::stateToString(new_state) << std::endl;

        // 触发状态变化回调
        if (state_change_callback) {
            state_change_callback(old_state, new_state);
        }
    }

    void setAudioUpload(bool enable) {
        bool old_value = audio_upload_enabled.exchange(enable);
        
        if (old_value != enable) {
            std::cout << "[StateMachine] Audio upload: " 
                      << (enable ? "ENABLED" : "DISABLED") << std::endl;
            
            // 触发音频上传控制回调
            if (audio_upload_callback) {
                audio_upload_callback(enable);
            }
        }
    }
};

// ============================================================================
// AIStateMachine 公共接口实现
// ============================================================================

AIStateMachine::AIStateMachine()
    : pimpl_(new Impl()) {
    std::cout << "[StateMachine] V2 AI state machine created" << std::endl;
}

AIStateMachine::~AIStateMachine() {
    if (pimpl_) {
        delete pimpl_;
        pimpl_ = nullptr;
    }
}

void AIStateMachine::setState(AIState state) {
    pimpl_->changeState(state);
}

AIState AIStateMachine::getState() const {
    return pimpl_->current_state;
}

bool AIStateMachine::isState(AIState state) const {
    return pimpl_->current_state == state;
}

// ============================================================================
// 事件触发接口（协议消息驱动）
// ============================================================================

void AIStateMachine::onHello() {
    std::cout << "[StateMachine] Event: Hello received" << std::endl;
    
    // Hello握手成功 → 进入IDLE状态（待机，等待唤醒词）
    pimpl_->changeState(AIState::IDLE);
    
    // IDLE状态下不启用音频上传到服务器，只做本地唤醒词检测
    pimpl_->setAudioUpload(false);
}

void AIStateMachine::onWakewordDetected() {
    std::cout << "[StateMachine] Event: Wakeword detected!" << std::endl;
    
    // 唤醒词检测到，准备进入监听状态
    // 注意：实际的状态切换在 onListenStart() 中完成
}

void AIStateMachine::onListenStart() {
    std::cout << "[StateMachine] Event: Listen start" << std::endl;
    
    // 开始监听 → 进入LISTENING状态
    pimpl_->changeState(AIState::LISTENING);
    
    // 启用音频上传
    pimpl_->setAudioUpload(true);
}

void AIStateMachine::onSTT(const std::string& text, bool is_final) {
    std::cout << "[StateMachine] Event: STT - \"" << text << "\" (final: " 
              << (is_final ? "true" : "false") << ")" << std::endl;
    
    if (is_final) {
        // 收到最终识别结果 → AI开始思考
        pimpl_->changeState(AIState::THINKING);
        
        // 停止音频上传（用户已说完）
        pimpl_->setAudioUpload(false);
    }
    // 否则保持LISTENING状态，继续上传音频
}

void AIStateMachine::onLLM(const std::string& text, bool is_final) {
    std::cout << "[StateMachine] Event: LLM - \"" << text << "\" (final: " 
              << (is_final ? "true" : "false") << ")" << std::endl;
    
    // LLM回复期间，状态保持THINKING
    // （实际上LLM是流式返回，会多次调用此函数）
}

void AIStateMachine::onTTS_start() {
    std::cout << "[StateMachine] Event: TTS start" << std::endl;
    
    // TTS开始 → 进入SPEAKING状态
    pimpl_->changeState(AIState::SPEAKING);
    
    // 音频上传已在THINKING时禁用，保持禁用
}

void AIStateMachine::onTTS_sentenceStart(const std::string& text) {
    std::cout << "[StateMachine] Event: TTS sentence start - \"" << text << "\"" << std::endl;
    
    // 句子开始 → 状态保持SPEAKING
    // 音频上传保持禁用
}

void AIStateMachine::onTTS_stop(int delay_ms) {
    std::cout << "[StateMachine] Event: TTS stop (delay: " << delay_ms << "ms)" << std::endl;
    
    // 等待之前的延迟线程结束（如果有）
    if (pimpl_->delay_thread.joinable()) {
        pimpl_->delay_thread.join();
    }
    
    // TTS结束 → 延迟后回到LISTENING状态（继续监听）
    pimpl_->delay_thread = std::thread([this, delay_ms]() {
        // 等待指定时间（避免AI听到自己说话的尾音）
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        
        // 回到LISTENING状态（继续监听）
        pimpl_->changeState(AIState::LISTENING);
        
        // 启用音频上传，继续监听用户说话
        pimpl_->setAudioUpload(true);
        
        std::cout << "[StateMachine] Back to LISTENING, continuous conversation mode" << std::endl;
        std::cout << "[StateMachine] (Server will timeout after 5min silence)" << std::endl;
    });
}

void AIStateMachine::onWebSocketClosed() {
    std::cout << "[StateMachine] Event: WebSocket closed (server timeout or disconnect)" << std::endl;
    
    AIState current = pimpl_->current_state.load();
    
    // 如果在对话中（LISTENING/THINKING/SPEAKING），回到IDLE
    if (current == AIState::LISTENING || 
        current == AIState::THINKING || 
        current == AIState::SPEAKING) {
        
        std::cout << "[StateMachine] Session ended by server, returning to IDLE..." << std::endl;
        
        pimpl_->changeState(AIState::IDLE);
        pimpl_->setAudioUpload(false);
        
        std::cout << "[StateMachine] Back to IDLE, waiting for wakeword..." << std::endl;
    }
}

void AIStateMachine::onError(const std::string& error_msg) {
    std::cerr << "[StateMachine] Event: Error - " << error_msg << std::endl;
    
    // 错误 → 进入ERROR状态
    pimpl_->changeState(AIState::ERROR);
    
    // 禁用音频上传
    pimpl_->setAudioUpload(false);
}

void AIStateMachine::reset() {
    std::cout << "[StateMachine] Reset to IDLE" << std::endl;
    
    // 重置到IDLE状态
    pimpl_->changeState(AIState::IDLE);
    
    // 禁用音频上传
    pimpl_->setAudioUpload(false);
}

// ============================================================================
// 音频上传控制
// ============================================================================

void AIStateMachine::enableAudioUpload() {
    pimpl_->setAudioUpload(true);
}

void AIStateMachine::disableAudioUpload() {
    pimpl_->setAudioUpload(false);
}

bool AIStateMachine::isAudioUploadEnabled() const {
    return pimpl_->audio_upload_enabled;
}

// ============================================================================
// 回调设置
// ============================================================================

void AIStateMachine::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    pimpl_->state_change_callback = callback;
}

void AIStateMachine::setAudioUploadCallback(AudioUploadCallback callback) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    pimpl_->audio_upload_callback = callback;
}

// ============================================================================
// 工具函数
// ============================================================================

std::string AIStateMachine::stateToString(AIState state) {
    switch (state) {
        case AIState::IDLE:         return "IDLE";
        case AIState::LISTENING:    return "LISTENING";
        case AIState::THINKING:     return "THINKING";
        case AIState::SPEAKING:     return "SPEAKING";
        case AIState::ERROR:        return "ERROR";
        default:                    return "INVALID";
    }
}

} // namespace statemachine
} // namespace chatbot
} // namespace glasses

