/* resampler.hpp - 重采样（libsamplerate） */

#pragma once

#include "../core/types.hpp"
#include "../core/frame_pool.hpp"
#include <memory>

namespace app::media::audio
{

    class Resampler
    {
    public:
        Resampler();
        ~Resampler();

        Resampler(const Resampler&)            = delete;
        Resampler& operator=(const Resampler&) = delete;

        Error init(uint8_t channels, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        FramePtr resample(const int16_t* in, size_t samples, uint32_t src_rate, uint32_t dst_rate);
        FramePtr resample(const FramePtr& in, uint32_t dst_rate);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
