/**
 * @file audio.cc
 * @brief 音频系统实现
 */

#include "audio.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/file/file.hpp"
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
            using namespace tool::file;

            namespace
            {
                struct WAVHeader
                {
                    char     riff[4] = {'R', 'I', 'F', 'F'};
                    uint32_t file_size;
                    char     wave[4]      = {'W', 'A', 'V', 'E'};
                    char     fmt[4]       = {'f', 'm', 't', ' '};
                    uint32_t fmt_size     = 16;
                    uint16_t audio_format = 1;
                    uint16_t num_channels;
                    uint32_t sample_rate;
                    uint32_t byte_rate;
                    uint16_t block_align;
                    uint16_t bits_per_sample = 16;
                    char     data[4]         = {'d', 'a', 't', 'a'};
                    uint32_t data_size;
                };

                bool writeWAVHeader(FileWrapper& file, int sample_rate, int channels,
                                    size_t data_size)
                {
                    WAVHeader header{};
                    header.num_channels    = static_cast<uint16_t>(channels);
                    header.sample_rate     = static_cast<uint32_t>(sample_rate);
                    header.bits_per_sample = 16;
                    header.block_align =
                        static_cast<uint16_t>(channels * header.bits_per_sample / 8);
                    header.byte_rate = header.sample_rate * header.block_align;
                    header.data_size = static_cast<uint32_t>(data_size);
                    header.file_size = static_cast<uint32_t>(sizeof(WAVHeader) - 8 + data_size);
                    return file.write(&header, sizeof(WAVHeader));
                }

                bool updateWAVHeader(FileWrapper& file, size_t data_size)
                {
                    long current_pos = file.getPosition();
                    if (current_pos < 0)
                        return false;

                    if (!file.seek(4, SEEK_SET))
                        return false;
                    uint32_t file_size = static_cast<uint32_t>(sizeof(WAVHeader) - 8 + data_size);
                    if (!file.write(&file_size, sizeof(uint32_t)))
                        return false;

                    if (!file.seek(40, SEEK_SET))
                        return false;
                    uint32_t data_size_32 = static_cast<uint32_t>(data_size);
                    if (!file.write(&data_size_32, sizeof(uint32_t)))
                        return false;

                    file.seek(current_pos, SEEK_SET);
                    return true;
                }

                constexpr const char* LOG_TAG                   = "AUDIO";
                constexpr double      BYTES_PER_KIB             = 1024.0;
                constexpr double      BYTES_PER_MIB             = BYTES_PER_KIB * 1024.0;
                constexpr double      FIXED_POOL_WARN_THRESHOLD = 70.0;
                constexpr uint64_t    FIXED_POOL_MIN_SAMPLES    = 100;
                constexpr int         MIN_OUTPUT_VOLUME         = 0;
                constexpr int         MAX_OUTPUT_VOLUME         = 100;
                constexpr int         DEFAULT_OUTPUT_VOLUME     = 50;
                constexpr float       VOLUME_TO_GAIN_FACTOR     = 0.02F;
                constexpr double      SAMPLE_RATE_INPUT_HZ      = 48000.0;
                constexpr double      SAMPLE_RATE_TARGET_HZ     = 16000.0;
                constexpr double      RESAMPLE_RATIO = SAMPLE_RATE_TARGET_HZ / SAMPLE_RATE_INPUT_HZ;
                constexpr float       PCM_NORMALIZE_DENOMINATOR = 32768.0F;
                constexpr float       PCM_CLAMP_NUMERATOR       = 32767.0F;
                constexpr float       PCM_CLAMP_MIN             = -1.0F;
                constexpr float       PCM_CLAMP_MAX             = 1.0F;
                constexpr size_t      WAKEWORD_BUFFER_MAX       = 16000;
                constexpr int         TARGET_FRAME_SAMPLES      = 320;
                constexpr size_t      AI_BUFFER_MAX_SAMPLES     = 16000;
                constexpr int         OPUS_TTS_SAMPLE_RATE      = 48000;
                constexpr int         OPUS_AI_SAMPLE_RATE       = 16000;
                constexpr size_t      OPUS_MAX_FRAME_BYTES      = 4096;
                constexpr int         MS_PER_SEC                = 1000;
                constexpr size_t      CACHE_LINE_ALIGNMENT      = 64;
                constexpr double      MEMORY_EXPANSION_FACTOR   = 2.0;
                constexpr size_t      FLOAT_BUFFER_SAMPLES      = 2048;
                constexpr int         PCM_STATS_SAMPLE_LIMIT    = 10;
            } // namespace

            // AudioMemoryPool::FixedPool
            AudioMemoryPool::FixedPool::FixedPool(size_t block_count)
                : actual_block_count(std::min(block_count, MAX_BLOCKS))
            {
                for (auto& bitmap_word : allocation_bitmap)
                    bitmap_word.store(0, std::memory_order_relaxed);

                blocks.resize(actual_block_count);
                frame_objects.resize(actual_block_count);

                for (size_t i = 0; i < actual_block_count; i++)
                {
                    frame_objects[i].is_from_fixed_pool = true;
                    frame_objects[i].fixed_pool_index   = static_cast<int>(i);
                }

                LOG_INFO(LOG_TAG, "固定内存池: %zu块 × %zu字节 = %.2fKB", actual_block_count,
                         BLOCK_SIZE, (actual_block_count * BLOCK_SIZE) / BYTES_PER_KIB);
            }

            uint8_t* AudioMemoryPool::FixedPool::getBlockPtr(int index)
            {
                if (index < 0 || index >= static_cast<int>(actual_block_count))
                    return nullptr;
                return blocks[static_cast<size_t>(index)].data();
            }

            int AudioMemoryPool::FixedPool::allocateBlock()
            {
                int bits_per_word = static_cast<int>(BITS_PER_WORD);
                int bitmap_count =
                    static_cast<int>((actual_block_count + BITS_PER_WORD - 1) / BITS_PER_WORD);

                for (int bitmap_index = 0; bitmap_index < bitmap_count; ++bitmap_index)
                {
                    auto& bitmap_atomic = allocation_bitmap.at(static_cast<size_t>(bitmap_index));
                    uint64_t bitmap     = bitmap_atomic.load(std::memory_order_acquire);

                    int base_index = bitmap_index * bits_per_word;
                    int max_blocks =
                        std::min(bits_per_word, static_cast<int>(actual_block_count) - base_index);
                    if (max_blocks <= 0)
                        break;

                    while (bitmap != UINT64_MAX)
                    {
                        uint64_t inverted = ~bitmap;
                        if (inverted == 0U)
                            break;

                        int free_bit = __builtin_ctzll(inverted);
                        if (free_bit >= max_blocks)
                            break;

                        uint64_t new_bitmap = bitmap | (1ULL << free_bit);
                        if (bitmap_atomic.compare_exchange_weak(bitmap, new_bitmap,
                                                                std::memory_order_acq_rel,
                                                                std::memory_order_acquire))
                            return base_index + free_bit;
                    }
                }
                return -1;
            }

            void AudioMemoryPool::FixedPool::deallocateBlock(int index)
            {
                if (index < 0 || index >= static_cast<int>(actual_block_count))
                {
                    LOG_ERROR(LOG_TAG, "无效的块索引: %d", index);
                    return;
                }

                int      bits_per_word = static_cast<int>(BITS_PER_WORD);
                int      bitmap_index  = index / bits_per_word;
                int      bit_position  = index % bits_per_word;
                uint64_t mask          = ~(1ULL << bit_position);
                allocation_bitmap.at(static_cast<size_t>(bitmap_index))
                    .fetch_and(mask, std::memory_order_release);
            }

            // AudioMemoryPool
            AudioMemoryPool::AudioMemoryPool(const AudioMemoryPoolConfig& config) : config_(config)
            {
                LOG_INFO(LOG_TAG, "初始化音频内存池...");
                fixed_pool_ = std::make_unique<FixedPool>(config_.fixed_block_count);

                if (config_.dynamic_pool_size > 0)
                {
                    dynamic_pool_ = std::make_unique<tool::memory::MemoryPool>(
                        config_.dynamic_pool_size, CACHE_LINE_ALIGNMENT, MEMORY_EXPANSION_FACTOR);
                    LOG_INFO(LOG_TAG, "  动态池: %.2fMB",
                             config_.dynamic_pool_size / BYTES_PER_MIB);
                }
            }

            AudioMemoryPool::~AudioMemoryPool()
            {
                logStats();
            }

            AudioFramePtr AudioMemoryPool::allocate(size_t size)
            {
                stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);

                if (size <= config_.fixed_block_size)
                {
                    auto frame = allocateFromFixed(size);
                    if (frame)
                    {
                        stats_.fixed_pool_hits.fetch_add(1, std::memory_order_relaxed);
                        return frame;
                    }
                }

                auto frame = allocateFromDynamic(size);
                if (frame)
                {
                    stats_.dynamic_pool_hits.fetch_add(1, std::memory_order_relaxed);
                    return frame;
                }

                stats_.allocation_failures.fetch_add(1, std::memory_order_relaxed);
                LOG_ERROR(LOG_TAG, "内存分配失败: %zu字节", size);
                return nullptr;
            }

            AudioFramePtr AudioMemoryPool::allocateFromFixed(size_t size)
            {
                if (size > config_.fixed_block_size)
                    return nullptr;

                int block_index = fixed_pool_->allocateBlock();
                if (block_index < 0)
                    return nullptr;

                AudioFrame* frame         = &fixed_pool_->frame_objects[block_index];
                frame->data               = fixed_pool_->getBlockPtr(block_index);
                frame->capacity           = FixedPool::BLOCK_SIZE;
                frame->size               = size;
                frame->timestamp          = get_nowus();
                frame->is_from_fixed_pool = true;
                frame->fixed_pool_index   = block_index;

                auto* pool_ptr = fixed_pool_.get();
                return std::shared_ptr<AudioFrame>(
                    frame,
                    [pool_ptr](AudioFrame* f)
                    {
                        if (f && f->is_from_fixed_pool && f->fixed_pool_index >= 0)
                            pool_ptr->deallocateBlock(f->fixed_pool_index);
                    });
            }

            AudioFramePtr AudioMemoryPool::allocateFromDynamic(size_t size)
            {
                if (!dynamic_pool_)
                    return nullptr;

                void* buffer = dynamic_pool_->allocate(size);
                if (!buffer)
                    return nullptr;

                auto frame = std::shared_ptr<AudioFrame>(new AudioFrame(),
                                                         [this](AudioFrame* f)
                                                         {
                                                             if (f->data && dynamic_pool_)
                                                                 dynamic_pool_->deallocate(f->data);
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
                uint64_t total = stats_.total_allocations.load();
                if (total == 0)
                    return;

                uint64_t fixed_hits   = stats_.fixed_pool_hits.load();
                uint64_t dynamic_hits = stats_.dynamic_pool_hits.load();
                uint64_t failures     = stats_.allocation_failures.load();
                double   fixed_rate   = (double)fixed_hits / total * 100.0;

                LOG_INFO(LOG_TAG,
                         "内存池统计: 总分配=%llu, 固定池=%llu(%.1f%%), 动态池=%llu, 失败=%llu",
                         total, fixed_hits, fixed_rate, dynamic_hits, failures);

                if (fixed_rate < FIXED_POOL_WARN_THRESHOLD && total > FIXED_POOL_MIN_SAMPLES)
                    LOG_WARN(LOG_TAG, "固定池命中率偏低(%.1f%%)", fixed_rate);
            }

            // AudioSystem::Impl
            class AudioSystem::Impl
            {
            public:
                AudioConfig config;

                std::atomic<AudioMainState>     main_state{AudioMainState::NONE};
                std::atomic<AudioControlState>  control_state{AudioControlState::NONE};
                std::atomic<AudioFunctionState> function_state{AudioFunctionState::NONE};

                std::atomic<bool> initialized{false};
                std::atomic<bool> is_recording{false};
                std::atomic<bool> is_playing{false};
                std::atomic<bool> is_ai_streaming{false};
                std::atomic<bool> is_webrtc_streaming{false};
                std::atomic<int>  output_volume{DEFAULT_OUTPUT_VOLUME};

                std::unique_ptr<AudioMemoryPool> mem_pool;

                OpusEncoderPtr webrtc_encoder;
                OpusDecoderPtr webrtc_decoder;
                OpusDecoderPtr tts_decoder;
                OpusEncoderPtr ai_encoder;

                SrcStatePtr          ai_resampler;
                std::vector<int16_t> ai_resample_buffer;
                SrcStatePtr          wakeword_resampler;
                std::vector<int16_t> wakeword_resample_buffer;

                SpeexStatePtr speex_state;
                PaStreamPtr   record_stream;
                PaStreamPtr   playback_stream;

                std::queue<AudioFramePtr> record_queue;
                std::queue<AudioFramePtr> playback_queue;
                std::mutex                record_queue_mutex;
                std::mutex                playback_queue_mutex;
                std::condition_variable   record_queue_cv;

                mutable std::mutex                     callback_mutex;
                AudioFrameCallback                     ai_audio_callback;
                AudioFrameCallback                     webrtc_audio_callback;
                WakewordCallback                       wakeword_callback;
                StateChangeCallback<AudioMainState>    main_state_callback;
                StateChangeCallback<AudioControlState> control_state_callback;

                std::shared_ptr<sync_context_t> sync_ctx;

                std::unique_ptr<FileWrapper>          record_file_;
                int                                   record_id_           = 0;
                int                                   record_duration_sec_ = 0;
                std::chrono::steady_clock::time_point record_start_time_;
                std::mutex                            record_file_mutex_;
                std::atomic<bool>                     is_recording_to_file_{false};
                size_t                                record_data_size_ = 0;

                Stats stats;

                alignas(CACHE_LINE_ALIGNMENT)
                    std::array<uint8_t, OPUS_MAX_FRAME_BYTES> temp_opus_buffer;
                alignas(CACHE_LINE_ALIGNMENT)
                    std::array<float, FLOAT_BUFFER_SAMPLES> temp_float_buffer_in;
                alignas(CACHE_LINE_ALIGNMENT)
                    std::array<float, FLOAT_BUFFER_SAMPLES> temp_float_buffer_out;

                explicit Impl(const AudioConfig& cfg) : config(cfg) {}
                ~Impl() = default;

                static int recordCallback(const void* input_buffer, void* output_buffer,
                                          unsigned long                   frames_per_buffer,
                                          const PaStreamCallbackTimeInfo* time_info,
                                          PaStreamCallbackFlags status_flags, void* user_data);

                static int playbackCallback(const void* input_buffer, void* output_buffer,
                                            unsigned long                   frames_per_buffer,
                                            const PaStreamCallbackTimeInfo* time_info,
                                            PaStreamCallbackFlags status_flags, void* user_data);

                struct PaStreamConfig
                {
                    double              sample_rate;
                    int                 frame_duration_ms;
                    PaStreamParameters* input_params;
                    PaStreamParameters* output_params;
                    PaStreamCallback*   callback;
                    AudioControlState   control_state;
                    std::atomic<bool>*  running_flag;
                    PaStreamPtr*        stream_ptr;
                    const char*         direction_name;
                };

                AudioError openAndStartPaStream(const PaStreamConfig& config)
                {
                    int frames_per_buffer =
                        config.sample_rate / MS_PER_SEC * config.frame_duration_ms;

                    PaStream* stream = nullptr;
                    PaError err = Pa_OpenStream(&stream, config.input_params, config.output_params,
                                                config.sample_rate, frames_per_buffer, paClipOff,
                                                config.callback, this);
                    if (err != paNoError)
                    {
                        LOG_ERROR(LOG_TAG, "打开%s流失败: %s", config.direction_name,
                                  Pa_GetErrorText(err));
                        return AudioError::STREAM_OPEN_FAILED;
                    }

                    err = Pa_StartStream(stream);
                    if (err != paNoError)
                    {
                        LOG_ERROR(LOG_TAG, "启动%s流失败: %s", config.direction_name,
                                  Pa_GetErrorText(err));
                        Pa_CloseStream(stream);
                        return AudioError::STREAM_START_FAILED;
                    }

                    config.stream_ptr->reset(stream);
                    config.running_flag->store(true);
                    setControlState(config.control_state);
                    return AudioError::NONE;
                }

                struct StreamTypeConfig
                {
                    AudioMainState     main_state;
                    std::atomic<bool>* streaming_flag;
                    const char*        stream_name;
                };

                StreamTypeConfig getStreamTypeConfig(StreamType type)
                {
                    return (type == StreamType::AI)
                               ? StreamTypeConfig{AudioMainState::AI, &is_ai_streaming, "AI"}
                               : StreamTypeConfig{AudioMainState::WEBRTC, &is_webrtc_streaming,
                                                  "WebRTC"};
                }

                void setMainState(AudioMainState new_state)
                {
                    AudioMainState old_state =
                        main_state.exchange(new_state, std::memory_order_acq_rel);
                    if (old_state != new_state)
                    {
                        LOG_INFO(LOG_TAG, "主状态: %d -> %d", static_cast<int>(old_state),
                                 static_cast<int>(new_state));
                        std::unique_lock<std::mutex> lock(callback_mutex, std::try_to_lock);
                        if (lock.owns_lock() && main_state_callback)
                        {
                            try
                            {
                                main_state_callback(old_state, new_state);
                            }
                            catch (const std::exception& e)
                            {
                                LOG_ERROR(LOG_TAG, "主状态回调异常: %s", e.what());
                            }
                        }
                    }
                }

                void setControlState(AudioControlState new_state)
                {
                    AudioControlState old_state =
                        control_state.exchange(new_state, std::memory_order_acq_rel);
                    if (old_state != new_state)
                    {
                        std::unique_lock<std::mutex> lock(callback_mutex, std::try_to_lock);
                        if (lock.owns_lock() && control_state_callback)
                        {
                            try
                            {
                                control_state_callback(old_state, new_state);
                            }
                            catch (const std::exception& e)
                            {
                                LOG_ERROR(LOG_TAG, "控制状态回调异常: %s", e.what());
                            }
                        }
                    }
                }

                void invokeAICallback(AudioFramePtr frame)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);
                    if (ai_audio_callback)
                    {
                        try
                        {
                            ai_audio_callback(std::move(frame));
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "AI回调异常: %s", e.what());
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
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "WebRTC回调异常: %s", e.what());
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
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "唤醒词回调异常: %s", e.what());
                        }
                    }
                }
            };

            // AudioSystem
            AudioSystem::AudioSystem(const AudioConfig& config)
                : pImpl_(std::make_unique<Impl>(config))
            {
                LOG_INFO(LOG_TAG, "音频系统已创建");
            }

            AudioSystem::~AudioSystem()
            {
                deinit();
                LOG_INFO(LOG_TAG, "音频系统已销毁");
            }

            AudioError AudioSystem::init(std::shared_ptr<sync_context_t> sync_ctx)
            {
                if (pImpl_->initialized.load())
                {
                    LOG_WARN(LOG_TAG, "已经初始化过了");
                    return AudioError::ALREADY_RUNNING;
                }

                LOG_INFO(LOG_TAG, "初始化音频系统...");
                pImpl_->sync_ctx = std::move(sync_ctx);
                createDirectory(pImpl_->config.record_path);

                pImpl_->mem_pool =
                    std::make_unique<AudioMemoryPool>(pImpl_->config.mem_pool_config);

                PaError err = Pa_Initialize();
                if (err != paNoError)
                {
                    LOG_ERROR(LOG_TAG, "PortAudio初始化失败: %s", Pa_GetErrorText(err));
                    return AudioError::INITIALIZE_FAILED;
                }
                LOG_INFO(LOG_TAG, "  PortAudio: %s", Pa_GetVersionText());

                int  opus_error      = 0;
                int  src_error       = 0;
                bool all_initialized = true;

                // WebRTC编码器
                OpusEncoder* webrtc_enc =
                    opus_encoder_create(pImpl_->config.sample_rate, pImpl_->config.channels,
                                        OPUS_APPLICATION_VOIP, &opus_error);
                if (opus_error == OPUS_OK && webrtc_enc)
                {
                    pImpl_->webrtc_encoder.reset(webrtc_enc);
                    opus_encoder_ctl(webrtc_enc, OPUS_SET_BITRATE(64000));
                    opus_encoder_ctl(webrtc_enc, OPUS_SET_VBR(1));
                    opus_encoder_ctl(webrtc_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                    LOG_INFO(LOG_TAG, "  WebRTC编码器: 48kHz, 64kbps");
                }
                else
                    all_initialized = false;

                // WebRTC解码器
                OpusDecoder* webrtc_dec = opus_decoder_create(pImpl_->config.sample_rate,
                                                              pImpl_->config.channels, &opus_error);
                if (opus_error == OPUS_OK && webrtc_dec)
                {
                    pImpl_->webrtc_decoder.reset(webrtc_dec);
                    LOG_INFO(LOG_TAG, "  WebRTC解码器: 48kHz");
                }
                else
                    all_initialized = false;

                // TTS解码器
                OpusDecoder* tts_dec = opus_decoder_create(OPUS_TTS_SAMPLE_RATE, 1, &opus_error);
                if (opus_error == OPUS_OK && tts_dec)
                {
                    pImpl_->tts_decoder.reset(tts_dec);
                    LOG_INFO(LOG_TAG, "  TTS解码器: 48kHz");
                }
                else
                    all_initialized = false;

                // AI编码器
                OpusEncoder* ai_enc =
                    opus_encoder_create(OPUS_AI_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &opus_error);
                if (opus_error == OPUS_OK && ai_enc)
                {
                    pImpl_->ai_encoder.reset(ai_enc);
                    opus_encoder_ctl(ai_enc, OPUS_SET_BITRATE(32000));
                    opus_encoder_ctl(ai_enc, OPUS_SET_VBR(1));
                    opus_encoder_ctl(ai_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                    LOG_INFO(LOG_TAG, "  AI编码器: 16kHz, 32kbps");
                }
                else
                    all_initialized = false;

                // 重采样器
                SRC_STATE* ai_resampler = src_new(SRC_SINC_BEST_QUALITY, 1, &src_error);
                if (ai_resampler)
                {
                    pImpl_->ai_resampler.reset(ai_resampler);
                    LOG_INFO(LOG_TAG, "  AI重采样器: 48kHz->16kHz");
                }
                else
                    all_initialized = false;

                SRC_STATE* wakeword_resampler = src_new(SRC_SINC_FASTEST, 1, &src_error);
                if (wakeword_resampler)
                {
                    pImpl_->wakeword_resampler.reset(wakeword_resampler);
                    LOG_INFO(LOG_TAG, "  唤醒词重采样器: 48kHz->16kHz");
                }
                else
                    all_initialized = false;

                if (!all_initialized)
                {
                    LOG_ERROR(LOG_TAG, "部分组件初始化失败");
                    pImpl_->webrtc_encoder.reset();
                    pImpl_->webrtc_decoder.reset();
                    pImpl_->tts_decoder.reset();
                    pImpl_->ai_encoder.reset();
                    pImpl_->ai_resampler.reset();
                    pImpl_->wakeword_resampler.reset();
                    Pa_Terminate();
                    return AudioError::INITIALIZE_FAILED;
                }

                // 3A算法
                if (pImpl_->config.enable_denoise || pImpl_->config.enable_agc)
                {
                    int frame_size =
                        pImpl_->config.sample_rate * pImpl_->config.frame_duration_ms / MS_PER_SEC;
                    SpeexPreprocessState* speex =
                        speex_preprocess_state_init(frame_size, pImpl_->config.sample_rate);
                    if (speex)
                    {
                        pImpl_->speex_state.reset(speex);
                        int denoise = pImpl_->config.enable_denoise ? 1 : 0;
                        int agc     = pImpl_->config.enable_agc ? 1 : 0;
                        speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
                        speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_AGC, &agc);
                        if (agc)
                        {
                            float agc_level = pImpl_->config.agc_level;
                            speex_preprocess_ctl(speex, SPEEX_PREPROCESS_SET_AGC_LEVEL, &agc_level);
                        }
                        LOG_INFO(LOG_TAG, "  3A: 降噪=%s, AGC=%s", denoise ? "开" : "关",
                                 agc ? "开" : "关");
                    }
                }

                pImpl_->initialized.store(true);
                pImpl_->output_volume.store(pImpl_->config.output_volume);
                LOG_INFO(LOG_TAG, "音频系统初始化完成");
                return AudioError::NONE;
            }

            void AudioSystem::deinit()
            {
                if (!pImpl_->initialized.load())
                    return;

                stopRecord();
                LOG_INFO(LOG_TAG, "关闭音频系统...");

                stopStream(StreamDirection::INPUT);
                stopStream(StreamDirection::OUTPUT);
                clearRecordQueue();
                clearPlaybackQueue();
                Pa_Terminate();

                if (pImpl_->mem_pool)
                    pImpl_->mem_pool->logStats();

                pImpl_->initialized.store(false);
                LOG_INFO(LOG_TAG, "音频系统已关闭");
            }

            bool AudioSystem::isInitialized() const
            {
                return pImpl_->initialized.load();
            }

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
                pImpl_->function_state.exchange(state);
                return AudioError::NONE;
            }
            AudioFunctionState AudioSystem::getFunctionState() const
            {
                return pImpl_->function_state.load();
            }

            void AudioSystem::setOutputVolume(int volume)
            {
                volume = std::clamp(volume, MIN_OUTPUT_VOLUME, MAX_OUTPUT_VOLUME);
                pImpl_->output_volume.store(volume, std::memory_order_relaxed);
                LOG_INFO(LOG_TAG, "音量: %d%%", volume);
            }
            int AudioSystem::getOutputVolume() const
            {
                return pImpl_->output_volume.load(std::memory_order_relaxed);
            }

            void AudioSystem::setAIAudioCallback(AudioFrameCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->ai_audio_callback = std::move(callback);
            }

            void AudioSystem::setWebRTCAudioCallback(AudioFrameCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->webrtc_audio_callback = std::move(callback);
            }

            void AudioSystem::setWakewordCallback(WakewordCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->wakeword_callback = std::move(callback);
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

            void AudioSystem::clearRecordQueue()
            {
                std::lock_guard<std::mutex> lock(pImpl_->record_queue_mutex);
                std::queue<AudioFramePtr>   empty;
                std::swap(pImpl_->record_queue, empty);
            }

            void AudioSystem::clearPlaybackQueue()
            {
                std::lock_guard<std::mutex> lock(pImpl_->playback_queue_mutex);
                std::queue<AudioFramePtr>   empty;
                std::swap(pImpl_->playback_queue, empty);
            }

            AudioError AudioSystem::startRecord(const std::string& filename, int duration_sec)
            {
                if (!pImpl_->initialized.load())
                    return AudioError::NOT_INITIALIZED;

                std::lock_guard<std::mutex> lock(pImpl_->record_file_mutex_);
                if (pImpl_->is_recording_to_file_.load())
                    return AudioError::ALREADY_RUNNING;

                std::string record_filename =
                    filename.empty() ? pImpl_->config.record_path + "record_" +
                                           std::to_string(pImpl_->record_id_++) + ".wav"
                                     : filename;

                if (record_filename.size() < 4 ||
                    record_filename.substr(record_filename.size() - 4) != ".wav")
                    if (record_filename.back() != '/')
                        record_filename += ".wav";

                pImpl_->record_file_ =
                    std::make_unique<FileWrapper>(record_filename, FileMode::WRITE);
                if (!pImpl_->record_file_->isValid())
                {
                    LOG_ERROR(LOG_TAG, "创建录音文件失败: %s", record_filename.c_str());
                    pImpl_->record_file_.reset();
                    return AudioError::STREAM_OPEN_FAILED;
                }

                if (!writeWAVHeader(*pImpl_->record_file_, pImpl_->config.sample_rate,
                                    pImpl_->config.channels, 0))
                {
                    pImpl_->record_file_.reset();
                    return AudioError::STREAM_OPEN_FAILED;
                }

                pImpl_->record_duration_sec_ = duration_sec;
                pImpl_->record_start_time_   = std::chrono::steady_clock::now();
                pImpl_->record_data_size_    = 0;
                pImpl_->is_recording_to_file_.store(true);

                LOG_INFO(LOG_TAG, "录音已启动: %s", record_filename.c_str());
                return AudioError::NONE;
            }

            AudioError AudioSystem::stopRecord()
            {
                std::lock_guard<std::mutex> lock(pImpl_->record_file_mutex_);
                if (!pImpl_->is_recording_to_file_.load())
                    return AudioError::NONE;

                pImpl_->is_recording_to_file_.store(false);

                if (pImpl_->record_file_)
                {
                    pImpl_->record_file_->flush();
                    if (pImpl_->record_data_size_ > 0)
                    {
                        updateWAVHeader(*pImpl_->record_file_, pImpl_->record_data_size_);
                        pImpl_->record_file_->flush();
                    }
                    LOG_INFO(LOG_TAG, "录音已停止: %zu字节", pImpl_->record_data_size_);
                    pImpl_->record_file_.reset();
                    pImpl_->record_data_size_ = 0;
                }
                return AudioError::NONE;
            }

            bool AudioSystem::isRecording() const
            {
                return pImpl_->is_recording_to_file_.load();
            }

            AudioFramePtr AudioSystem::getRecordedFrame(std::chrono::milliseconds timeout)
            {
                std::unique_lock<std::mutex> lock(pImpl_->record_queue_mutex);
                if (pImpl_->record_queue.empty())
                {
                    if (!pImpl_->record_queue_cv.wait_for(
                            lock, timeout,
                            [this]() {
                                return !pImpl_->record_queue.empty() ||
                                       !pImpl_->is_recording.load();
                            }))
                        return nullptr;
                }

                if (pImpl_->record_queue.empty())
                    return nullptr;

                AudioFramePtr frame = std::move(pImpl_->record_queue.front());
                pImpl_->record_queue.pop();
                return frame;
            }

            void AudioSystem::pushPlaybackFrame(AudioFramePtr frame)
            {
                if (!frame)
                    return;

                std::lock_guard<std::mutex> lock(pImpl_->playback_queue_mutex);
                if (pImpl_->playback_queue.size() >= pImpl_->config.max_playback_queue_size)
                {
                    pImpl_->playback_queue.pop();
                    pImpl_->stats.frames_dropped.fetch_add(1);
                }
                pImpl_->playback_queue.push(std::move(frame));
            }

            void AudioSystem::getStats(Stats& out_stats) const
            {
                out_stats.frames_recorded.store(pImpl_->stats.frames_recorded.load());
                out_stats.frames_played.store(pImpl_->stats.frames_played.load());
                out_stats.frames_dropped.store(pImpl_->stats.frames_dropped.load());
                out_stats.encode_count.store(pImpl_->stats.encode_count.load());
                out_stats.decode_count.store(pImpl_->stats.decode_count.load());
                if (pImpl_->mem_pool)
                    pImpl_->mem_pool->getStats(out_stats.mem_stats);
            }

            void AudioSystem::resetStats()
            {
                pImpl_->stats.frames_recorded.store(0);
                pImpl_->stats.frames_played.store(0);
                pImpl_->stats.frames_dropped.store(0);
                pImpl_->stats.encode_count.store(0);
                pImpl_->stats.decode_count.store(0);
                if (pImpl_->mem_pool)
                    pImpl_->mem_pool->resetStats();
            }

            void AudioSystem::logStats() const
            {
                LOG_INFO(LOG_TAG, "统计: 录制=%llu, 播放=%llu, 丢弃=%llu, 编码=%llu, 解码=%llu",
                         pImpl_->stats.frames_recorded.load(), pImpl_->stats.frames_played.load(),
                         pImpl_->stats.frames_dropped.load(), pImpl_->stats.encode_count.load(),
                         pImpl_->stats.decode_count.load());
                if (pImpl_->mem_pool)
                    pImpl_->mem_pool->logStats();
            }

            // 录音回调
            int AudioSystem::Impl::recordCallback(const void* input_buffer, void* output_buffer,
                                                  unsigned long                   frames_per_buffer,
                                                  const PaStreamCallbackTimeInfo* time_info,
                                                  PaStreamCallbackFlags           status_flags,
                                                  void*                           user_data)
            {
                (void)output_buffer;
                (void)time_info;
                (void)status_flags;

                auto*       impl  = static_cast<Impl*>(user_data);
                const auto* input = static_cast<const int16_t*>(input_buffer);
                size_t      frame_size_bytes =
                    frames_per_buffer * impl->config.channels * sizeof(int16_t);

                auto frame = impl->mem_pool->allocate(frame_size_bytes);
                if (!frame)
                    return paContinue;

                std::memcpy(frame->data, input, frame_size_bytes);
                frame->size      = frame_size_bytes;
                frame->timestamp = get_nowus();

                if (impl->speex_state)
                    speex_preprocess_run(impl->speex_state.get(), frame->getData<int16_t>());

                // 唤醒词检测
                if (!impl->is_ai_streaming.load() && impl->wakeword_callback &&
                    impl->wakeword_resampler)
                {
                    SRC_DATA src_data{};
                    float*   input_float  = impl->temp_float_buffer_in.data();
                    float*   output_float = impl->temp_float_buffer_out.data();

                    const int16_t* pcm_data = frame->getData<int16_t>();
                    std::transform(pcm_data, pcm_data + frames_per_buffer, input_float,
                                   [](int16_t s)
                                   { return static_cast<float>(s) / PCM_NORMALIZE_DENOMINATOR; });

                    src_data.data_in       = input_float;
                    src_data.input_frames  = static_cast<long>(frames_per_buffer);
                    src_data.data_out      = output_float;
                    src_data.output_frames = impl->temp_float_buffer_out.size();
                    src_data.src_ratio     = RESAMPLE_RATIO;
                    src_data.end_of_input  = 0;

                    if (src_process(impl->wakeword_resampler.get(), &src_data) == 0 &&
                        src_data.output_frames_gen > 0)
                    {
                        for (long i = 0;
                             i < src_data.output_frames_gen &&
                             impl->wakeword_resample_buffer.size() < WAKEWORD_BUFFER_MAX;
                             ++i)
                        {
                            float clamped =
                                std::clamp(output_float[i], PCM_CLAMP_MIN, PCM_CLAMP_MAX);
                            impl->wakeword_resample_buffer.push_back(
                                static_cast<int16_t>(clamped * PCM_CLAMP_NUMERATOR));
                        }

                        while (impl->wakeword_resample_buffer.size() >=
                               static_cast<size_t>(TARGET_FRAME_SAMPLES))
                        {
                            impl->invokeWakewordCallback(impl->wakeword_resample_buffer.data(),
                                                         TARGET_FRAME_SAMPLES);
                            impl->wakeword_resample_buffer.erase(
                                impl->wakeword_resample_buffer.begin(),
                                impl->wakeword_resample_buffer.begin() + TARGET_FRAME_SAMPLES);
                        }
                    }
                }

                // 录音队列
                {
                    std::lock_guard<std::mutex> lock(impl->record_queue_mutex);
                    if (impl->record_queue.size() >= impl->config.max_record_queue_size)
                    {
                        impl->record_queue.pop();
                        impl->stats.frames_dropped.fetch_add(1);
                    }
                    impl->record_queue.push(frame);
                }
                impl->record_queue_cv.notify_one();
                impl->stats.frames_recorded.fetch_add(1);

                // 录音文件存储
                if (impl->is_recording_to_file_.load())
                {
                    std::lock_guard<std::mutex> lock(impl->record_file_mutex_);
                    if (impl->record_file_ && impl->record_file_->isValid())
                    {
                        if (impl->record_file_->write(frame->data, frame->size))
                        {
                            impl->record_data_size_ += frame->size;
                            impl->record_file_->flush();

                            if (impl->record_duration_sec_ > 0)
                            {
                                auto elapsed =
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - impl->record_start_time_)
                                        .count();
                                if (elapsed >= impl->record_duration_sec_ * 1000)
                                    impl->is_recording_to_file_.store(false);
                            }
                        }
                    }
                }

                // WebRTC编码
                if (impl->main_state.load() == AudioMainState::WEBRTC &&
                    impl->is_webrtc_streaming.load() && impl->webrtc_encoder)
                {
                    uint8_t* opus_buffer = impl->temp_opus_buffer.data();
                    int      encoded_bytes =
                        opus_encode(impl->webrtc_encoder.get(), frame->getData<int16_t>(),
                                    frames_per_buffer, opus_buffer, impl->temp_opus_buffer.size());

                    if (encoded_bytes > 0)
                    {
                        auto encoded_frame = impl->mem_pool->allocate(encoded_bytes);
                        if (encoded_frame)
                        {
                            std::memcpy(encoded_frame->data, opus_buffer, encoded_bytes);
                            encoded_frame->size = encoded_bytes;
                            encoded_frame->timestamp =
                                impl->sync_ctx
                                    ? sync_get_timestamp(impl->sync_ctx.get(), get_nowus(), true)
                                    : get_nowus();
                            impl->invokeWebRTCCallback(encoded_frame);
                            impl->stats.encode_count.fetch_add(1);
                        }
                    }
                }

                // AI编码
                if (impl->main_state.load() == AudioMainState::AI && impl->is_ai_streaming.load() &&
                    impl->ai_encoder && impl->ai_resampler)
                {
                    SRC_DATA src_data{};
                    float*   input_float  = impl->temp_float_buffer_in.data();
                    float*   output_float = impl->temp_float_buffer_out.data();

                    const int16_t* pcm_data = frame->getData<int16_t>();
                    std::transform(pcm_data, pcm_data + frames_per_buffer, input_float,
                                   [](int16_t s)
                                   { return static_cast<float>(s) / PCM_NORMALIZE_DENOMINATOR; });

                    src_data.data_in       = input_float;
                    src_data.input_frames  = static_cast<long>(frames_per_buffer);
                    src_data.data_out      = output_float;
                    src_data.output_frames = impl->temp_float_buffer_out.size();
                    src_data.src_ratio     = RESAMPLE_RATIO;
                    src_data.end_of_input  = 0;

                    if (src_process(impl->ai_resampler.get(), &src_data) == 0)
                    {
                        for (long i = 0; i < src_data.output_frames_gen &&
                                         impl->ai_resample_buffer.size() < AI_BUFFER_MAX_SAMPLES;
                             ++i)
                        {
                            float clamped =
                                std::clamp(output_float[i], PCM_CLAMP_MIN, PCM_CLAMP_MAX);
                            impl->ai_resample_buffer.push_back(
                                static_cast<int16_t>(clamped * PCM_CLAMP_NUMERATOR));
                        }

                        while (impl->ai_resample_buffer.size() >=
                               static_cast<size_t>(TARGET_FRAME_SAMPLES))
                        {
                            uint8_t* opus_buffer   = impl->temp_opus_buffer.data();
                            int      encoded_bytes = opus_encode(
                                     impl->ai_encoder.get(), impl->ai_resample_buffer.data(),
                                     TARGET_FRAME_SAMPLES, opus_buffer, impl->temp_opus_buffer.size());

                            if (encoded_bytes > 0)
                            {
                                auto encoded_frame = impl->mem_pool->allocate(encoded_bytes);
                                if (encoded_frame)
                                {
                                    std::memcpy(encoded_frame->data, opus_buffer, encoded_bytes);
                                    encoded_frame->size      = encoded_bytes;
                                    encoded_frame->timestamp = get_nowus();
                                    impl->invokeAICallback(encoded_frame);
                                    impl->stats.encode_count.fetch_add(1);
                                }
                            }
                            impl->ai_resample_buffer.erase(impl->ai_resample_buffer.begin(),
                                                           impl->ai_resample_buffer.begin() +
                                                               TARGET_FRAME_SAMPLES);
                        }
                    }
                }

                return paContinue;
            }

            // 播放回调
            int AudioSystem::Impl::playbackCallback(const void* input_buffer, void* output_buffer,
                                                    unsigned long frames_per_buffer,
                                                    const PaStreamCallbackTimeInfo* time_info,
                                                    PaStreamCallbackFlags           status_flags,
                                                    void*                           user_data)
            {
                (void)input_buffer;
                (void)time_info;
                (void)status_flags;

                auto*  impl           = static_cast<Impl*>(user_data);
                auto*  output         = static_cast<int16_t*>(output_buffer);
                size_t samples_needed = frames_per_buffer * impl->config.channels;

                std::lock_guard<std::mutex> lock(impl->playback_queue_mutex);

                if (impl->playback_queue.empty())
                {
                    std::fill_n(output, samples_needed, 0);
                    return paContinue;
                }

                AudioFramePtr& frame           = impl->playback_queue.front();
                size_t         frame_samples   = frame->size / sizeof(int16_t);
                size_t         samples_to_copy = std::min(samples_needed, frame_samples);

                int   volume_percent = impl->output_volume.load(std::memory_order_relaxed);
                float gain           = volume_percent * VOLUME_TO_GAIN_FACTOR;

                const int16_t* src = frame->getData<int16_t>();
                std::transform(src, src + samples_to_copy, output,
                               [gain](int16_t s) { return static_cast<int16_t>(s * gain); });

                if (samples_to_copy < samples_needed)
                    std::fill_n(std::next(output, static_cast<std::ptrdiff_t>(samples_to_copy)),
                                samples_needed - samples_to_copy, 0);

                impl->playback_queue.pop();
                impl->stats.frames_played.fetch_add(1);

                return paContinue;
            }

            // 流控制
            AudioError AudioSystem::startStream(StreamDirection direction)
            {
                if (!pImpl_->initialized.load())
                {
                    LOG_ERROR(LOG_TAG, "未初始化");
                    return AudioError::NOT_INITIALIZED;
                }

                PaStreamParameters   input_params, output_params;
                PaStreamParameters*  input_params_ptr  = nullptr;
                PaStreamParameters*  output_params_ptr = nullptr;
                Impl::PaStreamConfig pa_config;

                if (direction == StreamDirection::INPUT)
                {
                    if (pImpl_->is_recording.load())
                        return AudioError::ALREADY_RUNNING;

                    input_params.device = Pa_GetDefaultInputDevice();
                    if (input_params.device == paNoDevice)
                    {
                        LOG_ERROR(LOG_TAG, "无输入设备");
                        return AudioError::DEVICE_NOT_FOUND;
                    }

                    input_params.channelCount = pImpl_->config.channels;
                    input_params.sampleFormat = paInt16;
                    input_params.suggestedLatency =
                        Pa_GetDeviceInfo(input_params.device)->defaultLowInputLatency;
                    input_params.hostApiSpecificStreamInfo = nullptr;
                    input_params_ptr                       = &input_params;

                    pa_config.sample_rate       = pImpl_->config.sample_rate;
                    pa_config.frame_duration_ms = pImpl_->config.frame_duration_ms;
                    pa_config.input_params      = input_params_ptr;
                    pa_config.output_params     = nullptr;
                    pa_config.callback          = Impl::recordCallback;
                    pa_config.control_state     = AudioControlState::RECORD;
                    pa_config.running_flag      = &pImpl_->is_recording;
                    pa_config.stream_ptr        = &pImpl_->record_stream;
                    pa_config.direction_name    = "录音";
                }
                else
                {
                    if (pImpl_->is_playing.load())
                        return AudioError::ALREADY_RUNNING;

                    output_params.device = Pa_GetDefaultOutputDevice();
                    if (output_params.device == paNoDevice)
                    {
                        LOG_ERROR(LOG_TAG, "无输出设备");
                        return AudioError::DEVICE_NOT_FOUND;
                    }

                    output_params.channelCount = pImpl_->config.channels;
                    output_params.sampleFormat = paInt16;
                    output_params.suggestedLatency =
                        Pa_GetDeviceInfo(output_params.device)->defaultLowOutputLatency;
                    output_params.hostApiSpecificStreamInfo = nullptr;
                    output_params_ptr                       = &output_params;

                    pa_config.sample_rate       = pImpl_->config.sample_rate;
                    pa_config.frame_duration_ms = pImpl_->config.frame_duration_ms;
                    pa_config.input_params      = nullptr;
                    pa_config.output_params     = output_params_ptr;
                    pa_config.callback          = Impl::playbackCallback;
                    pa_config.control_state     = AudioControlState::PLAYBACK;
                    pa_config.running_flag      = &pImpl_->is_playing;
                    pa_config.stream_ptr        = &pImpl_->playback_stream;
                    pa_config.direction_name    = "播放";
                }

                AudioError err = pImpl_->openAndStartPaStream(pa_config);
                if (err == AudioError::NONE)
                    LOG_INFO(LOG_TAG, "%s流已启动", pa_config.direction_name);
                return err;
            }

            AudioError AudioSystem::stopStream(StreamDirection direction)
            {
                if (direction == StreamDirection::INPUT)
                {
                    if (!pImpl_->is_recording.load())
                        return AudioError::NONE;

                    pImpl_->is_recording.store(false);
                    pImpl_->record_stream.reset();
                    pImpl_->setControlState(AudioControlState::NONE);
                    pImpl_->ai_resample_buffer.clear();
                    pImpl_->wakeword_resample_buffer.clear();
                    LOG_INFO(LOG_TAG, "录音流已停止");
                }
                else
                {
                    if (!pImpl_->is_playing.load())
                        return AudioError::NONE;

                    pImpl_->is_playing.store(false);
                    pImpl_->playback_stream.reset();
                    pImpl_->setControlState(AudioControlState::NONE);
                    LOG_INFO(LOG_TAG, "播放流已停止");
                }
                return AudioError::NONE;
            }

            bool AudioSystem::isStreamRunning(StreamDirection direction) const
            {
                return (direction == StreamDirection::INPUT) ? pImpl_->is_recording.load()
                                                             : pImpl_->is_playing.load();
            }

            // 编解码
            AudioFramePtr AudioSystem::encodeOpus(const int16_t* pcm_data, size_t pcm_size)
            {
                if (!pImpl_->webrtc_encoder)
                    return nullptr;

                int  frame_size    = pcm_size / sizeof(int16_t) / pImpl_->config.channels;
                auto encoded_frame = pImpl_->mem_pool->allocate(OPUS_MAX_FRAME_BYTES);
                if (!encoded_frame)
                    return nullptr;

                int encoded_bytes = opus_encode(pImpl_->webrtc_encoder.get(), pcm_data, frame_size,
                                                encoded_frame->data, encoded_frame->capacity);

                if (encoded_bytes < 0)
                    return nullptr;

                encoded_frame->size = encoded_bytes;
                pImpl_->stats.encode_count.fetch_add(1);
                return encoded_frame;
            }

            AudioFramePtr AudioSystem::decodeOpus(const uint8_t* opus_data, size_t opus_size)
            {
                OpusDecoder* decoder =
                    pImpl_->tts_decoder ? pImpl_->tts_decoder.get() : pImpl_->webrtc_decoder.get();
                int target_sample_rate =
                    pImpl_->tts_decoder ? OPUS_TTS_SAMPLE_RATE : pImpl_->config.sample_rate;

                if (!decoder)
                    return nullptr;

                int frame_size = target_sample_rate / MS_PER_SEC * pImpl_->config.frame_duration_ms;
                size_t pcm_size =
                    static_cast<size_t>(frame_size) * pImpl_->config.channels * sizeof(int16_t);

                auto decoded_frame = pImpl_->mem_pool->allocate(pcm_size);
                if (!decoded_frame)
                    return nullptr;

                int decoded_samples = opus_decode(decoder, opus_data, opus_size,
                                                  decoded_frame->getData<int16_t>(), frame_size, 0);

                if (decoded_samples < 0)
                    return nullptr;

                static std::atomic<int> decode_count{0};
                int                     count = decode_count.fetch_add(1);
                if (count < PCM_STATS_SAMPLE_LIMIT)
                {
                    const int16_t* pcm    = decoded_frame->getData<int16_t>();
                    auto [min_it, max_it] = std::minmax_element(pcm, pcm + decoded_samples);
                    LOG_INFO(LOG_TAG, "PCM#%d: 样本=%d, 范围=[%d,%d]", count, decoded_samples,
                             *min_it, *max_it);
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
                    return 0;

                int samples_per_frame =
                    pImpl_->config.sample_rate / MS_PER_SEC * pImpl_->config.frame_duration_ms;
                size_t total_samples = pcm_size / sizeof(int16_t) / pImpl_->config.channels;
                size_t encoded_count = 0;
                size_t offset        = 0;

                while (offset < total_samples)
                {
                    int current_size =
                        std::min(samples_per_frame, static_cast<int>(total_samples - offset));
                    auto encoded_frame = pImpl_->mem_pool->allocate(OPUS_MAX_FRAME_BYTES);
                    if (!encoded_frame)
                        break;

                    int encoded_bytes =
                        opus_encode(pImpl_->webrtc_encoder.get(), pcm_data + offset, current_size,
                                    encoded_frame->data, encoded_frame->capacity);

                    if (encoded_bytes > 0)
                    {
                        encoded_frame->size      = encoded_bytes;
                        encoded_frame->timestamp = get_nowus();
                        frames.push_back(encoded_frame);
                        encoded_count++;
                        pImpl_->stats.encode_count.fetch_add(1);
                    }
                    offset += current_size;
                }
                return encoded_count;
            }

            // 应用层流控制
            AudioError AudioSystem::startStream(StreamType type)
            {
                if (!pImpl_->initialized.load())
                    return AudioError::NOT_INITIALIZED;

                Impl::StreamTypeConfig config = pImpl_->getStreamTypeConfig(type);
                if (config.streaming_flag->load())
                    return AudioError::ALREADY_RUNNING;

                setMainState(config.main_state);
                config.streaming_flag->store(true);
                LOG_INFO(LOG_TAG, "%s音频流已启动", config.stream_name);
                return AudioError::NONE;
            }

            AudioError AudioSystem::stopStream(StreamType type)
            {
                Impl::StreamTypeConfig config = pImpl_->getStreamTypeConfig(type);
                if (!config.streaming_flag->load())
                    return AudioError::NONE;

                config.streaming_flag->store(false);
                LOG_INFO(LOG_TAG, "%s音频流已停止", config.stream_name);
                return AudioError::NONE;
            }

            bool AudioSystem::isStreamActive(StreamType type) const
            {
                return (type == StreamType::AI) ? pImpl_->is_ai_streaming.load()
                                                : pImpl_->is_webrtc_streaming.load();
            }

            AudioError AudioSystem::startMode(AudioMainState main_state, StreamType stream_type,
                                              const char* mode_name)
            {
                LOG_INFO(LOG_TAG, "启动%s模式...", mode_name);

                setMainState(main_state);

                if (!isStreamRunning(StreamDirection::INPUT))
                {
                    AudioError err = startStream(StreamDirection::INPUT);
                    if (err != AudioError::NONE)
                        return err;
                }

                AudioError err = startStream(stream_type);
                if (err != AudioError::NONE)
                    return err;

                LOG_INFO(LOG_TAG, "%s模式已启动", mode_name);
                return AudioError::NONE;
            }

            AudioError AudioSystem::stopMode(StreamType stream_type, const char* mode_name,
                                             bool stop_record)
            {
                LOG_INFO(LOG_TAG, "停止%s模式...", mode_name);

                stopStream(stream_type);
                if (stop_record)
                    stopStream(StreamDirection::INPUT);
                setMainState(AudioMainState::NONE);

                LOG_INFO(LOG_TAG, "%s模式已停止", mode_name);
                return AudioError::NONE;
            }

            AudioError AudioSystem::startAIMode()
            {
                return startMode(AudioMainState::AI, StreamType::AI, "AI");
            }
            AudioError AudioSystem::stopAIMode()
            {
                return stopMode(StreamType::AI, "AI", true);
            }
            AudioError AudioSystem::startWebRTCMode()
            {
                return startMode(AudioMainState::WEBRTC, StreamType::WEBRTC, "WebRTC");
            }
            AudioError AudioSystem::stopWebRTCMode()
            {
                return stopMode(StreamType::WEBRTC, "WebRTC", false);
            }

        } // namespace audio
    }     // namespace media
} // namespace app
