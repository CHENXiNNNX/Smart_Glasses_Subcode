/* alsa_capture.hpp - ALSA 采集后端 */

#pragma once

#include "capture_backend.hpp"
#include <memory>

namespace app::media::audio
{

    class AlsaCapture : public ICaptureBackend
    {
    public:
        AlsaCapture();
        ~AlsaCapture() override;

        Error init(const CaptureCfg& cfg, FramePool* pool) override;
        void  deinit() override;
        Error start(OnFrame on_frame) override;
        Error stop() override;
        bool  is_running() const override;

        bool has_builtin_vqe() const override
        {
            return false;
        }
        const char* name() const override
        {
            return "ALSA";
        }
        uint32_t drops() const override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
