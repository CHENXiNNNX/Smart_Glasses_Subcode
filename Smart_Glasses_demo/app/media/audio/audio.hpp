/**
 * @file audio.hpp
 * @brief 音频系统
 * @details 实现音频采集、播放、编解码等功能
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

            // ============================================================================
            // 前向声明
            // ============================================================================
            class AudioSystem;
            class AudioMemoryPool;
            struct AudioFrame;

            // ============================================================================
            // 音频状态枚举（按方案设计）
            // ============================================================================

            /**
             * @brief 音频主状态机
             */
            enum class AudioMainState
            {
                NONE = 0, // 音频初始态
                AI,       // AI模式（唤醒词 + 语音对话）
                WEBRTC    // WebRTC模式（音视频通话）
            };

            /**
             * @brief 音频控制子状态机
             */
            enum class AudioControlState
            {
                NONE = 0, // 音频控制初始态
                RECORD,   // 开始收音
                PLAYBACK  // 开始播放
            };

            /**
             * @brief 音频功能子状态机
             */
            enum class AudioFunctionState
            {
                NONE = 0, // 音频功能初始态
                REC_AUDIO // 录音功能（可配置时长，输出mp3）
            };

            /**
             * @brief 音频流类型（用于统一接口）
             */
            enum class StreamType
            {
                AI,     // AI音频流
                WEBRTC  // WebRTC音频流
            };

            /**
             * @brief 音频流方向（用于统一接口）
             */
            enum class StreamDirection
            {
                INPUT,   // 输入流（录音）
                OUTPUT   // 输出流（播放）
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

            // ============================================================================
            // RAII包装器
            // ============================================================================

            /**
             * @brief OpusEncoder自动删除器
             */
            struct OpusEncoderDeleter
            {
                void operator()(OpusEncoder* p) const
                {
                    if (p)
                        opus_encoder_destroy(p);
                }
            };
            using OpusEncoderPtr = std::unique_ptr<OpusEncoder, OpusEncoderDeleter>;

            /**
             * @brief OpusDecoder自动删除器
             */
            struct OpusDecoderDeleter
            {
                void operator()(OpusDecoder* p) const
                {
                    if (p)
                        opus_decoder_destroy(p);
                }
            };
            using OpusDecoderPtr = std::unique_ptr<OpusDecoder, OpusDecoderDeleter>;

            /**
             * @brief libsamplerate SRC_STATE自动删除器
             */
            struct SrcStateDeleter
            {
                void operator()(SRC_STATE* p) const
                {
                    if (p)
                        src_delete(p);
                }
            };
            using SrcStatePtr = std::unique_ptr<SRC_STATE, SrcStateDeleter>;

            /**
             * @brief Speex预处理状态自动删除器
             */
            struct SpeexStateDeleter
            {
                void operator()(SpeexPreprocessState* p) const
                {
                    if (p)
                        speex_preprocess_state_destroy(p);
                }
            };
            using SpeexStatePtr = std::unique_ptr<SpeexPreprocessState, SpeexStateDeleter>;

            /**
             * @brief PortAudio流自动删除器
             */
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
                uint8_t* data;               // 数据指针（指向内存池）
                size_t   capacity;           // 缓冲区容量
                size_t   size;               // 实际数据大小
                uint64_t timestamp;          // 时间戳（微秒）
                bool     is_from_fixed_pool; // 是否来自固定池
                int fixed_pool_index; // 固定池索引（用于释放，-1表示非固定池）

                AudioFrame()
                    : data(nullptr), capacity(0), size(0), timestamp(0), is_from_fixed_pool(false),
                      fixed_pool_index(-1)
                {
                }

                /**
                 * @brief 获取数据指针（类型安全）
                 */
                template <typename T = int16_t> T* getData()
                {
                    return reinterpret_cast<T*>(data);
                }

                /**
                 * @brief 获取数据指针（const版本）
                 */
                template <typename T = int16_t> const T* getData() const
                {
                    return reinterpret_cast<const T*>(data);
                }
            };

            // 智能指针类型
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
                size_t fixed_block_size  = 4 * 1024; // 3KB（音频帧）
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
                /**
                 * @brief 构造函数
                 */
                explicit AudioMemoryPool(const AudioMemoryPoolConfig& config);

                /**
                 * @brief 析构函数
                 */
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
                    std::atomic<uint64_t> fixed_pool_hits{0};     // 固定池命中次数
                    std::atomic<uint64_t> dynamic_pool_hits{0};   // 动态池命中次数
                    std::atomic<uint64_t> total_allocations{0};   // 总分配次数
                    std::atomic<uint64_t> allocation_failures{0}; // 分配失败次数

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
                // 第一级：固定大小对象池（无锁，快速路径）
                struct FixedPool
                {
                    static constexpr size_t BLOCK_SIZE = 2048;
                    static constexpr size_t MAX_BLOCKS = 1024; // 最大支持块数（用于位图大小）
                    static constexpr size_t BITMAP_WORD_COUNT = 16;
                    static constexpr size_t BITS_PER_WORD     = 64;

                    size_t actual_block_count; // 实际使用的块数（由配置决定）

                    alignas(64)
                        std::array<std::atomic<uint64_t>, BITMAP_WORD_COUNT> allocation_bitmap{};
                    std::vector<std::array<uint8_t, BLOCK_SIZE>> blocks; // 动态分配的数据块
                    std::vector<AudioFrame> frame_objects;               // 动态分配的帧对象

                    explicit FixedPool(size_t block_count);
                    ~FixedPool() = default;

                    int      allocateBlock();
                    void     deallocateBlock(int index);
                    uint8_t* getBlockPtr(int index);
                };

                AudioMemoryPoolConfig                     config_;
                std::unique_ptr<FixedPool>                fixed_pool_;
                std::unique_ptr<tool::memory::MemoryPool> dynamic_pool_; // 第二级：动态内存池
                Stats                                     stats_;

                // 内部分配方法
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
                int output_volume = 50; // 输出音量（0-100，50为正常音量，100为最大增益）

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
                size_t max_record_queue_size   = 300; // 录音队列最大长度（6秒缓冲）
                size_t max_playback_queue_size = 300; // 播放队列最大长度（6秒缓冲）

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
            using StateChangeCallback =
                std::function<void(StateEnum old_state, StateEnum new_state)>;

            // ============================================================================
            // 音频系统（核心类）
            // ============================================================================

            /**
             * @brief 音频系统
             * @details 提供音频采集、播放、编解码等功能
             */
            class AudioSystem
            {
            public:
                /**
                 * @brief 构造函数
                 * @param config 音频配置
                 */
                explicit AudioSystem(const AudioConfig& config = AudioConfig());

                /**
                 * @brief 析构函数
                 */
                ~AudioSystem();

                // ========================================================================
                // 初始化和关闭
                // ========================================================================

                /**
                 * @brief 初始化音频系统
                 * @param sync_ctx 时间同步上下文（可选）
                 * @return AudioError::NONE 成功
                 */
                AudioError initialize(std::shared_ptr<sync_context_t> sync_ctx = nullptr);

                /**
                 * @brief 关闭音频系统
                 */
                void shutdown();

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const;

                // ========================================================================
                // 状态控制
                // ========================================================================

                /**
                 * @brief 设置主状态（NONE/AI/WEBRTC）
                 * @param state 目标状态
                 * @return AudioError::NONE 成功
                 */
                AudioError setMainState(AudioMainState state);

                /**
                 * @brief 获取主状态
                 */
                AudioMainState getMainState() const;

                /**
                 * @brief 设置控制子状态（NONE/RECORD/PLAYBACK）
                 */
                AudioError setControlState(AudioControlState state);

                /**
                 * @brief 获取控制子状态
                 */
                AudioControlState getControlState() const;

                /**
                 * @brief 设置功能子状态（NONE/REC_AUDIO）
                 */
                AudioError setFunctionState(AudioFunctionState state);

                /**
                 * @brief 获取功能子状态
                 */
                AudioFunctionState getFunctionState() const;

                // ========================================================================
                // 流管理接口
                // ========================================================================

                /**
                 * @brief 启动音频流
                 * @param direction 流方向
                 * @return AudioError::NONE 成功
                 */
                AudioError startStream(StreamDirection direction);

                /**
                 * @brief 停止音频流
                 * @param direction 流方向
                 */
                AudioError stopStream(StreamDirection direction);

                /**
                 * @brief 检查音频流是否正在运行
                 * @param direction 流方向
                 */
                bool isStreamRunning(StreamDirection direction) const;

                /**
                 * @brief 启动应用层音频流
                 * @param type 流类型
                 * @return AudioError::NONE 成功
                 */
                AudioError startStream(StreamType type);

                /**
                 * @brief 停止应用层音频流
                 * @param type 流类型
                 */
                AudioError stopStream(StreamType type);

                /**
                 * @brief 检查应用层音频流是否激活
                 * @param type 流类型
                 */
                bool isStreamActive(StreamType type) const;

                // ========================================================================
                // 回调设置（线程安全）
                // ========================================================================

                /**
                 * @brief 设置AI音频帧回调
                 * @param callback 回调函数
                 */
                void setAIAudioCallback(AudioFrameCallback callback);

                /**
                 * @brief 设置WebRTC音频帧回调
                 * @param callback 回调函数
                 */
                void setWebRTCAudioCallback(AudioFrameCallback callback);

                /**
                 * @brief 设置唤醒词音频回调
                 * @param callback 回调函数
                 */
                void setWakewordCallback(WakewordCallback callback);

                // ========================================================================
                // 音频帧队列操作
                // ========================================================================

                /**
                 * @brief 获取录音帧（阻塞等待）
                 * @param timeout 超时时间
                 * @return 音频帧智能指针（失败返回nullptr）
                 */
                AudioFramePtr getRecordedFrame(
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(100));

                /**
                 * @brief 推送播放帧
                 * @param frame 音频帧智能指针
                 */
                void pushPlaybackFrame(AudioFramePtr frame);

                /**
                 * @brief 清空录音队列
                 */
                void clearRecordQueue();

                /**
                 * @brief 清空播放队列
                 */
                void clearPlaybackQueue();

                // ========================================================================
                // 编解码
                // ========================================================================

                /**
                 * @brief Opus编码（PCM -> Opus）
                 * @param pcm_data PCM数据
                 * @param pcm_size PCM大小（字节）
                 * @return Opus编码后的帧（失败返回nullptr）
                 */
                AudioFramePtr encodeOpus(const int16_t* pcm_data, size_t pcm_size);

                /**
                 * @brief Opus解码（Opus -> PCM）
                 * @param opus_data Opus数据
                 * @param opus_size Opus大小（字节）
                 * @return PCM解码后的帧（失败返回nullptr）
                 */
                AudioFramePtr decodeOpus(const uint8_t* opus_data, size_t opus_size);

                /**
                 * @brief 分帧Opus编码（大数据自动分帧）
                 * @param pcm_data PCM数据
                 * @param pcm_size PCM大小（字节）
                 * @param frames 输出编码后的帧列表
                 * @return 编码帧数量
                 */
                size_t encodeOpusFrames(const int16_t* pcm_data, size_t pcm_size,
                                        std::vector<AudioFramePtr>& frames);

                // ========================================================================
                // 音量控制
                // ========================================================================

                /**
                 * @brief 设置输出音量
                 * @param volume 音量百分比（0-100），50为正常音量，100为最大增益
                 */
                void setOutputVolume(int volume);

                /**
                 * @brief 获取输出音量
                 * @return 音量百分比（0-100）
                 */
                int getOutputVolume() const;

                // ========================================================================
                // 统计信息
                // ========================================================================

                /**
                 * @brief 音频系统统计信息
                 */
                struct Stats
                {
                    AudioMemoryPool::Stats mem_stats;          // 内存池统计
                    std::atomic<uint64_t>  frames_recorded{0}; // 已录制帧数
                    std::atomic<uint64_t>  frames_played{0};   // 已播放帧数
                    std::atomic<uint64_t>  frames_dropped{0};  // 丢弃帧数
                    std::atomic<uint64_t>  encode_count{0};    // 编码次数
                    std::atomic<uint64_t>  decode_count{0};    // 解码次数
                };

                /**
                 * @brief 获取统计信息（通过引用返回）
                 * @param out_stats 输出统计信息
                 */
                void getStats(Stats& out_stats) const;

                /**
                 * @brief 重置统计信息
                 */
                void resetStats();

                /**
                 * @brief 输出统计日志
                 */
                void logStats() const;

                // ========================================================================
                // 状态机回调设置
                // ========================================================================

                /**
                 * @brief 设置主状态变化回调
                 */
                void setMainStateCallback(StateChangeCallback<AudioMainState> callback);

                /**
                 * @brief 设置控制子状态变化回调
                 */
                void setControlStateCallback(StateChangeCallback<AudioControlState> callback);

                // ========================================================================
                // 便利函数
                // ========================================================================

                /**
                 * @brief 启动AI模式
                 * @return AudioError::NONE 成功
                 */
                AudioError startAIMode();

                /**
                 * @brief 停止AI模式
                 * @return AudioError::NONE 成功
                 */
                AudioError stopAIMode();

                /**
                 * @brief 启动WebRTC模式
                 * @return AudioError::NONE 成功
                 */
                AudioError startWebRTCMode();

                /**
                 * @brief 停止WebRTC模式
                 * @return AudioError::NONE 成功
                 */
                AudioError stopWebRTCMode();

                AudioSystem(const AudioSystem&)            = delete;
                AudioSystem& operator=(const AudioSystem&) = delete;

            private:
                /**
                 * @brief 通用启动模式函数
                 * @param main_state 主状态
                 * @param stream_type 流类型
                 * @param mode_name 模式名称
                 * @return AudioError::NONE 成功
                 */
                AudioError startMode(AudioMainState main_state, StreamType stream_type,
                                     const char* mode_name);

                /**
                 * @brief 通用停止模式函数
                 * @param stream_type 流类型
                 * @param mode_name 模式名称
                 * @param stop_record 是否停止录音
                 * @return AudioError::NONE 成功
                 */
                AudioError stopMode(StreamType stream_type, const char* mode_name, bool stop_record);

                class Impl;
                std::unique_ptr<Impl> pImpl_;
            };

        } // namespace audio
    }     // namespace media
} // namespace app

#endif // AUDIO_HPP
