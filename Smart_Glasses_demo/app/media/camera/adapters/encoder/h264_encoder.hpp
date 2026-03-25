/*
 * h264_encoder.hpp - H264 编码器
 */

#pragma once

#include "../../types.hpp"
#include "../../pool/frame_pool.hpp"

#include <cstdint>
#include <memory>

namespace app::media::camera
{

    class H264Encoder
    {
    public:
        H264Encoder();
        ~H264Encoder();

        H264Encoder(const H264Encoder&)            = delete;
        H264Encoder& operator=(const H264Encoder&) = delete;

        Error init(const H264Cfg& cfg, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        Error start();
        Error stop();
        bool  is_running() const;

        void set_cb(H264Cb cb);

        Error set_bitrate(uint16_t kbps);
        Error set_gop(uint8_t gop);
        Error set_resolution(uint16_t w, uint16_t h);
        Error request_idr();

        const H264Cfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
