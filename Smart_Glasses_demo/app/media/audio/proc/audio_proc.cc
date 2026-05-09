/* audio_proc.cc - Speex 3A 实现 */

#include "audio_proc.hpp"
#include "../../../tool/log/log.hpp"

/*========================================================================
 * Speex 条件编译
 *========================================================================*/

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

#define TAG "AudioProc"

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
                LOG_ERROR(TAG, "Speex 初始化失败");
                return Error::CODEC_ERROR;
            }
            apply_all_params();
            LOG_INFO(TAG, "DNS=%d AGC=%d VAD=%d DEREVERB=%d 帧=%u", cfg.denoise, cfg.agc, cfg.vad,
                     cfg.dereverb, frame_sz_);
#else
            LOG_WARN(TAG, "跳过(无Speex)");
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

        /* 运行时参数调整 */
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

} // namespace app::media::audio
