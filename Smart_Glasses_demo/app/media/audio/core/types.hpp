/* types.hpp - 音频系统公共类型与配置 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace app::media::audio
{

    /*========================================================================
     * 错误码
     *========================================================================*/

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

    /*========================================================================
     * 音频帧
     *========================================================================*/

    struct Frame
    {
        uint8_t* data      = nullptr;
        size_t   size      = 0;
        size_t   capacity  = 0;
        uint32_t samples   = 0;
        uint32_t rate      = 0;
        uint8_t  channels  = 0;
        uint64_t timestamp = 0;

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

    /*========================================================================
     * 流类型标识 — 发布-订阅的通道 key
     *========================================================================*/

    enum class StreamId : uint8_t
    {
        MicRaw,
        MicProcessed,
        SpeakerPcm,
        COUNT_,
    };

    /*========================================================================
     * 回调与订阅句柄
     *========================================================================*/

    using FrameCb   = std::function<void(const FramePtr&)>;
    using ErrorCb   = std::function<void(Error err, const char* msg)>;
    using SubHandle = uint64_t;

    /*========================================================================
     * 后端模式
     *========================================================================*/

    enum class BackendMode : uint8_t
    {
        Auto = 0,
        RkMpi,
        Alsa,
    };

    /*========================================================================
     * RK MPI 采集扩展参数
     *========================================================================*/

    struct RkMpiAiCfg
    {
        int32_t     ai_dev_id          = 0;
        int32_t     ai_chn_id          = 0;
        int32_t     ao_dev_id_for_vqe  = 0;
        int32_t     ao_chn_for_vqe     = 0;
        std::string card_name          = "hw:0,0";
        uint32_t    device_sample_rate = 48000;
        uint32_t    pt_num_per_frame   = 0;
        uint16_t    vqe_gap_ms         = 16;
        bool        enable_vqe         = false;
        std::string vqe_config_file;
        bool        rv1106_digital_loopback_mode2 = true;
        bool        rv1106_set_adc_alc            = true;
        uint32_t    soundcard_channel_count       = 2;
        /** RK_MPI AI 声卡位宽（8/16/24/32），与 rk_mpi AIO_ATTR 中 bitWidth 一致 */
        uint32_t    soundcard_bit_width           = 16;
    };

    /*========================================================================
     * RK MPI AO 播放扩展参数
     *========================================================================*/

    struct RkMpiAoCfg
    {
        int32_t     ao_dev_id          = 0;
        int32_t     ao_chn_id          = 0;
        std::string card_name          = "hw:0,0";
        uint32_t    device_sample_rate = 48000;
        uint32_t    pt_num_per_frame   = 0;
        /* RV1106 官方 simple_ao_send_frame：声卡侧常开 2ch，单声道用 SetTrackMode 映射 */
        uint32_t soundcard_channels = 2;
    };

    /*========================================================================
     * 采集配置
     *========================================================================*/

    struct CaptureCfg
    {
        BackendMode backend          = BackendMode::Auto;
        bool        fallback_to_alsa = true;

        uint32_t    rate     = 48000;
        uint8_t     channels = 1;
        uint8_t     frame_ms = 20;
        std::string device   = "default";

        RkMpiAiCfg rk;
    };

    /*========================================================================
     * 播放配置
     *========================================================================*/

    struct PlaybackCfg
    {
        BackendMode backend          = BackendMode::Auto;
        bool        fallback_to_alsa = true;

        uint32_t    rate     = 48000;
        uint8_t     channels = 1;
        uint8_t     frame_ms = 20;
        uint8_t     volume   = 70;
        std::string device   = "default";

        RkMpiAoCfg rk;
    };

    /*========================================================================
     * Opus 配置
     *========================================================================*/

    struct OpusCfg
    {
        uint32_t rate     = 48000;
        uint8_t  channels = 1;
        uint32_t bitrate  = 32000;
        bool     vbr      = true;
    };

    /*========================================================================
     * 应用层 3A（Speex；采集端未开 VQE 时生效）
     *========================================================================*/

    struct ProcCfg
    {
        bool denoise  = true;
        bool agc      = true;
        bool vad      = true;
        bool dereverb = true;

        float agc_level       = 8000.0f;
        int   agc_increment   = 12;
        int   agc_decrement   = -40;
        int   agc_max_gain_db = 10;

        int noise_suppress_db = -45;
        int echo_suppress_db  = -90;
    };

    /*========================================================================
     * 内存池配置
     *========================================================================*/

    struct MemoryCfg
    {
        size_t fixed_block_size  = 2048;
        size_t fixed_block_count = 256;
        size_t dynamic_max_size  = 2 * 1024 * 1024;
    };

    /*========================================================================
     * 总配置
     *========================================================================*/

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

    /*========================================================================
     * 运行统计
     *========================================================================*/

    struct Stats
    {
        uint32_t capture_frames = 0;
        uint32_t capture_drops  = 0;
        float    capture_fps    = 0.0f;

        uint32_t playback_frames = 0;
        uint32_t playback_drops  = 0;
        float    playback_fps    = 0.0f;

        uint32_t encode_cnt = 0;
        uint32_t decode_cnt = 0;

        uint32_t wav_frames = 0;
        uint32_t wav_sec    = 0;

        size_t mem_used  = 0;
        size_t mem_total = 0;

        const char* capture_backend  = "none";
        const char* playback_backend = "none";
    };

} // namespace app::media::audio
