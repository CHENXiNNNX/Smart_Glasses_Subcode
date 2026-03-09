/*
 * rtsp_server.hpp - RTSP 推流
 */

#pragma once

#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace app::media::camera
{

    class RtspServer
    {
    public:
        RtspServer();
        ~RtspServer();

        RtspServer(const RtspServer&)            = delete;
        RtspServer& operator=(const RtspServer&) = delete;

        Error start(uint16_t port, const std::string& path);
        Error stop();
        bool  is_running() const;

        void send_frame(const uint8_t* data, size_t size, uint64_t pts, bool keyframe = false);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
