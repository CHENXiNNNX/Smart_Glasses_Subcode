/* wav_recorder.hpp - WAV 文件录制工具 */

#pragma once

#include "../core/types.hpp"
#include <memory>
#include <string>

namespace app::media::audio
{

    class WavRecorder
    {
    public:
        WavRecorder();
        ~WavRecorder();

        WavRecorder(const WavRecorder&)            = delete;
        WavRecorder& operator=(const WavRecorder&) = delete;

        Error start(const std::string& path, uint32_t rate, uint8_t channels, int duration_sec = 0);
        Error stop();
        bool  is_recording() const;

        void write(const int16_t* data, size_t samples);
        void write(const FramePtr& frame);

        uint32_t duration_sec() const;
        uint64_t file_size() const;
        uint32_t frames() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
