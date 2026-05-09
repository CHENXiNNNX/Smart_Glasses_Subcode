/* rkmpi_playback.cc - RK MPI AO 播放后端实现 */

#include "rkmpi_playback.hpp"
#include "../../../tool/log/log.hpp"
#include "../../../tool/time/time.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>

/*========================================================================
 * RK MPI 条件编译
 *========================================================================*/

#if __has_include(<rk_mpi_ao.h>)
#define HAS_RKMPI_AO 1
#include <rk_mpi_sys.h>
#include <rk_mpi_ao.h>
#include <rk_mpi_mb.h>
#include <rk_comm_aio.h>
#else
#define HAS_RKMPI_AO 0
#endif

namespace app::media::audio
{

    using namespace tool::log;

#define TAG "RkMpi_Play"

    static constexpr size_t PLAY_QUEUE_MAX = 100;

#if HAS_RKMPI_AO

    static AUDIO_SAMPLE_RATE_E map_rate_ao(uint32_t rate)
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
            return AUDIO_SAMPLE_RATE_48000;
        }
    }

    class RkMpiPlayback::Impl
    {
    public:
        PlaybackCfg             cfg_;
        FramePool*              pool_ = nullptr;
        AUDIO_DEV               dev_  = 0;
        AO_CHN                  chn_  = 0;
        bool                    init_ = false;
        std::atomic<bool>       running_{false};
        std::atomic<bool>       stop_{false};
        std::thread             thread_;
        std::atomic<uint8_t>    volume_{70};
        std::queue<FramePtr>    queue_;
        std::mutex              queue_mtx_;
        std::condition_variable queue_cv_;
        std::atomic<uint32_t>   drops_{0};

        Error init(const PlaybackCfg& cfg, FramePool* pool)
        {
            cfg_  = cfg;
            pool_ = pool;
            dev_  = static_cast<AUDIO_DEV>(cfg.rk.ao_dev_id);
            chn_  = static_cast<AO_CHN>(cfg.rk.ao_chn_id);
            volume_.store(cfg.volume);

            /* 注意：SYS_Init 可能已被采集端调用，重复调用是安全的 */

            /* 与 media/samples/simple_test/simple_ao_send_frame.c 一致：默认 1024，易过 tinyalsa */
            uint32_t pt_num = cfg.rk.pt_num_per_frame;
            if (pt_num == 0)
                pt_num = 1024;

            uint32_t hw_chn = cfg.rk.soundcard_channels;
            if (hw_chn == 0)
                hw_chn = 2;
            if (hw_chn < static_cast<uint32_t>(cfg.channels))
                hw_chn = cfg.channels;

            AIO_ATTR_S attr;
            std::memset(&attr, 0, sizeof(attr));
            attr.soundCard.channels   = hw_chn;
            attr.soundCard.sampleRate = cfg.rk.device_sample_rate;
            attr.soundCard.bitWidth   = AUDIO_BIT_WIDTH_16;
            attr.enSamplerate         = map_rate_ao(cfg.rk.device_sample_rate);
            attr.enBitwidth           = AUDIO_BIT_WIDTH_16;
            attr.enSoundmode =
                (cfg.channels == 1) ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO;
            attr.u32FrmNum      = 4;
            attr.u32PtNumPerFrm = pt_num;
            attr.u32ChnCnt      = hw_chn;
            attr.u32EXFlag      = 0;

            if (!cfg.rk.card_name.empty())
                std::strncpy(reinterpret_cast<char*>(attr.u8CardName), cfg.rk.card_name.c_str(),
                             sizeof(attr.u8CardName) - 1);

            RK_S32 ret = RK_MPI_AO_SetPubAttr(dev_, &attr);
            if (ret != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "SetPubAttr 失败: %d", ret);
                return Error::DEVICE_ERROR;
            }

            ret = RK_MPI_AO_Enable(dev_);
            if (ret != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "Enable 失败: %d", ret);
                return Error::DEVICE_ERROR;
            }

            AO_CHN_PARAM_S chn_param;
            std::memset(&chn_param, 0, sizeof(chn_param));
            chn_param.enLoopbackMode = AUDIO_LOOPBACK_NONE;
            ret                      = RK_MPI_AO_SetChnParams(dev_, chn_, &chn_param);
            if (ret != RK_SUCCESS)
                LOG_WARN(TAG, "SetChnParams: %d", ret);

            /* 单声道应用：声卡 2ch + OUT_STEREO，与官方 demo 一致，避免1ch 打开 PCM 失败 */
            if (cfg.channels == 1)
                RK_MPI_AO_SetTrackMode(dev_, AUDIO_TRACK_OUT_STEREO);
            else
                RK_MPI_AO_SetTrackMode(dev_, AUDIO_TRACK_NORMAL);

            ret = RK_MPI_AO_EnableChn(dev_, chn_);
            if (ret != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "EnableChn 失败: %d", ret);
                RK_MPI_AO_Disable(dev_);
                return Error::DEVICE_ERROR;
            }

            /* 官方 demo 在 EnableChn 后始终 EnableReSmp，入参为「送入 AO 的 PCM 采样率」 */
            ret = RK_MPI_AO_EnableReSmp(dev_, chn_, map_rate_ao(cfg.rate));
            if (ret != RK_SUCCESS)
                LOG_WARN(TAG, "EnableReSmp: %d（若与 device 相同部分 BSP 仍返回非0）", ret);

            RK_MPI_AO_SetVolume(dev_, static_cast<RK_S32>(cfg.volume));

            LOG_INFO(TAG, "AO 就绪: card=%s dev=%d chn=%d rate=%u 逻辑ch=%u 声卡ch=%u pt=%u",
                     cfg.rk.card_name.c_str(), dev_, chn_, cfg.rk.device_sample_rate, cfg.channels,
                     hw_chn, pt_num);

            init_ = true;
            return Error::OK;
        }

        void deinit()
        {
            stop();
            clear();
            if (!init_)
                return;

            RK_MPI_AO_DisableReSmp(dev_, chn_);
            RK_MPI_AO_DisableChn(dev_, chn_);
            RK_MPI_AO_Disable(dev_);

            init_ = false;
            LOG_INFO(TAG, "已释放");
        }

        Error start()
        {
            if (!init_ || running_)
                return Error::NOT_INIT;
            stop_    = false;
            thread_  = std::thread(&Impl::loop, this);
            running_ = true;
            LOG_INFO(TAG, "启动");
            return Error::OK;
        }

        Error stop()
        {
            if (!running_)
                return Error::OK;
            stop_ = true;
            queue_cv_.notify_all();
            if (thread_.joinable())
                thread_.join();
            running_ = false;
            LOG_INFO(TAG, "停止");
            return Error::OK;
        }

        Error push_frame(const FramePtr& frame)
        {
            if (!frame)
                return Error::INVALID_PARAM;
            std::lock_guard<std::mutex> lk(queue_mtx_);
            if (queue_.size() >= PLAY_QUEUE_MAX)
            {
                queue_.pop();
                drops_++;
            }
            queue_.push(frame);
            queue_cv_.notify_one();
            return Error::OK;
        }

        Error push_raw(const int16_t* data, size_t samples)
        {
            if (!data || samples == 0)
                return Error::INVALID_PARAM;

            size_t   bytes = samples * cfg_.channels * sizeof(int16_t);
            FramePtr f     = pool_ ? pool_->alloc(bytes) : nullptr;
            if (!f)
                return Error::MEMORY_ERROR;
            std::memcpy(f->data, data, bytes);
            f->samples  = static_cast<uint32_t>(samples);
            f->size     = bytes;
            f->rate     = cfg_.rate;
            f->channels = cfg_.channels;
            return push_frame(f);
        }

        void clear()
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            std::queue<FramePtr>        empty;
            std::swap(queue_, empty);
        }

        void loop()
        {
            while (!stop_)
            {
                FramePtr frame;
                {
                    std::unique_lock<std::mutex> lk(queue_mtx_);
                    queue_cv_.wait_for(lk, std::chrono::milliseconds(5),
                                       [this]() { return !queue_.empty() || stop_; });
                    if (stop_)
                        break;
                    if (queue_.empty())
                        continue;
                    frame = queue_.front();
                    queue_.pop();
                }

                if (!frame || frame->samples == 0)
                    continue;

                /* 音量处理 */
                float vol = static_cast<float>(volume_.load()) / 100.0f;
                if (vol < 0.99f)
                {
                    int16_t* pcm = frame->get<int16_t>();
                    size_t   n   = frame->samples * frame->channels;
                    for (size_t i = 0; i < n; ++i)
                    {
                        float v = static_cast<float>(pcm[i]) * vol;
                        v       = (v < -32768.0f) ? -32768.0f : ((v > 32767.0f) ? 32767.0f : v);
                        pcm[i]  = static_cast<int16_t>(v);
                    }
                }

                size_t bytes = frame->size;
                MB_BLK mb    = RK_NULL;
                RK_S32 ret   = RK_MPI_SYS_MmzAlloc(&mb, RK_NULL, RK_NULL, bytes);
                if (ret != RK_SUCCESS || !mb)
                {
                    drops_++;
                    continue;
                }

                void* vaddr = RK_MPI_MB_Handle2VirAddr(mb);
                if (vaddr)
                    std::memcpy(vaddr, frame->data, bytes);

                AUDIO_FRAME_S af;
                std::memset(&af, 0, sizeof(af));
                af.pMbBlk       = mb;
                af.u32Len       = static_cast<RK_U32>(bytes);
                af.u64TimeStamp = frame->timestamp;
                af.enBitWidth   = AUDIO_BIT_WIDTH_16;
                af.enSoundMode =
                    (frame->channels == 1) ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO;

                ret = RK_MPI_AO_SendFrame(dev_, chn_, &af, 200);
                RK_MPI_MB_ReleaseMB(mb);

                if (ret != RK_SUCCESS)
                    drops_++;
            }
        }

        size_t queue_size() const
        {
            std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(queue_mtx_));
            return queue_.size();
        }
    };

#else /* !HAS_RKMPI_AO */

    class RkMpiPlayback::Impl
    {
    public:
        bool                  init_ = false;
        std::atomic<bool>     running_{false};
        std::atomic<uint8_t>  volume_{70};
        std::atomic<uint32_t> drops_{0};

        Error init(const PlaybackCfg&, FramePool*)
        {
            LOG_ERROR(TAG, "编译时未包含 RK MPI AO");
            return Error::NOT_SUPPORTED;
        }
        void  deinit() {}
        Error start()
        {
            return Error::NOT_SUPPORTED;
        }
        Error stop()
        {
            return Error::OK;
        }
        Error push_frame(const FramePtr&)
        {
            return Error::NOT_SUPPORTED;
        }
        Error push_raw(const int16_t*, size_t)
        {
            return Error::NOT_SUPPORTED;
        }
        void   clear() {}
        size_t queue_size() const
        {
            return 0;
        }
    };

#endif /* HAS_RKMPI_AO */

    RkMpiPlayback::RkMpiPlayback() : impl_(std::make_unique<Impl>()) {}
    RkMpiPlayback::~RkMpiPlayback()
    {
        deinit();
    }

    Error RkMpiPlayback::init(const PlaybackCfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void RkMpiPlayback::deinit()
    {
        impl_->deinit();
    }
    Error RkMpiPlayback::start()
    {
        return impl_->start();
    }
    Error RkMpiPlayback::stop()
    {
        return impl_->stop();
    }
    bool RkMpiPlayback::is_running() const
    {
        return impl_->running_;
    }

    Error RkMpiPlayback::push(const FramePtr& frame)
    {
        return impl_->push_frame(frame);
    }
    Error RkMpiPlayback::push(const int16_t* data, size_t samples)
    {
        return impl_->push_raw(data, samples);
    }
    void RkMpiPlayback::clear()
    {
        impl_->clear();
    }

    void RkMpiPlayback::set_volume(uint8_t v)
    {
        impl_->volume_.store(v > 100 ? 100 : v);
    }
    uint8_t RkMpiPlayback::volume() const
    {
        return impl_->volume_.load();
    }
    size_t RkMpiPlayback::queue_size() const
    {
        return impl_->queue_size();
    }
    uint32_t RkMpiPlayback::drops() const
    {
        return impl_->drops_.load();
    }

} // namespace app::media::audio
