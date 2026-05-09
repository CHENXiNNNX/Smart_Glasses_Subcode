/* opus_codec.hpp - Opus 编解码 */

#pragma once

#include "../core/types.hpp"
#include "../core/frame_pool.hpp"
#include <memory>

namespace app::media::audio
{

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

} // namespace app::media::audio
