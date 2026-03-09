/* audio.hpp - 音频驱动 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace app::media::audio
{

    /*============================================================================
     * 前向声明
     *============================================================================*/

    class Capture;
    class Playback;
    class OpusCodec;
    class Resampler;
    class AudioProc;
    class WavRecorder;
    class FramePool;

    /*============================================================================
     * 错误码
     *============================================================================*/

    enum class Error
    {
        OK = 0,
        NOT_INIT,
        ALREADY_INIT,
        INVALID_PARAM,
        DEVICE_ERROR,
        CODEC_ERROR,
        MEMORY_ERROR,
        TIMEOUT,
        BUSY,
        NOT_SUPPORTED,
    };

    /*============================================================================
     * 音频帧
     *============================================================================*/

    struct Frame
    {
        uint8_t* data      = nullptr;
        size_t   size      = 0;
        size_t   capacity  = 0;
        uint32_t samples   = 0; // 采样点数（单声道）
        uint32_t rate      = 0; // 采样率 (Hz)
        uint8_t  channels  = 0; // 声道数
        uint64_t timestamp = 0; // 时间戳 (us, 单调时钟)

        /* 内部使用 */
        void*                 priv = nullptr;
        std::function<void()> release;

        ~Frame()
        {
            if (release)
                release();
        }

        template <typename T = int16_t> T* get()
        {
            return reinterpret_cast<T*>(data);
        }

        template <typename T = int16_t> const T* get() const
        {
            return reinterpret_cast<const T*>(data);
        }
    };

    using FramePtr = std::shared_ptr<Frame>;

    /*============================================================================
     * 回调类型
     *============================================================================*/

    using CaptureCb  = std::function<void(const FramePtr& frame)>;
    using PlaybackCb = std::function<void(const FramePtr& frame)>;
    using ErrorCb    = std::function<void(Error err, const char* msg)>;

    /*============================================================================
     * 配置结构
     *============================================================================*/

    struct CaptureCfg
    {
        uint32_t    rate     = 48000;
        uint8_t     channels = 1;
        uint8_t     frame_ms = 20;
        std::string device   = "default";
    };

    struct PlaybackCfg
    {
        uint32_t    rate     = 48000;
        uint8_t     channels = 1;
        uint8_t     frame_ms = 20;
        uint8_t     volume   = 70; // 0-100
        std::string device   = "default";
    };

    struct OpusCfg
    {
        uint32_t rate     = 48000;
        uint8_t  channels = 1;
        uint32_t bitrate  = 32000; // 32kbps（WebRTC 语音）
        bool     vbr      = true;
    };

    struct ProcCfg
    {
        /* 功能开关 */
        bool denoise  = true;
        bool agc      = true;
        bool vad      = true; // 语音活动检测
        bool dereverb = true; // 去混响

        /* AGC 参数 */
        float agc_level       = 8000.0f; // 目标电平 (1000-32768)
        int   agc_increment   = 12;      // 增益上升速率 (dB/s, 0-30)
        int   agc_decrement   = -40;     // 增益下降速率 (dB/s, -90-0)
        int   agc_max_gain_db = 10;      // 最大增益 (dB, 0-60)

        /* 噪声/回声抑制级别 */
        int noise_suppress_db = -45; // 噪声抑制 (dB, -30~0, 越小越强)
        int echo_suppress_db  = -90; // 回声抑制 (dB, -90~0, 越小越强)
    };

    struct MemoryCfg
    {
        size_t fixed_block_size  = 2048; // 适配 48kHz/20ms 帧 (~1920B)
        size_t fixed_block_count = 256;
        size_t dynamic_max_size  = 2 * 1024 * 1024; // 2MB 动态池
    };

    struct AudioCfg
    {
        CaptureCfg  capture;
        PlaybackCfg playback;
        OpusCfg     opus;
        ProcCfg     proc;
        MemoryCfg   memory;

        bool enable_capture  = true;
        bool enable_playback = true;
        bool enable_opus     = true;
        bool enable_proc     = true;
    };

    /*============================================================================
     * 统计信息
     *============================================================================*/

    struct Stats
    {
        /* 采集 */
        uint32_t capture_frames = 0;
        uint32_t capture_drops  = 0;
        float    capture_fps    = 0.0f;

        /* 播放 */
        uint32_t playback_frames = 0;
        uint32_t playback_drops  = 0;
        float    playback_fps    = 0.0f;

        /* 编解码 */
        uint32_t encode_cnt = 0;
        uint32_t decode_cnt = 0;

        /* WAV 录制 */
        uint32_t wav_frames = 0;
        uint32_t wav_sec    = 0;

        /* 内存 */
        size_t mem_used  = 0;
        size_t mem_total = 0;
    };

    /*============================================================================
     * FramePool - 帧内存池
     *============================================================================*/

    class FramePool
    {
    public:
        FramePool();
        ~FramePool();

        FramePool(const FramePool&)            = delete;
        FramePool& operator=(const FramePool&) = delete;

        Error init(const MemoryCfg& cfg);
        void  deinit();

        FramePtr alloc(size_t size);

        size_t used() const;
        size_t total() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * AudioProc - 音频 3A 处理（Speex）
     *============================================================================*/

    class AudioProc
    {
    public:
        AudioProc();
        ~AudioProc();

        AudioProc(const AudioProc&)            = delete;
        AudioProc& operator=(const AudioProc&) = delete;

        Error init(const ProcCfg& cfg, uint32_t rate, uint8_t frame_ms);
        void  deinit();
        bool  is_init() const;

        /* 原地处理 */
        void process(int16_t* pcm, size_t samples);

        /* 运行时参数调整 */
        void set_denoise(bool en);
        void set_agc(bool en);
        void set_vad(bool en);
        void set_dereverb(bool en);
        void set_agc_level(float level);
        void set_noise_suppress(int db);
        void set_echo_suppress(int db);

        bool denoise() const;
        bool agc() const;
        bool vad() const;
        bool dereverb() const;

        const ProcCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * Capture - ALSA 采集
     *============================================================================*/

    class Capture
    {
    public:
        Capture();
        ~Capture();

        Capture(const Capture&)            = delete;
        Capture& operator=(const Capture&) = delete;

        Error init(const CaptureCfg& cfg, FramePool* pool, AudioProc* proc = nullptr);
        void  deinit();
        bool  is_init() const;

        Error start();
        Error stop();
        bool  is_running() const;

        void set_cb(CaptureCb cb);

        const CaptureCfg& cfg() const;
        uint32_t          drops() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * Playback - ALSA 播放
     *============================================================================*/

    class Playback
    {
    public:
        Playback();
        ~Playback();

        Playback(const Playback&)            = delete;
        Playback& operator=(const Playback&) = delete;

        Error init(const PlaybackCfg& cfg, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        Error start();
        Error stop();
        bool  is_running() const;

        Error push(const FramePtr& frame);
        Error push(const int16_t* data, size_t samples);
        void  clear();

        void    set_volume(uint8_t vol);
        uint8_t volume() const;

        const PlaybackCfg& cfg() const;
        size_t             queue_size() const;
        uint32_t           drops() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * OpusCodec - Opus 编解码
     *============================================================================*/

    class OpusCodec
    {
    public:
        OpusCodec();
        ~OpusCodec();

        OpusCodec(const OpusCodec&)            = delete;
        OpusCodec& operator=(const OpusCodec&) = delete;

        Error init(const OpusCfg& cfg, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        FramePtr encode(const int16_t* pcm, size_t samples);
        FramePtr encode(const FramePtr& pcm);

        FramePtr decode(const uint8_t* opus, size_t len);
        FramePtr decode(const FramePtr& opus);

        Error set_bitrate(uint32_t bps);

        const OpusCfg& cfg() const;
        uint32_t       encode_cnt() const;
        uint32_t       decode_cnt() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * Resampler - 重采样（libsamplerate）
     *============================================================================*/

    class Resampler
    {
    public:
        Resampler();
        ~Resampler();

        Resampler(const Resampler&)            = delete;
        Resampler& operator=(const Resampler&) = delete;

        Error init(uint8_t channels, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        FramePtr resample(const int16_t* in, size_t samples, uint32_t src_rate, uint32_t dst_rate);
        FramePtr resample(const FramePtr& in, uint32_t dst_rate);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * WavRecorder - WAV 文件录制
     *============================================================================*/

    class WavRecorder
    {
    public:
        WavRecorder();
        ~WavRecorder();

        WavRecorder(const WavRecorder&)            = delete;
        WavRecorder& operator=(const WavRecorder&) = delete;

        Error start(const std::string& path, uint32_t rate, uint8_t channels, int duration_sec = 0);
        Error stop();
        bool  is_recording() const;

        void write(const int16_t* data, size_t samples);
        void write(const FramePtr& frame);

        uint32_t duration_sec() const;
        uint64_t file_size() const;
        uint32_t frames() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * AudioDrv - 主门面
     *============================================================================*/

    class AudioDrv
    {
    public:
        AudioDrv();
        ~AudioDrv();

        AudioDrv(const AudioDrv&)            = delete;
        AudioDrv& operator=(const AudioDrv&) = delete;

        /*--------------------------------------------------------------------
         * 生命周期
         *--------------------------------------------------------------------*/
        Error init(const AudioCfg& cfg);
        void  deinit();
        bool  is_init() const;

        /*--------------------------------------------------------------------
         * 控制
         *--------------------------------------------------------------------*/
        Error start();
        Error stop();
        bool  is_running() const;

        /*--------------------------------------------------------------------
         * 子模块访问
         *--------------------------------------------------------------------*/
        Capture&     capture();
        Playback&    playback();
        OpusCodec&   opus();
        Resampler&   resampler();
        AudioProc&   proc();
        WavRecorder& wav();
        FramePool&   pool();

        /*--------------------------------------------------------------------
         * 回调设置
         *--------------------------------------------------------------------*/
        void set_capture_cb(CaptureCb cb);
        void set_error_cb(ErrorCb cb);

        /*--------------------------------------------------------------------
         * AI 上发编码
         *--------------------------------------------------------------------*/
        FramePtr encodeCaptureForAI(const FramePtr& pcm_48k);

        /*--------------------------------------------------------------------
         * 统计
         *--------------------------------------------------------------------*/
        Stats stats() const;
        void  reset_stats();

        /*--------------------------------------------------------------------
         * 配置只读访问
         *--------------------------------------------------------------------*/
        const AudioCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
