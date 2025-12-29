/**
 * @file wakeword.cc
 * @brief 唤醒词检测器实现
 */

#include "wakeword.hpp"
#include "../../tool/log/log.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <stdexcept>

// Snowboy C接口
extern "C"
{
#include "../../../third_party/snowboy/include/snowboy-detect-c-wrapper.h"
}

namespace
{
    constexpr const char* LOG_TAG                  = "WAKEWORD";
    constexpr std::size_t SENSITIVITY_STRING_SIZE  = 32;
    constexpr int         MAX_FRAME_LENGTH_SAMPLES = 65536;
} // namespace

namespace app
{
    namespace chatbot
    {
        namespace wakeword
        {

            using namespace tool::log;

            // ============================================================================
            // Snowboy检测器封装
            // ============================================================================

            /**
             * @brief Snowboy检测器封装
             */
            class SnowboyDetectorWrapper
            {
            public:
                SnowboyDetectorWrapper(const std::string& resource_file,
                                       const std::string& model_file)
                    : detector_(SnowboyDetectConstructor(resource_file.c_str(), model_file.c_str()))
                {
                    if (!detector_)
                    {
                        throw std::runtime_error("Failed to create Snowboy detector");
                    }
                }

                ~SnowboyDetectorWrapper()
                {
                    if (detector_)
                    {
                        SnowboyDetectDestructor(detector_);
                        detector_ = nullptr;
                    }
                }

                // 禁止拷贝
                SnowboyDetectorWrapper(const SnowboyDetectorWrapper&)            = delete;
                SnowboyDetectorWrapper& operator=(const SnowboyDetectorWrapper&) = delete;

                // 获取原始指针
                SnowboyDetect* get()
                {
                    return detector_;
                }
                const SnowboyDetect* get() const
                {
                    return detector_;
                }

                // 各种Snowboy操作的封装
                void setSensitivity(float sensitivity)
                {
                    if (detector_)
                    {
                        std::array<char, SENSITIVITY_STRING_SIZE> sensitivity_str{};
                        std::snprintf(sensitivity_str.data(), sensitivity_str.size(), "%.2f",
                                      static_cast<double>(sensitivity));
                        SnowboyDetectSetSensitivity(detector_, sensitivity_str.data());
                    }
                }

                void setAudioGain(float gain)
                {
                    if (detector_)
                    {
                        SnowboyDetectSetAudioGain(detector_, gain);
                    }
                }

                void applyFrontend(bool apply)
                {
                    if (detector_)
                    {
                        SnowboyDetectApplyFrontend(detector_, apply);
                    }
                }

                int runDetection(const int16_t* data, int length)
                {
                    if (!detector_)
                    {
                        return static_cast<int>(WakewordResult::ERROR);
                    }
                    return SnowboyDetectRunDetection(detector_, data, length, false);
                }

                void reset()
                {
                    if (detector_)
                    {
                        SnowboyDetectReset(detector_);
                    }
                }

                int getSampleRate() const
                {
                    return detector_ ? SnowboyDetectSampleRate(detector_) : 0;
                }

                int getNumChannels() const
                {
                    return detector_ ? SnowboyDetectNumChannels(detector_) : 0;
                }

                int getBitsPerSample() const
                {
                    return detector_ ? SnowboyDetectBitsPerSample(detector_) : 0;
                }

                int getNumHotwords() const
                {
                    return detector_ ? SnowboyDetectNumHotwords(detector_) : 0;
                }

            private:
                SnowboyDetect* detector_;
            };

            // ============================================================================
            // 音频缓冲管理
            // ============================================================================

            class AudioBuffer
            {
            public:
                explicit AudioBuffer(size_t max_size) : max_size_(max_size)
                {
                    buffer_.reserve(max_size);
                }

                void append(const int16_t* data, int length)
                {
                    if (!data || length <= 0)
                    {
                        return;
                    }

                    if (buffer_.size() + static_cast<size_t>(length) > max_size_)
                    {
                        LOG_WARN(LOG_TAG, "Audio buffer overflow, clearing old data");
                        buffer_.clear();
                    }

                    auto old_size = buffer_.size();
                    buffer_.resize(old_size + static_cast<size_t>(length));
                    std::copy_n(data, static_cast<size_t>(length),
                                buffer_.begin() + static_cast<std::ptrdiff_t>(old_size));
                }

                bool hasEnoughData(size_t required_size) const
                {
                    return buffer_.size() >= required_size;
                }

                std::vector<int16_t> getFrame(size_t frame_size)
                {
                    if (buffer_.size() < frame_size)
                    {
                        return {};
                    }

                    std::vector<int16_t> frame(buffer_.begin(), buffer_.begin() + frame_size);
                    buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);

                    return frame;
                }

                void clear()
                {
                    buffer_.clear();
                }

                size_t size() const
                {
                    return buffer_.size();
                }

            private:
                std::vector<int16_t> buffer_;
                size_t               max_size_;
            };

            // ============================================================================
            // WakewordDetector::Impl 内部实现
            // ============================================================================

            class WakewordDetector::Impl
            {
            public:
                // 配置
                WakewordConfig config_;

                // Snowboy检测器
                std::unique_ptr<SnowboyDetectorWrapper> detector_;

                // 音频缓冲
                std::unique_ptr<AudioBuffer> audio_buffer_;

                // 回调函数
                WakewordCallback      wakeword_callback_;
                WakewordErrorCallback error_callback_;
                std::mutex            callback_mutex_;

                // 状态
                std::atomic<bool> initialized_{false};
                std::atomic<bool> enabled_{false};

                explicit Impl(WakewordConfig config) : config_(std::move(config))
                {
                    if (config_.enable_buffering)
                    {
                        audio_buffer_ = std::make_unique<AudioBuffer>(config_.max_buffer_size);
                    }
                }

                ~Impl()
                {
                    LOG_DEBUG(LOG_TAG, "Impl destroyed");
                }

                // ========================================================================
                // 初始化
                // ========================================================================

                WakewordError init()
                {
                    if (initialized_.load(std::memory_order_acquire))
                    {
                        LOG_WARN(LOG_TAG, "Already initialized");
                        return WakewordError::ALREADY_INITIALIZED;
                    }

                    LOG_INFO(LOG_TAG, "Initializing...");
                    LOG_DEBUG(LOG_TAG, "  Resource: %s", config_.resource_file.c_str());
                    LOG_DEBUG(LOG_TAG, "  Model: %s", config_.model_file.c_str());
                    LOG_DEBUG(LOG_TAG, "  Sensitivity: %.2f", (double)config_.sensitivity);
                    LOG_DEBUG(LOG_TAG, "  Audio Gain: %.2f", (double)config_.audio_gain);

                    try
                    {
                        // 创建Snowboy检测器
                        detector_ = std::make_unique<SnowboyDetectorWrapper>(config_.resource_file,
                                                                             config_.model_file);

                        // 设置参数
                        detector_->setSensitivity(config_.sensitivity);
                        detector_->setAudioGain(config_.audio_gain);
                        detector_->applyFrontend(config_.apply_frontend);

                        // 打印检测器信息
                        int sample_rate     = detector_->getSampleRate();
                        int num_channels    = detector_->getNumChannels();
                        int bits_per_sample = detector_->getBitsPerSample();
                        int num_hotwords    = detector_->getNumHotwords();

                        LOG_INFO(LOG_TAG, " Detector created");
                        LOG_DEBUG(LOG_TAG, "  Sample Rate: %d Hz", sample_rate);
                        LOG_DEBUG(LOG_TAG, "  Channels: %d", num_channels);
                        LOG_DEBUG(LOG_TAG, "  Bits/Sample: %d", bits_per_sample);
                        LOG_DEBUG(LOG_TAG, "  Num Hotwords: %d", num_hotwords);

                        initialized_.store(true, std::memory_order_release);
                        enabled_.store(true, std::memory_order_release);

                        return WakewordError::NONE;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "✗ Failed to create detector: %s", e.what());

                        // 触发错误回调
                        invokeErrorCallback(WakewordError::DETECTOR_ERROR, e.what());

                        return WakewordError::DETECTOR_ERROR;
                    }
                }

                // ========================================================================
                // 音频处理
                // ========================================================================

                WakewordResult processAudioFrame(const int16_t* data, int length)
                {
                    // 参数验证
                    if (!data)
                    {
                        LOG_ERROR(LOG_TAG, "Null audio data pointer");
                        invokeErrorCallback(WakewordError::INVALID_PARAMS, "Null audio data");
                        return WakewordResult::ERROR;
                    }

                    if (length <= 0 || length > MAX_FRAME_LENGTH_SAMPLES)
                    {
                        LOG_ERROR(LOG_TAG, "Invalid frame length: %d", length);
                        invokeErrorCallback(WakewordError::INVALID_PARAMS,
                                            "Invalid frame length: " + std::to_string(length));
                        return WakewordResult::ERROR;
                    }

                    // 状态检查
                    if (!initialized_.load(std::memory_order_acquire))
                    {
                        return WakewordResult::NONE;
                    }

                    if (!enabled_.load(std::memory_order_acquire))
                    {
                        return WakewordResult::NONE;
                    }

                    if (!detector_)
                    {
                        return WakewordResult::ERROR;
                    }

                    // 如果启用缓冲
                    if (config_.enable_buffering && audio_buffer_)
                    {
                        audio_buffer_->append(data, length);
                    }

                    // 运行检测
                    int result = detector_->runDetection(data, length);

                    auto wakeword_result = static_cast<WakewordResult>(result);

                    // 处理检测结果
                    if (result > 0)
                    {
                        LOG_INFO(LOG_TAG, " Hotword %d detected!", result);
                        invokeWakewordCallback(wakeword_result, result);
                    }
                    else if (result == static_cast<int>(WakewordResult::SILENCE))
                    {
                        // 静音检测（可选日志）
                        // LOG_DEBUG(LOG_TAG, "Silence detected");
                    }
                    else if (result == static_cast<int>(WakewordResult::ERROR))
                    {
                        // LOG_ERROR(LOG_TAG, "Detection error");
                        invokeErrorCallback(WakewordError::DETECTOR_ERROR,
                                            "Detection returned error");
                    }

                    return wakeword_result;
                }

                // ========================================================================
                // 回调管理
                // ========================================================================

                void setWakewordCallback(WakewordCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    wakeword_callback_ = std::move(callback);
                }

                void setErrorCallback(WakewordErrorCallback callback)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    error_callback_ = std::move(callback);
                }

                void invokeWakewordCallback(WakewordResult result, int hotword_index)
                {
                    WakewordCallback cb;
                    {
                        std::lock_guard<std::mutex> lock(callback_mutex_);
                        cb = wakeword_callback_;
                    }

                    if (cb)
                    {
                        try
                        {
                            cb(result, hotword_index);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "Wakeword callback exception: %s", e.what());
                            invokeErrorCallback(WakewordError::CALLBACK_EXCEPTION, e.what());
                        }
                        catch (...)
                        {
                            LOG_ERROR(LOG_TAG, "Wakeword callback unknown exception");
                            invokeErrorCallback(WakewordError::CALLBACK_EXCEPTION,
                                                "Unknown exception");
                        }
                    }
                }

                void invokeErrorCallback(WakewordError error, const std::string& message)
                {
                    WakewordErrorCallback cb;
                    {
                        std::lock_guard<std::mutex> lock(callback_mutex_);
                        cb = error_callback_;
                    }

                    if (cb)
                    {
                        try
                        {
                            cb(error, message);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "Error callback exception: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_ERROR(LOG_TAG, "Error callback unknown exception");
                        }
                    }
                }

                // ========================================================================
                // 参数设置
                // ========================================================================

                void setSensitivity(float sensitivity)
                {
                    if (detector_)
                    {
                        detector_->setSensitivity(sensitivity);
                        config_.sensitivity = sensitivity;
                        LOG_DEBUG(LOG_TAG, "Sensitivity set to %.2f", (double)sensitivity);
                    }
                }

                void setAudioGain(float gain)
                {
                    if (detector_)
                    {
                        detector_->setAudioGain(gain);
                        config_.audio_gain = gain;
                        LOG_DEBUG(LOG_TAG, "Audio gain set to %.2f", (double)gain);
                    }
                }

                void setEnabled(bool enabled)
                {
                    enabled_.store(enabled, std::memory_order_release);
                    LOG_INFO(LOG_TAG, "%s", enabled ? "Enabled" : "Disabled");
                }

                void reset() // NOLINT(readability-make-member-function-const)
                {
                    if (detector_)
                    {
                        detector_->reset();
                        LOG_DEBUG(LOG_TAG, "Detector reset");
                    }

                    if (audio_buffer_)
                    {
                        audio_buffer_->clear();
                        LOG_DEBUG(LOG_TAG, "Audio buffer cleared");
                    }
                }
            };

            // ============================================================================
            // WakewordDetector 公共接口实现
            // ============================================================================

            WakewordDetector::WakewordDetector(const WakewordConfig& config)
                : pImpl_(std::make_unique<Impl>(config))
            {
                LOG_DEBUG(LOG_TAG, "WakewordDetector created");
            }

            WakewordDetector::~WakewordDetector()
            {
                LOG_DEBUG(LOG_TAG, "WakewordDetector destroying...");
                LOG_DEBUG(LOG_TAG, "WakewordDetector destroyed");
            }

            // ========================================================================
            // 初始化和控制
            // ========================================================================

            WakewordError WakewordDetector::init()
            {
                return pImpl_->init();
            }

            void WakewordDetector::setEnabled(bool enabled)
            {
                pImpl_->setEnabled(enabled);
            }

            bool WakewordDetector::isEnabled() const
            {
                return pImpl_->enabled_.load(std::memory_order_acquire);
            }

            bool WakewordDetector::isInitialized() const
            {
                return pImpl_->initialized_.load(std::memory_order_acquire);
            }

            void WakewordDetector::reset()
            {
                pImpl_->reset();
            }

            // ========================================================================
            // 音频处理
            // ========================================================================

            WakewordResult WakewordDetector::processAudioFrame(const int16_t* data, int length)
            {
                return pImpl_->processAudioFrame(data, length);
            }

            // ========================================================================
            // 参数设置
            // ========================================================================

            void WakewordDetector::setSensitivity(float sensitivity)
            {
                pImpl_->setSensitivity(sensitivity);
            }

            void WakewordDetector::setAudioGain(float gain)
            {
                pImpl_->setAudioGain(gain);
            }

            // ========================================================================
            // 回调设置
            // ========================================================================

            void WakewordDetector::setWakewordCallback(WakewordCallback callback)
            {
                pImpl_->setWakewordCallback(std::move(callback));
            }

            void WakewordDetector::setErrorCallback(WakewordErrorCallback callback)
            {
                pImpl_->setErrorCallback(std::move(callback));
            }

            // ========================================================================
            // 信息查询
            // ========================================================================

            int WakewordDetector::getSampleRate() const
            {
                return pImpl_->detector_ ? pImpl_->detector_->getSampleRate() : 0;
            }

            int WakewordDetector::getNumChannels() const
            {
                return pImpl_->detector_ ? pImpl_->detector_->getNumChannels() : 0;
            }

            int WakewordDetector::getBitsPerSample() const
            {
                return pImpl_->detector_ ? pImpl_->detector_->getBitsPerSample() : 0;
            }

            int WakewordDetector::getNumHotwords() const
            {
                return pImpl_->detector_ ? pImpl_->detector_->getNumHotwords() : 0;
            }

        } // namespace wakeword
    }     // namespace chatbot
} // namespace app
