/**
 * @file wakewordv2.cc
 * @brief 唤醒词检测器V2实现
 */

#include "wakewordv2.h"
#include "../../tool/log/log.h"
#include <mutex>
#include <cstring>
#include <stdexcept>
#include <atomic>

// Snowboy C接口
extern "C" {
    #include "../../../3rdparty/snowboy/include/snowboy-detect-c-wrapper.h"
}

namespace glasses {
namespace chatbot {
namespace wakeword {

using namespace tool::logger;

// ============================================================================
// RAII封装Snowboy检测器
// ============================================================================

/**
 * @brief Snowboy检测器RAII封装
 * @details 自动管理Snowboy检测器的生命周期，防止内存泄漏
 */
class SnowboyDetectorWrapper {
public:
    SnowboyDetectorWrapper(const std::string& resource_file, 
                           const std::string& model_file)
        : detector_(nullptr) {
        detector_ = SnowboyDetectConstructor(resource_file.c_str(), 
                                             model_file.c_str());
        if (!detector_) {
            throw std::runtime_error("Failed to create Snowboy detector");
        }
    }
    
    ~SnowboyDetectorWrapper() {
        if (detector_) {
            SnowboyDetectDestructor(detector_);
            detector_ = nullptr;
        }
    }
    
    // 禁止拷贝
    SnowboyDetectorWrapper(const SnowboyDetectorWrapper&) = delete;
    SnowboyDetectorWrapper& operator=(const SnowboyDetectorWrapper&) = delete;
    
    // 获取原始指针
    SnowboyDetect* get() { return detector_; }
    const SnowboyDetect* get() const { return detector_; }
    
    // 各种Snowboy操作的封装
    void setSensitivity(float sensitivity) {
        if (detector_) {
            char sensitivity_str[32];
            snprintf(sensitivity_str, sizeof(sensitivity_str), "%.2f", (double)sensitivity);
            SnowboyDetectSetSensitivity(detector_, sensitivity_str);
        }
    }
    
    void setAudioGain(float gain) {
        if (detector_) {
            SnowboyDetectSetAudioGain(detector_, gain);
        }
    }
    
    void applyFrontend(bool apply) {
        if (detector_) {
            SnowboyDetectApplyFrontend(detector_, apply);
        }
    }
    
    int runDetection(const int16_t* data, int length) {
        if (!detector_) {
            return static_cast<int>(WakewordResult::ERROR);
        }
        return SnowboyDetectRunDetection(detector_, data, length, false);
    }
    
    void reset() {
        if (detector_) {
            SnowboyDetectReset(detector_);
        }
    }
    
    int getSampleRate() const {
        return detector_ ? SnowboyDetectSampleRate(detector_) : 0;
    }
    
    int getNumChannels() const {
        return detector_ ? SnowboyDetectNumChannels(detector_) : 0;
    }
    
    int getBitsPerSample() const {
        return detector_ ? SnowboyDetectBitsPerSample(detector_) : 0;
    }
    
    int getNumHotwords() const {
        return detector_ ? SnowboyDetectNumHotwords(detector_) : 0;
    }

private:
    SnowboyDetect* detector_;
};

// ============================================================================
// 音频缓冲管理（可选）
// ============================================================================

class AudioBuffer {
public:
    explicit AudioBuffer(size_t max_size)
        : max_size_(max_size) {
        buffer_.reserve(max_size);
    }
    
    void append(const int16_t* data, int length) {
        if (!data || length <= 0) {
            return;
        }
        
        // 防止缓冲区溢出
        if (buffer_.size() + length > max_size_) {
            LOG_WARN("WakewordV2", "Audio buffer overflow, clearing old data");
            buffer_.clear();
        }
        
        buffer_.insert(buffer_.end(), data, data + length);
    }
    
    bool hasEnoughData(size_t required_size) const {
        return buffer_.size() >= required_size;
    }
    
    std::vector<int16_t> getFrame(size_t frame_size) {
        if (buffer_.size() < frame_size) {
            return {};
        }
        
        std::vector<int16_t> frame(buffer_.begin(), buffer_.begin() + frame_size);
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
        
        return frame;
    }
    
    void clear() {
        buffer_.clear();
    }
    
    size_t size() const {
        return buffer_.size();
    }

private:
    std::vector<int16_t> buffer_;
    size_t max_size_;
};

// ============================================================================
// WakewordDetectorV2::Impl 内部实现
// ============================================================================

class WakewordDetectorV2::Impl {
public:
    // 配置
    WakewordConfig config_;
    
    // Snowboy检测器（RAII智能指针）
    std::unique_ptr<SnowboyDetectorWrapper> detector_;
    
    // 音频缓冲
    std::unique_ptr<AudioBuffer> audio_buffer_;
    
    // 回调函数（线程安全）
    WakewordCallback wakeword_callback_;
    WakewordErrorCallback error_callback_;
    std::mutex callback_mutex_;
    
    // 状态
    std::atomic<bool> initialized_{false};
    std::atomic<bool> enabled_{false};
    
    explicit Impl(const WakewordConfig& config)
        : config_(config) {
        if (config_.enable_buffering) {
            audio_buffer_ = std::make_unique<AudioBuffer>(config_.max_buffer_size);
        }
    }
    
    ~Impl() {
        LOG_DEBUG(config_.log_prefix.c_str(), "Impl destroyed (detector auto-released)");
    }
    
    // ========================================================================
    // 初始化
    // ========================================================================
    
    WakewordError initialize() {
        if (initialized_.load(std::memory_order_acquire)) {
            LOG_WARN(config_.log_prefix.c_str(), "Already initialized");
            return WakewordError::ALREADY_INITIALIZED;
        }
        
        LOG_INFO(config_.log_prefix.c_str(), "Initializing...");
        LOG_DEBUG(config_.log_prefix.c_str(), "  Resource: %s", config_.resource_file.c_str());
        LOG_DEBUG(config_.log_prefix.c_str(), "  Model: %s", config_.model_file.c_str());
        LOG_DEBUG(config_.log_prefix.c_str(), "  Sensitivity: %.2f", (double)config_.sensitivity);
        LOG_DEBUG(config_.log_prefix.c_str(), "  Audio Gain: %.2f", (double)config_.audio_gain);
        
        try {
            // 创建Snowboy检测器（RAII）
            detector_ = std::make_unique<SnowboyDetectorWrapper>(
                config_.resource_file,
                config_.model_file
            );
            
            // 设置参数
            detector_->setSensitivity(config_.sensitivity);
            detector_->setAudioGain(config_.audio_gain);
            detector_->applyFrontend(config_.apply_frontend);
            
            // 打印检测器信息
            int sample_rate = detector_->getSampleRate();
            int num_channels = detector_->getNumChannels();
            int bits_per_sample = detector_->getBitsPerSample();
            int num_hotwords = detector_->getNumHotwords();
            
            LOG_INFO(config_.log_prefix.c_str(), "✓ Detector created");
            LOG_DEBUG(config_.log_prefix.c_str(), "  Sample Rate: %d Hz", sample_rate);
            LOG_DEBUG(config_.log_prefix.c_str(), "  Channels: %d", num_channels);
            LOG_DEBUG(config_.log_prefix.c_str(), "  Bits/Sample: %d", bits_per_sample);
            LOG_DEBUG(config_.log_prefix.c_str(), "  Num Hotwords: %d", num_hotwords);
            
            initialized_.store(true, std::memory_order_release);
            enabled_.store(true, std::memory_order_release);
            
            return WakewordError::NONE;
            
        } catch (const std::exception& e) {
            LOG_ERROR(config_.log_prefix.c_str(), 
                     "✗ Failed to create detector: %s", e.what());
            
            // 触发错误回调
            invokeErrorCallback(WakewordError::DETECTOR_ERROR, e.what());
            
            return WakewordError::DETECTOR_ERROR;
        }
    }
    
    // ========================================================================
    // 音频处理
    // ========================================================================
    
    WakewordResult processAudioFrame(const int16_t* data, int length) {
        // 参数验证
        if (!data) {
            LOG_ERROR(config_.log_prefix.c_str(), "Null audio data pointer");
            invokeErrorCallback(WakewordError::INVALID_PARAMS, "Null audio data");
            return WakewordResult::ERROR;
        }
        
        if (length <= 0 || length > 65536) {  // 合理范围检查
            LOG_ERROR(config_.log_prefix.c_str(), "Invalid frame length: %d", length);
            invokeErrorCallback(WakewordError::INVALID_PARAMS, 
                               "Invalid frame length: " + std::to_string(length));
            return WakewordResult::ERROR;
        }
        
        // 状态检查
        if (!initialized_.load(std::memory_order_acquire)) {
            return WakewordResult::NONE;
        }
        
        if (!enabled_.load(std::memory_order_acquire)) {
            return WakewordResult::NONE;
        }
        
        if (!detector_) {
            return WakewordResult::ERROR;
        }
        
        // 如果启用缓冲
        if (config_.enable_buffering && audio_buffer_) {
            audio_buffer_->append(data, length);
            // TODO: 从缓冲区提取帧并处理
            // 这里暂时直接处理
        }
        
        // 运行检测
        int result = detector_->runDetection(data, length);
        
        WakewordResult wakeword_result = static_cast<WakewordResult>(result);
        
        // 处理检测结果
        if (result > 0) {
            LOG_INFO(config_.log_prefix.c_str(), "✓ Hotword %d detected!", result);
            invokeWakewordCallback(wakeword_result, result);
        } else if (result == -1) {
            // 静音，不记录
            wakeword_result = WakewordResult::SILENCE;
        } else if (result == -2) {
            wakeword_result = WakewordResult::ERROR;
            // 可以选择性记录：每100次错误才记录一次
            static std::atomic<int> error_count{0};
            if (error_count.fetch_add(1) % 200 == 0) {
                LOG_WARN(config_.log_prefix.c_str(), "Detection errors: %d (audio quality issue)", error_count.load());
            }
        }
        
        return wakeword_result;
    }
    
    // ========================================================================
    // 回调管理（线程安全+异常安全）
    // ========================================================================
    
    void setWakewordCallback(WakewordCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        wakeword_callback_ = std::move(callback);
    }
    
    void setErrorCallback(WakewordErrorCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        error_callback_ = std::move(callback);
    }
    
    void invokeWakewordCallback(WakewordResult result, int hotword_index) {
        WakewordCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            cb = wakeword_callback_;  // 拷贝callback
        }
        
        if (cb) {
            try {
                cb(result, hotword_index);
            } catch (const std::exception& e) {
                LOG_ERROR(config_.log_prefix.c_str(), 
                         "Wakeword callback exception: %s", e.what());
                invokeErrorCallback(WakewordError::CALLBACK_EXCEPTION, e.what());
            } catch (...) {
                LOG_ERROR(config_.log_prefix.c_str(), 
                         "Wakeword callback unknown exception");
                invokeErrorCallback(WakewordError::CALLBACK_EXCEPTION, "Unknown exception");
            }
        }
    }
    
    void invokeErrorCallback(WakewordError error, const std::string& message) {
        WakewordErrorCallback cb;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            cb = error_callback_;  // 拷贝callback
        }
        
        if (cb) {
            try {
                cb(error, message);
            } catch (const std::exception& e) {
                LOG_ERROR(config_.log_prefix.c_str(), 
                         "Error callback exception: %s", e.what());
            } catch (...) {
                LOG_ERROR(config_.log_prefix.c_str(), 
                         "Error callback unknown exception");
            }
        }
    }
    
    // ========================================================================
    // 参数设置
    // ========================================================================
    
    void setSensitivity(float sensitivity) {
        if (detector_) {
            detector_->setSensitivity(sensitivity);
            config_.sensitivity = sensitivity;
            LOG_DEBUG(config_.log_prefix.c_str(), "Sensitivity set to %.2f", (double)sensitivity);
        }
    }
    
    void setAudioGain(float gain) {
        if (detector_) {
            detector_->setAudioGain(gain);
            config_.audio_gain = gain;
            LOG_DEBUG(config_.log_prefix.c_str(), "Audio gain set to %.2f", (double)gain);
        }
    }
    
    void setEnabled(bool enabled) {
        enabled_.store(enabled, std::memory_order_release);
        LOG_INFO(config_.log_prefix.c_str(), "%s", enabled ? "Enabled" : "Disabled");
    }
    
    void reset() {
        if (detector_) {
            detector_->reset();
            LOG_DEBUG(config_.log_prefix.c_str(), "Detector reset");
        }
        
        if (audio_buffer_) {
            audio_buffer_->clear();
            LOG_DEBUG(config_.log_prefix.c_str(), "Audio buffer cleared");
        }
    }
};

// ============================================================================
// WakewordDetectorV2 公共接口实现
// ============================================================================

WakewordDetectorV2::WakewordDetectorV2(const WakewordConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_DEBUG(config.log_prefix.c_str(), "WakewordDetectorV2 created");
}

WakewordDetectorV2::~WakewordDetectorV2() {
    LOG_DEBUG(pImpl_->config_.log_prefix.c_str(), "WakewordDetectorV2 destroying...");
    // RAII自动清理
    LOG_DEBUG(pImpl_->config_.log_prefix.c_str(), "WakewordDetectorV2 destroyed");
}

// ========================================================================
// 初始化和控制
// ========================================================================

WakewordError WakewordDetectorV2::initialize() {
    return pImpl_->initialize();
}

void WakewordDetectorV2::setEnabled(bool enabled) {
    pImpl_->setEnabled(enabled);
}

bool WakewordDetectorV2::isEnabled() const {
    return pImpl_->enabled_.load(std::memory_order_acquire);
}

bool WakewordDetectorV2::isInitialized() const {
    return pImpl_->initialized_.load(std::memory_order_acquire);
}

void WakewordDetectorV2::reset() {
    pImpl_->reset();
}

// ========================================================================
// 音频处理
// ========================================================================

WakewordResult WakewordDetectorV2::processAudioFrame(const int16_t* data, int length) {
    return pImpl_->processAudioFrame(data, length);
}

// ========================================================================
// 参数设置
// ========================================================================

void WakewordDetectorV2::setSensitivity(float sensitivity) {
    pImpl_->setSensitivity(sensitivity);
}

void WakewordDetectorV2::setAudioGain(float gain) {
    pImpl_->setAudioGain(gain);
}

// ========================================================================
// 回调设置
// ========================================================================

void WakewordDetectorV2::setWakewordCallback(WakewordCallback callback) {
    pImpl_->setWakewordCallback(std::move(callback));
}

void WakewordDetectorV2::setErrorCallback(WakewordErrorCallback callback) {
    pImpl_->setErrorCallback(std::move(callback));
}

// ========================================================================
// 信息查询
// ========================================================================

int WakewordDetectorV2::getSampleRate() const {
    return pImpl_->detector_ ? pImpl_->detector_->getSampleRate() : 0;
}

int WakewordDetectorV2::getNumChannels() const {
    return pImpl_->detector_ ? pImpl_->detector_->getNumChannels() : 0;
}

int WakewordDetectorV2::getBitsPerSample() const {
    return pImpl_->detector_ ? pImpl_->detector_->getBitsPerSample() : 0;
}

int WakewordDetectorV2::getNumHotwords() const {
    return pImpl_->detector_ ? pImpl_->detector_->getNumHotwords() : 0;
}

} // namespace wakeword
} // namespace chatbot
} // namespace glasses

