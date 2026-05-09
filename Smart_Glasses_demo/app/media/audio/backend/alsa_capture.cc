/* alsa_capture.cc - ALSA 采集后端实现 */

#include "alsa_capture.hpp"
#include "../../../tool/log/log.hpp"
#include "../../../tool/time/time.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

/*========================================================================
 * ALSA 条件编译
 *========================================================================*/

#if __has_include(<alsa/asoundlib.h>)
#define HAS_ALSA 1
#include <alsa/asoundlib.h>
#else
#define HAS_ALSA 0
struct snd_pcm_t;
typedef long snd_pcm_sframes_t;
#define SND_PCM_STREAM_CAPTURE 0
#define SND_PCM_FORMAT_S16_LE 2
#define SND_PCM_ACCESS_RW_INTERLEAVED 3
static int   snd_pcm_open(snd_pcm_t**, const char*, int, int)
{
    return -1;
}
static int snd_pcm_close(snd_pcm_t*)
{
    return 0;
}
static int snd_pcm_set_params(snd_pcm_t*, int, int, unsigned, unsigned, int, unsigned)
{
    return -1;
}
static snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t*, void*, unsigned long)
{
    return -1;
}
static int snd_pcm_recover(snd_pcm_t*, int, int)
{
    return 0;
}
static const char* snd_strerror(int)
{
    return "no ALSA";
}
#endif

namespace app::media::audio
{

    using namespace tool::log;
    using namespace tool::time;

#define TAG "ALSA_Cap"

    class AlsaCapture::Impl
    {
    public:
        CaptureCfg            cfg_;
        FramePool*            pool_   = nullptr;
        snd_pcm_t*            handle_ = nullptr;
        bool                  init_   = false;
        std::atomic<bool>     running_{false};
        std::atomic<bool>     stop_{false};
        std::thread           thread_;
        OnFrame               on_frame_;
        std::atomic<uint32_t> drops_{0};

        Error init(const CaptureCfg& cfg, FramePool* pool)
        {
            cfg_  = cfg;
            pool_ = pool;

#if HAS_ALSA
            int err = snd_pcm_open(&handle_, cfg.device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
            if (err < 0)
            {
                LOG_ERROR(TAG, "打开失败 %s", snd_strerror(err));
                return Error::DEVICE_ERROR;
            }

            err = snd_pcm_set_params(handle_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                     cfg.channels, cfg.rate, 1, cfg.frame_ms * 1000);
            if (err < 0)
            {
                LOG_ERROR(TAG, "配置失败 %s", snd_strerror(err));
                snd_pcm_close(handle_);
                handle_ = nullptr;
                return Error::DEVICE_ERROR;
            }
            LOG_INFO(TAG, "就绪 %s %uHz %uch %ums", cfg.device.c_str(), cfg.rate, cfg.channels,
                     cfg.frame_ms);
#else
            LOG_WARN(TAG, "跳过(无ALSA)");
#endif
            init_ = true;
            return Error::OK;
        }

        void deinit()
        {
            stop();
#if HAS_ALSA
            if (handle_)
            {
                snd_pcm_close(handle_);
                handle_ = nullptr;
            }
#endif
            init_ = false;
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
#if HAS_ALSA
            size_t samples_per_frame = cfg_.rate * cfg_.frame_ms / 1000;
            size_t bytes_per_frame   = samples_per_frame * cfg_.channels * sizeof(int16_t);

            while (!stop_)
            {
                FramePtr frame = pool_ ? pool_->alloc(bytes_per_frame) : nullptr;
                if (!frame)
                {
                    drops_++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.frame_ms));
                    continue;
                }

                snd_pcm_sframes_t ret = snd_pcm_readi(
                    handle_, frame->data, static_cast<unsigned long>(samples_per_frame));
                if (ret < 0)
                {
                    snd_pcm_recover(handle_, static_cast<int>(ret), 1);
                    continue;
                }

                frame->samples   = static_cast<uint32_t>(ret);
                frame->size      = static_cast<size_t>(ret) * cfg_.channels * sizeof(int16_t);
                frame->rate      = cfg_.rate;
                frame->channels  = cfg_.channels;
                frame->timestamp = uptime_us();

                if (on_frame_)
                    on_frame_(std::move(frame));
            }
#else
            while (!stop_)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
        }
    };

    AlsaCapture::AlsaCapture() : impl_(std::make_unique<Impl>()) {}
    AlsaCapture::~AlsaCapture()
    {
        deinit();
    }

    Error AlsaCapture::init(const CaptureCfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void AlsaCapture::deinit()
    {
        impl_->deinit();
    }

    Error AlsaCapture::start(OnFrame on_frame)
    {
        return impl_->start(std::move(on_frame));
    }
    Error AlsaCapture::stop()
    {
        return impl_->stop();
    }
    bool AlsaCapture::is_running() const
    {
        return impl_->running_;
    }
    uint32_t AlsaCapture::drops() const
    {
        return impl_->drops_.load();
    }

} // namespace app::media::audio
