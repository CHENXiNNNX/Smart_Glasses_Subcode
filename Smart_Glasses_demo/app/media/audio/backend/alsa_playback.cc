/* alsa_playback.cc - ALSA 播放后端实现 */

#include "alsa_playback.hpp"
#include "../../../tool/log/log.hpp"
#include "../../../tool/time/time.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

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
#define SND_PCM_STREAM_PLAYBACK 1
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
static snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t*, const void*, unsigned long)
{
    return -1;
}
static int snd_pcm_recover(snd_pcm_t*, int, int)
{
    return 0;
}
static int snd_pcm_drain(snd_pcm_t*)
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

#define TAG "ALSA_Play"

#if HAS_ALSA
    static std::string playback_pcm_name(const PlaybackCfg& cfg)
    {
        if (!cfg.device.empty() && cfg.device != "default")
            return cfg.device;
        if (!cfg.rk.card_name.empty())
            return cfg.rk.card_name;
        return "default";
    }
#endif

    static constexpr size_t   PLAY_QUEUE_MAX = 100;
    static constexpr uint32_t PLAY_WAIT_MS   = 5;

    class AlsaPlayback::Impl
    {
    public:
        PlaybackCfg             cfg_;
        FramePool*              pool_   = nullptr;
        snd_pcm_t*              handle_ = nullptr;
        bool                    init_   = false;
        std::atomic<bool>       running_{false};
        std::atomic<bool>       stop_{false};
        std::thread             thread_;
        std::atomic<uint8_t>    volume_{70};
        std::queue<FramePtr>    queue_;
        std::mutex              queue_mtx_;
        std::condition_variable queue_cv_;
        std::atomic<uint32_t>   frames_{0};
        std::atomic<uint32_t>   drops_{0};

        Error init(const PlaybackCfg& cfg, FramePool* pool)
        {
            cfg_  = cfg;
            pool_ = pool;
            volume_.store(cfg.volume);

#if HAS_ALSA
            int err = snd_pcm_open(&handle_, cfg.device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
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
            clear();
#if HAS_ALSA
            if (handle_)
            {
                snd_pcm_drain(handle_);
                snd_pcm_close(handle_);
                handle_ = nullptr;
            }
#endif
            init_ = false;
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
#if HAS_ALSA
            size_t               samples_per_frame = cfg_.rate * cfg_.frame_ms / 1000;
            size_t               buf_n             = samples_per_frame * cfg_.channels;
            std::vector<int16_t> buf(buf_n);
            std::vector<int16_t> silence(buf_n, 0);

            while (!stop_)
            {
                FramePtr frame;
                {
                    std::unique_lock<std::mutex> lk(queue_mtx_);
                    queue_cv_.wait_for(lk, std::chrono::milliseconds(PLAY_WAIT_MS),
                                       [this]() { return !queue_.empty() || stop_; });
                    if (stop_)
                        break;
                    if (!queue_.empty())
                    {
                        frame = queue_.front();
                        queue_.pop();
                    }
                }

                const int16_t* src   = silence.data();
                size_t         count = samples_per_frame;

                if (frame && frame->samples > 0)
                {
                    size_t n   = frame->samples * frame->channels;
                    float  vol = static_cast<float>(volume_.load()) / 100.0f;

                    if (buf.size() < n)
                        buf.resize(n);

                    const int16_t* in = frame->get<int16_t>();
                    for (size_t i = 0; i < n; ++i)
                    {
                        float v = static_cast<float>(in[i]) * vol;
                        v       = (v < -32768.0f) ? -32768.0f : ((v > 32767.0f) ? 32767.0f : v);
                        buf[i]  = static_cast<int16_t>(v);
                    }
                    src   = buf.data();
                    count = frame->samples;
                    frames_++;
                }

                snd_pcm_sframes_t ret =
                    snd_pcm_writei(handle_, src, static_cast<unsigned long>(count));
                if (ret < 0)
                    snd_pcm_recover(handle_, static_cast<int>(ret), 1);
            }
#else
            while (!stop_)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
        }

        size_t queue_size() const
        {
            std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(queue_mtx_));
            return queue_.size();
        }
    };

    AlsaPlayback::AlsaPlayback() : impl_(std::make_unique<Impl>()) {}
    AlsaPlayback::~AlsaPlayback()
    {
        deinit();
    }

    Error AlsaPlayback::init(const PlaybackCfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void AlsaPlayback::deinit()
    {
        impl_->deinit();
    }
    Error AlsaPlayback::start()
    {
        return impl_->start();
    }
    Error AlsaPlayback::stop()
    {
        return impl_->stop();
    }
    bool AlsaPlayback::is_running() const
    {
        return impl_->running_;
    }

    Error AlsaPlayback::push(const FramePtr& frame)
    {
        return impl_->push_frame(frame);
    }
    Error AlsaPlayback::push(const int16_t* data, size_t samples)
    {
        return impl_->push_raw(data, samples);
    }
    void AlsaPlayback::clear()
    {
        impl_->clear();
    }

    void AlsaPlayback::set_volume(uint8_t v)
    {
        impl_->volume_.store(v > 100 ? 100 : v);
    }
    uint8_t AlsaPlayback::volume() const
    {
        return impl_->volume_.load();
    }
    size_t AlsaPlayback::queue_size() const
    {
        return impl_->queue_size();
    }
    uint32_t AlsaPlayback::drops() const
    {
        return impl_->drops_.load();
    }

} // namespace app::media::audio
