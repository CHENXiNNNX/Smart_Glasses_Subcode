/* resampler.cc - 重采样实现 */

#include "resampler.hpp"
#include "../../../tool/log/log.hpp"

#include <vector>

/*========================================================================
 * libsamplerate 条件编译
 *========================================================================*/

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

namespace app::media::audio
{

    using namespace tool::log;

#define TAG "Resampler"

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
                LOG_ERROR(TAG, "初始化失败");
                return Error::CODEC_ERROR;
            }
            LOG_INFO(TAG, "%uch", channels);
#else
            LOG_WARN(TAG, "跳过(无库)");
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
                return nullptr;

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

} // namespace app::media::audio
