/**
 * @file audio.hpp
 * @brief 音频系统
 */

#ifndef AUDIO_HPP
#define AUDIO_HPP

#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <array>
#include <chrono>
#include <condition_variable>

#if __has_include(<samplerate.h>)
#include <samplerate.h>
#else
extern "C"
{
    struct SRC_STATE;

#ifndef SRC_SINC_BEST_QUALITY
#define SRC_SINC_BEST_QUALITY 0
#endif
#ifndef SRC_SINC_FASTEST
#define SRC_SINC_FASTEST 1
#endif

    struct SRC_DATA
    {
        const float* data_in;
        long         input_frames;
        float*       data_out;
        long         output_frames;
        double       src_ratio;
        int          end_of_input;
        long         output_frames_gen;
    };

    SRC_STATE*  src_new(int converter_type, int channels, int* error);
    int         src_process(SRC_STATE* state, SRC_DATA* data);
    void        src_delete(SRC_STATE* state);
    const char* src_strerror(int error_code);
}
#endif

#include <opus/opus.h>
#include <portaudio.h>

#if __has_include(<speex/speex_preprocess.h>)
#include <speex/speex_preprocess.h>
#else
extern "C"
{
    struct SpeexPreprocessState;
    typedef int16_t spx_int16_t;

#ifndef SPEEX_PREPROCESS_SET_DENOISE
#define SPEEX_PREPROCESS_SET_DENOISE 0
#endif
#ifndef SPEEX_PREPROCESS_SET_AGC
#define SPEEX_PREPROCESS_SET_AGC 1
#endif
#ifndef SPEEX_PREPROCESS_SET_AGC_LEVEL
#define SPEEX_PREPROCESS_SET_AGC_LEVEL 2
#endif

    SpeexPreprocessState* speex_preprocess_state_init(int frame_size, int sampling_rate);
    int                   speex_preprocess_ctl(SpeexPreprocessState* state, int request, void* ptr);
    int                   speex_preprocess_run(SpeexPreprocessState* state, spx_int16_t* x);
    void                  speex_preprocess_state_destroy(SpeexPreprocessState* state);
}
#endif

#include "../sync.hpp"
#include "../../tool/memory/mem_pool.hpp"

namespace app
{
    namespace media
    {
        namespace audio
        {
            class AudioSystem;
            class AudioMemoryPool;
            struct AudioFrame;

            // ============================================================================
            // 音频状态枚举
            // ============================================================================

            /**
             * @brief 音频主状态机
             */
            enum class AudioMainState
            {
                NONE = 0,
                AI,
                WEBRTC
            };

            /**
             * @brief 音频控制子状态机
             */
            enum class AudioControlState
            {
                NONE = 0,
                RECORD,
                PLAYBACK
            };

            /**
             * @brief 音频功能子状态机
             */
            enum class AudioFunctionState
            {
                NONE = 0,
                REC_AUDIO
            };

            /**
             * @brief 音频流类型
             */
            enum class StreamType
            {
                AI,
                WEBRTC
            };

            /**
             * @brief 音频流方向
             */
            enum class StreamDirection
            {
                INPUT,
                OUTPUT
            };

            /**
             * @brief 音频错误类型
             */
            enum class AudioError
            {
                NONE = 0,
                INITIALIZE_FAILED,
                DEVICE_NOT_FOUND,
                STREAM_OPEN_FAILED,
                STREAM_START_FAILED,
                ENCODE_FAILED,
                DECODE_FAILED,
                MODE_CONFLICT,
                INVALID_PARAM,
                MEMORY_ALLOC_FAILED,
                NOT_INITIALIZED,
                ALREADY_RUNNING
            };

            // RAII包装器
            struct OpusEncoderDeleter
            {
                void operator()(OpusEncoder* p) const
                {
                    if (p) opus_encoder_destroy(p);
                }
            };
            using OpusEncoderPtr = std::unique_ptr<OpusEncoder, OpusEncoderDeleter>;

            struct OpusDecoderDeleter
            {
                void operator()(OpusDecoder* p) const
                {
                    if (p) opus_decoder_destroy(p);
                }
            };
            using OpusDecoderPtr = std::unique_ptr<OpusDecoder, OpusDecoderDeleter>;

            struct SrcStateDeleter
            {
                void operator()(SRC_STATE* p) const
                {
                    if (p) src_delete(p);
                }
            };
            using SrcStatePtr = std::unique_ptr<SRC_STATE, SrcStateDeleter>;

            struct SpeexStateDeleter
            {
                void operator()(SpeexPreprocessState* p) const
                {
                    if (p) speex_preprocess_state_destroy(p);
                }
            };
            using SpeexStatePtr = std::unique_ptr<SpeexPreprocessState, SpeexStateDeleter>;

            struct PaStreamDeleter
            {
                void operator()(PaStream* p) const
                {
                    if (p)
                    {
                        Pa_StopStream(p);
                        Pa_CloseStream(p);
                    }
                }
            };
            using PaStreamPtr = std::unique_ptr<PaStream, PaStreamDeleter>;

            // ============================================================================
            // 音频帧结构
            // ============================================================================

            /**
             * @brief 音频帧
             */
            struct AudioFrame
            {
                uint8_t* data               = nullptr;
                size_t   capacity           = 0;
                size_t   size               = 0;
                uint64_t timestamp          = 0;
                bool     is_from_fixed_pool = false;
                int      fixed_pool_index   = -1;

                AudioFrame() = default;

                template <typename T = int16_t>
                T* getData() { return reinterpret_cast<T*>(data); }

                template <typename T = int16_t>
                const T* getData() const { return reinterpret_cast<const T*>(data); }
            };

            using AudioFramePtr = std::shared_ptr<AudioFrame>;

            // ============================================================================
            // 音频内存池
            // ============================================================================

            /**
             * @brief 音频内存池配置
             */
            struct AudioMemoryPoolConfig
            {
                // 第一级：固定池配置
                size_t fixed_block_size  = 4 * 1024; // 4KB
                size_t fixed_block_count = 400;      // 400个块

                // 第二级：动态池配置
                size_t dynamic_pool_size = 2 * 1024 * 1024; // 2MB
            };

            /**
             * @brief 音频内存池
             */
            class AudioMemoryPool
            {
            public:
                explicit AudioMemoryPool(const AudioMemoryPoolConfig& config);
                ~AudioMemoryPool();

                /**
                 * @brief 分配音频帧
                 * @param size 数据大小（字节）
                 * @return 音频帧指针
                 */
                AudioFramePtr allocate(size_t size);

                /**
                 * @brief 获取统计信息
                 */
                struct Stats
                {
                    std::atomic<uint64_t> fixed_pool_hits{0};
                    std::atomic<uint64_t> dynamic_pool_hits{0};
                    std::atomic<uint64_t> total_allocations{0};
                    std::atomic<uint64_t> allocation_failures{0};

                    // 计算固定池命中率
                    double getFixedPoolHitRate() const
                    {
                        uint64_t total = total_allocations.load();
                        return total > 0 ? (double)fixed_pool_hits.load() / total * 100.0 : 0.0;
                    }
                };

                void getStats(Stats& out_stats) const;
                void resetStats();
                void logStats() const;

                AudioMemoryPool(const AudioMemoryPool&)            = delete;
                AudioMemoryPool& operator=(const AudioMemoryPool&) = delete;

            private:
                struct FixedPool
                {
                    static constexpr size_t BLOCK_SIZE       = 2048;
                    static constexpr size_t MAX_BLOCKS       = 1024;
                    static constexpr size_t BITMAP_WORD_COUNT = 16;
                    static constexpr size_t BITS_PER_WORD    = 64;

                    size_t actual_block_count;

                    alignas(64) std::array<std::atomic<uint64_t>, BITMAP_WORD_COUNT> allocation_bitmap{};
                    std::vector<std::array<uint8_t, BLOCK_SIZE>> blocks;
                    std::vector<AudioFrame> frame_objects;

                    explicit FixedPool(size_t block_count);
                    ~FixedPool() = default;

                    int      allocateBlock();
                    void     deallocateBlock(int index);
                    uint8_t* getBlockPtr(int index);
                };

                AudioMemoryPoolConfig                     config_;
                std::unique_ptr<FixedPool>                fixed_pool_;
                std::unique_ptr<tool::memory::MemoryPool> dynamic_pool_;
                Stats                                     stats_;

                AudioFramePtr allocateFromFixed(size_t size);
                AudioFramePtr allocateFromDynamic(size_t size);
            };

            // ============================================================================
            // 音频系统配置
            // ============================================================================

            /**
             * @brief 音频系统配置
             */
            struct AudioConfig
            {
                // 音频参数
                int sample_rate       = 48000; // 采样率
                int channels          = 1;     // 声道数
                int frame_duration_ms = 20;    // 帧时长（毫秒）

                // 音量控制
                int output_volume = 50; // 输出音量（0-100）

                // 3A算法配置
                bool enable_denoise  = true;  // 降噪
                bool enable_agc      = true;  // 自动增益控制
                bool enable_vad      = false; // 语音活动检测
                bool enable_dereverb = false; // 去混响

                float agc_level            = 8000.0f; // AGC目标电平
                int   noise_suppress_level = -15;     // 噪声抑制级别（dB）
                int   echo_suppress_level  = -40;     // 回声抑制级别（dB）
                int   agc_increment        = 12;      // AGC增益增加速度（dB/s）
                int   agc_decrement        = -40;     // AGC增益减少速度（dB/s）
                int   agc_max_gain         = 30;      // AGC最大增益（dB）

                // 队列配置
                size_t max_record_queue_size   = 300; // 录音队列最大长度
                size_t max_playback_queue_size = 300; // 播放队列最大长度

                // 录音文件存储配置
                std::string record_path         = "/root/audio/"; // 录音保存路径
                int         record_duration_sec = 0;              // 录音时长（秒，0表示手动停止）

                // 内存池配置
                AudioMemoryPoolConfig mem_pool_config;
            };

            // ============================================================================
            // 回调函数类型
            // ============================================================================

            /**
             * @brief 音频帧回调
             */
            using AudioFrameCallback = std::function<void(AudioFramePtr frame)>;

            /**
             * @brief 唤醒词音频回调
             */
            using WakewordCallback = std::function<void(const int16_t* data, size_t length)>;

            /**
             * @brief 状态变化回调
             */
            template <typename StateEnum>
            using StateChangeCallback = std::function<void(StateEnum old_state, StateEnum new_state)>;

            class AudioSystem
            {
            public:
                explicit AudioSystem(const AudioConfig& config = AudioConfig());
                ~AudioSystem();

                AudioError init(std::shared_ptr<sync_context_t> sync_ctx = nullptr);
                void       deinit();
                bool       isInitialized() const;

                AudioError         setMainState(AudioMainState state);
                AudioMainState     getMainState() const;
                AudioError         setControlState(AudioControlState state);
                AudioControlState  getControlState() const;
                AudioError         setFunctionState(AudioFunctionState state);
                AudioFunctionState getFunctionState() const;

                AudioError startStream(StreamDirection direction);
                AudioError stopStream(StreamDirection direction);
                bool       isStreamRunning(StreamDirection direction) const;

                AudioError startStream(StreamType type);
                AudioError stopStream(StreamType type);
                bool       isStreamActive(StreamType type) const;

                void setAIAudioCallback(AudioFrameCallback callback);
                void setWebRTCAudioCallback(AudioFrameCallback callback);
                void setWakewordCallback(WakewordCallback callback);

                AudioFramePtr getRecordedFrame(std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
                void          pushPlaybackFrame(AudioFramePtr frame);
                void          clearRecordQueue();
                void          clearPlaybackQueue();

                AudioError startRecord(const std::string& filename = "", int duration_sec = 0);
                AudioError stopRecord();
                bool       isRecording() const;

                AudioFramePtr encodeOpus(const int16_t* pcm_data, size_t pcm_size);
                AudioFramePtr decodeOpus(const uint8_t* opus_data, size_t opus_size);
                size_t        encodeOpusFrames(const int16_t* pcm_data, size_t pcm_size,
                                               std::vector<AudioFramePtr>& frames);

                void setOutputVolume(int volume);
                int  getOutputVolume() const;

                struct Stats
                {
                    AudioMemoryPool::Stats mem_stats;
                    std::atomic<uint64_t>  frames_recorded{0};
                    std::atomic<uint64_t>  frames_played{0};
                    std::atomic<uint64_t>  frames_dropped{0};
                    std::atomic<uint64_t>  encode_count{0};
                    std::atomic<uint64_t>  decode_count{0};
                };

                void getStats(Stats& out_stats) const;
                void resetStats();
                void logStats() const;

                void setMainStateCallback(StateChangeCallback<AudioMainState> callback);
                void setControlStateCallback(StateChangeCallback<AudioControlState> callback);

                AudioError startAIMode();
                AudioError stopAIMode();
                AudioError startWebRTCMode();
                AudioError stopWebRTCMode();

                AudioSystem(const AudioSystem&)            = delete;
                AudioSystem& operator=(const AudioSystem&) = delete;

            private:
                AudioError startMode(AudioMainState main_state, StreamType stream_type, const char* mode_name);
                AudioError stopMode(StreamType stream_type, const char* mode_name, bool stop_record);

                class Impl;
                std::unique_ptr<Impl> pImpl_;
            };

        } // namespace audio
    }     // namespace media
} // namespace app

#endif // AUDIO_HPP
