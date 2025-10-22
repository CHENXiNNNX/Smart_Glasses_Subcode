/**
 * @file audiov2.cc
 * @brief 音频系统V2实现
 */

#include "audiov2.h"
#include "../../tool/log/log.h"
#include "../../../common/common.h"
#include <cstring>
#include <algorithm>

namespace app {
namespace media {
namespace audio {

using namespace tool::log;

// ============================================================================
// AudioFrame实现
// ============================================================================

// ============================================================================
// AudioMemoryPool::FixedPool实现（无锁固定池）
// ============================================================================

// FixedPool构造函数 - 支持动态大小
AudioMemoryPool::FixedPool::FixedPool(size_t block_count) 
    : actual_block_count(std::min(block_count, MAX_BLOCKS)) {
    
    // 初始化位图（所有位为0表示空闲）
    for (size_t i = 0; i < 8; i++) {
        allocation_bitmap_[i].store(0, std::memory_order_relaxed);
    }
    
    // 动态分配数据块和帧对象
    blocks.resize(actual_block_count);
    frame_objects.resize(actual_block_count);
    
    // 初始化帧对象
    for (size_t i = 0; i < actual_block_count; i++) {
        frame_objects[i].is_from_fixed_pool = true;
        frame_objects[i].fixed_pool_index = static_cast<int>(i);
    }
    
    LOG_INFO("AudioBuffer", "Fixed pool created: %zu blocks × %zu bytes = %.2f KB",
             actual_block_count, BLOCK_SIZE, (actual_block_count * BLOCK_SIZE) / 1024.0);
}

uint8_t* AudioMemoryPool::FixedPool::getBlockPtr(int index) {
    if (index < 0 || index >= static_cast<int>(actual_block_count)) {
        return nullptr;
    }
    return blocks[index].data();
}

int AudioMemoryPool::FixedPool::allocateBlock() {
    // 遍历位图（根据实际块数计算需要的位图数量）
    int bitmap_count = (actual_block_count + 63) / 64;  // 向上取整
    
    for (int bitmap_index = 0; bitmap_index < bitmap_count; bitmap_index++) {
        uint64_t bitmap = allocation_bitmap_[bitmap_index].load(std::memory_order_acquire);
        
        // 计算此位图管理的块范围
        int base_index = bitmap_index * 64;
        int max_blocks = std::min(64, static_cast<int>(actual_block_count) - base_index);
        
        if (max_blocks <= 0) break;
        
        // 查找空闲块
        while (bitmap != UINT64_MAX) {
            uint64_t inverted = ~bitmap;
            if (inverted == 0) break;
            
            int free_bit = __builtin_ctzll(inverted);
            if (free_bit >= max_blocks) break;
            
            // CAS尝试分配
            uint64_t new_bitmap = bitmap | (1ULL << free_bit);
            if (allocation_bitmap_[bitmap_index].compare_exchange_weak(bitmap, new_bitmap,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
                return base_index + free_bit;  // 返回全局索引
            }
            // CAS失败，bitmap已更新，继续循环
        }
    }
    
    return -1;  // 池满
}

void AudioMemoryPool::FixedPool::deallocateBlock(int index) {
    if (index < 0 || index >= static_cast<int>(actual_block_count)) {
        LOG_ERROR("AudioBuffer", "Invalid block index: %d", index);
        return;
    }
    
    // 计算位图索引和位位置
    int bitmap_index = index / 64;
    int bit_position = index % 64;
    
    // 原子清零对应位
    uint64_t mask = ~(1ULL << bit_position);
    allocation_bitmap_[bitmap_index].fetch_and(mask, std::memory_order_release);
}

// ============================================================================
// AudioMemoryPool实现（两级缓冲池）
// ============================================================================

AudioMemoryPool::AudioMemoryPool(const AudioMemoryPoolConfig& config)
    : config_(config) {
    
    LOG_INFO("AudioBuffer", "Initializing audio memory pool (2-tier)...");
    LOG_INFO("AudioBuffer", "  Fixed pool: %zu blocks × %zu bytes = %.2f KB",
             config_.fixed_block_count, config_.fixed_block_size,
             (config_.fixed_block_count * config_.fixed_block_size) / 1024.0);
    
    // 创建动态大小的固定池
    fixed_pool_ = std::make_unique<FixedPool>(config_.fixed_block_count);
    
    // 创建动态内存池（第二级）
    if (config_.dynamic_pool_size > 0) {
        dynamic_pool_ = std::make_unique<tool::memory::MemoryPool>(
            config_.dynamic_pool_size,  // initialSize
            64,                         // alignment
            2.0                         // expansionFactor
        );
        LOG_INFO("AudioBuffer", "  Dynamic pool: %.2f MB (initial)",
                 config_.dynamic_pool_size / (1024.0 * 1024.0));
    }
    
    LOG_INFO("AudioBuffer", "Audio memory pool initialized successfully");
}

AudioMemoryPool::~AudioMemoryPool() {
    LOG_INFO("AudioBuffer", "Destroying audio memory pool...");
    
    // 输出最终统计
    logStats();
    
    // 固定池和动态池会自动析构（unique_ptr）
    LOG_INFO("AudioBuffer", "Audio memory pool destroyed");
}

AudioFramePtr AudioMemoryPool::allocate(size_t size) {
    stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);
    
    // 第一级：尝试固定池（快速路径）
    if (size <= config_.fixed_block_size) {
        auto frame = allocateFromFixed(size);
        if (frame) {
            stats_.fixed_pool_hits.fetch_add(1, std::memory_order_relaxed);
            return frame;
        }
        
        // 固定池未命中
        LOG_WARN("AudioBuffer", "Fixed pool miss, using dynamic pool (size: %zu)", size);
    }
    
    // 第二级：动态池（慢速路径）
    auto frame = allocateFromDynamic(size);
    if (frame) {
        stats_.dynamic_pool_hits.fetch_add(1, std::memory_order_relaxed);
        return frame;
    }
    
    // 分配失败
    stats_.allocation_failures.fetch_add(1, std::memory_order_relaxed);
    LOG_ERROR("AudioBuffer", "Memory allocation failed: %zu bytes", size);
    return nullptr;
}

AudioFramePtr AudioMemoryPool::allocateFromFixed(size_t size) {
    if (size > config_.fixed_block_size) {
        return nullptr;
    }
    
    // 无锁分配
    int block_index = fixed_pool_->allocateBlock();
    if (block_index < 0) {
        return nullptr;  // 池满
    }
    
    // 使用对象池中的帧对象
    AudioFrame* frame = &fixed_pool_->frame_objects[block_index];
    frame->data = fixed_pool_->getBlockPtr(block_index);
    frame->capacity = FixedPool::BLOCK_SIZE;
    frame->size = size;
    frame->timestamp = get_nowus();
    frame->is_from_fixed_pool = true;
    frame->fixed_pool_index = block_index;
    
    // 创建智能指针，使用自定义删除器回收到固定池
    auto pool_ptr = fixed_pool_.get();
    return std::shared_ptr<AudioFrame>(frame, [pool_ptr](AudioFrame* f) {
        if (f && f->is_from_fixed_pool && f->fixed_pool_index >= 0) {
            pool_ptr->deallocateBlock(f->fixed_pool_index);
        }
        // 注意：不delete f，因为它来自对象池
    });
}

AudioFramePtr AudioMemoryPool::allocateFromDynamic(size_t size) {
    if (!dynamic_pool_) {
        LOG_ERROR("AudioBuffer", "Dynamic pool not initialized");
        return nullptr;
    }
    
    // 从动态池分配数据缓冲区
    void* buffer = dynamic_pool_->allocate(size);
    if (!buffer) {
        return nullptr;
    }
    
    // 使用自定义删除器确保正确释放动态池内存
    auto frame = std::shared_ptr<AudioFrame>(new AudioFrame(), 
        [this](AudioFrame* f) {
            // 先释放动态池内存，再删除AudioFrame对象
            if (f->data && dynamic_pool_) {
                dynamic_pool_->deallocate(f->data);
            }
            delete f;
        });
    
    frame->data = static_cast<uint8_t*>(buffer);
    frame->capacity = size;
    frame->size = size;
    frame->timestamp = get_nowus();
    frame->is_from_fixed_pool = false;
    frame->fixed_pool_index = -1;
    
    return frame;
}

// deallocate函数不再需要，所有清理工作由shared_ptr的deleter负责

void AudioMemoryPool::getStats(Stats& out_stats) const {
    out_stats.fixed_pool_hits.store(stats_.fixed_pool_hits.load());
    out_stats.dynamic_pool_hits.store(stats_.dynamic_pool_hits.load());
    out_stats.total_allocations.store(stats_.total_allocations.load());
    out_stats.allocation_failures.store(stats_.allocation_failures.load());
}

void AudioMemoryPool::resetStats() {
    stats_.fixed_pool_hits.store(0);
    stats_.dynamic_pool_hits.store(0);
    stats_.total_allocations.store(0);
    stats_.allocation_failures.store(0);
}

void AudioMemoryPool::logStats() const {
    uint64_t total = stats_.total_allocations.load();
    uint64_t fixed_hits = stats_.fixed_pool_hits.load();
    uint64_t dynamic_hits = stats_.dynamic_pool_hits.load();
    uint64_t failures = stats_.allocation_failures.load();
    
    if (total == 0) {
        LOG_INFO("AudioBuffer", "No allocations yet");
        return;
    }
    
    double fixed_rate = (double)fixed_hits / total * 100.0;
    double dynamic_rate = (double)dynamic_hits / total * 100.0;
    
    LOG_INFO("AudioBuffer", "=== Memory Pool Statistics ===");
    LOG_INFO("AudioBuffer", "  Total allocations: %llu", total);
    LOG_INFO("AudioBuffer", "  Fixed pool hits:   %llu (%.2f%%)", fixed_hits, fixed_rate);
    LOG_INFO("AudioBuffer", "  Dynamic pool hits: %llu (%.2f%%)", dynamic_hits, dynamic_rate);
    LOG_INFO("AudioBuffer", "  Failures:          %llu", failures);
    
    if (fixed_rate < 90.0) {
        LOG_WARN("AudioBuffer", "Fixed pool hit rate low, consider increasing pool size");
    }
}

// ============================================================================
// AudioSystemV2::Impl定义（Pimpl实现）
// ============================================================================

class AudioSystemV2::Impl {
public:
    // 配置
    AudioConfig config;
    
    // 状态机
    std::atomic<AudioMainState> main_state{AudioMainState::NONE};
    std::atomic<AudioControlState> control_state{AudioControlState::NONE};
    std::atomic<AudioFunctionState> function_state{AudioFunctionState::NONE};
    
    // 运行状态
    std::atomic<bool> initialized{false};
    std::atomic<bool> is_recording{false};
    std::atomic<bool> is_playing{false};
    std::atomic<bool> is_ai_streaming{false};
    std::atomic<bool> is_webrtc_streaming{false};
    
    // 音量控制
    std::atomic<float> output_volume{1.0f};
    
    // 内存池
    std::unique_ptr<AudioMemoryPool> mem_pool;
    
    // 编解码器（RAII智能指针）
    OpusEncoderPtr webrtc_encoder;    // 48kHz编码器（WebRTC）
    OpusDecoderPtr webrtc_decoder;    // 48kHz解码器（WebRTC）
    OpusDecoderPtr tts_decoder;       // 48kHz解码器（TTS）
    OpusEncoderPtr ai_encoder;        // 16kHz编码器（AI）
    
    // 重采样器（RAII智能指针）
    SrcStatePtr ai_resampler;         // 48kHz → 16kHz（AI）
    std::vector<int16_t> ai_resample_buffer;  // AI重采样累积缓冲区
    
    SrcStatePtr wakeword_resampler;   // 48kHz → 16kHz（唤醒词）
    std::vector<int16_t> wakeword_resample_buffer;  // 唤醒词重采样累积缓冲区
    
    // 3A算法（RAII智能指针）
    SpeexStatePtr speex_state;
    
    // PortAudio流（RAII智能指针）
    PaStreamPtr record_stream;
    PaStreamPtr playback_stream;
    
    // 音频帧队列（零拷贝，使用智能指针）
    std::queue<AudioFramePtr> record_queue;
    std::queue<AudioFramePtr> playback_queue;
    std::mutex record_queue_mutex;
    std::mutex playback_queue_mutex;
    std::condition_variable record_queue_cv;
    
    // 回调函数（线程安全）
    mutable std::mutex callback_mutex;
    AudioFrameCallback ai_audio_callback;
    AudioFrameCallback webrtc_audio_callback;
    WakewordCallback wakeword_callback;
    StateChangeCallback<AudioMainState> main_state_callback;
    StateChangeCallback<AudioControlState> control_state_callback;
    
    // 时间同步
    std::shared_ptr<sync_context_t> sync_ctx;
    
    // 统计信息
    Stats stats;
    
    // 临时缓冲区（预分配，避免回调中频繁分配）
    alignas(64) std::array<uint8_t, 4096> temp_opus_buffer;
    alignas(64) std::array<float, 2048> temp_float_buffer_in;
    alignas(64) std::array<float, 2048> temp_float_buffer_out;
    
    explicit Impl(const AudioConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("AudioSystemV2", "Impl created");
    }
    
    ~Impl() {
        LOG_DEBUG("AudioSystemV2", "Impl destroyed");
    }
    
    // 静态回调函数（C函数指针兼容）
    static int recordCallback(const void* inputBuffer, void* outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void* userData);
    
    static int playbackCallback(const void* inputBuffer, void* outputBuffer,
                               unsigned long framesPerBuffer,
                               const PaStreamCallbackTimeInfo* timeInfo,
                               PaStreamCallbackFlags statusFlags,
                               void* userData);
    
    // 状态设置（带回调）
    void setMainState(AudioMainState new_state) {
        AudioMainState old_state = main_state.exchange(new_state, std::memory_order_acq_rel);
        
        if (old_state != new_state) {
            LOG_INFO("AudioSystemV2", "Main State: %d → %d", 
                     static_cast<int>(old_state), static_cast<int>(new_state));
            
            // 调用回调（如果设置了）
            // 注意：使用try_lock避免死锁，如果获取锁失败就跳过回调
            std::unique_lock<std::mutex> lock(callback_mutex, std::try_to_lock);
            if (lock.owns_lock() && main_state_callback) {
                try {
                    main_state_callback(old_state, new_state);
                } catch (const std::runtime_error& e) {
                    LOG_ERROR("AudioSystemV2", "Main state callback runtime error: %s", e.what());
                } catch (const std::logic_error& e) {
                    LOG_ERROR("AudioSystemV2", "Main state callback logic error: %s", e.what());
                } catch (const std::exception& e) {
                    LOG_ERROR("AudioSystemV2", "Main state callback exception: %s", e.what());
                }
            } else if (!lock.owns_lock()) {
                LOG_DEBUG("AudioSystemV2", "Callback mutex busy, skipping main state callback");
            }
        }
    }
    
    void setControlState(AudioControlState new_state) {
        AudioControlState old_state = control_state.exchange(new_state, std::memory_order_acq_rel);
        
        if (old_state != new_state) {
            LOG_DEBUG("AudioSystemV2", "Control State: %d → %d",
                     static_cast<int>(old_state), static_cast<int>(new_state));
            
            // 使用try_lock避免死锁
            std::unique_lock<std::mutex> lock(callback_mutex, std::try_to_lock);
            if (lock.owns_lock() && control_state_callback) {
                try {
                    control_state_callback(old_state, new_state);
                } catch (const std::runtime_error& e) {
                    LOG_ERROR("AudioSystemV2", "Control state callback runtime error: %s", e.what());
                } catch (const std::logic_error& e) {
                    LOG_ERROR("AudioSystemV2", "Control state callback logic error: %s", e.what());
                } catch (const std::exception& e) {
                    LOG_ERROR("AudioSystemV2", "Control state callback exception: %s", e.what());
                }
            }
        }
    }
    
    // 线程安全的回调调用
    void invokeAICallback(AudioFramePtr frame) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (ai_audio_callback) {
            try {
                ai_audio_callback(frame);
            } catch (const std::runtime_error& e) {
                LOG_ERROR("AudioSystemV2", "AI callback runtime error: %s", e.what());
            } catch (const std::logic_error& e) {
                LOG_ERROR("AudioSystemV2", "AI callback logic error: %s", e.what());
            } catch (const std::exception& e) {
                LOG_ERROR("AudioSystemV2", "AI callback exception: %s", e.what());
            }
        }
    }
    
    void invokeWebRTCCallback(AudioFramePtr frame) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (webrtc_audio_callback) {
            try {
                webrtc_audio_callback(frame);
            } catch (const std::runtime_error& e) {
                LOG_ERROR("AudioSystemV2", "WebRTC callback runtime error: %s", e.what());
            } catch (const std::logic_error& e) {
                LOG_ERROR("AudioSystemV2", "WebRTC callback logic error: %s", e.what());
            } catch (const std::exception& e) {
                LOG_ERROR("AudioSystemV2", "WebRTC callback exception: %s", e.what());
            }
        }
    }
    
    void invokeWakewordCallback(const int16_t* data, size_t length) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (wakeword_callback) {
            try {
                wakeword_callback(data, length);
            } catch (const std::runtime_error& e) {
                LOG_ERROR("AudioSystemV2", "Wakeword callback runtime error: %s", e.what());
            } catch (const std::logic_error& e) {
                LOG_ERROR("AudioSystemV2", "Wakeword callback logic error: %s", e.what());
            } catch (const std::exception& e) {
                LOG_ERROR("AudioSystemV2", "Wakeword callback exception: %s", e.what());
            }
        }
    }
};

// ============================================================================
// AudioSystemV2公共接口实现
// ============================================================================

AudioSystemV2::AudioSystemV2(const AudioConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_INFO("AudioSystemV2", "AudioSystemV2 created");
}

AudioSystemV2::~AudioSystemV2() {
    shutdown();
    LOG_INFO("AudioSystemV2", "AudioSystemV2 destroyed");
}

AudioError AudioSystemV2::initialize(std::shared_ptr<sync_context_t> sync_ctx) {
    if (pImpl_->initialized.load()) {
        LOG_WARN("AudioSystemV2", "Already initialized");
        return AudioError::ALREADY_RUNNING;
    }
    
    LOG_INFO("AudioSystemV2", "========================================");
    LOG_INFO("AudioSystemV2", "Initializing Audio System V2...");
    LOG_INFO("AudioSystemV2", "========================================");
    
    pImpl_->sync_ctx = sync_ctx;
    
    // 1. 创建内存池
    LOG_INFO("AudioSystemV2", "Step 1: Creating memory pool...");
    pImpl_->mem_pool = std::make_unique<AudioMemoryPool>(pImpl_->config.mem_pool_config);
    
    // 2. 初始化PortAudio
    LOG_INFO("AudioSystemV2", "Step 2: Initializing PortAudio...");
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        LOG_ERROR("AudioSystemV2", "PortAudio init failed: %s", Pa_GetErrorText(err));
        return AudioError::INITIALIZE_FAILED;
    }
    LOG_INFO("AudioSystemV2", "  PortAudio version: %s", Pa_GetVersionText());
    
    // 3. 创建所有Opus编解码器和重采样器
    LOG_INFO("AudioSystemV2", "Step 3: Creating all Opus encoders/decoders and resamplers...");
    int opus_error;
    int src_error;
    bool all_initialized = true;
    
    // 3.1 创建WebRTC编码器（48kHz）
    OpusEncoder* webrtc_encoder = opus_encoder_create(
        pImpl_->config.sample_rate, 
        pImpl_->config.channels, 
        OPUS_APPLICATION_VOIP, 
        &opus_error
    );
    
    if (opus_error != OPUS_OK || !webrtc_encoder) {
        LOG_ERROR("AudioSystemV2", "WebRTC encoder create failed: %s", opus_strerror(opus_error));
        all_initialized = false;
    } else {
        pImpl_->webrtc_encoder.reset(webrtc_encoder);
        opus_encoder_ctl(pImpl_->webrtc_encoder.get(), OPUS_SET_BITRATE(64000));
        opus_encoder_ctl(pImpl_->webrtc_encoder.get(), OPUS_SET_VBR(1));
        opus_encoder_ctl(pImpl_->webrtc_encoder.get(), OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        LOG_INFO("AudioSystemV2", "  ✓ WebRTC encoder: 48kHz, 64kbps");
    }
    
    // 3.2 创建WebRTC解码器（48kHz）
    OpusDecoder* webrtc_decoder = opus_decoder_create(
        pImpl_->config.sample_rate,
        pImpl_->config.channels,
        &opus_error
    );
    
    if (opus_error != OPUS_OK || !webrtc_decoder) {
        LOG_ERROR("AudioSystemV2", "WebRTC decoder create failed: %s", opus_strerror(opus_error));
        all_initialized = false;
    } else {
        pImpl_->webrtc_decoder.reset(webrtc_decoder);
        LOG_INFO("AudioSystemV2", "  ✓ WebRTC decoder: 48kHz");
    }
    
    // 3.3 创建TTS解码器（48kHz）
    OpusDecoder* tts_decoder = opus_decoder_create(48000, 1, &opus_error);
    if (opus_error != OPUS_OK || !tts_decoder) {
        LOG_ERROR("AudioSystemV2", "TTS decoder create failed: %s", opus_strerror(opus_error));
        all_initialized = false;
    } else {
        pImpl_->tts_decoder.reset(tts_decoder);
        LOG_INFO("AudioSystemV2", "  ✓ TTS decoder: 48kHz");
    }
    
    // 3.4 创建AI编码器（16kHz）
    OpusEncoder* ai_encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &opus_error);
    if (opus_error != OPUS_OK || !ai_encoder) {
        LOG_ERROR("AudioSystemV2", "AI encoder create failed: %s", opus_strerror(opus_error));
        all_initialized = false;
    } else {
        pImpl_->ai_encoder.reset(ai_encoder);
        opus_encoder_ctl(pImpl_->ai_encoder.get(), OPUS_SET_BITRATE(32000));
        opus_encoder_ctl(pImpl_->ai_encoder.get(), OPUS_SET_VBR(1));
        opus_encoder_ctl(pImpl_->ai_encoder.get(), OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        LOG_INFO("AudioSystemV2", "  ✓ AI encoder: 16kHz, 32kbps");
    }
    
    // 3.5 创建AI重采样器（48kHz → 16kHz）
    SRC_STATE* ai_resampler = src_new(SRC_SINC_BEST_QUALITY, 1, &src_error);
    if (!ai_resampler) {
        LOG_ERROR("AudioSystemV2", "AI resampler create failed: %s", src_strerror(src_error));
        all_initialized = false;
    } else {
        pImpl_->ai_resampler.reset(ai_resampler);
        LOG_INFO("AudioSystemV2", "  ✓ AI resampler: 48kHz → 16kHz (BEST_QUALITY)");
    }
    
    // 3.6 创建唤醒词重采样器（48kHz → 16kHz）
    SRC_STATE* wakeword_resampler = src_new(SRC_SINC_FASTEST, 1, &src_error);
    if (!wakeword_resampler) {
        LOG_ERROR("AudioSystemV2", "Wakeword resampler create failed: %s", src_strerror(src_error));
        all_initialized = false;
    } else {
        pImpl_->wakeword_resampler.reset(wakeword_resampler);
        LOG_INFO("AudioSystemV2", "  ✓ Wakeword resampler: 48kHz → 16kHz (FASTEST)");
    }
    
    // 检查所有组件是否初始化成功
    if (!all_initialized) {
        LOG_ERROR("AudioSystemV2", "Some audio components failed to initialize");
        // 清理已创建的资源
        pImpl_->webrtc_encoder.reset();
        pImpl_->webrtc_decoder.reset();
        pImpl_->tts_decoder.reset();
        pImpl_->ai_encoder.reset();
        pImpl_->ai_resampler.reset();
        pImpl_->wakeword_resampler.reset();
        Pa_Terminate();
        return AudioError::INITIALIZE_FAILED;
    }
    
    LOG_INFO("AudioSystemV2", "  ✓ All audio components initialized successfully");
    
    // 8. 初始化3A算法
    if (pImpl_->config.enable_denoise || pImpl_->config.enable_agc) {
        LOG_INFO("AudioSystemV2", "Step 4: Initializing 3A algorithms...");
        
        int frame_size = pImpl_->config.sample_rate * pImpl_->config.frame_duration_ms / 1000;
        SpeexPreprocessState* speex = speex_preprocess_state_init(frame_size, pImpl_->config.sample_rate);
        
        if (!speex) {
            LOG_WARN("AudioSystemV2", "Speex preprocess init failed, 3A disabled");
        } else {
            pImpl_->speex_state.reset(speex);
            
            // 配置3A参数
            int denoise = pImpl_->config.enable_denoise ? 1 : 0;
            speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
            
            int agc = pImpl_->config.enable_agc ? 1 : 0;
            speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_AGC, &agc);
            
            if (pImpl_->config.enable_agc) {
                float agc_level = pImpl_->config.agc_level;
                speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_AGC_LEVEL, &agc_level);
            }
            
            LOG_INFO("AudioSystemV2", "  3A: Denoise=%s, AGC=%s",
                     denoise ? "ON" : "OFF", agc ? "ON" : "OFF");
        }
    }
    
    pImpl_->initialized.store(true);
    pImpl_->output_volume.store(pImpl_->config.output_volume);
    
    LOG_INFO("AudioSystemV2", "========================================");
    LOG_INFO("AudioSystemV2", "Audio System V2 initialized successfully!");
    LOG_INFO("AudioSystemV2", "========================================");
    
    return AudioError::NONE;
}

void AudioSystemV2::shutdown() {
    if (!pImpl_->initialized.load()) {
        return;
    }
    
    LOG_INFO("AudioSystemV2", "Shutting down Audio System V2...");
    
    // 停止录音和播放
    stopRecord();
    stopPlayback();
    
    // 清空队列
    clearRecordQueue();
    clearPlaybackQueue();
    
    // RAII智能指针会自动释放资源：
    // - webrtc_encoder, webrtc_decoder, ai_encoder, tts_decoder
    // - ai_resampler, wakeword_resampler
    // - speex_state
    // - record_stream, playback_stream
    
    // 终止PortAudio
    Pa_Terminate();
    
    // 内存池统计
    if (pImpl_->mem_pool) {
        pImpl_->mem_pool->logStats();
    }
    
    pImpl_->initialized.store(false);
    LOG_INFO("AudioSystemV2", "Audio System V2 shutdown complete");
}

bool AudioSystemV2::isInitialized() const {
    return pImpl_->initialized.load();
}

// ========================================================================
// 状态控制
// ========================================================================

AudioError AudioSystemV2::setMainState(AudioMainState state) {
    pImpl_->setMainState(state);
    return AudioError::NONE;
}

AudioMainState AudioSystemV2::getMainState() const {
    return pImpl_->main_state.load();
}

AudioError AudioSystemV2::setControlState(AudioControlState state) {
    pImpl_->setControlState(state);
    return AudioError::NONE;
}

AudioControlState AudioSystemV2::getControlState() const {
    return pImpl_->control_state.load();
}

AudioError AudioSystemV2::setFunctionState(AudioFunctionState state) {
    AudioFunctionState old_state = pImpl_->function_state.exchange(state);
    
    if (old_state != state) {
        LOG_DEBUG("AudioSystemV2", "Function State: %d → %d",
                 static_cast<int>(old_state), static_cast<int>(state));
    }
    
    return AudioError::NONE;
}

AudioFunctionState AudioSystemV2::getFunctionState() const {
    return pImpl_->function_state.load();
}

// ========================================================================
// 音量控制
// ========================================================================

void AudioSystemV2::setOutputVolume(float volume) {
    // 限制音量范围 [0.0, 2.0]
    volume = std::max(0.0f, std::min(2.0f, volume));
    pImpl_->output_volume.store(volume, std::memory_order_relaxed);
    LOG_INFO("AudioSystemV2", "Output volume set to: %.2f", volume);
}

float AudioSystemV2::getOutputVolume() const {
    return pImpl_->output_volume.load(std::memory_order_relaxed);
}

// ========================================================================
// 回调设置（线程安全）
// ========================================================================

void AudioSystemV2::setAIAudioCallback(AudioFrameCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->ai_audio_callback = callback;
    LOG_DEBUG("AudioSystemV2", "AI audio callback set");
}

void AudioSystemV2::setWebRTCAudioCallback(AudioFrameCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->webrtc_audio_callback = callback;
    LOG_DEBUG("AudioSystemV2", "WebRTC audio callback set");
}

void AudioSystemV2::setWakewordCallback(WakewordCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->wakeword_callback = callback;
    LOG_DEBUG("AudioSystemV2", "Wakeword callback set");
}

void AudioSystemV2::setMainStateCallback(StateChangeCallback<AudioMainState> callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->main_state_callback = callback;
}

void AudioSystemV2::setControlStateCallback(StateChangeCallback<AudioControlState> callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->control_state_callback = callback;
}

// ========================================================================
// 队列操作
// ========================================================================

void AudioSystemV2::clearRecordQueue() {
    std::lock_guard<std::mutex> lock(pImpl_->record_queue_mutex);
    std::queue<AudioFramePtr> empty;
    std::swap(pImpl_->record_queue, empty);
    LOG_DEBUG("AudioSystemV2", "Record queue cleared");
}

void AudioSystemV2::clearPlaybackQueue() {
    std::lock_guard<std::mutex> lock(pImpl_->playback_queue_mutex);
    std::queue<AudioFramePtr> empty;
    std::swap(pImpl_->playback_queue, empty);
    LOG_DEBUG("AudioSystemV2", "Playback queue cleared");
}

AudioFramePtr AudioSystemV2::getRecordedFrame(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(pImpl_->record_queue_mutex);
    
    // 等待队列非空或超时
    if (pImpl_->record_queue.empty()) {
        if (!pImpl_->record_queue_cv.wait_for(lock, timeout, [this]() {
            return !pImpl_->record_queue.empty() || !pImpl_->is_recording.load();
        })) {
            return nullptr;  // 超时
        }
    }
    
    if (pImpl_->record_queue.empty()) {
        return nullptr;
    }
    
    // 取出帧（零拷贝，只移动智能指针）
    AudioFramePtr frame = std::move(pImpl_->record_queue.front());
    pImpl_->record_queue.pop();
    
    return frame;
}

void AudioSystemV2::pushPlaybackFrame(AudioFramePtr frame) {
    if (!frame) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(pImpl_->playback_queue_mutex);
    
    // 检查队列长度
    if (pImpl_->playback_queue.size() >= pImpl_->config.max_playback_queue_size) {
        pImpl_->playback_queue.pop();  // 丢弃最旧的帧
        pImpl_->stats.frames_dropped.fetch_add(1);
        LOG_WARN("AudioSystemV2", "Playback queue full, dropped oldest frame");
    }
    
    // 推送帧（零拷贝，智能指针移动）
    pImpl_->playback_queue.push(std::move(frame));
}

// ========================================================================
// 统计信息
// ========================================================================

void AudioSystemV2::getStats(Stats& out_stats) const {
    out_stats.frames_recorded.store(pImpl_->stats.frames_recorded.load());
    out_stats.frames_played.store(pImpl_->stats.frames_played.load());
    out_stats.frames_dropped.store(pImpl_->stats.frames_dropped.load());
    out_stats.encode_count.store(pImpl_->stats.encode_count.load());
    out_stats.decode_count.store(pImpl_->stats.decode_count.load());
    
    if (pImpl_->mem_pool) {
        pImpl_->mem_pool->getStats(out_stats.mem_stats);
    }
}

void AudioSystemV2::resetStats() {
    pImpl_->stats.frames_recorded.store(0);
    pImpl_->stats.frames_played.store(0);
    pImpl_->stats.frames_dropped.store(0);
    pImpl_->stats.encode_count.store(0);
    pImpl_->stats.decode_count.store(0);
    
    if (pImpl_->mem_pool) {
        pImpl_->mem_pool->resetStats();
    }
    
    LOG_INFO("AudioSystemV2", "Stats reset");
}

void AudioSystemV2::logStats() const {
    LOG_INFO("AudioSystemV2", "=== Audio System V2 Statistics ===");
    LOG_INFO("AudioSystemV2", "  Frames recorded: %llu", pImpl_->stats.frames_recorded.load());
    LOG_INFO("AudioSystemV2", "  Frames played:   %llu", pImpl_->stats.frames_played.load());
    LOG_INFO("AudioSystemV2", "  Frames dropped:  %llu", pImpl_->stats.frames_dropped.load());
    LOG_INFO("AudioSystemV2", "  Encode count:    %llu", pImpl_->stats.encode_count.load());
    LOG_INFO("AudioSystemV2", "  Decode count:    %llu", pImpl_->stats.decode_count.load());
    
    if (pImpl_->mem_pool) {
        pImpl_->mem_pool->logStats();
    }
}

// ============================================================================
// Impl静态回调函数实现
// ============================================================================

int AudioSystemV2::Impl::recordCallback(const void* inputBuffer, void* outputBuffer,
                                        unsigned long framesPerBuffer,
                                        const PaStreamCallbackTimeInfo* timeInfo,
                                        PaStreamCallbackFlags statusFlags,
                                        void* userData) {
    (void)outputBuffer;
    (void)timeInfo;
    (void)statusFlags;
    
    auto* impl = static_cast<AudioSystemV2::Impl*>(userData);
    const int16_t* input = static_cast<const int16_t*>(inputBuffer);
    size_t frame_size_bytes = framesPerBuffer * impl->config.channels * sizeof(int16_t);
    
    // ✅ 优化1：从内存池分配（<50ns）
    auto frame = impl->mem_pool->allocate(frame_size_bytes);
    if (!frame) {
        LOG_ERROR("AudioCallback", "Failed to allocate frame");
        return paContinue;
    }
    
    // ✅ 优化2：一次拷贝
    std::memcpy(frame->data, input, frame_size_bytes);
    frame->size = frame_size_bytes;
    frame->timestamp = get_nowus();
    
    // 3A算法处理
    if (impl->speex_state) {
        speex_preprocess_run(impl->speex_state.get(), frame->getData<int16_t>());
    }
    
    // 唤醒词检测（仅在非AI流式传输时，需要16kHz音频）
    if (!impl->is_ai_streaming.load() && impl->wakeword_callback && impl->wakeword_resampler) {
        // 重采样 48kHz → 16kHz
        double src_ratio = 16000.0 / 48000.0;  // 1/3
        SRC_DATA src_data;
        
        // 使用预分配的临时缓冲区
        float* input_float = impl->temp_float_buffer_in.data();
        float* output_float = impl->temp_float_buffer_out.data();
        
        // int16 → float
        const int16_t* pcm_data = frame->getData<int16_t>();
        for (size_t i = 0; i < framesPerBuffer; i++) {
            input_float[i] = static_cast<float>(pcm_data[i]) / 32768.0f;
        }
        
        // 重采样
        src_data.data_in = input_float;
        src_data.input_frames = framesPerBuffer;
        src_data.data_out = output_float;
        src_data.output_frames = impl->temp_float_buffer_out.size();
        src_data.src_ratio = src_ratio;
        src_data.end_of_input = 0;
        
        int resample_error = src_process(impl->wakeword_resampler.get(), &src_data);
        if (resample_error == 0 && src_data.output_frames_gen > 0) {
            // 缓冲区大小限制
            const size_t MAX_WAKEWORD_BUFFER_SIZE = 16000;  // 1秒@16kHz
            
            // float → int16，添加到累积缓冲区（带溢出保护）
            for (long i = 0; i < src_data.output_frames_gen; i++) {
                if (impl->wakeword_resample_buffer.size() >= MAX_WAKEWORD_BUFFER_SIZE) {
                    LOG_WARN("AudioCallback", "Wakeword resample buffer overflow, dropping samples");
                    break;
                }
                
                float sample = std::max(-1.0f, std::min(1.0f, output_float[i]));
                impl->wakeword_resample_buffer.push_back(static_cast<int16_t>(sample * 32767.0f));
            }
            
            // 当累积到320样本（16kHz 20ms）时，传递给唤醒词检测
            const int TARGET_FRAME_SIZE = 320;  // 16kHz 20ms = 320 samples
            while (impl->wakeword_resample_buffer.size() >= static_cast<size_t>(TARGET_FRAME_SIZE)) {
                // 传递给唤醒词检测（16kHz音频）
                impl->invokeWakewordCallback(
                    impl->wakeword_resample_buffer.data(), 
                    TARGET_FRAME_SIZE
                );
                
                // 移除已处理的样本
                impl->wakeword_resample_buffer.erase(
                    impl->wakeword_resample_buffer.begin(),
                    impl->wakeword_resample_buffer.begin() + TARGET_FRAME_SIZE
                );
            }
        }
    }
    
    // 添加到录音队列（零拷贝，智能指针）
    {
        std::lock_guard<std::mutex> lock(impl->record_queue_mutex);
        
        if (impl->record_queue.size() >= impl->config.max_record_queue_size) {
            impl->record_queue.pop();
            impl->stats.frames_dropped.fetch_add(1);
        }
        
        impl->record_queue.push(frame);  // shared_ptr拷贝，引用计数+1
    }
    impl->record_queue_cv.notify_one();
    impl->stats.frames_recorded.fetch_add(1);
    
    // WebRTC音频发送（48kHz Opus编码）
    if (impl->main_state.load() == AudioMainState::WEBRTC && 
        impl->is_webrtc_streaming.load() &&
        impl->webrtc_encoder) {
        
        // 使用WebRTC编码器编码（48kHz）
        uint8_t* opus_buffer = impl->temp_opus_buffer.data();
        int encoded_bytes = opus_encode(
            impl->webrtc_encoder.get(),
            frame->getData<int16_t>(),
            framesPerBuffer,
            opus_buffer,
            impl->temp_opus_buffer.size()
        );
        
        if (encoded_bytes > 0) {
            // 分配帧并拷贝编码数据
            auto encoded_frame = impl->mem_pool->allocate(encoded_bytes);
            if (encoded_frame) {
                std::memcpy(encoded_frame->data, opus_buffer, encoded_bytes);
                encoded_frame->size = encoded_bytes;
                
                // 获取同步后的时间戳
                if (impl->sync_ctx) {
                    encoded_frame->timestamp = sync_get_timestamp(
                        impl->sync_ctx.get(), 
                        get_nowus(), 
                        true
                    );
                } else {
                    encoded_frame->timestamp = get_nowus();
                }
                
                // 调用WebRTC回调
                impl->invokeWebRTCCallback(encoded_frame);
                impl->stats.encode_count.fetch_add(1);
            }
        }
    }
    
    // AI音频发送（48kHz → 16kHz → Opus编码）
    static std::atomic<int> ai_frame_count{0};
    static std::atomic<uint64_t> last_ai_log_time{0};
    
    bool main_state_ok = (impl->main_state.load() == AudioMainState::AI);
    bool streaming_ok = impl->is_ai_streaming.load();
    bool encoder_ok = (impl->ai_encoder != nullptr);
    bool resampler_ok = (impl->ai_resampler != nullptr);
    
    if (main_state_ok && streaming_ok && encoder_ok && resampler_ok) {
        // 每100帧打印一次（约2秒）
        if (ai_frame_count.fetch_add(1) % 100 == 0) {
            uint64_t now = get_nowus();
            if (now - last_ai_log_time.load() > 2000000) {  // 2秒
                LOG_DEBUG("AudioCallback", "AI audio processing... (frame %d)", ai_frame_count.load());
                last_ai_log_time.store(now);
            }
        }
        
        // 1. 重采样 48kHz → 16kHz
        double src_ratio = 16000.0 / 48000.0;  // 1/3
        SRC_DATA src_data;
        
        // 使用预分配的临时缓冲区
        float* input_float = impl->temp_float_buffer_in.data();
        float* output_float = impl->temp_float_buffer_out.data();
        
        // int16 → float
        const int16_t* pcm_data = frame->getData<int16_t>();
        for (size_t i = 0; i < framesPerBuffer; i++) {
            input_float[i] = static_cast<float>(pcm_data[i]) / 32768.0f;
        }
        
        // 重采样
        src_data.data_in = input_float;
        src_data.input_frames = framesPerBuffer;
        src_data.data_out = output_float;
        src_data.output_frames = impl->temp_float_buffer_out.size();
        src_data.src_ratio = src_ratio;
        src_data.end_of_input = 0;
        
        int resample_error = src_process(impl->ai_resampler.get(), &src_data);
        if (resample_error == 0) {
            // 缓冲区大小限制
            const size_t MAX_RESAMPLE_BUFFER_SIZE = 16000;  // 1秒@16kHz
            
            // float → int16，添加到累积缓冲区（带溢出保护）
            for (long i = 0; i < src_data.output_frames_gen; i++) {
                if (impl->ai_resample_buffer.size() >= MAX_RESAMPLE_BUFFER_SIZE) {
                    LOG_WARN("AudioCallback", "AI resample buffer overflow, dropping samples");
                    break;  // 停止添加更多样本
                }
                
                float sample = std::max(-1.0f, std::min(1.0f, output_float[i]));
                impl->ai_resample_buffer.push_back(static_cast<int16_t>(sample * 32767.0f));
            }
            
            // 2. 当累积到320样本（16kHz 20ms）时，进行Opus编码
            const int TARGET_FRAME_SIZE = 320;
            while (impl->ai_resample_buffer.size() >= static_cast<size_t>(TARGET_FRAME_SIZE)) {
                // 编码320样本
                uint8_t* opus_buffer = impl->temp_opus_buffer.data();
                int encoded_bytes = opus_encode(
                    impl->ai_encoder.get(),
                    impl->ai_resample_buffer.data(),
                    TARGET_FRAME_SIZE,
                    opus_buffer,
                    impl->temp_opus_buffer.size()
                );
                
                if (encoded_bytes > 0) {
                    // 分配帧并拷贝编码数据
                    auto encoded_frame = impl->mem_pool->allocate(encoded_bytes);
                    if (encoded_frame) {
                        std::memcpy(encoded_frame->data, opus_buffer, encoded_bytes);
                        encoded_frame->size = encoded_bytes;
                        encoded_frame->timestamp = get_nowus();
                        
                        // 3. 调用AI回调
                        impl->invokeAICallback(encoded_frame);
                        impl->stats.encode_count.fetch_add(1);
                    }
                }
                
                // 移除已编码的样本
                impl->ai_resample_buffer.erase(
                    impl->ai_resample_buffer.begin(),
                    impl->ai_resample_buffer.begin() + TARGET_FRAME_SIZE
                );
            }
        }
    }
    
    return paContinue;
}

int AudioSystemV2::Impl::playbackCallback(const void* inputBuffer, void* outputBuffer,
                                          unsigned long framesPerBuffer,
                                          const PaStreamCallbackTimeInfo* timeInfo,
                                          PaStreamCallbackFlags statusFlags,
                                          void* userData) {
    (void)inputBuffer;
    (void)timeInfo;
    (void)statusFlags;
    
    auto* impl = static_cast<AudioSystemV2::Impl*>(userData);
    int16_t* output = static_cast<int16_t*>(outputBuffer);
    size_t samples_needed = framesPerBuffer * impl->config.channels;
    
    std::lock_guard<std::mutex> lock(impl->playback_queue_mutex);
    
    if (impl->playback_queue.empty()) {
        // 队列空，填充静音
        std::fill(output, output + samples_needed, 0);
        return paContinue;
    }
    
    // 取出帧（零拷贝引用）
    AudioFramePtr& frame = impl->playback_queue.front();
    size_t frame_samples = frame->size / sizeof(int16_t);
    size_t samples_to_copy = std::min(samples_needed, frame_samples);
    
    // 拷贝数据并应用音量
    float volume = impl->output_volume.load(std::memory_order_relaxed);
    const int16_t* src = frame->getData<int16_t>();
    
    for (size_t i = 0; i < samples_to_copy; i++) {
        output[i] = static_cast<int16_t>(src[i] * volume);
    }
    
    // 填充剩余部分为静音
    if (samples_to_copy < samples_needed) {
        std::fill(output + samples_to_copy, output + samples_needed, 0);
    }
    
    // 移除已播放的帧
    impl->playback_queue.pop();
    impl->stats.frames_played.fetch_add(1);
    
    return paContinue;
}

// ============================================================================
// 录音/播放控制
// ============================================================================

AudioError AudioSystemV2::startRecord() {
    if (!pImpl_->initialized.load()) {
        LOG_ERROR("AudioSystemV2", "Not initialized");
        return AudioError::NOT_INITIALIZED;
    }
    
    if (pImpl_->is_recording.load()) {
        LOG_WARN("AudioSystemV2", "Already recording");
        return AudioError::ALREADY_RUNNING;
    }
    
    LOG_INFO("AudioSystemV2", "Starting recording...");
    
    // 配置输入参数
    PaStreamParameters inputParams;
    inputParams.device = Pa_GetDefaultInputDevice();
    if (inputParams.device == paNoDevice) {
        LOG_ERROR("AudioSystemV2", "No input device found");
        return AudioError::DEVICE_NOT_FOUND;
    }
    
    inputParams.channelCount = pImpl_->config.channels;
    inputParams.sampleFormat = paInt16;
    inputParams.suggestedLatency = Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;
    
    // 打开流
    PaStream* stream = nullptr;
    int frames_per_buffer = pImpl_->config.sample_rate / 1000 * pImpl_->config.frame_duration_ms;
    
    PaError err = Pa_OpenStream(
        &stream,
        &inputParams,
        nullptr,  // 无输出
        pImpl_->config.sample_rate,
        frames_per_buffer,
        paClipOff,
        Impl::recordCallback,
        pImpl_.get()
    );
    
    if (err != paNoError) {
        LOG_ERROR("AudioSystemV2", "Failed to open record stream: %s", Pa_GetErrorText(err));
        return AudioError::STREAM_OPEN_FAILED;
    }
    
    // 启动流
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        LOG_ERROR("AudioSystemV2", "Failed to start record stream: %s", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        return AudioError::STREAM_START_FAILED;
    }
    
    pImpl_->record_stream.reset(stream);
    pImpl_->is_recording.store(true);
    pImpl_->setControlState(AudioControlState::RECORD);
    
    LOG_INFO("AudioSystemV2", "Recording started (device: %d, %dHz, %dch)",
             inputParams.device, pImpl_->config.sample_rate, pImpl_->config.channels);
    
    return AudioError::NONE;
}

AudioError AudioSystemV2::stopRecord() {
    if (!pImpl_->is_recording.load()) {
        return AudioError::NONE;
    }
    
    LOG_INFO("AudioSystemV2", "Stopping recording...");
    
    pImpl_->is_recording.store(false);
    pImpl_->record_stream.reset();  // RAII自动stop和close
    pImpl_->setControlState(AudioControlState::NONE);
    
    // 清理重采样缓冲区
    if (!pImpl_->ai_resample_buffer.empty()) {
        pImpl_->ai_resample_buffer.clear();
        LOG_INFO("AudioSystemV2", "AI resample buffer cleared safely");
    }
    
    if (!pImpl_->wakeword_resample_buffer.empty()) {
        pImpl_->wakeword_resample_buffer.clear();
        LOG_DEBUG("AudioSystemV2", "Wakeword resample buffer cleared");
    }
    
    LOG_INFO("AudioSystemV2", "Recording stopped");
    return AudioError::NONE;
}

bool AudioSystemV2::isRecording() const {
    return pImpl_->is_recording.load();
}

AudioError AudioSystemV2::startPlayback() {
    if (!pImpl_->initialized.load()) {
        LOG_ERROR("AudioSystemV2", "Not initialized");
        return AudioError::NOT_INITIALIZED;
    }
    
    if (pImpl_->is_playing.load()) {
        LOG_WARN("AudioSystemV2", "Already playing");
        return AudioError::ALREADY_RUNNING;
    }
    
    LOG_INFO("AudioSystemV2", "Starting playback...");
    
    // 配置输出参数
    PaStreamParameters outputParams;
    outputParams.device = Pa_GetDefaultOutputDevice();
    if (outputParams.device == paNoDevice) {
        LOG_ERROR("AudioSystemV2", "No output device found");
        return AudioError::DEVICE_NOT_FOUND;
    }
    
    outputParams.channelCount = pImpl_->config.channels;
    outputParams.sampleFormat = paInt16;
    outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;
    
    // 打开流
    PaStream* stream = nullptr;
    int frames_per_buffer = pImpl_->config.sample_rate / 1000 * pImpl_->config.frame_duration_ms;
    
    PaError err = Pa_OpenStream(
        &stream,
        nullptr,  // 无输入
        &outputParams,
        pImpl_->config.sample_rate,
        frames_per_buffer,
        paClipOff,
        Impl::playbackCallback,
        pImpl_.get()
    );
    
    if (err != paNoError) {
        LOG_ERROR("AudioSystemV2", "Failed to open playback stream: %s", Pa_GetErrorText(err));
        return AudioError::STREAM_OPEN_FAILED;
    }
    
    // 启动流
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        LOG_ERROR("AudioSystemV2", "Failed to start playback stream: %s", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        return AudioError::STREAM_START_FAILED;
    }
    
    pImpl_->playback_stream.reset(stream);
    pImpl_->is_playing.store(true);
    pImpl_->setControlState(AudioControlState::PLAYBACK);
    
    LOG_INFO("AudioSystemV2", "Playback started (device: %d)", outputParams.device);
    
    return AudioError::NONE;
}

AudioError AudioSystemV2::stopPlayback() {
    if (!pImpl_->is_playing.load()) {
        return AudioError::NONE;
    }
    
    LOG_INFO("AudioSystemV2", "Stopping playback...");
    
    pImpl_->is_playing.store(false);
    pImpl_->playback_stream.reset();  // RAII自动stop和close
    pImpl_->setControlState(AudioControlState::NONE);
    
    LOG_INFO("AudioSystemV2", "Playback stopped");
    return AudioError::NONE;
}

bool AudioSystemV2::isPlaying() const {
    return pImpl_->is_playing.load();
}

// ============================================================================
// 编解码实现
// ============================================================================

AudioFramePtr AudioSystemV2::encodeOpus(const int16_t* pcm_data, size_t pcm_size) {
    if (!pImpl_->webrtc_encoder) {
        LOG_ERROR("AudioSystemV2", "WebRTC encoder not initialized");
        return nullptr;
    }
    
    // 计算样本数
    int frame_size = pcm_size / sizeof(int16_t) / pImpl_->config.channels;
    
    // 分配输出帧
    auto encoded_frame = pImpl_->mem_pool->allocate(4096);  // Opus最大4KB
    if (!encoded_frame) {
        LOG_ERROR("AudioSystemV2", "Failed to allocate encode buffer");
        return nullptr;
    }
    
    // 编码
    int encoded_bytes = opus_encode(
        pImpl_->webrtc_encoder.get(),
        pcm_data,
        frame_size,
        encoded_frame->data,
        encoded_frame->capacity
    );
    
    if (encoded_bytes < 0) {
        LOG_ERROR("AudioSystemV2", "Opus encode failed: %s", opus_strerror(encoded_bytes));
        return nullptr;
    }
    
    encoded_frame->size = encoded_bytes;
    pImpl_->stats.encode_count.fetch_add(1);
    
    return encoded_frame;
}

AudioFramePtr AudioSystemV2::decodeOpus(const uint8_t* opus_data, size_t opus_size) {
    // 优先使用TTS解码器（48kHz），如果没有则使用主解码器（48kHz）
    OpusDecoder* decoder = nullptr;
    int target_sample_rate = 0;
    
    if (pImpl_->tts_decoder) {
        decoder = pImpl_->tts_decoder.get();
        target_sample_rate = 48000;
    } else if (pImpl_->webrtc_decoder) {
        decoder = pImpl_->webrtc_decoder.get();
        target_sample_rate = pImpl_->config.sample_rate;
    } else {
        LOG_ERROR("AudioSystemV2", "No decoder available");
        return nullptr;
    }
    
    // 打印解码器配置（仅第一次）
    static std::atomic<bool> first_decode{true};
    if (first_decode.exchange(false)) {
        LOG_INFO("AudioSystemV2", "🔊 TTS Decoder Config:");
        LOG_INFO("AudioSystemV2", "  Using: %d Hz decoder", target_sample_rate);
        LOG_INFO("AudioSystemV2", "  Channels: %d", pImpl_->config.channels);
        LOG_INFO("AudioSystemV2", "  Frame Duration: %d ms", pImpl_->config.frame_duration_ms);
    }
    
    // 计算PCM帧大小（使用目标采样率）
    int frame_size = target_sample_rate / 1000 * pImpl_->config.frame_duration_ms;
    size_t pcm_size = frame_size * pImpl_->config.channels * sizeof(int16_t);

    
    // 分配输出帧
    auto decoded_frame = pImpl_->mem_pool->allocate(pcm_size);
    if (!decoded_frame) {
        LOG_ERROR("AudioSystemV2", "Failed to allocate decode buffer");
        return nullptr;
    }
    
    // 解码
    int decoded_samples = opus_decode(
        decoder,
        opus_data,
        opus_size,
        decoded_frame->getData<int16_t>(),
        frame_size,
        0
    );
    
    if (decoded_samples < 0) {
        LOG_ERROR("AudioSystemV2", "Opus decode failed: %s (opus_size=%zu)", 
                  opus_strerror(decoded_samples), opus_size);
        return nullptr;
    }
    
    // 检查解码结果
    if (decoded_samples != frame_size) {
        LOG_WARN("AudioSystemV2", "Decoded samples mismatch: got %d, expected %d", 
                 decoded_samples, frame_size);
    }
    
    // 打印PCM样本统计（第一个和最后10个包）
    static std::atomic<int> decode_count{0};
    int count = decode_count.fetch_add(1);
    if (count < 10) {
        int16_t* pcm = decoded_frame->getData<int16_t>();
        int16_t max_val = 0, min_val = 0;
        for (int i = 0; i < decoded_samples; i++) {
            if (pcm[i] > max_val) max_val = pcm[i];
            if (pcm[i] < min_val) min_val = pcm[i];
        }
        LOG_INFO("AudioSystemV2", "  PCM #%d: samples=%d, range=[%d, %d]", 
                 count, decoded_samples, min_val, max_val);
    }
    
    decoded_frame->size = decoded_samples * pImpl_->config.channels * sizeof(int16_t);
    pImpl_->stats.decode_count.fetch_add(1);
    
    return decoded_frame;
}

size_t AudioSystemV2::encodeOpusFrames(const int16_t* pcm_data, size_t pcm_size, 
                                       std::vector<AudioFramePtr>& frames) {
    if (!pImpl_->webrtc_encoder) {
        LOG_ERROR("AudioSystemV2", "WebRTC encoder not initialized");
        return 0;
    }
    
    // 计算单帧样本数
    int samples_per_frame = pImpl_->config.sample_rate / 1000 * pImpl_->config.frame_duration_ms;
    size_t total_samples = pcm_size / sizeof(int16_t) / pImpl_->config.channels;
    
    size_t encoded_count = 0;
    size_t offset = 0;
    
    // 分帧编码
    while (offset < total_samples) {
        int current_frame_size = std::min(
            samples_per_frame, 
            static_cast<int>(total_samples - offset)
        );
        
        // 分配输出帧
        auto encoded_frame = pImpl_->mem_pool->allocate(4096);
        if (!encoded_frame) {
            LOG_ERROR("AudioSystemV2", "Failed to allocate encode buffer");
            break;
        }
        
        // 编码
        int encoded_bytes = opus_encode(
            pImpl_->webrtc_encoder.get(),
            pcm_data + offset,
            current_frame_size,
            encoded_frame->data,
            encoded_frame->capacity
        );
        
        if (encoded_bytes > 0) {
            encoded_frame->size = encoded_bytes;
            encoded_frame->timestamp = get_nowus();
            frames.push_back(encoded_frame);
            encoded_count++;
            pImpl_->stats.encode_count.fetch_add(1);
        } else {
            LOG_ERROR("AudioSystemV2", "Frame encode failed: %s", opus_strerror(encoded_bytes));
        }
        
        offset += current_frame_size;
    }
    
    LOG_DEBUG("AudioSystemV2", "Encoded %zu samples into %zu frames", total_samples, encoded_count);
    return encoded_count;
}

// ============================================================================
// AI/WebRTC音频流管理
// ============================================================================

AudioError AudioSystemV2::startAIStream() {
    if (!pImpl_->initialized.load()) {
        LOG_ERROR("AudioSystemV2", "Not initialized");
        return AudioError::NOT_INITIALIZED;
    }
    
    if (pImpl_->is_ai_streaming.load()) {
        LOG_WARN("AudioSystemV2", "AI stream already started");
        return AudioError::ALREADY_RUNNING;
    }
    
    LOG_INFO("AudioSystemV2", "Starting AI audio stream...");
    
    // 设置主状态为AI
    LOG_DEBUG("AudioSystemV2", "  Step 1: Setting main state to AI...");
    setMainState(AudioMainState::AI);
    LOG_DEBUG("AudioSystemV2", "  Step 1: ✓ Main state set");
    
    // 标记为流式传输
    LOG_DEBUG("AudioSystemV2", "  Step 2: Setting streaming flag...");
    pImpl_->is_ai_streaming.store(true);
    LOG_DEBUG("AudioSystemV2", "  Step 2: ✓ Streaming flag set (is_ai_streaming=%d)", 
              pImpl_->is_ai_streaming.load());
    
    // 验证条件
    LOG_DEBUG("AudioSystemV2", "  Verification:");
    LOG_DEBUG("AudioSystemV2", "    - main_state: %d", static_cast<int>(pImpl_->main_state.load()));
    LOG_DEBUG("AudioSystemV2", "    - is_ai_streaming: %d", pImpl_->is_ai_streaming.load());
    LOG_DEBUG("AudioSystemV2", "    - ai_encoder: %p", pImpl_->ai_encoder.get());
    LOG_DEBUG("AudioSystemV2", "    - ai_resampler: %p", pImpl_->ai_resampler.get());
    
    LOG_INFO("AudioSystemV2", "AI audio stream started");
    return AudioError::NONE;
}

AudioError AudioSystemV2::stopAIStream() {
    if (!pImpl_->is_ai_streaming.load()) {
        return AudioError::NONE;
    }
    
    LOG_INFO("AudioSystemV2", "Stopping AI audio stream...");
    
    // 停止AI流标志，让录音回调不再处理AI数据
    pImpl_->is_ai_streaming.store(false);
    
    
    LOG_INFO("AudioSystemV2", "AI audio stream stopped");
    return AudioError::NONE;
}

bool AudioSystemV2::isAIStreamActive() const {
    return pImpl_->is_ai_streaming.load();
}

AudioError AudioSystemV2::startWebRTCStream() {
    if (!pImpl_->initialized.load()) {
        LOG_ERROR("AudioSystemV2", "Not initialized");
        return AudioError::NOT_INITIALIZED;
    }
    
    if (pImpl_->is_webrtc_streaming.load()) {
        LOG_WARN("AudioSystemV2", "WebRTC stream already started");
        return AudioError::ALREADY_RUNNING;
    }
    
    LOG_INFO("AudioSystemV2", "Starting WebRTC audio stream...");
    
    // 设置主状态为WebRTC
    setMainState(AudioMainState::WEBRTC);
    
    // 标记为流式传输
    pImpl_->is_webrtc_streaming.store(true);
    
    LOG_INFO("AudioSystemV2", "WebRTC audio stream started");
    return AudioError::NONE;
}

AudioError AudioSystemV2::stopWebRTCStream() {
    if (!pImpl_->is_webrtc_streaming.load()) {
        return AudioError::NONE;
    }
    
    LOG_INFO("AudioSystemV2", "Stopping WebRTC audio stream...");
    
    pImpl_->is_webrtc_streaming.store(false);
    
    LOG_INFO("AudioSystemV2", "WebRTC audio stream stopped");
    return AudioError::NONE;
}

bool AudioSystemV2::isWebRTCStreamActive() const {
    return pImpl_->is_webrtc_streaming.load();
}

// ============================================================================
// 便利函数（一键启动/停止模式）
// ============================================================================

AudioError AudioSystemV2::startAIMode() {
    LOG_INFO("AudioSystemV2", "Starting AI mode...");
    
    // 1. 设置主状态为AI
    setMainState(AudioMainState::AI);
    
    // 2. 开始录音（如果未启动）
    if (!isRecording()) {
        AudioError err = startRecord();
        if (err != AudioError::NONE) {
            LOG_ERROR("AudioSystemV2", "Failed to start recording");
            return err;
        }
    }
    
    // 3. 启动AI音频流
    AudioError err = startAIStream();
    if (err != AudioError::NONE) {
        LOG_ERROR("AudioSystemV2", "Failed to start AI stream");
        return err;
    }
    
    LOG_INFO("AudioSystemV2", "AI mode started successfully");
    return AudioError::NONE;
}

AudioError AudioSystemV2::stopAIMode() {
    LOG_INFO("AudioSystemV2", "Stopping AI mode...");
    
    // 1. 停止AI音频流
    stopAIStream();
    
    // 2. 停止录音
    stopRecord();
    
    // 3. 重置主状态
    setMainState(AudioMainState::NONE);
    
    LOG_INFO("AudioSystemV2", "AI mode stopped");
    return AudioError::NONE;
}

AudioError AudioSystemV2::startWebRTCMode() {
    LOG_INFO("AudioSystemV2", "Starting WebRTC mode...");
    
    // 1. 设置主状态为WebRTC
    setMainState(AudioMainState::WEBRTC);
    
    // 2. 开始录音（如果未启动）
    if (!isRecording()) {
        AudioError err = startRecord();
        if (err != AudioError::NONE) {
            LOG_ERROR("AudioSystemV2", "Failed to start recording");
            return err;
        }
    }
    
    // 3. 启动WebRTC音频流
    AudioError err = startWebRTCStream();
    if (err != AudioError::NONE) {
        LOG_ERROR("AudioSystemV2", "Failed to start WebRTC stream");
        return err;
    }
    
    LOG_INFO("AudioSystemV2", "WebRTC mode started successfully");
    return AudioError::NONE;
}

AudioError AudioSystemV2::stopWebRTCMode() {
    LOG_INFO("AudioSystemV2", "Stopping WebRTC mode...");
    
    // 1. 停止WebRTC音频流
    stopWebRTCStream();
    
    // 2. 停止录音
    stopRecord();
    
    // 3. 重置主状态
    setMainState(AudioMainState::NONE);
    
    LOG_INFO("AudioSystemV2", "WebRTC mode stopped");
    return AudioError::NONE;
}

// ============================================================================
// 全部完成！AudioV2实现完毕
// ============================================================================

} // namespace audio
} // namespace media
} // namespace app


