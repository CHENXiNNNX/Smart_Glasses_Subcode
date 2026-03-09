/*
 * jpeg_encoder.hpp - JPEG 编码器
 */

#pragma once

#include "../../types.hpp"
#include "../../pool/frame_pool.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace app::media::camera
{

    class JpegEncoder
    {
    public:
        JpegEncoder();
        ~JpegEncoder();

        JpegEncoder(const JpegEncoder&)            = delete;
        JpegEncoder& operator=(const JpegEncoder&) = delete;

        Error init(const JpegCfg& cfg, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        Error start();
        Error stop();
        bool  is_running() const;

        void set_cb(JpegCb cb);

        Error set_quality(uint8_t quality);
        Error set_resolution(uint16_t w, uint16_t h);
        Error save(const std::string& path, PhotoCb cb = nullptr);

        const JpegCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
