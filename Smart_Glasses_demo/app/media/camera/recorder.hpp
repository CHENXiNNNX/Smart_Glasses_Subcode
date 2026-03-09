/*
 * recorder.hpp - 录像
 */

#pragma once

#include "types.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace app::media::camera
{

    class Recorder
    {
    public:
        Recorder();
        ~Recorder();

        Recorder(const Recorder&)            = delete;
        Recorder& operator=(const Recorder&) = delete;

        Error start(const std::string& path, int duration_sec = 0);
        Error stop();
        bool  is_recording() const;

        uint32_t duration_sec() const;
        uint64_t file_size() const;
        uint32_t frames() const;

        void write_frame(const uint8_t* data, size_t size);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
