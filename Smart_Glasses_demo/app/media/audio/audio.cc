/**
 * @file audio.cc
 * @brief 音频系统实现
 */

#include "audio.hpp"
#include "../../tool/log/log.hpp"
#include "../../../common/common.hpp"
#include <cstring>
#include <algorithm>
#include <iterator>

namespace app
{
    namespace media
    {
        namespace audio
        {

            using namespace tool::log;

            namespace
            {
                constexpr double   BYTES_PER_KIB             = 1024.0;
                constexpr double   BYTES_PER_MIB             = BYTES_PER_KIB * 1024.0;
                constexpr double   FIXED_POOL_WARN_THRESHOLD = 70.0;
                constexpr uint64_t FIXED_POOL_MIN_SAMPLES    = 100;
                constexpr float    MIN_OUTPUT_VOLUME         = 0.0F;
                constexpr float    MAX_OUTPUT_VOLUME         = 2.0F;
                constexpr double   SAMPLE_RATE_INPUT_HZ      = 48000.0;
                constexpr double   SAMPLE_RATE_TARGET_HZ     = 16000.0;
                constexpr double   RESAMPLE_RATIO = SAMPLE_RATE_TARGET_HZ / SAMPLE_RATE_INPUT_HZ;
                constexpr float    PCM_NORMALIZE_DENOMINATOR   = 32768.0F;
                constexpr float    PCM_CLAMP_NUMERATOR         = 32767.0F;
                constexpr float    PCM_CLAMP_MIN               = -1.0F;
                constexpr float    PCM_CLAMP_MAX               = 1.0F;
                constexpr size_t   WAKEWORD_BUFFER_MAX_SAMPLES = 16000; // 1秒@16kHz
                constexpr int      TARGET_FRAME_SAMPLES        = 320;   // 16kHz 20ms
                constexpr size_t   AI_BUFFER_MAX_SAMPLES       = 16000; // 1秒@16kHz
                constexpr int      OPUS_TTS_SAMPLE_RATE        = 48000;
                constexpr int      OPUS_AI_SAMPLE_RATE         = 16000;
                constexpr size_t   OPUS_MAX_FRAME_BYTES        = 4096;
                constexpr int      MILLISECONDS_PER_SECOND     = 1000;
                constexpr int      LOG_SAMPLE_LIMIT            = 10;
                constexpr size_t   CACHE_LINE_ALIGNMENT        = 64;
                constexpr double   MEMORY_EXPANSION_FACTOR     = 2.0;
                constexpr size_t   FLOAT_BUFFER_SAMPLES        = 2048;
                constexpr int      PCM_STATS_SAMPLE_LIMIT      = 10; // PCM统计打印前N包
            }                                                        // namespace

            // ============================================================================
            // AudioFrame实现
            // ============================================================================

            // ============================================================================
            // AudioMemoryPool::FixedPool实现（无锁固定池）
            // ============================================================================

            // FixedPool构造函数 - 支持动态大小
            AudioMemoryPool::FixedPool::FixedPool(size_t block_count)
                : actual_block_count(std::min(block_count, MAX_BLOCKS))
            {

                // 初始化位图（所有位为0表示空闲）
                for (auto& bitmap_word : allocation_bitmap)
                {
                    bitmap_word.store(0, std::memory_order_relaxed);
                }

                // 动态分配数据块和帧对象
                blocks.resize(actual_block_count);
                frame_objects.resize(actual_block_count);

                // 初始化帧对象
                for (size_t i = 0; i < actual_block_count; i++)
                {
                    frame_objects[i].is_from_fixed_pool = true;
                    frame_objects[i].fixed_pool_index   = static_cast<int>(i);
                }

                LOG_INFO("AudioBuffer", "固定内存池已创建: %zu 块 × %zu 字节 = %.2f KB",
                         actual_block_count, BLOCK_SIZE,
                         (actual_block_count * BLOCK_SIZE) / BYTES_PER_KIB);
            }

            uint8_t* AudioMemoryPool::FixedPool::getBlockPtr(int index)
            {
                if (index < 0 || index >= static_cast<int>(actual_block_count))
                {
                    return nullptr;
                }
                return blocks[static_cast<size_t>(index)].data();
            }

            int AudioMemoryPool::FixedPool::allocateBlock()
            {
                // 遍历位图（根据实际块数计算需要的位图数量）
                int bits_per_word = static_cast<int>(FixedPool::BITS_PER_WORD);
                int bitmap_count =
                    static_cast<int>((actual_block_count + BITS_PER_WORD - 1) / BITS_PER_WORD);

                for (int bitmap_index = 0; bitmap_index < bitmap_count; ++bitmap_index)
                {
                    auto& bitmap_atomic = allocation_bitmap.at(static_cast<size_t>(bitmap_index));
                    uint64_t bitmap     = bitmap_atomic.load(std::memory_order_acquire);

                    // 计算此位图管理的块范围
                    int base_index = bitmap_index * bits_per_word;
                    int max_blocks =
                        std::min(bits_per_word, static_cast<int>(actual_block_count) - base_index);

                    if (max_blocks <= 0)
                    {
                        break;
                    }

                    // 查找空闲块
                    while (bitmap != UINT64_MAX)
                    {
                        uint64_t inverted = ~bitmap;
                        if (inverted == 0U)
                        {
                            break;
                        }

                        int free_bit = __builtin_ctzll(inverted);
                        if (free_bit >= max_blocks)
                        {
                            break;
                        }

                        // CAS尝试分配
                        uint64_t new_bitmap = bitmap | (1ULL << free_bit);
                        if (bitmap_atomic.compare_exchange_weak(bitmap, new_bitmap,
                                                                std::memory_order_acq_rel,
                                                                std::memory_order_acquire))
                        {
                            return base_index + free_bit; // 返回全局索引
                        }
                        // CAS失败，bitmap已更新，继续循环
                    }
                }

                return -1; // 池满
            }

            void AudioMemoryPool::FixedPool::deallocateBlock(int index)
            {
                if (index < 0 || index >= static_cast<int>(actual_block_count))
                {
                    LOG_ERROR("AudioBuffer", "无效的块索引: %d", index);
                    return;
                }

                // 计算位图索引和位位置
                int bits_per_word = static_cast<int>(FixedPool::BITS_PER_WORD);
                int bitmap_index  = index / bits_per_word;
                int bit_position  = index % bits_per_word;

                // 原子清零对应位
                uint64_t mask = ~(1ULL << bit_position);
                allocation_bitmap.at(static_cast<size_t>(bitmap_index))
                    .fetch_and(mask, std::memory_order_release);
            }

            // ============================================================================
            // AudioMemoryPool实现（两级缓冲池）
            // ============================================================================

            AudioMemoryPool::AudioMemoryPool(const AudioMemoryPoolConfig& config) : config_(config)
            {

                LOG_INFO("AudioBuffer", "初始化音频内存池（两级架构）...");
                LOG_INFO("AudioBuffer", "  固定池: %zu 块 × %zu 字节 = %.2f KB",
                         config_.fixed_block_count, config_.fixed_block_size,
                         (config_.fixed_block_count * config_.fixed_block_size) / BYTES_PER_KIB);

                // 创建动态大小的固定池
                fixed_pool_ = std::make_unique<FixedPool>(config_.fixed_block_count);

                // 创建动态内存池（第二级）
                if (config_.dynamic_pool_size > 0)
                {
                    dynamic_pool_ = std::make_unique<tool::memory::MemoryPool>(
                        config_.dynamic_pool_size, // initialSize
                        CACHE_LINE_ALIGNMENT,      // alignment
                        MEMORY_EXPANSION_FACTOR);
                    LOG_INFO("AudioBuffer", "  动态池: %.2f MB（初始）",
                             config_.dynamic_pool_size / BYTES_PER_MIB);
                }

                LOG_INFO("AudioBuffer", "音频内存池初始化成功");
            }

            AudioMemoryPool::~AudioMemoryPool()
            {
                LOG_INFO("AudioBuffer", "销毁音频内存池...");

                // 输出最终统计
                logStats();

                // 固定池和动态池会自动析构（unique_ptr）
                LOG_INFO("AudioBuffer", "音频内存池已销毁");
            }

            AudioFramePtr AudioMemoryPool::allocate(size_t size)
            {
                stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);

                // 第一级：尝试固定池（快速路径）
                if (size <= config_.fixed_block_size)
                {
                    auto frame = allocateFromFixed(size);
                    if (frame)
                    {
                        stats_.fixed_pool_hits.fetch_add(1, std::memory_order_relaxed);
                        return frame;
                    }

                    // 固定池未命中，回退到动态池
                    LOG_DEBUG("AudioBuffer", "固定池未命中，回退到动态池（大小: %zu）", size);
                }

                // 第二级：动态池（慢速路径）
                auto frame = allocateFromDynamic(size);
                if (frame)
                {
                    stats_.dynamic_pool_hits.fetch_add(1, std::memory_order_relaxed);
                    return frame;
                }

                // 分配失败
                stats_.allocation_failures.fetch_add(1, std::memory_order_relaxed);
                LOG_ERROR("AudioBuffer", "内存分配失败: %zu 字节", size);
                return nullptr;
            }

            AudioFramePtr AudioMemoryPool::allocateFromFixed(size_t size)
            {
                if (size > config_.fixed_block_size)
                {
                    return nullptr;
                }

                // 无锁分配
                int block_index = fixed_pool_->allocateBlock();
                if (block_index < 0)
                {
                    return nullptr; // 池满
                }

                // 使用对象池中的帧对象
                AudioFrame* frame         = &fixed_pool_->frame_objects[block_index];
                frame->data               = fixed_pool_->getBlockPtr(block_index);
                frame->capacity           = FixedPool::BLOCK_SIZE;
                frame->size               = size;
                frame->timestamp          = get_nowus();
                frame->is_from_fixed_pool = true;
                frame->fixed_pool_index   = block_index;

                // 创建智能指针，使用自定义删除器回收到固定池
                auto* pool_ptr = fixed_pool_.get();
                return std::shared_ptr<AudioFrame>(
                    frame,
                    [pool_ptr](AudioFrame* f)
                    {
                        if (f && f->is_from_fixed_pool && f->fixed_pool_index >= 0)
                        {
                            pool_ptr->deallocateBlock(f->fixed_pool_index);
                        }
                        // 注意：不delete f，因为它来自对象池
                    });
            }

            AudioFramePtr AudioMemoryPool::allocateFromDynamic(size_t size)
            {
                if (!dynamic_pool_)
                {
                    LOG_ERROR("AudioBuffer", "动态池未初始化");
                    return nullptr;
                }

                // 从动态池分配数据缓冲区
                void* buffer = dynamic_pool_->allocate(size);
                if (!buffer)
                {
                    return nullptr;
                }

                // 使用自定义删除器确保正确释放动态池内存
                auto frame = std::shared_ptr<AudioFrame>(new AudioFrame(),
                                                         [this](AudioFrame* f)
                                                         {
                                                             // 先释放动态池内存，再删除AudioFrame对象
                                                             if (f->data && dynamic_pool_)
                                                             {
                                                                 dynamic_pool_->deallocate(f->data);
                                                             }
                                                             delete f;
                                                         });

                frame->data               = static_cast<uint8_t*>(buffer);
                frame->capacity           = size;
                frame->size               = size;
                frame->timestamp          = get_nowus();
                frame->is_from_fixed_pool = false;
                frame->fixed_pool_index   = -1;

                return frame;
            }

            // deallocate函数不再需要，所有清理工作由shared_ptr的deleter负责

            void AudioMemoryPool::getStats(Stats& out_stats) const
            {
                out_stats.fixed_pool_hits.store(stats_.fixed_pool_hits.load());
                out_stats.dynamic_pool_hits.store(stats_.dynamic_pool_hits.load());
                out_stats.total_allocations.store(stats_.total_allocations.load());
                out_stats.allocation_failures.store(stats_.allocation_failures.load());
            }

            void AudioMemoryPool::resetStats()
            {
                stats_.fixed_pool_hits.store(0);
                stats_.dynamic_pool_hits.store(0);
                stats_.total_allocations.store(0);
                stats_.allocation_failures.store(0);
            }

            void AudioMemoryPool::logStats() const
            {
                uint64_t total        = stats_.total_allocations.load();
                uint64_t fixed_hits   = stats_.fixed_pool_hits.load();
                uint64_t dynamic_hits = stats_.dynamic_pool_hits.load();
                uint64_t failures     = stats_.allocation_failures.load();

                if (total == 0)
                {
                    LOG_INFO("AudioBuffer", "暂无分配统计");
                    return;
                }

                double fixed_rate   = (double)fixed_hits / total * 100.0;
                double dynamic_rate = (double)dynamic_hits / total * 100.0;

                LOG_INFO("AudioBuffer", "=== 内存池统计 ===");
                LOG_INFO("AudioBuffer", "  总分配次数: %llu", total);
                LOG_INFO("AudioBuffer", "  固定池命中:   %llu (%.2f%%)", fixed_hits, fixed_rate);
                LOG_INFO("AudioBuffer", "  动态池命中: %llu (%.2f%%)", dynamic_hits, dynamic_rate);
                LOG_INFO("AudioBuffer", "  分配失败:          %llu", failures);

                // 只有在命中率 < 70% 时才警告（固定池大小已优化到600）
                if (fixed_rate < FIXED_POOL_WARN_THRESHOLD && total > FIXED_POOL_MIN_SAMPLES)
                {
                    LOG_WARN("AudioBuffer",
                             "固定池命中率偏低 (%.2f%%)，建议增加池大小或检查内存泄漏");
                }
            }

            // ============================================================================
            // AudioSystem::Impl定义（Pimpl实现）
            // ============================================================================

            class AudioSystem::Impl
            {
            public:
                // 配置
                AudioConfig config;

                // 状态机
                std::atomic<AudioMainState>     main_state{AudioMainState::NONE};
                std::atomic<AudioControlState>  control_state{AudioControlState::NONE};
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
                OpusEncoderPtr webrtc_encoder; // 48kHz编码器（WebRTC）
                OpusDecoderPtr webrtc_decoder; // 48kHz解码器（WebRTC）
                OpusDecoderPtr tts_decoder;    // 48kHz解码器（TTS）
                OpusEncoderPtr ai_encoder;     // 16kHz编码器（AI）

                // 重采样器（RAII智能指针）
                SrcStatePtr          ai_resampler;       // 48kHz → 16kHz（AI）
                std::vector<int16_t> ai_resample_buffer; // AI重采样累积缓冲区

                SrcStatePtr          wakeword_resampler;       // 48kHz → 16kHz（唤醒词）
                std::vector<int16_t> wakeword_resample_buffer; // 唤醒词重采样累积缓冲区

                // 3A算法（RAII智能指针）
                SpeexStatePtr speex_state;

                // PortAudio流（RAII智能指针）
                PaStreamPtr record_stream;
                PaStreamPtr playback_stream;

                // 音频帧队列（零拷贝，使用智能指针）
                std::queue<AudioFramePtr> record_queue;
                std::queue<AudioFramePtr> playback_queue;
                std::mutex                record_queue_mutex;
                std::mutex                playback_queue_mutex;
                std::condition_variable   record_queue_cv;

                // 回调函数（线程安全）
                mutable std::mutex                     callback_mutex;
                AudioFrameCallback                     ai_audio_callback;
                AudioFrameCallback                     webrtc_audio_callback;
                WakewordCallback                       wakeword_callback;
                StateChangeCallback<AudioMainState>    main_state_callback;
                StateChangeCallback<AudioControlState> control_state_callback;

                // 时间同步
                std::shared_ptr<sync_context_t> sync_ctx;

                // 统计信息
                Stats stats;

                // 临时缓冲区（预分配，避免回调中频繁分配）
                alignas(CACHE_LINE_ALIGNMENT)
                    std::array<uint8_t, OPUS_MAX_FRAME_BYTES> temp_opus_buffer;
                alignas(CACHE_LINE_ALIGNMENT)
                    std::array<float, FLOAT_BUFFER_SAMPLES> temp_float_buffer_in;
                alignas(CACHE_LINE_ALIGNMENT)
                    std::array<float, FLOAT_BUFFER_SAMPLES> temp_float_buffer_out;

                explicit Impl(const AudioConfig& cfg) : config(cfg)
                {
                    LOG_DEBUG("AudioSystem", "Impl已创建");
                }

                ~Impl()
                {
                    LOG_DEBUG("AudioSystem", "Impl已销毁");
                }

                // 静态回调函数（C函数指针兼容）
                static int recordCallback(const void* input_buffer, void* output_buffer,
                                          unsigned long                   frames_per_buffer,
                                          const PaStreamCallbackTimeInfo* time_info,
                                          PaStreamCallbackFlags status_flags, void* user_data);

                static int playbackCallback(const void* input_buffer, void* output_buffer,
                                            unsigned long                   frames_per_buffer,
                                            const PaStreamCallbackTimeInfo* time_info,
                                            PaStreamCallbackFlags status_flags, void* user_data);

                // 状态设置（带回调）
                void setMainState(AudioMainState new_state)
                {
                    AudioMainState old_state =
                        main_state.exchange(new_state, std::memory_order_acq_rel);

                    if (old_state != new_state)
                    {
                        LOG_INFO("AudioSystem", "主状态: %d → %d", static_cast<int>(old_state),
                                 static_cast<int>(new_state));

                        // 调用回调（如果设置了）
                        // 注意：使用try_lock避免死锁，如果获取锁失败就跳过回调
                        std::unique_lock<std::mutex> lock(callback_mutex, std::try_to_lock);
                        if (lock.owns_lock() && main_state_callback)
                        {
                            try
                            {
                                main_state_callback(old_state, new_state);
                            }
                            catch (const std::runtime_error& e)
                            {
                                LOG_ERROR("AudioSystem", "主状态回调运行时错误: %s", e.what());
                            }
                            catch (const std::logic_error& e)
                            {
                                LOG_ERROR("AudioSystem", "主状态回调逻辑错误: %s", e.what());
                            }
                            catch (const std::exception& e)
                            {
                                LOG_ERROR("AudioSystem", "主状态回调异常: %s", e.what());
                            }
                        }
                        else if (!lock.owns_lock())
                        {
                            LOG_DEBUG("AudioSystem", "回调互斥锁繁忙，跳过主状态回调");
                        }
                    }
                }

                void setControlState(AudioControlState new_state)
                {
                    AudioControlState old_state =
                        control_state.exchange(new_state, std::memory_order_acq_rel);

                    if (old_state != new_state)
                    {
                        LOG_DEBUG("AudioSystem", "控制状态: %d → %d", static_cast<int>(old_state),
                                  static_cast<int>(new_state));

                        // 使用try_lock避免死锁
                        std::unique_lock<std::mutex> lock(callback_mutex, std::try_to_lock);
                        if (lock.owns_lock() && control_state_callback)
                        {
                            try
                            {
                                control_state_callback(old_state, new_state);
                            }
                            catch (const std::runtime_error& e)
                            {
                                LOG_ERROR("AudioSystem", "控制状态回调运行时错误: %s", e.what());
                            }
                            catch (const std::logic_error& e)
                            {
                                LOG_ERROR("AudioSystem", "控制状态回调逻辑错误: %s", e.what());
                            }
                            catch (const std::exception& e)
                            {
                                LOG_ERROR("AudioSystem", "控制状态回调异常: %s", e.what());
                            }
                        }
                    }
                }

                // 线程安全的回调调用
                void invokeAICallback(AudioFramePtr frame)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);
                    if (ai_audio_callback)
                    {
                        try
                        {
                            ai_audio_callback(std::move(frame));
                        }
                        catch (const std::runtime_error& e)
                        {
                            LOG_ERROR("AudioSystem", "AI回调运行时错误: %s", e.what());
                        }
                        catch (const std::logic_error& e)
                        {
                            LOG_ERROR("AudioSystem", "AI回调逻辑错误: %s", e.what());
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR("AudioSystem", "AI回调异常: %s", e.what());
                        }
                    }
                }

                void invokeWebRTCCallback(AudioFramePtr frame)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);
                    if (webrtc_audio_callback)
                    {
                        try
                        {
                            webrtc_audio_callback(std::move(frame));
                        }
                        catch (const std::runtime_error& e)
                        {
                            LOG_ERROR("AudioSystem", "WebRTC回调运行时错误: %s", e.what());
                        }
                        catch (const std::logic_error& e)
                        {
                            LOG_ERROR("AudioSystem", "WebRTC回调逻辑错误: %s", e.what());
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR("AudioSystem", "WebRTC回调异常: %s", e.what());
                        }
                    }
                }

                void invokeWakewordCallback(const int16_t* data, size_t length)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);
                    if (wakeword_callback)
                    {
                        try
                        {
                            wakeword_callback(data, length);
                        }
                        catch (const std::runtime_error& e)
                        {
                            LOG_ERROR("AudioSystem", "唤醒词回调运行时错误: %s", e.what());
                        }
                        catch (const std::logic_error& e)
                        {
                            LOG_ERROR("AudioSystem", "唤醒词回调逻辑错误: %s", e.what());
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR("AudioSystem", "唤醒词回调异常: %s", e.what());
                        }
                    }
                }
            };

            // ============================================================================
            // AudioSystem公共接口实现
            // ============================================================================

            AudioSystem::AudioSystem(const AudioConfig& config)
                : pImpl_(std::make_unique<Impl>(config))
            {
                LOG_INFO("AudioSystem", "音频系统已创建");
            }

            AudioSystem::~AudioSystem()
            {
                shutdown();
                LOG_INFO("AudioSystem", "音频系统已销毁");
            }

            AudioError AudioSystem::initialize(std::shared_ptr<sync_context_t> sync_ctx)
            {
                if (pImpl_->initialized.load())
                {
                    LOG_WARN("AudioSystem", "已经初始化过了");
                    return AudioError::ALREADY_RUNNING;
                }

                LOG_INFO("AudioSystem", "========================================");
                LOG_INFO("AudioSystem", "初始化音频系统...");
                LOG_INFO("AudioSystem", "========================================");

                pImpl_->sync_ctx = std::move(sync_ctx);

                // 1. 创建内存池
                LOG_INFO("AudioSystem", "步骤 1: 创建内存池...");
                pImpl_->mem_pool =
                    std::make_unique<AudioMemoryPool>(pImpl_->config.mem_pool_config);

                // 2. 初始化PortAudio
                LOG_INFO("AudioSystem", "步骤 2: 初始化PortAudio...");
                PaError err = Pa_Initialize();
                if (err != paNoError)
                {
                    LOG_ERROR("AudioSystem", "PortAudio初始化失败: %s", Pa_GetErrorText(err));
                    return AudioError::INITIALIZE_FAILED;
                }
                LOG_INFO("AudioSystem", "  PortAudio版本: %s", Pa_GetVersionText());

                // 3. 创建所有Opus编解码器和重采样器
                LOG_INFO("AudioSystem", "步骤 3: 创建所有Opus编解码器和重采样器...");
                int  opus_error      = 0;
                int  src_error       = 0;
                bool all_initialized = true;

                // 3.1 创建WebRTC编码器（48kHz）
                OpusEncoder* webrtc_encoder =
                    opus_encoder_create(pImpl_->config.sample_rate, pImpl_->config.channels,
                                        OPUS_APPLICATION_VOIP, &opus_error);

                if (opus_error != OPUS_OK || !webrtc_encoder)
                {
                    LOG_ERROR("AudioSystem", "WebRTC编码器创建失败: %s", opus_strerror(opus_error));
                    all_initialized = false;
                }
                else
                {
                    pImpl_->webrtc_encoder.reset(webrtc_encoder);
                    opus_encoder_ctl(pImpl_->webrtc_encoder.get(), OPUS_SET_BITRATE(64000));
                    opus_encoder_ctl(pImpl_->webrtc_encoder.get(), OPUS_SET_VBR(1));
                    opus_encoder_ctl(pImpl_->webrtc_encoder.get(),
                                     OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                    LOG_INFO("AudioSystem", "  ✓ WebRTC编码器: 48kHz, 64kbps");
                }

                // 3.2 创建WebRTC解码器（48kHz）
                OpusDecoder* webrtc_decoder = opus_decoder_create(
                    pImpl_->config.sample_rate, pImpl_->config.channels, &opus_error);

                if (opus_error != OPUS_OK || !webrtc_decoder)
                {
                    LOG_ERROR("AudioSystem", "WebRTC解码器创建失败: %s", opus_strerror(opus_error));
                    all_initialized = false;
                }
                else
                {
                    pImpl_->webrtc_decoder.reset(webrtc_decoder);
                    LOG_INFO("AudioSystem", "  ✓ WebRTC解码器: 48kHz");
                }

                // 3.3 创建TTS解码器（48kHz）
                OpusDecoder* tts_decoder =
                    opus_decoder_create(OPUS_TTS_SAMPLE_RATE, 1, &opus_error);
                if (opus_error != OPUS_OK || !tts_decoder)
                {
                    LOG_ERROR("AudioSystem", "TTS解码器创建失败: %s", opus_strerror(opus_error));
                    all_initialized = false;
                }
                else
                {
                    pImpl_->tts_decoder.reset(tts_decoder);
                    LOG_INFO("AudioSystem", "  ✓ TTS解码器: 48kHz");
                }

                // 3.4 创建AI编码器（16kHz）
                OpusEncoder* ai_encoder =
                    opus_encoder_create(OPUS_AI_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &opus_error);
                if (opus_error != OPUS_OK || !ai_encoder)
                {
                    LOG_ERROR("AudioSystem", "AI编码器创建失败: %s", opus_strerror(opus_error));
                    all_initialized = false;
                }
                else
                {
                    pImpl_->ai_encoder.reset(ai_encoder);
                    opus_encoder_ctl(pImpl_->ai_encoder.get(), OPUS_SET_BITRATE(32000));
                    opus_encoder_ctl(pImpl_->ai_encoder.get(), OPUS_SET_VBR(1));
                    opus_encoder_ctl(pImpl_->ai_encoder.get(), OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                    LOG_INFO("AudioSystem", "  ✓ AI编码器: 16kHz, 32kbps");
                }

                // 3.5 创建AI重采样器（48kHz → 16kHz）
                SRC_STATE* ai_resampler = src_new(SRC_SINC_BEST_QUALITY, 1, &src_error);
                if (!ai_resampler)
                {
                    LOG_ERROR("AudioSystem", "AI重采样器创建失败: %s", src_strerror(src_error));
                    all_initialized = false;
                }
                else
                {
                    pImpl_->ai_resampler.reset(ai_resampler);
                    LOG_INFO("AudioSystem", "  ✓ AI重采样器: 48kHz → 16kHz（最佳质量）");
                }

                // 3.6 创建唤醒词重采样器（48kHz → 16kHz）
                SRC_STATE* wakeword_resampler = src_new(SRC_SINC_FASTEST, 1, &src_error);
                if (!wakeword_resampler)
                {
                    LOG_ERROR("AudioSystem", "唤醒词重采样器创建失败: %s", src_strerror(src_error));
                    all_initialized = false;
                }
                else
                {
                    pImpl_->wakeword_resampler.reset(wakeword_resampler);
                    LOG_INFO("AudioSystem", "  ✓ 唤醒词重采样器: 48kHz → 16kHz（最快速度）");
                }

                // 检查所有组件是否初始化成功
                if (!all_initialized)
                {
                    LOG_ERROR("AudioSystem", "部分音频组件初始化失败");
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

                LOG_INFO("AudioSystem", "  ✓ 所有音频组件初始化成功");

                // 4. 初始化3A算法
                if (pImpl_->config.enable_denoise || pImpl_->config.enable_agc)
                {
                    LOG_INFO("AudioSystem", "步骤 4: 初始化3A算法...");

                    int frame_size = pImpl_->config.sample_rate * pImpl_->config.frame_duration_ms /
                                     MILLISECONDS_PER_SECOND;
                    SpeexPreprocessState* speex =
                        speex_preprocess_state_init(frame_size, pImpl_->config.sample_rate);

                    if (!speex)
                    {
                        LOG_WARN("AudioSystem", "Speex预处理初始化失败，3A已禁用");
                    }
                    else
                    {
                        pImpl_->speex_state.reset(speex);

                        // 配置3A参数
                        int denoise = pImpl_->config.enable_denoise ? 1 : 0;
                        speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_DENOISE, &denoise);

                        int agc = pImpl_->config.enable_agc ? 1 : 0;
                        speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_AGC, &agc);

                        if (pImpl_->config.enable_agc)
                        {
                            float agc_level = pImpl_->config.agc_level;
                            speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_AGC_LEVEL, &agc_level);
                        }

                        LOG_INFO("AudioSystem", "  3A: 降噪=%s, AGC=%s", denoise ? "开启" : "关闭",
                                 agc ? "开启" : "关闭");
                    }
                }

                pImpl_->initialized.store(true);
                pImpl_->output_volume.store(pImpl_->config.output_volume);

                LOG_INFO("AudioSystem", "========================================");
                LOG_INFO("AudioSystem", "音频系统初始化成功！");
                LOG_INFO("AudioSystem", "========================================");

                return AudioError::NONE;
            }

            void AudioSystem::shutdown()
            {
                if (!pImpl_->initialized.load())
                {
                    return;
                }

                LOG_INFO("AudioSystem", "关闭音频系统...");

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
                if (pImpl_->mem_pool)
                {
                    pImpl_->mem_pool->logStats();
                }

                pImpl_->initialized.store(false);
                LOG_INFO("AudioSystem", "音频系统关闭完成");
            }

            bool AudioSystem::isInitialized() const
            {
                return pImpl_->initialized.load();
            }

            // ========================================================================
            // 状态控制
            // ========================================================================

            AudioError AudioSystem::setMainState(AudioMainState state)
            {
                pImpl_->setMainState(state);
                return AudioError::NONE;
            }

            AudioMainState AudioSystem::getMainState() const
            {
                return pImpl_->main_state.load();
            }

            AudioError AudioSystem::setControlState(AudioControlState state)
            {
                pImpl_->setControlState(state);
                return AudioError::NONE;
            }

            AudioControlState AudioSystem::getControlState() const
            {
                return pImpl_->control_state.load();
            }

            AudioError AudioSystem::setFunctionState(AudioFunctionState state)
            {
                AudioFunctionState old_state = pImpl_->function_state.exchange(state);

                if (old_state != state)
                {
                    LOG_DEBUG("AudioSystem", "功能状态: %d → %d", static_cast<int>(old_state),
                              static_cast<int>(state));
                }

                return AudioError::NONE;
            }

            AudioFunctionState AudioSystem::getFunctionState() const
            {
                return pImpl_->function_state.load();
            }

            // ========================================================================
            // 音量控制
            // ========================================================================

            void AudioSystem::setOutputVolume(float volume)
            {
                // 限制音量范围 [0.0, 2.0]
                volume = std::clamp(volume, MIN_OUTPUT_VOLUME, MAX_OUTPUT_VOLUME);
                pImpl_->output_volume.store(volume, std::memory_order_relaxed);
                LOG_INFO("AudioSystem", "输出音量设置为: %.2f", volume);
            }

            float AudioSystem::getOutputVolume() const
            {
                return pImpl_->output_volume.load(std::memory_order_relaxed);
            }

            // ========================================================================
            // 回调设置（线程安全）
            // ========================================================================

            void AudioSystem::setAIAudioCallback(AudioFrameCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->ai_audio_callback = std::move(callback);
                LOG_DEBUG("AudioSystem", "AI音频回调已设置");
            }

            void AudioSystem::setWebRTCAudioCallback(AudioFrameCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->webrtc_audio_callback = std::move(callback);
                LOG_DEBUG("AudioSystem", "WebRTC音频回调已设置");
            }

            void AudioSystem::setWakewordCallback(WakewordCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->wakeword_callback = std::move(callback);
                LOG_DEBUG("AudioSystem", "唤醒词回调已设置");
            }

            void AudioSystem::setMainStateCallback(StateChangeCallback<AudioMainState> callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->main_state_callback = std::move(callback);
            }

            void
            AudioSystem::setControlStateCallback(StateChangeCallback<AudioControlState> callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->control_state_callback = std::move(callback);
            }

            // ========================================================================
            // 队列操作
            // ========================================================================

            void AudioSystem::clearRecordQueue()
            {
                std::lock_guard<std::mutex> lock(pImpl_->record_queue_mutex);
                std::queue<AudioFramePtr>   empty;
                std::swap(pImpl_->record_queue, empty);
                LOG_DEBUG("AudioSystem", "录音队列已清空");
            }

            void AudioSystem::clearPlaybackQueue()
            {
                std::lock_guard<std::mutex> lock(pImpl_->playback_queue_mutex);
                std::queue<AudioFramePtr>   empty;
                std::swap(pImpl_->playback_queue, empty);
                LOG_DEBUG("AudioSystem", "播放队列已清空");
            }

            AudioFramePtr AudioSystem::getRecordedFrame(std::chrono::milliseconds timeout)
            {
                std::unique_lock<std::mutex> lock(pImpl_->record_queue_mutex);

                // 等待队列非空或超时
                if (pImpl_->record_queue.empty())
                {
                    if (!pImpl_->record_queue_cv.wait_for(
                            lock, timeout,
                            [this]() {
                                return !pImpl_->record_queue.empty() ||
                                       !pImpl_->is_recording.load();
                            }))
                    {
                        return nullptr; // 超时
                    }
                }

                if (pImpl_->record_queue.empty())
                {
                    return nullptr;
                }

                // 取出帧（零拷贝，只移动智能指针）
                AudioFramePtr frame = std::move(pImpl_->record_queue.front());
                pImpl_->record_queue.pop();

                return frame;
            }

            void AudioSystem::pushPlaybackFrame(AudioFramePtr frame)
            {
                if (!frame)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(pImpl_->playback_queue_mutex);

                // 检查队列长度
                if (pImpl_->playback_queue.size() >= pImpl_->config.max_playback_queue_size)
                {
                    pImpl_->playback_queue.pop(); // 丢弃最旧的帧
                    pImpl_->stats.frames_dropped.fetch_add(1);
                    LOG_WARN("AudioSystem", "播放队列已满，丢弃最旧的帧");
                }

                // 推送帧（零拷贝，智能指针移动）
                pImpl_->playback_queue.push(std::move(frame));
            }

            // ========================================================================
            // 统计信息
            // ========================================================================

            void AudioSystem::getStats(Stats& out_stats) const
            {
                out_stats.frames_recorded.store(pImpl_->stats.frames_recorded.load());
                out_stats.frames_played.store(pImpl_->stats.frames_played.load());
                out_stats.frames_dropped.store(pImpl_->stats.frames_dropped.load());
                out_stats.encode_count.store(pImpl_->stats.encode_count.load());
                out_stats.decode_count.store(pImpl_->stats.decode_count.load());

                if (pImpl_->mem_pool)
                {
                    pImpl_->mem_pool->getStats(out_stats.mem_stats);
                }
            }

            void AudioSystem::resetStats()
            {
                pImpl_->stats.frames_recorded.store(0);
                pImpl_->stats.frames_played.store(0);
                pImpl_->stats.frames_dropped.store(0);
                pImpl_->stats.encode_count.store(0);
                pImpl_->stats.decode_count.store(0);

                if (pImpl_->mem_pool)
                {
                    pImpl_->mem_pool->resetStats();
                }

                LOG_INFO("AudioSystem", "统计信息已重置");
            }

            void AudioSystem::logStats() const
            {
                LOG_INFO("AudioSystem", "=== 音频系统统计 ===");
                LOG_INFO("AudioSystem", "  已录制帧数: %llu", pImpl_->stats.frames_recorded.load());
                LOG_INFO("AudioSystem", "  已播放帧数:   %llu", pImpl_->stats.frames_played.load());
                LOG_INFO("AudioSystem", "  已丢弃帧数:  %llu", pImpl_->stats.frames_dropped.load());
                LOG_INFO("AudioSystem", "  编码次数:    %llu", pImpl_->stats.encode_count.load());
                LOG_INFO("AudioSystem", "  解码次数:    %llu", pImpl_->stats.decode_count.load());

                if (pImpl_->mem_pool)
                {
                    pImpl_->mem_pool->logStats();
                }
            }

            // ============================================================================
            // Impl静态回调函数实现
            // ============================================================================

            int AudioSystem::Impl::recordCallback(const void* input_buffer, void* output_buffer,
                                                  unsigned long                   frames_per_buffer,
                                                  const PaStreamCallbackTimeInfo* time_info,
                                                  PaStreamCallbackFlags           status_flags,
                                                  void*                           user_data)
            {
                (void)output_buffer;
                (void)time_info;
                (void)status_flags;

                auto*       impl  = static_cast<AudioSystem::Impl*>(user_data);
                const auto* input = static_cast<const int16_t*>(input_buffer);
                size_t      frame_size_bytes =
                    frames_per_buffer * impl->config.channels * sizeof(int16_t);

                // ✅ 优化1：从内存池分配（<50ns）
                auto frame = impl->mem_pool->allocate(frame_size_bytes);
                if (!frame)
                {
                    LOG_ERROR("AudioCallback", "帧分配失败");
                    return paContinue;
                }

                // ✅ 优化2：一次拷贝
                std::memcpy(frame->data, input, frame_size_bytes);
                frame->size      = frame_size_bytes;
                frame->timestamp = get_nowus();

                // 3A算法处理
                if (impl->speex_state)
                {
                    speex_preprocess_run(impl->speex_state.get(), frame->getData<int16_t>());
                }

                // 唤醒词检测（仅在非AI流式传输时，需要16kHz音频）
                if (!impl->is_ai_streaming.load() && impl->wakeword_callback &&
                    impl->wakeword_resampler)
                {
                    // 重采样 48kHz → 16kHz
                    SRC_DATA src_data{};

                    // 使用预分配的临时缓冲区
                    float* input_float  = impl->temp_float_buffer_in.data();
                    float* output_float = impl->temp_float_buffer_out.data();

                    // int16 → float
                    const int16_t* pcm_data = frame->getData<int16_t>();
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    std::transform(
                        pcm_data, pcm_data + frames_per_buffer, input_float,
                        [](int16_t sample)
                        { return static_cast<float>(sample) / PCM_NORMALIZE_DENOMINATOR; });

                    // 重采样
                    src_data.data_in       = input_float;
                    src_data.input_frames  = static_cast<long>(frames_per_buffer);
                    src_data.data_out      = output_float;
                    src_data.output_frames = impl->temp_float_buffer_out.size();
                    src_data.src_ratio     = RESAMPLE_RATIO;
                    src_data.end_of_input  = 0;

                    int resample_error = src_process(impl->wakeword_resampler.get(), &src_data);
                    if (resample_error == 0 && src_data.output_frames_gen > 0)
                    {
                        // float → int16，添加到累积缓冲区（带溢出保护）
                        for (long i = 0; i < src_data.output_frames_gen; i++)
                        {
                            if (impl->wakeword_resample_buffer.size() >=
                                WAKEWORD_BUFFER_MAX_SAMPLES)
                            {
                                LOG_WARN("AudioCallback", "唤醒词重采样缓冲区溢出，丢弃样本");
                                break;
                            }

                            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                            float clamped_sample =
                                std::clamp(output_float[i], PCM_CLAMP_MIN, PCM_CLAMP_MAX);
                            impl->wakeword_resample_buffer.push_back(
                                static_cast<int16_t>(clamped_sample * PCM_CLAMP_NUMERATOR));
                        }

                        // 当累积到320样本（16kHz 20ms）时，传递给唤醒词检测
                        const int TARGET_FRAME_SIZE = 320; // 16kHz 20ms = 320 samples
                        while (impl->wakeword_resample_buffer.size() >=
                               static_cast<size_t>(TARGET_FRAME_SIZE))
                        {
                            // 传递给唤醒词检测（16kHz音频）
                            impl->invokeWakewordCallback(impl->wakeword_resample_buffer.data(),
                                                         TARGET_FRAME_SIZE);

                            // 移除已处理的样本
                            impl->wakeword_resample_buffer.erase(
                                impl->wakeword_resample_buffer.begin(),
                                impl->wakeword_resample_buffer.begin() + TARGET_FRAME_SIZE);
                        }
                    }
                }

                // 添加到录音队列（零拷贝，智能指针）
                {
                    std::lock_guard<std::mutex> lock(impl->record_queue_mutex);

                    if (impl->record_queue.size() >= impl->config.max_record_queue_size)
                    {
                        impl->record_queue.pop();
                        impl->stats.frames_dropped.fetch_add(1);
                    }

                    impl->record_queue.push(frame); // shared_ptr拷贝，引用计数+1
                }
                impl->record_queue_cv.notify_one();
                impl->stats.frames_recorded.fetch_add(1);

                // WebRTC音频发送（48kHz Opus编码）
                if (impl->main_state.load() == AudioMainState::WEBRTC &&
                    impl->is_webrtc_streaming.load() && impl->webrtc_encoder)
                {

                    // 使用WebRTC编码器编码（48kHz）
                    uint8_t* opus_buffer = impl->temp_opus_buffer.data();
                    int      encoded_bytes =
                        opus_encode(impl->webrtc_encoder.get(), frame->getData<int16_t>(),
                                    frames_per_buffer, opus_buffer, impl->temp_opus_buffer.size());

                    if (encoded_bytes > 0)
                    {
                        // 分配帧并拷贝编码数据
                        auto encoded_frame = impl->mem_pool->allocate(encoded_bytes);
                        if (encoded_frame)
                        {
                            std::memcpy(encoded_frame->data, opus_buffer, encoded_bytes);
                            encoded_frame->size = encoded_bytes;

                            // 获取同步后的时间戳
                            if (impl->sync_ctx)
                            {
                                encoded_frame->timestamp =
                                    sync_get_timestamp(impl->sync_ctx.get(), get_nowus(), true);
                            }
                            else
                            {
                                encoded_frame->timestamp = get_nowus();
                            }

                            // 调用WebRTC回调
                            impl->invokeWebRTCCallback(encoded_frame);
                            impl->stats.encode_count.fetch_add(1);
                        }
                    }
                }

                // AI音频发送（48kHz → 16kHz → Opus编码）
                static std::atomic<int>      ai_frame_count{0};
                static std::atomic<uint64_t> last_ai_log_time{0};

                bool main_state_ok = (impl->main_state.load() == AudioMainState::AI);
                bool streaming_ok  = impl->is_ai_streaming.load();
                bool encoder_ok    = (impl->ai_encoder != nullptr);
                bool resampler_ok  = (impl->ai_resampler != nullptr);

                if (main_state_ok && streaming_ok && encoder_ok && resampler_ok)
                {
                    // 每100帧打印一次（约2秒）
                    //  if (ai_frame_count.fetch_add(1) % 100 == 0) {
                    //      uint64_t now = get_nowus();
                    //      if (now - last_ai_log_time.load() > 2000000) {  // 2秒
                    //          LOG_DEBUG("AudioCallback", "AI音频处理中... (帧 %d)",
                    //          ai_frame_count.load()); last_ai_log_time.store(now);
                    //      }
                    //  }

                    // 1. 重采样 48kHz → 16kHz
                    SRC_DATA src_data{};

                    // 使用预分配的临时缓冲区
                    float* input_float  = impl->temp_float_buffer_in.data();
                    float* output_float = impl->temp_float_buffer_out.data();

                    // int16 → float
                    const int16_t* pcm_data = frame->getData<int16_t>();
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    std::transform(
                        pcm_data, pcm_data + frames_per_buffer, input_float,
                        [](int16_t sample)
                        { return static_cast<float>(sample) / PCM_NORMALIZE_DENOMINATOR; });

                    // 重采样
                    src_data.data_in       = input_float;
                    src_data.input_frames  = static_cast<long>(frames_per_buffer);
                    src_data.data_out      = output_float;
                    src_data.output_frames = impl->temp_float_buffer_out.size();
                    src_data.src_ratio     = RESAMPLE_RATIO;
                    src_data.end_of_input  = 0;

                    int resample_error = src_process(impl->ai_resampler.get(), &src_data);
                    if (resample_error == 0)
                    {
                        // float → int16，添加到累积缓冲区（带溢出保护）
                        for (long i = 0; i < src_data.output_frames_gen; ++i)
                        {
                            if (impl->ai_resample_buffer.size() >= AI_BUFFER_MAX_SAMPLES)
                            {
                                LOG_WARN("AudioCallback", "AI重采样缓冲区溢出，丢弃样本");
                                break; // 停止添加更多样本
                            }

                            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                            float clamped_sample =
                                std::clamp(output_float[i], PCM_CLAMP_MIN, PCM_CLAMP_MAX);
                            impl->ai_resample_buffer.push_back(
                                static_cast<int16_t>(clamped_sample * PCM_CLAMP_NUMERATOR));
                        }

                        // 2. 当累积到320样本（16kHz 20ms）时，进行Opus编码
                        const int TARGET_FRAME_SIZE = 320;
                        while (impl->ai_resample_buffer.size() >=
                               static_cast<size_t>(TARGET_FRAME_SIZE))
                        {
                            // 编码320样本
                            uint8_t* opus_buffer   = impl->temp_opus_buffer.data();
                            int      encoded_bytes = opus_encode(
                                     impl->ai_encoder.get(), impl->ai_resample_buffer.data(),
                                     TARGET_FRAME_SIZE, opus_buffer, impl->temp_opus_buffer.size());

                            if (encoded_bytes > 0)
                            {
                                // 分配帧并拷贝编码数据
                                auto encoded_frame = impl->mem_pool->allocate(encoded_bytes);
                                if (encoded_frame)
                                {
                                    std::memcpy(encoded_frame->data, opus_buffer, encoded_bytes);
                                    encoded_frame->size      = encoded_bytes;
                                    encoded_frame->timestamp = get_nowus();

                                    // 3. 调用AI回调
                                    impl->invokeAICallback(encoded_frame);
                                    impl->stats.encode_count.fetch_add(1);
                                }
                            }

                            // 移除已编码的样本
                            impl->ai_resample_buffer.erase(impl->ai_resample_buffer.begin(),
                                                           impl->ai_resample_buffer.begin() +
                                                               TARGET_FRAME_SIZE);
                        }
                    }
                }

                return paContinue;
            }

            int AudioSystem::Impl::playbackCallback(const void* input_buffer, void* output_buffer,
                                                    unsigned long frames_per_buffer,
                                                    const PaStreamCallbackTimeInfo* time_info,
                                                    PaStreamCallbackFlags           status_flags,
                                                    void*                           user_data)
            {
                (void)input_buffer;
                (void)time_info;
                (void)status_flags;

                auto*  impl           = static_cast<AudioSystem::Impl*>(user_data);
                auto*  output         = static_cast<int16_t*>(output_buffer);
                size_t samples_needed = frames_per_buffer * impl->config.channels;

                std::lock_guard<std::mutex> lock(impl->playback_queue_mutex);

                if (impl->playback_queue.empty())
                {
                    // 队列空，填充静音
                    std::fill_n(output, samples_needed, 0);
                    return paContinue;
                }

                // 取出帧（零拷贝引用）
                AudioFramePtr& frame           = impl->playback_queue.front();
                size_t         frame_samples   = frame->size / sizeof(int16_t);
                size_t         samples_to_copy = std::min(samples_needed, frame_samples);
                float          output_volume = impl->output_volume.load(std::memory_order_relaxed);

                // 拷贝数据并应用音量
                const int16_t* src = frame->getData<int16_t>();

                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                std::transform(src, src + samples_to_copy, output,
                               [output_volume](int16_t sample)
                               { return static_cast<int16_t>(sample * output_volume); });

                // 填充剩余部分为静音
                if (samples_to_copy < samples_needed)
                {
                    std::fill_n(std::next(output, static_cast<std::ptrdiff_t>(samples_to_copy)),
                                samples_needed - samples_to_copy, 0);
                }

                // 移除已播放的帧
                impl->playback_queue.pop();
                impl->stats.frames_played.fetch_add(1);

                return paContinue;
            }

            // ============================================================================
            // 录音/播放控制
            // ============================================================================

            AudioError AudioSystem::startRecord()
            {
                if (!pImpl_->initialized.load())
                {
                    LOG_ERROR("AudioSystem", "未初始化");
                    return AudioError::NOT_INITIALIZED;
                }

                if (pImpl_->is_recording.load())
                {
                    LOG_WARN("AudioSystem", "已经在录音中");
                    return AudioError::ALREADY_RUNNING;
                }

                LOG_INFO("AudioSystem", "启动录音...");

                // 配置输入参数
                PaStreamParameters input_params;
                input_params.device = Pa_GetDefaultInputDevice();
                if (input_params.device == paNoDevice)
                {
                    LOG_ERROR("AudioSystem", "未找到输入设备");
                    return AudioError::DEVICE_NOT_FOUND;
                }

                input_params.channelCount = pImpl_->config.channels;
                input_params.sampleFormat = paInt16;
                input_params.suggestedLatency =
                    Pa_GetDeviceInfo(input_params.device)->defaultLowInputLatency;
                input_params.hostApiSpecificStreamInfo = nullptr;

                // 打开流
                PaStream* stream            = nullptr;
                int       frames_per_buffer = pImpl_->config.sample_rate / MILLISECONDS_PER_SECOND *
                                        pImpl_->config.frame_duration_ms;

                PaError err = Pa_OpenStream(&stream, &input_params,
                                            nullptr, // 无输出
                                            pImpl_->config.sample_rate, frames_per_buffer,
                                            paClipOff, Impl::recordCallback, pImpl_.get());

                if (err != paNoError)
                {
                    LOG_ERROR("AudioSystem", "打开录音流失败: %s", Pa_GetErrorText(err));
                    return AudioError::STREAM_OPEN_FAILED;
                }

                // 启动流
                err = Pa_StartStream(stream);
                if (err != paNoError)
                {
                    LOG_ERROR("AudioSystem", "启动录音流失败: %s", Pa_GetErrorText(err));
                    Pa_CloseStream(stream);
                    return AudioError::STREAM_START_FAILED;
                }

                pImpl_->record_stream.reset(stream);
                pImpl_->is_recording.store(true);
                pImpl_->setControlState(AudioControlState::RECORD);

                LOG_INFO("AudioSystem", "录音已启动（设备: %d, %dHz, %d声道）", input_params.device,
                         pImpl_->config.sample_rate, pImpl_->config.channels);

                return AudioError::NONE;
            }

            AudioError AudioSystem::stopRecord()
            {
                if (!pImpl_->is_recording.load())
                {
                    return AudioError::NONE;
                }

                LOG_INFO("AudioSystem", "停止录音...");

                pImpl_->is_recording.store(false);
                pImpl_->record_stream.reset(); // RAII自动stop和close
                pImpl_->setControlState(AudioControlState::NONE);

                // 清理重采样缓冲区
                if (!pImpl_->ai_resample_buffer.empty())
                {
                    pImpl_->ai_resample_buffer.clear();
                    LOG_INFO("AudioSystem", "AI重采样缓冲区已安全清除");
                }

                if (!pImpl_->wakeword_resample_buffer.empty())
                {
                    pImpl_->wakeword_resample_buffer.clear();
                    LOG_DEBUG("AudioSystem", "唤醒词重采样缓冲区已清除");
                }

                LOG_INFO("AudioSystem", "录音已停止");
                return AudioError::NONE;
            }

            bool AudioSystem::isRecording() const
            {
                return pImpl_->is_recording.load();
            }

            AudioError AudioSystem::startPlayback()
            {
                if (!pImpl_->initialized.load())
                {
                    LOG_ERROR("AudioSystem", "未初始化");
                    return AudioError::NOT_INITIALIZED;
                }

                if (pImpl_->is_playing.load())
                {
                    LOG_WARN("AudioSystem", "已经在播放中");
                    return AudioError::ALREADY_RUNNING;
                }

                LOG_INFO("AudioSystem", "启动播放...");

                // 配置输出参数
                PaStreamParameters output_params;
                output_params.device = Pa_GetDefaultOutputDevice();
                if (output_params.device == paNoDevice)
                {
                    LOG_ERROR("AudioSystem", "未找到输出设备");
                    return AudioError::DEVICE_NOT_FOUND;
                }

                output_params.channelCount = pImpl_->config.channels;
                output_params.sampleFormat = paInt16;
                output_params.suggestedLatency =
                    Pa_GetDeviceInfo(output_params.device)->defaultLowOutputLatency;
                output_params.hostApiSpecificStreamInfo = nullptr;

                // 打开流
                PaStream* stream            = nullptr;
                int       frames_per_buffer = pImpl_->config.sample_rate / MILLISECONDS_PER_SECOND *
                                        pImpl_->config.frame_duration_ms;

                PaError err =
                    Pa_OpenStream(&stream,
                                  nullptr, // 无输入
                                  &output_params, pImpl_->config.sample_rate, frames_per_buffer,
                                  paClipOff, Impl::playbackCallback, pImpl_.get());

                if (err != paNoError)
                {
                    LOG_ERROR("AudioSystem", "打开播放流失败: %s", Pa_GetErrorText(err));
                    return AudioError::STREAM_OPEN_FAILED;
                }

                // 启动流
                err = Pa_StartStream(stream);
                if (err != paNoError)
                {
                    LOG_ERROR("AudioSystem", "启动播放流失败: %s", Pa_GetErrorText(err));
                    Pa_CloseStream(stream);
                    return AudioError::STREAM_START_FAILED;
                }

                pImpl_->playback_stream.reset(stream);
                pImpl_->is_playing.store(true);
                pImpl_->setControlState(AudioControlState::PLAYBACK);

                LOG_INFO("AudioSystem", "播放已启动（设备: %d）", output_params.device);

                return AudioError::NONE;
            }

            AudioError AudioSystem::stopPlayback()
            {
                if (!pImpl_->is_playing.load())
                {
                    return AudioError::NONE;
                }

                LOG_INFO("AudioSystem", "停止播放...");

                pImpl_->is_playing.store(false);
                pImpl_->playback_stream.reset(); // RAII自动stop和close
                pImpl_->setControlState(AudioControlState::NONE);

                LOG_INFO("AudioSystem", "播放已停止");
                return AudioError::NONE;
            }

            bool AudioSystem::isPlaying() const
            {
                return pImpl_->is_playing.load();
            }

            // ============================================================================
            // 编解码实现
            // ============================================================================

            AudioFramePtr AudioSystem::encodeOpus(const int16_t* pcm_data, size_t pcm_size)
            {
                if (!pImpl_->webrtc_encoder)
                {
                    LOG_ERROR("AudioSystem", "WebRTC编码器未初始化");
                    return nullptr;
                }

                // 计算样本数
                int frame_size = pcm_size / sizeof(int16_t) / pImpl_->config.channels;

                // 分配输出帧
                auto encoded_frame =
                    pImpl_->mem_pool->allocate(OPUS_MAX_FRAME_BYTES); // Opus最大4KB
                if (!encoded_frame)
                {
                    LOG_ERROR("AudioSystem", "编码缓冲区分配失败");
                    return nullptr;
                }

                // 编码
                int encoded_bytes = opus_encode(pImpl_->webrtc_encoder.get(), pcm_data, frame_size,
                                                encoded_frame->data, encoded_frame->capacity);

                if (encoded_bytes < 0)
                {
                    LOG_ERROR("AudioSystem", "Opus编码失败: %s", opus_strerror(encoded_bytes));
                    return nullptr;
                }

                encoded_frame->size = encoded_bytes;
                pImpl_->stats.encode_count.fetch_add(1);

                return encoded_frame;
            }

            AudioFramePtr AudioSystem::decodeOpus(const uint8_t* opus_data, size_t opus_size)
            {
                // 优先使用TTS解码器（48kHz），如果没有则使用主解码器（48kHz）
                OpusDecoder* decoder            = nullptr;
                int          target_sample_rate = 0;

                if (pImpl_->tts_decoder)
                {
                    decoder            = pImpl_->tts_decoder.get();
                    target_sample_rate = OPUS_TTS_SAMPLE_RATE;
                }
                else if (pImpl_->webrtc_decoder)
                {
                    decoder            = pImpl_->webrtc_decoder.get();
                    target_sample_rate = pImpl_->config.sample_rate;
                }
                else
                {
                    LOG_ERROR("AudioSystem", "无可用解码器");
                    return nullptr;
                }

                // 打印解码器配置（仅第一次）
                static std::atomic<bool> first_decode{true};
                if (first_decode.exchange(false))
                {
                    LOG_INFO("AudioSystem", "🔊 TTS解码器配置:");
                    LOG_INFO("AudioSystem", "  使用: %d Hz 解码器", target_sample_rate);
                    LOG_INFO("AudioSystem", "  声道数: %d", pImpl_->config.channels);
                    LOG_INFO("AudioSystem", "  帧时长: %d ms", pImpl_->config.frame_duration_ms);
                }

                // 计算PCM帧大小（使用目标采样率）
                int frame_size =
                    target_sample_rate / MILLISECONDS_PER_SECOND * pImpl_->config.frame_duration_ms;
                size_t pcm_size =
                    static_cast<size_t>(frame_size) * pImpl_->config.channels * sizeof(int16_t);

                // 分配输出帧
                auto decoded_frame = pImpl_->mem_pool->allocate(pcm_size);
                if (!decoded_frame)
                {
                    LOG_ERROR("AudioSystem", "解码缓冲区分配失败");
                    return nullptr;
                }

                // 解码
                int decoded_samples = opus_decode(decoder, opus_data, opus_size,
                                                  decoded_frame->getData<int16_t>(), frame_size, 0);

                if (decoded_samples < 0)
                {
                    LOG_ERROR("AudioSystem", "Opus解码失败: %s (opus_size=%zu)",
                              opus_strerror(decoded_samples), opus_size);
                    return nullptr;
                }

                // 检查解码结果
                if (decoded_samples != frame_size)
                {
                    LOG_WARN("AudioSystem", "解码样本数不匹配: 获得 %d，期望 %d", decoded_samples,
                             frame_size);
                }

                // 打印PCM样本统计（前N个包）
                static std::atomic<int> decode_count{0};
                int                     count = decode_count.fetch_add(1);
                if (count < PCM_STATS_SAMPLE_LIMIT)
                {
                    const int16_t* pcm = decoded_frame->getData<int16_t>();
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    auto [min_it, max_it] = std::minmax_element(pcm, pcm + decoded_samples);
                    int16_t min_val       = *min_it;
                    int16_t max_val       = *max_it;
                    LOG_INFO("AudioSystem", "  PCM #%d: 样本数=%d, 范围=[%d, %d]", count,
                             decoded_samples, min_val, max_val);
                }

                decoded_frame->size = static_cast<size_t>(decoded_samples) *
                                      pImpl_->config.channels * sizeof(int16_t);
                pImpl_->stats.decode_count.fetch_add(1);

                return decoded_frame;
            }

            size_t AudioSystem::encodeOpusFrames(const int16_t* pcm_data, size_t pcm_size,
                                                 std::vector<AudioFramePtr>& frames)
            {
                if (!pImpl_->webrtc_encoder)
                {
                    LOG_ERROR("AudioSystem", "WebRTC编码器未初始化");
                    return 0;
                }

                // 计算单帧样本数
                int samples_per_frame = pImpl_->config.sample_rate / MILLISECONDS_PER_SECOND *
                                        pImpl_->config.frame_duration_ms;
                size_t total_samples = pcm_size / sizeof(int16_t) / pImpl_->config.channels;

                size_t encoded_count = 0;
                size_t offset        = 0;

                // 分帧编码
                while (offset < total_samples)
                {
                    int current_frame_size =
                        std::min(samples_per_frame, static_cast<int>(total_samples - offset));

                    // 分配输出帧
                    auto encoded_frame =
                        pImpl_->mem_pool->allocate(OPUS_MAX_FRAME_BYTES); // Opus最大4KB
                    if (!encoded_frame)
                    {
                        LOG_ERROR("AudioSystem", "编码缓冲区分配失败");
                        break;
                    }

                    // 编码
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    int encoded_bytes = opus_encode(pImpl_->webrtc_encoder.get(), pcm_data + offset,
                                                    current_frame_size, encoded_frame->data,
                                                    encoded_frame->capacity);

                    if (encoded_bytes > 0)
                    {
                        encoded_frame->size      = encoded_bytes;
                        encoded_frame->timestamp = get_nowus();
                        frames.push_back(encoded_frame);
                        encoded_count++;
                        pImpl_->stats.encode_count.fetch_add(1);
                    }
                    else
                    {
                        LOG_ERROR("AudioSystem", "帧编码失败: %s", opus_strerror(encoded_bytes));
                    }

                    offset += current_frame_size;
                }

                LOG_DEBUG("AudioSystem", "编码了 %zu 个样本为 %zu 帧", total_samples,
                          encoded_count);
                return encoded_count;
            }

            // ============================================================================
            // AI/WebRTC音频流管理
            // ============================================================================

            AudioError AudioSystem::startAIStream()
            {
                if (!pImpl_->initialized.load())
                {
                    LOG_ERROR("AudioSystem", "未初始化");
                    return AudioError::NOT_INITIALIZED;
                }

                if (pImpl_->is_ai_streaming.load())
                {
                    LOG_WARN("AudioSystem", "AI流已经启动");
                    return AudioError::ALREADY_RUNNING;
                }

                LOG_INFO("AudioSystem", "启动AI音频流...");

                // 设置主状态为AI
                LOG_DEBUG("AudioSystem", "  步骤 1: 设置主状态为AI...");
                setMainState(AudioMainState::AI);
                LOG_DEBUG("AudioSystem", "  步骤 1: ✓ 主状态已设置");

                // 标记为流式传输
                LOG_DEBUG("AudioSystem", "  步骤 2: 设置流式传输标志...");
                pImpl_->is_ai_streaming.store(true);
                LOG_DEBUG("AudioSystem", "  步骤 2: ✓ 流式传输标志已设置 (is_ai_streaming=%d)",
                          pImpl_->is_ai_streaming.load());

                // 验证条件
                LOG_DEBUG("AudioSystem", "  验证:");
                LOG_DEBUG("AudioSystem", "    - 主状态: %d",
                          static_cast<int>(pImpl_->main_state.load()));
                LOG_DEBUG("AudioSystem", "    - is_ai_streaming: %d",
                          pImpl_->is_ai_streaming.load());
                LOG_DEBUG("AudioSystem", "    - ai_encoder: %p", pImpl_->ai_encoder.get());
                LOG_DEBUG("AudioSystem", "    - ai_resampler: %p", pImpl_->ai_resampler.get());

                LOG_INFO("AudioSystem", "AI音频流已启动");
                return AudioError::NONE;
            }

            AudioError AudioSystem::stopAIStream()
            {
                if (!pImpl_->is_ai_streaming.load())
                {
                    return AudioError::NONE;
                }

                LOG_INFO("AudioSystem", "停止AI音频流...");

                // 停止AI流标志，让录音回调不再处理AI数据
                pImpl_->is_ai_streaming.store(false);

                LOG_INFO("AudioSystem", "AI音频流已停止");
                return AudioError::NONE;
            }

            bool AudioSystem::isAIStreamActive() const
            {
                return pImpl_->is_ai_streaming.load();
            }

            AudioError AudioSystem::startWebRTCStream()
            {
                if (!pImpl_->initialized.load())
                {
                    LOG_ERROR("AudioSystem", "未初始化");
                    return AudioError::NOT_INITIALIZED;
                }

                if (pImpl_->is_webrtc_streaming.load())
                {
                    LOG_WARN("AudioSystem", "WebRTC流已经启动");
                    return AudioError::ALREADY_RUNNING;
                }

                LOG_INFO("AudioSystem", "启动WebRTC音频流...");

                // 设置主状态为WebRTC
                setMainState(AudioMainState::WEBRTC);

                // 标记为流式传输
                pImpl_->is_webrtc_streaming.store(true);

                LOG_INFO("AudioSystem", "WebRTC音频流已启动");
                return AudioError::NONE;
            }

            AudioError AudioSystem::stopWebRTCStream()
            {
                if (!pImpl_->is_webrtc_streaming.load())
                {
                    return AudioError::NONE;
                }

                LOG_INFO("AudioSystem", "停止WebRTC音频流...");

                pImpl_->is_webrtc_streaming.store(false);

                LOG_INFO("AudioSystem", "WebRTC音频流已停止");
                return AudioError::NONE;
            }

            bool AudioSystem::isWebRTCStreamActive() const
            {
                return pImpl_->is_webrtc_streaming.load();
            }

            // ============================================================================
            // 便利函数（一键启动/停止模式）
            // ============================================================================

            AudioError AudioSystem::startAIMode()
            {
                LOG_INFO("AudioSystem", "启动AI模式...");

                // 1. 设置主状态为AI
                setMainState(AudioMainState::AI);

                // 2. 开始录音（如果未启动）
                if (!isRecording())
                {
                    AudioError err = startRecord();
                    if (err != AudioError::NONE)
                    {
                        LOG_ERROR("AudioSystem", "启动录音失败");
                        return err;
                    }
                }

                // 3. 启动AI音频流
                AudioError err = startAIStream();
                if (err != AudioError::NONE)
                {
                    LOG_ERROR("AudioSystem", "启动AI流失败");
                    return err;
                }

                LOG_INFO("AudioSystem", "AI模式启动成功");
                return AudioError::NONE;
            }

            AudioError AudioSystem::stopAIMode()
            {
                LOG_INFO("AudioSystem", "停止AI模式...");

                // 1. 停止AI音频流
                stopAIStream();

                // 2. 停止录音
                stopRecord();

                // 3. 重置主状态
                setMainState(AudioMainState::NONE);

                LOG_INFO("AudioSystem", "AI模式已停止");
                return AudioError::NONE;
            }

            AudioError AudioSystem::startWebRTCMode()
            {
                LOG_INFO("AudioSystem", "启动WebRTC模式...");

                // 1. 设置主状态为WebRTC
                setMainState(AudioMainState::WEBRTC);

                // 2. 开始录音（如果未启动）
                if (!isRecording())
                {
                    AudioError err = startRecord();
                    if (err != AudioError::NONE)
                    {
                        LOG_ERROR("AudioSystem", "启动录音失败");
                        return err;
                    }
                }

                // 3. 启动WebRTC音频流
                AudioError err = startWebRTCStream();
                if (err != AudioError::NONE)
                {
                    LOG_ERROR("AudioSystem", "启动WebRTC流失败");
                    return err;
                }

                LOG_INFO("AudioSystem", "WebRTC模式启动成功");
                return AudioError::NONE;
            }

            AudioError AudioSystem::stopWebRTCMode()
            {
                LOG_INFO("AudioSystem", "停止WebRTC模式...");

                // 1. 停止WebRTC音频流
                stopWebRTCStream();

                // 2. 停止录音
                stopRecord();

                // 3. 重置主状态
                setMainState(AudioMainState::NONE);

                LOG_INFO("AudioSystem", "WebRTC模式已停止");
                return AudioError::NONE;
            }

            // ============================================================================
            // 全部完成！Audio实现完毕
            // ============================================================================

        } // namespace audio
    }     // namespace media
} // namespace app
