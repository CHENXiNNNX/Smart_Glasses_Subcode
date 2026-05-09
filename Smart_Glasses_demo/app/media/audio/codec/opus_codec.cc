/* opus_codec.cc - Opus 编解码实现 */

#include "opus_codec.hpp"
#include "../../../tool/log/log.hpp"

#include <atomic>
#include <vector>

/*========================================================================
 * Opus 条件编译
 *========================================================================*/

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

namespace app::media::audio
{

    using namespace tool::log;

#define TAG "Opus"

    static constexpr size_t OPUS_MAX_PACKET = 4096;

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

        std::vector<int16_t> enc_pcm_;
        uint32_t             enc_frame_spc_ = 0;

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
                LOG_ERROR(TAG, "编码器创建失败");
                return Error::CODEC_ERROR;
            }
            opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(static_cast<int>(cfg.bitrate)));
            opus_encoder_ctl(encoder_, OPUS_SET_VBR(cfg.vbr ? 1 : 0));
            opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

            decoder_ = opus_decoder_create(static_cast<int>(cfg.rate), cfg.channels, &err);
            if (err != OPUS_OK || !decoder_)
            {
                LOG_ERROR(TAG, "解码器创建失败");
                opus_encoder_destroy(encoder_);
                encoder_ = nullptr;
                return Error::CODEC_ERROR;
            }
            enc_frame_spc_ = cfg.rate * 20 / 1000;
            if (enc_frame_spc_ == 0)
                enc_frame_spc_ = 480;
            enc_pcm_.clear();
            LOG_INFO(TAG, "%uHz %uch %ukbps VBR=%d 帧=%ums/%u样点/声", cfg.rate, cfg.channels,
                     cfg.bitrate / 1000, cfg.vbr, 20u, enc_frame_spc_);
#else
            LOG_WARN(TAG, "跳过(无库)");
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
            enc_pcm_.clear();
            init_ = false;
        }

        /* samples =每声道样点数（与 ALSA snd_pcm_readi、Frame::samples 一致）；每次调用最多输出 1
         * 个 Opus 包 */
        FramePtr encode(const int16_t* pcm, size_t samples_per_channel)
        {
#if HAS_OPUS
            if (!encoder_ || !pool_)
                return nullptr;

            if (pcm && samples_per_channel > 0)
            {
                const size_t ch = cfg_.channels;
                enc_pcm_.insert(enc_pcm_.end(), pcm, pcm + samples_per_channel * ch);
            }

            const size_t need = static_cast<size_t>(enc_frame_spc_) * cfg_.channels;
            if (enc_pcm_.size() < need)
                return nullptr;

            FramePtr out = pool_->alloc(OPUS_MAX_PACKET);
            if (!out)
                return nullptr;

            int len = opus_encode(encoder_, enc_pcm_.data(), static_cast<int>(enc_frame_spc_),
                                  out->data, static_cast<int>(out->capacity));
            if (len < 0)
            {
                LOG_WARN(TAG, "opus_encode 失败: %d", len);
                enc_pcm_.erase(enc_pcm_.begin(), enc_pcm_.begin() + need);
                return nullptr;
            }

            enc_pcm_.erase(enc_pcm_.begin(), enc_pcm_.begin() + need);
            out->size     = static_cast<size_t>(len);
            out->samples  = enc_frame_spc_;
            out->rate     = cfg_.rate;
            out->channels = cfg_.channels;
            enc_cnt_++;
            return out;
#else
            (void)pcm;
            (void)samples_per_channel;
            return nullptr;
#endif
        }

        FramePtr decode(const uint8_t* opus_data, size_t len)
        {
#if HAS_OPUS
            if (!decoder_ || !pool_)
                return nullptr;

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

} // namespace app::media::audio
