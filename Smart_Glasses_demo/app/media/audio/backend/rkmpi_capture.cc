#include "rkmpi_capture.hpp"
#include "../../../tool/log/log.hpp"
#include "../../../tool/time/time.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

#if __has_include(<rk_mpi_ai.h>)
#define HAS_RKMPI_AI 1
#include <rk_mpi_sys.h>
#include <rk_mpi_ai.h>
#include <rk_mpi_amix.h>
#include <rk_mpi_mb.h>
#include <rk_comm_aio.h>
#else
#define HAS_RKMPI_AI 0
#endif

namespace app::media::audio
{

    using namespace tool::log;
    using namespace tool::time;

#define TAG "RkMpi_Cap"

#if HAS_RKMPI_AI

    static AUDIO_SAMPLE_RATE_E map_sample_rate(uint32_t rate)
    {
        switch (rate)
        {
        case 8000:
            return AUDIO_SAMPLE_RATE_8000;
        case 16000:
            return AUDIO_SAMPLE_RATE_16000;
        case 32000:
            return AUDIO_SAMPLE_RATE_32000;
        case 44100:
            return AUDIO_SAMPLE_RATE_44100;
        case 48000:
            return AUDIO_SAMPLE_RATE_48000;
        default:
            return AUDIO_SAMPLE_RATE_16000;
        }
    }

    static AUDIO_BIT_WIDTH_E map_bit_width_u32(uint32_t bits)
    {
        switch (bits)
        {
        case 8:
            return AUDIO_BIT_WIDTH_8;
        case 24:
            return AUDIO_BIT_WIDTH_24;
        case 32:
            return AUDIO_BIT_WIDTH_32;
        case 16:
        default:
            return AUDIO_BIT_WIDTH_16;
        }
    }

    static AUDIO_SOUND_MODE_E map_sound_mode_channels(uint32_t ch)
    {
        switch (ch)
        {
        case 1:
            return AUDIO_SOUND_MODE_MONO;
        case 2:
            return AUDIO_SOUND_MODE_STEREO;
        case 4:
            return AUDIO_SOUND_MODE_4_CHN;
        case 6:
            return AUDIO_SOUND_MODE_6_CHN;
        case 8:
            return AUDIO_SOUND_MODE_8_CHN;
        default:
            return AUDIO_SOUND_MODE_STEREO;
        }
    }

    static int16_t clamp_s32_to_s16(int32_t x)
    {
        if (x > 32767)
            return 32767;
        if (x < -32768)
            return -32768;
        return static_cast<int16_t>(x);
    }

    static void enable_loopback(AUDIO_DEV dev, bool mode2)
    {
        if (!mode2)
            return;
        char   mode[] = "Mode2";
        RK_S32 ret    = RK_MPI_AMIX_SetControl(dev, "I2STDM Digital Loopback Mode", mode);
        if (ret != RK_SUCCESS)
            LOG_WARN(TAG, "AMIX loopback 设置失败: dev=%d ret=%d", dev, ret);
        else
            LOG_INFO(TAG, "AMIX loopback: dev=%d Mode2", dev);
    }

    static void apply_vqe_modules(AUDIO_DEV dev, AI_CHN chn)
    {
        AI_VQE_MOD_ENABLE_S mods;
        std::memset(&mods, 0, sizeof(mods));
        mods.bAec      = RK_TRUE;
        mods.bBf       = RK_TRUE;
        mods.bFastAec  = RK_TRUE;
        mods.bAes      = RK_TRUE;
        mods.bAgc      = RK_TRUE;
        mods.bAnr      = RK_TRUE;
        mods.bDereverb = RK_TRUE;
        mods.bDtd      = RK_TRUE;
        mods.bHowling  = RK_TRUE;
        mods.bWakeup   = RK_FALSE;
        mods.bGsc      = RK_FALSE;
        mods.bNlp      = RK_FALSE;
        mods.bCng      = RK_FALSE;
        mods.bEq       = RK_FALSE;
        mods.bDoa      = RK_FALSE;

        RK_S32 ret = RK_MPI_AI_SetVqeModuleEnable(dev, chn, &mods);
        if (ret != RK_SUCCESS)
            LOG_WARN(TAG, "VQE SetVqeModuleEnable 失败: %d", ret);
    }

    static void apply_ai_defaults(AUDIO_DEV dev)
    {
        AUDIO_FADE_S fade;
        std::memset(&fade, 0, sizeof(fade));
        RK_MPI_AI_SetMute(dev, RK_FALSE, &fade);
        RK_MPI_AI_SetVolume(dev, 100);
    }

    static unsigned hw_ch_from_sound_mode(AUDIO_SOUND_MODE_E m)
    {
        switch (m)
        {
        case AUDIO_SOUND_MODE_MONO:
            return 1;
        case AUDIO_SOUND_MODE_STEREO:
            return 2;
        case AUDIO_SOUND_MODE_4_CHN:
            return 4;
        case AUDIO_SOUND_MODE_6_CHN:
            return 6;
        case AUDIO_SOUND_MODE_8_CHN:
            return 8;
        default:
            return 0;
        }
    }

    class RkMpiCapture::Impl
    {
    public:
        CaptureCfg            cfg_;
        FramePool*            pool_        = nullptr;
        AUDIO_DEV             dev_         = 0;
        AI_CHN                chn_         = 0;
        bool                  init_        = false;
        bool                  vqe_enabled_ = false;
        std::atomic<bool>     running_{false};
        std::atomic<bool>     stop_{false};
        std::thread           thread_;
        OnFrame               on_frame_;
        std::atomic<uint32_t> drops_{0};
        uint32_t              dev_channels_ = 2;

        Error init(const CaptureCfg& cfg, FramePool* pool)
        {
            cfg_          = cfg;
            pool_         = pool;
            dev_          = static_cast<AUDIO_DEV>(cfg.rk.ai_dev_id);
            chn_          = static_cast<AI_CHN>(cfg.rk.ai_chn_id);
            dev_channels_ = cfg.rk.soundcard_channel_count;

            if (RK_MPI_SYS_Init() != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "RK_MPI_SYS_Init 失败");
                return Error::DEVICE_ERROR;
            }

            enable_loopback(dev_, cfg.rk.rv1106_digital_loopback_mode2);

            uint32_t pt_num = cfg.rk.pt_num_per_frame;
            if (pt_num == 0)
            {
                pt_num = cfg.rk.device_sample_rate * cfg.frame_ms / 1000;
                if (pt_num == 0)
                    pt_num = 960;
            }

            const AUDIO_BIT_WIDTH_E hw_bw = map_bit_width_u32(cfg.rk.soundcard_bit_width);

            AIO_ATTR_S attr;
            std::memset(&attr, 0, sizeof(attr));
            attr.soundCard.channels   = dev_channels_;
            attr.soundCard.sampleRate = cfg.rk.device_sample_rate;
            attr.soundCard.bitWidth   = hw_bw;
            attr.enSamplerate         = map_sample_rate(cfg.rk.device_sample_rate);
            attr.enBitwidth           = hw_bw;
            attr.enSoundmode          = map_sound_mode_channels(dev_channels_);
            attr.u32FrmNum      = 4;
            attr.u32PtNumPerFrm = pt_num;
            attr.u32ChnCnt      = dev_channels_;
            attr.u32EXFlag      = 0;

            if (!cfg.rk.card_name.empty())
                std::strncpy(reinterpret_cast<char*>(attr.u8CardName), cfg.rk.card_name.c_str(),
                             sizeof(attr.u8CardName) - 1);

            RK_S32 ret = RK_MPI_AI_SetPubAttr(dev_, &attr);
            if (ret != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "SetPubAttr 失败: %d", ret);
                RK_MPI_SYS_Exit();
                return Error::DEVICE_ERROR;
            }

            ret = RK_MPI_AI_Enable(dev_);
            if (ret != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "Enable 失败: %d", ret);
                RK_MPI_SYS_Exit();
                return Error::DEVICE_ERROR;
            }

            LOG_INFO(TAG, "采集就绪: card=%s dev=%d chn=%d rate=%u ch=%u frame=%u",
                     cfg.rk.card_name.c_str(), dev_, chn_, cfg.rk.device_sample_rate, dev_channels_,
                     pt_num);

            if (cfg.rk.enable_vqe && !cfg.rk.vqe_config_file.empty())
            {
                uint32_t vqe_frame = (cfg.rk.device_sample_rate * cfg.rk.vqe_gap_ms) / 1000;

                AI_VQE_CONFIG_S vqe;
                std::memset(&vqe, 0, sizeof(vqe));
                vqe.enCfgMode            = AIO_VQE_CONFIG_LOAD_FILE;
                vqe.s32WorkSampleRate    = static_cast<RK_S32>(cfg.rk.device_sample_rate);
                vqe.s32FrameSample       = static_cast<RK_S32>(vqe_frame);
                vqe.s64RefChannelType    = 0x2;
                vqe.s64RecChannelType    = 0x1;
                vqe.s64ChannelLayoutType = 0x3;
                std::strncpy(vqe.aCfgFile, cfg.rk.vqe_config_file.c_str(),
                             sizeof(vqe.aCfgFile) - 1);

                apply_vqe_modules(dev_, chn_);

                ret = RK_MPI_AI_SetVqeAttr(dev_, chn_,
                                           static_cast<AUDIO_DEV>(cfg.rk.ao_dev_id_for_vqe),
                                           static_cast<AO_CHN>(cfg.rk.ao_chn_for_vqe), &vqe);
                if (ret != RK_SUCCESS)
                {
                    LOG_ERROR(TAG, "SetVqeAttr 失败: %d", ret);
                    RK_MPI_AI_Disable(dev_);
                    RK_MPI_SYS_Exit();
                    return Error::DEVICE_ERROR;
                }

                ret = RK_MPI_AI_EnableVqe(dev_, chn_);
                if (ret != RK_SUCCESS)
                {
                    LOG_ERROR(TAG, "EnableVqe 失败: %d", ret);
                    RK_MPI_AI_Disable(dev_);
                    RK_MPI_SYS_Exit();
                    return Error::DEVICE_ERROR;
                }
                vqe_enabled_ = true;
                LOG_INFO(TAG, "VQE 开启 file=%s", cfg.rk.vqe_config_file.c_str());
            }

            AI_CHN_PARAM_S chn_param;
            std::memset(&chn_param, 0, sizeof(chn_param));
            chn_param.s32UsrFrmDepth = 4;
            chn_param.enLoopbackMode = AUDIO_LOOPBACK_NONE;
            RK_MPI_AI_SetChnParam(dev_, chn_, &chn_param);

            ret = RK_MPI_AI_EnableChn(dev_, chn_);
            if (ret != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "EnableChn 失败: %d", ret);
                if (vqe_enabled_)
                    RK_MPI_AI_DisableVqe(dev_, chn_);
                RK_MPI_AI_Disable(dev_);
                RK_MPI_SYS_Exit();
                return Error::DEVICE_ERROR;
            }

            AI_CHN_ATTR_S chn_attr;
            std::memset(&chn_attr, 0, sizeof(chn_attr));
            chn_attr.enChnAttr     = AUDIO_CHN_ATTR_RATE;
            chn_attr.u32SampleRate = cfg.rk.device_sample_rate;
            RK_MPI_AI_SetChnAttr(dev_, chn_, &chn_attr);

            /* 多路 TDM 时勿强行单声道 TrackMode，易与驱动期望不一致 */
            if (cfg.channels == 1 && dev_channels_ <= 2)
                RK_MPI_AI_SetTrackMode(dev_, AUDIO_TRACK_FRONT_LEFT);

            apply_ai_defaults(dev_);

            ret = RK_MPI_AI_EnableReSmp(dev_, chn_, map_sample_rate(cfg.rate));
            if (ret != RK_SUCCESS)
                LOG_WARN(TAG, "EnableReSmp: %d", ret);

            init_ = true;
            return Error::OK;
        }

        void deinit()
        {
            stop();
            if (!init_)
                return;

            if (vqe_enabled_)
                RK_MPI_AI_DisableVqe(dev_, chn_);
            RK_MPI_AI_DisableDataRead(dev_, chn_);
            RK_MPI_AI_DisableReSmp(dev_, chn_);
            RK_MPI_AI_DisableChn(dev_, chn_);
            RK_MPI_AI_Disable(dev_);
            RK_MPI_SYS_Exit();

            init_ = false;
            LOG_INFO(TAG, "已释放");
        }

        Error start(OnFrame on_frame)
        {
            if (!init_ || running_)
                return Error::NOT_INIT;
            on_frame_ = std::move(on_frame);
            stop_     = false;
            thread_   = std::thread(&Impl::loop, this);
            running_  = true;
            LOG_INFO(TAG, "启动");
            return Error::OK;
        }

        Error stop()
        {
            if (!running_)
                return Error::OK;
            stop_ = true;
            if (thread_.joinable())
                thread_.join();
            running_ = false;
            LOG_INFO(TAG, "停止");
            return Error::OK;
        }

        void loop()
        {
            bool first_logged = false;

            while (!stop_)
            {
                AUDIO_FRAME_S frame;
                AEC_FRAME_S   aec;
                std::memset(&frame, 0, sizeof(frame));
                std::memset(&aec, 0, sizeof(aec));

                RK_S32 ret = RK_MPI_AI_GetFrame(dev_, chn_, &frame, &aec, 200);
                if (ret != RK_SUCCESS)
                {
                    drops_++;
                    continue;
                }

                void* raw = RK_MPI_MB_Handle2VirAddr(frame.pMbBlk);
                if (!raw)
                {
                    RK_MPI_AI_ReleaseFrame(dev_, chn_, &frame, &aec);
                    drops_++;
                    continue;
                }

                unsigned hw_ch = hw_ch_from_sound_mode(frame.enSoundMode);
                if (hw_ch == 0)
                    hw_ch = dev_channels_;

                unsigned bps = 2;
                if (frame.enBitWidth == AUDIO_BIT_WIDTH_32)
                    bps = 4;
                else if (frame.enBitWidth == AUDIO_BIT_WIDTH_24)
                    bps = 3;
                else
                    bps = 2;

                const size_t frame_stride = static_cast<size_t>(bps) * hw_ch;
                if (frame.u32Len == 0 || frame_stride == 0 || (frame.u32Len % frame_stride) != 0)
                {
                    RK_MPI_AI_ReleaseFrame(dev_, chn_, &frame, &aec);
                    drops_++;
                    continue;
                }

                const size_t spc = frame.u32Len / frame_stride;

                if (!first_logged)
                {
                    LOG_INFO(TAG,
                             "首帧: u32Len=%u seq=%u bitW=%u soundMode=%u hw_ch=%u bps=%u "
                             "frame/周期样点=%u",
                             frame.u32Len, frame.u32Seq, static_cast<unsigned>(frame.enBitWidth),
                             static_cast<unsigned>(frame.enSoundMode), hw_ch, bps,
                             static_cast<unsigned>(spc));
                    first_logged = true;
                }

                auto s32_ch = [&](size_t frame_idx, unsigned ch) -> int16_t {
                    auto* s32 = reinterpret_cast<const std::int32_t*>(raw);
                    const std::int32_t x = s32[frame_idx * hw_ch + ch];
                    return clamp_s32_to_s16(x >> 16);
                };

                size_t   pcm_spc   = 0;
                size_t   out_bytes = 0;
                FramePtr fp        = nullptr;
                int16_t* dst       = nullptr;

                if (cfg_.channels == 1)
                {
                    pcm_spc   = spc;
                    out_bytes = pcm_spc * sizeof(int16_t);
                    fp        = pool_ ? pool_->alloc(out_bytes) : nullptr;
                    if (!fp)
                    {
                        RK_MPI_AI_ReleaseFrame(dev_, chn_, &frame, &aec);
                        drops_++;
                        continue;
                    }
                    dst = fp->get<int16_t>();
                    if (frame.enBitWidth == AUDIO_BIT_WIDTH_32)
                    {
                        for (size_t i = 0; i < spc; ++i)
                            dst[i] = s32_ch(i, 0);
                    }
                    else if (hw_ch == 1)
                    {
                        std::memcpy(dst, raw, out_bytes);
                    }
                    else
                    {
                        auto* s16 = reinterpret_cast<const int16_t*>(raw);
                        for (size_t i = 0; i < spc; ++i)
                            dst[i] = s16[i * hw_ch];
                    }
                }
                else
                {
                    if (hw_ch < 2)
                    {
                        RK_MPI_AI_ReleaseFrame(dev_, chn_, &frame, &aec);
                        drops_++;
                        continue;
                    }
                    pcm_spc   = spc;
                    out_bytes = pcm_spc * 2 * sizeof(int16_t);
                    fp        = pool_ ? pool_->alloc(out_bytes) : nullptr;
                    if (!fp)
                    {
                        RK_MPI_AI_ReleaseFrame(dev_, chn_, &frame, &aec);
                        drops_++;
                        continue;
                    }
                    dst = fp->get<int16_t>();
                    if (frame.enBitWidth == AUDIO_BIT_WIDTH_32)
                    {
                        for (size_t i = 0; i < spc; ++i)
                        {
                            dst[i * 2]     = s32_ch(i, 0);
                            dst[i * 2 + 1] = s32_ch(i, 1);
                        }
                    }
                    else
                    {
                        auto* s16 = reinterpret_cast<const int16_t*>(raw);
                        for (size_t i = 0; i < spc; ++i)
                        {
                            dst[i * 2]     = s16[i * hw_ch];
                            dst[i * 2 + 1] = s16[i * hw_ch + 1];
                        }
                    }
                }

                fp->samples   = static_cast<uint32_t>(pcm_spc);
                fp->size      = out_bytes;
                fp->rate      = cfg_.rk.device_sample_rate;
                fp->channels  = cfg_.channels;
                fp->timestamp = uptime_us();

                RK_MPI_AI_ReleaseFrame(dev_, chn_, &frame, &aec);

                if (on_frame_)
                    on_frame_(std::move(fp));
            }
        }
    };

#else

    class RkMpiCapture::Impl
    {
    public:
        bool                  init_        = false;
        bool                  vqe_enabled_ = false;
        std::atomic<bool>     running_{false};
        std::atomic<uint32_t> drops_{0};

        Error init(const CaptureCfg&, FramePool*)
        {
            LOG_ERROR(TAG, "MPI 采集未编译");
            return Error::NOT_SUPPORTED;
        }
        void  deinit() {}
        Error start(ICaptureBackend::OnFrame)
        {
            return Error::NOT_SUPPORTED;
        }
        Error stop()
        {
            return Error::OK;
        }
    };

#endif

    RkMpiCapture::RkMpiCapture() : impl_(std::make_unique<Impl>()) {}
    RkMpiCapture::~RkMpiCapture()
    {
        deinit();
    }

    Error RkMpiCapture::init(const CaptureCfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void RkMpiCapture::deinit()
    {
        impl_->deinit();
    }
    Error RkMpiCapture::start(OnFrame on_frame)
    {
        return impl_->start(std::move(on_frame));
    }
    Error RkMpiCapture::stop()
    {
        return impl_->stop();
    }
    bool RkMpiCapture::is_running() const
    {
        return impl_->running_;
    }
    bool RkMpiCapture::has_builtin_vqe() const
    {
        return impl_->vqe_enabled_;
    }
    uint32_t RkMpiCapture::drops() const
    {
        return impl_->drops_.load();
    }

} // namespace app::media::audio
