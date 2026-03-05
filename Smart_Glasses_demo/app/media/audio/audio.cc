/* audio.cc - 音频驱动 */

#include "audio.hpp"
#include "../../tool/file/file.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/memory/memory.hpp"
#include "../../tool/time/time.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/*============================================================================
 * ALSA
 *============================================================================*/

#if __has_include(<alsa/asoundlib.h>)
#define HAS_ALSA 1
#include <alsa/asoundlib.h>
#else
#define HAS_ALSA 0
struct snd_pcm_t;
typedef long snd_pcm_sframes_t;
#define SND_PCM_STREAM_CAPTURE 0
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
static snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t*, void*, unsigned long)
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
    return "无ALSA";
}
#endif

/*============================================================================
 * Opus
 *============================================================================*/

#if __has_include(<opus/opus.h>)
#define HAS_OPUS 1
#include <opus/opus.h>
#else
#define HAS_OPUS 0
struct OpusEncoder;
struct OpusDecoder;
#define OPUS_OK 0
#define OPUS_APPLICATION_VOIP 2048
#define OPUS_SET_BITRATE(x) 4002
#define OPUS_SET_VBR(x) 4006
#define OPUS_SET_SIGNAL(x) 4024
#define OPUS_SIGNAL_VOICE 3001
static OpusEncoder* opus_encoder_create(int, int, int, int*)
{
    return nullptr;
}
static int opus_encode(OpusEncoder*, const int16_t*, int, uint8_t*, int)
{
    return -1;
}
static int opus_encoder_ctl(OpusEncoder*, int, ...)
{
    return -1;
}
static void         opus_encoder_destroy(OpusEncoder*) {}
static OpusDecoder* opus_decoder_create(int, int, int*)
{
    return nullptr;
}
static int opus_decode(OpusDecoder*, const uint8_t*, int32_t, int16_t*, int, int)
{
    return -1;
}
static void opus_decoder_destroy(OpusDecoder*) {}
#endif

/*============================================================================
 * libsamplerate
 *============================================================================*/

#if __has_include(<samplerate.h>)
#define HAS_SRC 1
#include <samplerate.h>
#else
#define HAS_SRC 0
struct SRC_STATE;
struct SRC_DATA
{
    const float* data_in;
    float*       data_out;
    long         input_frames;
    long         output_frames;
    long         input_frames_used;
    long         output_frames_gen;
    int          end_of_input;
    double       src_ratio;
};
#define SRC_SINC_FASTEST 2
static SRC_STATE* src_new(int, int, int*)
{
    return nullptr;
}
static int src_process(SRC_STATE*, SRC_DATA*)
{
    return -1;
}
static int src_reset(SRC_STATE*)
{
    return 0;
}
static SRC_STATE* src_delete(SRC_STATE*)
{
    return nullptr;
}
#endif

/*============================================================================
 * Speex 预处理 (3A)
 *============================================================================*/

#if __has_include(<speex/speex_preprocess.h>)
#define HAS_SPEEX 1
#include <speex/speex_preprocess.h>
#else
#define HAS_SPEEX 0
struct SpeexPreprocessState;
typedef int16_t              spx_int16_t;
#define SPEEX_PREPROCESS_SET_DENOISE 0
#define SPEEX_PREPROCESS_SET_AGC 2
#define SPEEX_PREPROCESS_SET_VAD 4
#define SPEEX_PREPROCESS_SET_AGC_LEVEL 6
#define SPEEX_PREPROCESS_SET_DEREVERB 8
#define SPEEX_PREPROCESS_SET_NOISE_SUPPRESS 18
#define SPEEX_PREPROCESS_SET_ECHO_SUPPRESS 20
#define SPEEX_PREPROCESS_SET_AGC_INCREMENT 24
#define SPEEX_PREPROCESS_SET_AGC_DECREMENT 26
#define SPEEX_PREPROCESS_SET_AGC_MAX_GAIN 28
static SpeexPreprocessState* speex_preprocess_state_init(int, int)
{
    return nullptr;
}
static int speex_preprocess_ctl(SpeexPreprocessState*, int, void*)
{
    return -1;
}
static int speex_preprocess_run(SpeexPreprocessState*, spx_int16_t*)
{
    return 0;
}
static void speex_preprocess_state_destroy(SpeexPreprocessState*) {}
#endif

namespace app::media::audio
{

    using namespace tool::log;
    using namespace tool::file;
    using namespace tool::time;

#define TAG "Audio"

    /*============================================================================
     * 常量
     *============================================================================*/

    static constexpr size_t   OPUS_MAX_PACKET = 4096;
    static constexpr size_t   PLAY_QUEUE_MAX  = 100;
    static constexpr uint32_t PLAY_WAIT_MS    = 5;

    /*============================================================================
     * WAV 文件头
     *============================================================================*/

#pragma pack(push, 1)
    struct WavHeader
    {
        char     riff[4]         = {'R', 'I', 'F', 'F'};
        uint32_t file_size       = 0;
        char     wave[4]         = {'W', 'A', 'V', 'E'};
        char     fmt[4]          = {'f', 'm', 't', ' '};
        uint32_t fmt_size        = 16;
        uint16_t audio_fmt       = 1;
        uint16_t channels        = 1;
        uint32_t sample_rate     = 48000;
        uint32_t byte_rate       = 96000;
        uint16_t block_align     = 2;
        uint16_t bits_per_sample = 16;
        char     data[4]         = {'d', 'a', 't', 'a'};
        uint32_t data_size       = 0;
    };
#pragma pack(pop)

    /*============================================================================
     * FramePool 实现
     *============================================================================*/

    class FramePool::Impl
    {
    public:
        std::unique_ptr<tool::memory::MemoryPool> pool_;
        std::mutex                                mtx_;
        size_t                                    total_ = 0;

        Error init(const MemoryCfg& cfg)
        {
            size_t sz = cfg.fixed_block_size * cfg.fixed_block_count + cfg.dynamic_max_size;
            pool_     = std::make_unique<tool::memory::MemoryPool>(sz);
            total_    = sz;
            LOG_INFO(TAG, "帧池: %uKB", (unsigned)(sz / 1024));
            return Error::OK;
        }

        void deinit()
        {
            pool_.reset();
            total_ = 0;
        }

        FramePtr alloc(size_t size)
        {
            if (!pool_ || !pool_->valid())
                return nullptr;

            void* mem = nullptr;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                mem = pool_->allocate(size);
            }
            if (!mem)
                return nullptr;

            Frame* f     = new Frame();
            f->data      = static_cast<uint8_t*>(mem);
            f->size      = size;
            f->capacity  = size;
            f->timestamp = uptime_us();

            return FramePtr(f,
                            [this, mem](Frame* p)
                            {
                                std::lock_guard<std::mutex> lk(mtx_);
                                if (pool_)
                                    pool_->deallocate(mem);
                                delete p;
                            });
        }

        size_t used() const
        {
            return pool_ ? pool_->get_used_memory_fast() : 0;
        }
        size_t total() const
        {
            return total_;
        }
    };

    FramePool::FramePool() : impl_(std::make_unique<Impl>()) {}
    FramePool::~FramePool()
    {
        deinit();
    }
    Error FramePool::init(const MemoryCfg& cfg)
    {
        return impl_->init(cfg);
    }
    void FramePool::deinit()
    {
        impl_->deinit();
    }
    FramePtr FramePool::alloc(size_t size)
    {
        return impl_->alloc(size);
    }
    size_t FramePool::used() const
    {
        return impl_->used();
    }
    size_t FramePool::total() const
    {
        return impl_->total();
    }

    /*============================================================================
     * AudioProc - Speex 3A
     *============================================================================*/

    class AudioProc::Impl
    {
    public:
        ProcCfg               cfg_;
        SpeexPreprocessState* speex_    = nullptr;
        bool                  init_     = false;
        uint32_t              frame_sz_ = 0;

        Error init(const ProcCfg& cfg, uint32_t rate, uint8_t frame_ms)
        {
            cfg_      = cfg;
            frame_sz_ = rate * frame_ms / 1000;

#if HAS_SPEEX
            speex_ =
                speex_preprocess_state_init(static_cast<int>(frame_sz_), static_cast<int>(rate));
            if (!speex_)
            {
                LOG_ERROR(TAG, "3A: Speex 初始化失败");
                return Error::CODEC_ERROR;
            }

            apply_all_params();

            LOG_INFO(TAG, "3A: DNS=%d AGC=%d VAD=%d DEREVERB=%d 帧=%u", cfg.denoise, cfg.agc,
                     cfg.vad, cfg.dereverb, frame_sz_);
#else
            LOG_WARN(TAG, "3A: 跳过(无Speex)");
#endif
            init_ = true;
            return Error::OK;
        }

        void deinit()
        {
#if HAS_SPEEX
            if (speex_)
            {
                speex_preprocess_state_destroy(speex_);
                speex_ = nullptr;
            }
#endif
            init_ = false;
        }

        void process(int16_t* pcm, size_t samples)
        {
#if HAS_SPEEX
            if (speex_ && samples == frame_sz_)
                speex_preprocess_run(speex_, pcm);
#else
            (void)pcm;
            (void)samples;
#endif
        }

        /* --- 运行时参数调整 --- */

        void set_denoise(bool en)
        {
            cfg_.denoise = en;
#if HAS_SPEEX
            if (speex_)
            {
                int v = en ? 1 : 0;
                speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_DENOISE, &v);
            }
#endif
        }

        void set_agc(bool en)
        {
            cfg_.agc = en;
#if HAS_SPEEX
            if (speex_)
            {
                int v = en ? 1 : 0;
                speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_AGC, &v);
            }
#endif
        }

        void set_vad(bool en)
        {
            cfg_.vad = en;
#if HAS_SPEEX
            if (speex_)
            {
                int v = en ? 1 : 0;
                speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_VAD, &v);
            }
#endif
        }

        void set_dereverb(bool en)
        {
            cfg_.dereverb = en;
#if HAS_SPEEX
            if (speex_)
            {
                int v = en ? 1 : 0;
                speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_DEREVERB, &v);
            }
#endif
        }

        void set_agc_level(float lv)
        {
            cfg_.agc_level = lv;
#if HAS_SPEEX
            if (speex_)
                speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_AGC_LEVEL, &lv);
#endif
        }

        void set_noise_suppress(int db)
        {
            cfg_.noise_suppress_db = db;
#if HAS_SPEEX
            if (speex_)
                speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &db);
#endif
        }

        void set_echo_suppress(int db)
        {
            cfg_.echo_suppress_db = db;
#if HAS_SPEEX
            if (speex_)
                speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &db);
#endif
        }

    private:
        void apply_all_params()
        {
#if HAS_SPEEX
            if (!speex_)
                return;

            int ival;

            ival = cfg_.denoise ? 1 : 0;
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_DENOISE, &ival);

            ival = cfg_.agc ? 1 : 0;
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_AGC, &ival);

            ival = cfg_.vad ? 1 : 0;
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_VAD, &ival);

            ival = cfg_.dereverb ? 1 : 0;
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_DEREVERB, &ival);

            float fval = cfg_.agc_level;
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_AGC_LEVEL, &fval);

            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
                                 &cfg_.noise_suppress_db);
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,
                                 &cfg_.echo_suppress_db);
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_AGC_INCREMENT, &cfg_.agc_increment);
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_AGC_DECREMENT, &cfg_.agc_decrement);
            speex_preprocess_ctl(speex_, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &cfg_.agc_max_gain_db);
#endif
        }
    };

    AudioProc::AudioProc() : impl_(std::make_unique<Impl>()) {}
    AudioProc::~AudioProc()
    {
        deinit();
    }
    Error AudioProc::init(const ProcCfg& cfg, uint32_t rate, uint8_t frame_ms)
    {
        return impl_->init(cfg, rate, frame_ms);
    }
    void AudioProc::deinit()
    {
        impl_->deinit();
    }
    bool AudioProc::is_init() const
    {
        return impl_->init_;
    }
    void AudioProc::process(int16_t* p, size_t n)
    {
        impl_->process(p, n);
    }
    void AudioProc::set_denoise(bool en)
    {
        impl_->set_denoise(en);
    }
    void AudioProc::set_agc(bool en)
    {
        impl_->set_agc(en);
    }
    void AudioProc::set_vad(bool en)
    {
        impl_->set_vad(en);
    }
    void AudioProc::set_dereverb(bool en)
    {
        impl_->set_dereverb(en);
    }
    void AudioProc::set_agc_level(float lv)
    {
        impl_->set_agc_level(lv);
    }
    void AudioProc::set_noise_suppress(int db)
    {
        impl_->set_noise_suppress(db);
    }
    void AudioProc::set_echo_suppress(int db)
    {
        impl_->set_echo_suppress(db);
    }
    bool AudioProc::denoise() const
    {
        return impl_->cfg_.denoise;
    }
    bool AudioProc::agc() const
    {
        return impl_->cfg_.agc;
    }
    bool AudioProc::vad() const
    {
        return impl_->cfg_.vad;
    }
    bool AudioProc::dereverb() const
    {
        return impl_->cfg_.dereverb;
    }
    const ProcCfg& AudioProc::cfg() const
    {
        return impl_->cfg_;
    }

    /*============================================================================
     * Capture - ALSA 采集
     *============================================================================*/

    class Capture::Impl
    {
    public:
        CaptureCfg            cfg_;
        FramePool*            pool_   = nullptr;
        AudioProc*            proc_   = nullptr;
        snd_pcm_t*            handle_ = nullptr;
        bool                  init_   = false;
        std::atomic<bool>     running_{false};
        std::atomic<bool>     stop_{false};
        std::thread           thread_;
        CaptureCb             cb_;
        std::mutex            cb_mtx_;
        std::atomic<uint32_t> frames_{0};
        std::atomic<uint32_t> drops_{0};

        Error init(const CaptureCfg& cfg, FramePool* pool, AudioProc* proc)
        {
            cfg_  = cfg;
            pool_ = pool;
            proc_ = proc;

#if HAS_ALSA
            int err = snd_pcm_open(&handle_, cfg.device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
            if (err < 0)
            {
                LOG_ERROR(TAG, "采集: 打开失败 %s", snd_strerror(err));
                return Error::DEVICE_ERROR;
            }

            err = snd_pcm_set_params(handle_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                     cfg.channels, cfg.rate, 1, cfg.frame_ms * 1000);
            if (err < 0)
            {
                LOG_ERROR(TAG, "采集: 配置失败 %s", snd_strerror(err));
                snd_pcm_close(handle_);
                handle_ = nullptr;
                return Error::DEVICE_ERROR;
            }
            LOG_INFO(TAG, "采集: %s %uHz %uch %ums", cfg.device.c_str(), cfg.rate, cfg.channels,
                     cfg.frame_ms);
#else
            LOG_WARN(TAG, "采集: 跳过(无ALSA)");
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

        Error start()
        {
            if (!init_ || running_)
                return Error::NOT_INIT;
            stop_    = false;
            thread_  = std::thread(&Impl::loop, this);
            running_ = true;
            LOG_INFO(TAG, "采集: 启动");
            return Error::OK;
        }

        void stop()
        {
            if (!running_)
                return;
            stop_ = true;
            if (thread_.joinable())
                thread_.join();
            running_ = false;
            LOG_INFO(TAG, "采集: 停止");
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

                /* 3A 原地处理 */
                if (proc_ && proc_->is_init())
                    proc_->process(frame->get<int16_t>(), frame->samples);

                frames_++;

                /* 触发回调 */
                std::lock_guard<std::mutex> lk(cb_mtx_);
                if (cb_)
                    cb_(frame);
            }
#else
            while (!stop_)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
        }

        void set_cb(CaptureCb cb)
        {
            std::lock_guard<std::mutex> lk(cb_mtx_);
            cb_ = std::move(cb);
        }
    };

    Capture::Capture() : impl_(std::make_unique<Impl>()) {}
    Capture::~Capture()
    {
        deinit();
    }
    Error Capture::init(const CaptureCfg& cfg, FramePool* pool, AudioProc* proc)
    {
        return impl_->init(cfg, pool, proc);
    }
    void Capture::deinit()
    {
        impl_->deinit();
    }
    bool Capture::is_init() const
    {
        return impl_->init_;
    }
    Error Capture::start()
    {
        return impl_->start();
    }
    Error Capture::stop()
    {
        impl_->stop();
        return Error::OK;
    }
    bool Capture::is_running() const
    {
        return impl_->running_;
    }
    void Capture::set_cb(CaptureCb cb)
    {
        impl_->set_cb(std::move(cb));
    }
    const CaptureCfg& Capture::cfg() const
    {
        return impl_->cfg_;
    }
    uint32_t Capture::drops() const
    {
        return impl_->drops_.load();
    }

    /*============================================================================
     * Playback - ALSA 播放
     *============================================================================*/

    class Playback::Impl
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
                LOG_ERROR(TAG, "播放: 打开失败 %s", snd_strerror(err));
                return Error::DEVICE_ERROR;
            }

            err = snd_pcm_set_params(handle_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                     cfg.channels, cfg.rate, 1, cfg.frame_ms * 1000);
            if (err < 0)
            {
                LOG_ERROR(TAG, "播放: 配置失败 %s", snd_strerror(err));
                snd_pcm_close(handle_);
                handle_ = nullptr;
                return Error::DEVICE_ERROR;
            }
            LOG_INFO(TAG, "播放: %s %uHz %uch %ums", cfg.device.c_str(), cfg.rate, cfg.channels,
                     cfg.frame_ms);
#else
            LOG_WARN(TAG, "播放: 跳过(无ALSA)");
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
            LOG_INFO(TAG, "播放: 启动");
            return Error::OK;
        }

        void stop()
        {
            if (!running_)
                return;
            stop_ = true;
            queue_cv_.notify_all();
            if (thread_.joinable())
                thread_.join();
            running_ = false;
            LOG_INFO(TAG, "播放: 停止");
        }

        Error push(const FramePtr& frame)
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

        Error push(const int16_t* data, size_t samples)
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
            return push(f);
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

    Playback::Playback() : impl_(std::make_unique<Impl>()) {}
    Playback::~Playback()
    {
        deinit();
    }
    Error Playback::init(const PlaybackCfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void Playback::deinit()
    {
        impl_->deinit();
    }
    bool Playback::is_init() const
    {
        return impl_->init_;
    }
    Error Playback::start()
    {
        return impl_->start();
    }
    Error Playback::stop()
    {
        impl_->stop();
        return Error::OK;
    }
    bool Playback::is_running() const
    {
        return impl_->running_;
    }
    Error Playback::push(const FramePtr& f)
    {
        return impl_->push(f);
    }
    Error Playback::push(const int16_t* d, size_t n)
    {
        return impl_->push(d, n);
    }
    void Playback::clear()
    {
        impl_->clear();
    }
    void Playback::set_volume(uint8_t v)
    {
        impl_->volume_.store(v > 100 ? 100 : v);
    }
    uint8_t Playback::volume() const
    {
        return impl_->volume_.load();
    }
    const PlaybackCfg& Playback::cfg() const
    {
        return impl_->cfg_;
    }
    size_t Playback::queue_size() const
    {
        return impl_->queue_size();
    }
    uint32_t Playback::drops() const
    {
        return impl_->drops_.load();
    }

    /*============================================================================
     * OpusCodec 实现
     *============================================================================*/

    class OpusCodec::Impl
    {
    public:
        OpusCfg               cfg_;
        FramePool*            pool_    = nullptr;
        OpusEncoder*          encoder_ = nullptr;
        OpusDecoder*          decoder_ = nullptr;
        bool                  init_    = false;
        std::atomic<uint32_t> enc_cnt_{0};
        std::atomic<uint32_t> dec_cnt_{0};

        Error init(const OpusCfg& cfg, FramePool* pool)
        {
            cfg_  = cfg;
            pool_ = pool;

#if HAS_OPUS
            int err  = 0;
            encoder_ = opus_encoder_create(static_cast<int>(cfg.rate), cfg.channels,
                                           OPUS_APPLICATION_VOIP, &err);
            if (err != OPUS_OK || !encoder_)
            {
                LOG_ERROR(TAG, "Opus: 编码器创建失败");
                return Error::CODEC_ERROR;
            }
            opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(static_cast<int>(cfg.bitrate)));
            opus_encoder_ctl(encoder_, OPUS_SET_VBR(cfg.vbr ? 1 : 0));
            opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

            decoder_ = opus_decoder_create(static_cast<int>(cfg.rate), cfg.channels, &err);
            if (err != OPUS_OK || !decoder_)
            {
                LOG_ERROR(TAG, "Opus: 解码器创建失败");
                opus_encoder_destroy(encoder_);
                encoder_ = nullptr;
                return Error::CODEC_ERROR;
            }
            LOG_INFO(TAG, "Opus: %uHz %uch %ukbps VBR=%d", cfg.rate, cfg.channels,
                     cfg.bitrate / 1000, cfg.vbr);
#else
            LOG_WARN(TAG, "Opus: 跳过(无库)");
#endif
            init_ = true;
            return Error::OK;
        }

        void deinit()
        {
#if HAS_OPUS
            if (encoder_)
            {
                opus_encoder_destroy(encoder_);
                encoder_ = nullptr;
            }
            if (decoder_)
            {
                opus_decoder_destroy(decoder_);
                decoder_ = nullptr;
            }
#endif
            init_ = false;
        }

        FramePtr encode(const int16_t* pcm, size_t samples)
        {
#if HAS_OPUS
            if (!encoder_ || !pool_)
                return nullptr;

            FramePtr out = pool_->alloc(OPUS_MAX_PACKET);
            if (!out)
                return nullptr;

            int len = opus_encode(encoder_, pcm, static_cast<int>(samples), out->data,
                                  static_cast<int>(out->capacity));
            if (len < 0)
                return nullptr;

            out->size    = static_cast<size_t>(len);
            out->samples = static_cast<uint32_t>(samples);
            enc_cnt_++;
            return out;
#else
            (void)pcm;
            (void)samples;
            return nullptr;
#endif
        }

        FramePtr decode(const uint8_t* opus_data, size_t len)
        {
#if HAS_OPUS
            if (!decoder_ || !pool_)
                return nullptr;

            /* 20ms 帧 */
            size_t   frame_samples = cfg_.rate * 20 / 1000;
            size_t   bytes         = frame_samples * cfg_.channels * sizeof(int16_t);
            FramePtr out           = pool_->alloc(bytes);
            if (!out)
                return nullptr;

            int ret = opus_decode(decoder_, opus_data, static_cast<int32_t>(len),
                                  out->get<int16_t>(), static_cast<int>(frame_samples), 0);
            if (ret < 0)
                return nullptr;

            out->samples  = static_cast<uint32_t>(ret);
            out->size     = static_cast<size_t>(ret) * cfg_.channels * sizeof(int16_t);
            out->rate     = cfg_.rate;
            out->channels = cfg_.channels;
            dec_cnt_++;
            return out;
#else
            (void)opus_data;
            (void)len;
            return nullptr;
#endif
        }

        Error set_bitrate(uint32_t bps)
        {
            cfg_.bitrate = bps;
#if HAS_OPUS
            if (encoder_)
                opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(static_cast<int>(bps)));
#endif
            return Error::OK;
        }
    };

    OpusCodec::OpusCodec() : impl_(std::make_unique<Impl>()) {}
    OpusCodec::~OpusCodec()
    {
        deinit();
    }
    Error OpusCodec::init(const OpusCfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void OpusCodec::deinit()
    {
        impl_->deinit();
    }
    bool OpusCodec::is_init() const
    {
        return impl_->init_;
    }
    FramePtr OpusCodec::encode(const int16_t* p, size_t n)
    {
        return impl_->encode(p, n);
    }
    FramePtr OpusCodec::encode(const FramePtr& f)
    {
        return f ? impl_->encode(f->get<int16_t>(), f->samples) : nullptr;
    }
    FramePtr OpusCodec::decode(const uint8_t* p, size_t n)
    {
        return impl_->decode(p, n);
    }
    FramePtr OpusCodec::decode(const FramePtr& f)
    {
        return f ? impl_->decode(f->data, f->size) : nullptr;
    }
    Error OpusCodec::set_bitrate(uint32_t bps)
    {
        return impl_->set_bitrate(bps);
    }
    const OpusCfg& OpusCodec::cfg() const
    {
        return impl_->cfg_;
    }
    uint32_t OpusCodec::encode_cnt() const
    {
        return impl_->enc_cnt_.load();
    }
    uint32_t OpusCodec::decode_cnt() const
    {
        return impl_->dec_cnt_.load();
    }

    /*============================================================================
     * Resampler - libsamplerate
     *============================================================================*/

    class Resampler::Impl
    {
    public:
        FramePool* pool_     = nullptr;
        SRC_STATE* src_      = nullptr;
        uint8_t    channels_ = 1;
        bool       init_     = false;

        Error init(uint8_t channels, FramePool* pool)
        {
            channels_ = channels;
            pool_     = pool;

#if HAS_SRC
            int err = 0;
            src_    = src_new(SRC_SINC_FASTEST, channels, &err);
            if (!src_)
            {
                LOG_ERROR(TAG, "重采样: 初始化失败");
                return Error::CODEC_ERROR;
            }
            LOG_INFO(TAG, "重采样: %uch", channels);
#else
            LOG_WARN(TAG, "重采样: 跳过(无库)");
#endif
            init_ = true;
            return Error::OK;
        }

        void deinit()
        {
#if HAS_SRC
            if (src_)
            {
                src_delete(src_);
                src_ = nullptr;
            }
#endif
            init_ = false;
        }

        FramePtr resample(const int16_t* in, size_t samples, uint32_t src_rate, uint32_t dst_rate)
        {
            if (src_rate == dst_rate)
                return nullptr; /* 不需要重采样 */

#if HAS_SRC
            if (!src_ || !pool_)
                return nullptr;

            src_reset(src_);

            double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
            size_t out_n = static_cast<size_t>(samples * ratio) + 16;
            size_t bytes = out_n * channels_ * sizeof(int16_t);

            FramePtr out = pool_->alloc(bytes);
            if (!out)
                return nullptr;

            std::vector<float> fi(samples * channels_);
            std::vector<float> fo(out_n * channels_);

            for (size_t i = 0; i < samples * channels_; ++i)
                fi[i] = static_cast<float>(in[i]) / 32768.0f;

            SRC_DATA d{};
            d.data_in       = fi.data();
            d.input_frames  = static_cast<long>(samples);
            d.data_out      = fo.data();
            d.output_frames = static_cast<long>(out_n);
            d.src_ratio     = ratio;
            d.end_of_input  = 1;

            if (src_process(src_, &d) != 0)
                return nullptr;

            int16_t* po = out->get<int16_t>();
            for (long i = 0; i < d.output_frames_gen * static_cast<long>(channels_); ++i)
            {
                float v = fo[static_cast<size_t>(i)];
                v       = (v < -1.0f) ? -1.0f : ((v > 1.0f) ? 1.0f : v);
                po[i]   = static_cast<int16_t>(v * 32767.0f);
            }

            out->samples  = static_cast<uint32_t>(d.output_frames_gen);
            out->size     = static_cast<size_t>(d.output_frames_gen) * channels_ * sizeof(int16_t);
            out->rate     = dst_rate;
            out->channels = channels_;
            return out;
#else
            (void)in;
            (void)samples;
            (void)src_rate;
            (void)dst_rate;
            return nullptr;
#endif
        }
    };

    Resampler::Resampler() : impl_(std::make_unique<Impl>()) {}
    Resampler::~Resampler()
    {
        deinit();
    }
    Error Resampler::init(uint8_t channels, FramePool* pool)
    {
        return impl_->init(channels, pool);
    }
    void Resampler::deinit()
    {
        impl_->deinit();
    }
    bool Resampler::is_init() const
    {
        return impl_->init_;
    }
    FramePtr Resampler::resample(const int16_t* in, size_t n, uint32_t sr, uint32_t dr)
    {
        return impl_->resample(in, n, sr, dr);
    }
    FramePtr Resampler::resample(const FramePtr& in, uint32_t dst_rate)
    {
        return in ? impl_->resample(in->get<int16_t>(), in->samples, in->rate, dst_rate) : nullptr;
    }

    /*============================================================================
     * WavRecorder 实现
     *============================================================================*/

    class WavRecorder::Impl
    {
    public:
        std::unique_ptr<FileWrapper>          file_;
        std::mutex                            mtx_;
        bool                                  recording_ = false;
        uint32_t                              rate_      = 48000;
        uint8_t                               channels_  = 1;
        int                                   duration_  = 0;
        std::atomic<uint32_t>                 frames_{0};
        std::atomic<size_t>                   bytes_{0};
        std::chrono::steady_clock::time_point start_;

        Error start(const std::string& path, uint32_t rate, uint8_t channels, int duration_sec)
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (recording_)
                return Error::BUSY;

            file_ = std::make_unique<FileWrapper>(path, FileMode::WRITE);
            if (!file_->valid())
            {
                LOG_ERROR(TAG, "WAV: 创建失败 %s", path.c_str());
                file_.reset();
                return Error::DEVICE_ERROR;
            }

            rate_     = rate;
            channels_ = channels;
            duration_ = duration_sec;

            WavHeader h{};
            h.channels        = channels;
            h.sample_rate     = rate;
            h.bits_per_sample = 16;
            h.block_align     = static_cast<uint16_t>(channels * 2);
            h.byte_rate       = rate * h.block_align;

            if (!file_->write(&h, sizeof(h)))
            {
                file_.reset();
                return Error::DEVICE_ERROR;
            }

            frames_    = 0;
            bytes_     = 0;
            start_     = std::chrono::steady_clock::now();
            recording_ = true;
            LOG_INFO(TAG, "WAV: 开始 %s %uHz %uch", path.c_str(), rate, channels);
            return Error::OK;
        }

        Error stop()
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!recording_)
                return Error::OK;

            recording_ = false;
            if (file_)
            {
                size_t data_sz = bytes_.load();

                /* 回填 RIFF 总大小 */
                file_->seek(4, SEEK_SET);
                uint32_t fsz = static_cast<uint32_t>(sizeof(WavHeader) - 8 + data_sz);
                file_->write(&fsz, sizeof(fsz));

                /* 回填 data chunk 大小 */
                file_->seek(40, SEEK_SET);
                uint32_t dsz = static_cast<uint32_t>(data_sz);
                file_->write(&dsz, sizeof(dsz));

                file_->flush();
                file_.reset();
                LOG_INFO(TAG, "WAV: 停止 %u帧 %uKB", frames_.load(), (unsigned)(data_sz / 1024));
            }
            return Error::OK;
        }

        void write(const int16_t* data, size_t samples)
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!recording_ || !file_)
                return;

            size_t bytes = samples * channels_ * sizeof(int16_t);
            if (file_->write(data, bytes))
            {
                frames_++;
                bytes_ += bytes;
            }

            /* 时长限制 */
            if (duration_ > 0)
            {
                auto sec = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start_)
                               .count();
                if (sec >= duration_)
                    recording_ = false;
            }
        }

        void write(const FramePtr& f)
        {
            if (f)
                write(f->get<int16_t>(), f->samples);
        }

        uint32_t duration_sec() const
        {
            if (!recording_)
                return 0;
            return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::steady_clock::now() - start_)
                                             .count());
        }

        uint64_t file_size() const
        {
            return bytes_.load();
        }
        uint32_t frames() const
        {
            return frames_.load();
        }
    };

    WavRecorder::WavRecorder() : impl_(std::make_unique<Impl>()) {}
    WavRecorder::~WavRecorder()
    {
        stop();
    }
    Error WavRecorder::start(const std::string& path, uint32_t rate, uint8_t channels, int dur)
    {
        return impl_->start(path, rate, channels, dur);
    }
    Error WavRecorder::stop()
    {
        return impl_->stop();
    }
    bool WavRecorder::is_recording() const
    {
        return impl_->recording_;
    }
    void WavRecorder::write(const int16_t* d, size_t n)
    {
        impl_->write(d, n);
    }
    void WavRecorder::write(const FramePtr& f)
    {
        impl_->write(f);
    }
    uint32_t WavRecorder::duration_sec() const
    {
        return impl_->duration_sec();
    }
    uint64_t WavRecorder::file_size() const
    {
        return impl_->file_size();
    }
    uint32_t WavRecorder::frames() const
    {
        return impl_->frames();
    }

    /*============================================================================
     * AudioDrv - 主门面
     *============================================================================*/

    static constexpr uint32_t AI_OPUS_RATE    = 16000;
    static constexpr uint32_t AI_OPUS_BITRATE = 32000;
    static constexpr uint32_t CAPTURE_RATE    = 48000;

    class AudioDrv::Impl
    {
    public:
        AudioCfg   cfg_;
        bool       init_    = false;
        bool       running_ = false;
        ErrorCb    error_cb_;
        std::mutex error_mtx_;

        FramePool   pool_;
        Capture     capture_;
        Playback    playback_;
        OpusCodec   opus_;
        Resampler   resampler_;
        AudioProc   proc_;
        WavRecorder wav_;

#if HAS_OPUS
        OpusEncoder* ai_encoder_ = nullptr; // 16kHz Opus 编码器
#endif

        std::atomic<uint32_t>                 capture_frames_{0};
        std::atomic<uint32_t>                 playback_frames_{0};
        std::chrono::steady_clock::time_point stats_time_;

        Error init(const AudioCfg& cfg)
        {
            if (init_)
                return Error::ALREADY_INIT;

            cfg_ = cfg;

            /* 1. 帧内存池 */
            if (pool_.init(cfg.memory) != Error::OK)
                return Error::MEMORY_ERROR;

            /* 2. 3A 处理器 */
            if (cfg.enable_proc)
            {
                if (proc_.init(cfg.proc, cfg.capture.rate, cfg.capture.frame_ms) != Error::OK)
                {
                    pool_.deinit();
                    return Error::CODEC_ERROR;
                }
            }

            /* 3. 采集 */
            if (cfg.enable_capture)
            {
                AudioProc* p = cfg.enable_proc ? &proc_ : nullptr;
                if (capture_.init(cfg.capture, &pool_, p) != Error::OK)
                {
                    proc_.deinit();
                    pool_.deinit();
                    return Error::DEVICE_ERROR;
                }
            }

            /* 4. 播放 */
            if (cfg.enable_playback)
            {
                if (playback_.init(cfg.playback, &pool_) != Error::OK)
                {
                    capture_.deinit();
                    proc_.deinit();
                    pool_.deinit();
                    return Error::DEVICE_ERROR;
                }
            }

            /* 5. Opus 编解码 */
            if (cfg.enable_opus)
            {
                if (opus_.init(cfg.opus, &pool_) != Error::OK)
                {
                    playback_.deinit();
                    capture_.deinit();
                    proc_.deinit();
                    pool_.deinit();
                    return Error::CODEC_ERROR;
                }
            }

            /* 6. 重采样器 */
            if (resampler_.init(cfg.capture.channels, &pool_) != Error::OK)
            {
                opus_.deinit();
                playback_.deinit();
                capture_.deinit();
                proc_.deinit();
                pool_.deinit();
                return Error::CODEC_ERROR;
            }

#if HAS_OPUS
            /* 7. AI 上发用 16kHz Opus 编码器 */
            int opus_err = 0;
            ai_encoder_  = opus_encoder_create(static_cast<int>(AI_OPUS_RATE), 1,
                                               OPUS_APPLICATION_VOIP, &opus_err);
            if (opus_err == OPUS_OK && ai_encoder_)
            {
                opus_encoder_ctl(ai_encoder_, OPUS_SET_BITRATE(static_cast<int>(AI_OPUS_BITRATE)));
                opus_encoder_ctl(ai_encoder_, OPUS_SET_VBR(1));
                opus_encoder_ctl(ai_encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
                LOG_INFO(TAG, "AI Opus: %uHz 1ch %ukbps", AI_OPUS_RATE, AI_OPUS_BITRATE / 1000);
            }
            else
            {
                LOG_WARN(TAG, "AI Opus 编码器创建失败，上发将不可用");
            }
#endif

            stats_time_ = std::chrono::steady_clock::now();
            init_       = true;
            LOG_INFO(TAG, "初始化完成 capture=%d playback=%d opus=%d proc=%d", cfg.enable_capture,
                     cfg.enable_playback, cfg.enable_opus, cfg.enable_proc);
            return Error::OK;
        }

        void deinit()
        {
            if (!init_)
                return;
            stop();
#if HAS_OPUS
            if (ai_encoder_)
            {
                opus_encoder_destroy(ai_encoder_);
                ai_encoder_ = nullptr;
            }
#endif
            resampler_.deinit();
            opus_.deinit();
            playback_.deinit();
            capture_.deinit();
            proc_.deinit();
            pool_.deinit();
            init_ = false;
            LOG_INFO(TAG, "已释放");
        }

        Error start()
        {
            if (running_)
                return Error::OK;

            if (cfg_.enable_capture && capture_.start() != Error::OK)
                return Error::DEVICE_ERROR;

            if (cfg_.enable_playback && playback_.start() != Error::OK)
            {
                capture_.stop();
                return Error::DEVICE_ERROR;
            }

            running_ = true;
            LOG_INFO(TAG, "启动");
            return Error::OK;
        }

        void stop()
        {
            if (!running_)
                return;
            wav_.stop();
            playback_.stop();
            capture_.stop();
            running_ = false;
            LOG_INFO(TAG, "停止");
        }

        Stats stats() const
        {
            Stats s{};
            auto  now = std::chrono::steady_clock::now();
            auto  elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - stats_time_).count();

            if (elapsed > 0)
            {
                s.capture_fps  = capture_frames_.load() * 1000.0f / static_cast<float>(elapsed);
                s.playback_fps = playback_frames_.load() * 1000.0f / static_cast<float>(elapsed);
            }

            s.capture_frames  = capture_frames_.load();
            s.capture_drops   = capture_.drops();
            s.playback_frames = playback_frames_.load();
            s.playback_drops  = playback_.drops();
            s.encode_cnt      = opus_.encode_cnt();
            s.decode_cnt      = opus_.decode_cnt();
            s.wav_frames      = wav_.frames();
            s.wav_sec         = wav_.duration_sec();
            s.mem_used        = pool_.used();
            s.mem_total       = pool_.total();
            return s;
        }

        void reset_stats()
        {
            capture_frames_  = 0;
            playback_frames_ = 0;
            stats_time_      = std::chrono::steady_clock::now();
        }

        FramePtr encodeCaptureForAI(const FramePtr& pcm_48k)
        {
            if (!pcm_48k || !pcm_48k->data || pcm_48k->samples == 0)
                return nullptr;
#if HAS_OPUS
            if (!ai_encoder_ || !resampler_.is_init())
                return nullptr;

            /* 1. 重采样 48kHz → 16kHz */
            FramePtr pcm_16k = resampler_.resample(pcm_48k->get<int16_t>(), pcm_48k->samples,
                                                   CAPTURE_RATE, AI_OPUS_RATE);
            if (!pcm_16k || pcm_16k->samples == 0)
                return nullptr;

            /* 2. Opus 编码 */
            FramePtr opus_frame = pool_.alloc(OPUS_MAX_PACKET);
            if (!opus_frame)
                return nullptr;

            int len = opus_encode(ai_encoder_, pcm_16k->get<int16_t>(),
                                  static_cast<int>(pcm_16k->samples), opus_frame->data,
                                  static_cast<int>(opus_frame->capacity));
            if (len <= 0)
                return nullptr;

            opus_frame->size    = static_cast<size_t>(len);
            opus_frame->samples = pcm_16k->samples;
            opus_frame->rate    = AI_OPUS_RATE;
            return opus_frame;
#else
            (void)pcm_48k;
            return nullptr;
#endif
        }
    };

    AudioDrv::AudioDrv() : impl_(std::make_unique<Impl>()) {}
    AudioDrv::~AudioDrv()
    {
        deinit();
    }
    Error AudioDrv::init(const AudioCfg& cfg)
    {
        return impl_->init(cfg);
    }
    void AudioDrv::deinit()
    {
        impl_->deinit();
    }
    bool AudioDrv::is_init() const
    {
        return impl_->init_;
    }
    Error AudioDrv::start()
    {
        return impl_->start();
    }
    Error AudioDrv::stop()
    {
        impl_->stop();
        return Error::OK;
    }
    bool AudioDrv::is_running() const
    {
        return impl_->running_;
    }

    Capture& AudioDrv::capture()
    {
        return impl_->capture_;
    }
    Playback& AudioDrv::playback()
    {
        return impl_->playback_;
    }
    OpusCodec& AudioDrv::opus()
    {
        return impl_->opus_;
    }
    Resampler& AudioDrv::resampler()
    {
        return impl_->resampler_;
    }
    AudioProc& AudioDrv::proc()
    {
        return impl_->proc_;
    }
    WavRecorder& AudioDrv::wav()
    {
        return impl_->wav_;
    }
    FramePool& AudioDrv::pool()
    {
        return impl_->pool_;
    }

    void AudioDrv::set_capture_cb(CaptureCb cb)
    {
        impl_->capture_.set_cb(
            [this, cb = std::move(cb)](const FramePtr& f)
            {
                impl_->capture_frames_++;
                if (cb)
                    cb(f);
            });
    }

    void AudioDrv::set_error_cb(ErrorCb cb)
    {
        std::lock_guard<std::mutex> lk(impl_->error_mtx_);
        impl_->error_cb_ = std::move(cb);
    }

    FramePtr AudioDrv::encodeCaptureForAI(const FramePtr& f)
    {
        return impl_->encodeCaptureForAI(f);
    }
    Stats AudioDrv::stats() const
    {
        return impl_->stats();
    }
    void AudioDrv::reset_stats()
    {
        impl_->reset_stats();
    }
    const AudioCfg& AudioDrv::cfg() const
    {
        return impl_->cfg_;
    }

} // namespace app::media::audio
