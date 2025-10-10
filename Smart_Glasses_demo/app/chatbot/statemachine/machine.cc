/**
 * @file machine.cc
 * @brief AI状态机模块实现
 */

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
    std::thread delay_thread;

    Impl()
        : current_state(AIState::UNKNOWN)
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
        AIState old_state = current_state.load();
        
        if (old_state == new_state) {
            return;  // 状态未改变，不需要通知
        }

        current_state = new_state;
        
        std::cout << "[StateMachine] State changed: " 
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
    std::cout << "[StateMachine] AI state machine created" << std::endl;
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
    
    // Hello握手成功 → 进入IDLE状态
    pimpl_->changeState(AIState::IDLE);
    
    // 此时不启用音频上传，等待listen消息
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
    }
    
    // 音频上传保持开启（直到TTS开始）
}

void AIStateMachine::onLLM(const std::string& text, bool is_final) {
    std::cout << "[StateMachine] Event: LLM - \"" << text << "\" (final: " 
              << (is_final ? "true" : "false") << ")" << std::endl;
    
    // LLM回复期间，状态保持THINKING
    // （实际上LLM是流式返回，多次调用此函数）
}

void AIStateMachine::onTTS_start() {
    std::cout << "[StateMachine] Event: TTS start" << std::endl;
    
    // TTS开始 → 禁用音频上传（避免AI听到自己的声音）
    pimpl_->setAudioUpload(false);
    
    // 状态保持LISTENING（xiaozhi的设计）
    // 注意：这里不改为SPEAKING，因为xiaozhi在sentence_start时才改为SPEAKING
}

void AIStateMachine::onTTS_sentenceStart(const std::string& text) {
    std::cout << "[StateMachine] Event: TTS sentence start - \"" << text << "\"" << std::endl;
    
    // 句子开始 → 进入SPEAKING状态
    pimpl_->changeState(AIState::SPEAKING);
    
    // 音频上传保持禁用
}

void AIStateMachine::onTTS_stop(int delay_ms) {
    std::cout << "[StateMachine] Event: TTS stop (delay: " << delay_ms << "ms)" << std::endl;
    
    // 等待延迟线程结束（如果有）
    if (pimpl_->delay_thread.joinable()) {
        pimpl_->delay_thread.join();
    }
    
    // TTS结束 → 延迟后恢复监听
    pimpl_->delay_thread = std::thread([this, delay_ms]() {
        // 等待指定时间（避免AI听到自己说话的尾音）
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        
        // 恢复监听状态
        pimpl_->changeState(AIState::LISTENING);
        
        // 恢复音频上传
        pimpl_->setAudioUpload(true);
        
        std::cout << "[StateMachine] Resumed listening after TTS" << std::endl;
    });
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
        case AIState::UNKNOWN:      return "UNKNOWN";
        case AIState::STARTING:     return "STARTING";
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

