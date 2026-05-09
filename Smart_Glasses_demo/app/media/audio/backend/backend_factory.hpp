/* backend_factory.hpp - 后端工厂 */

#pragma once

#include "capture_backend.hpp"
#include "playback_backend.hpp"

#include <memory>

namespace app::media::audio
{

    class BackendFactory
    {
    public:
        static std::unique_ptr<ICaptureBackend>
        createCapture(BackendMode mode, const CaptureCfg& cfg, FramePool* pool);

        static std::unique_ptr<IPlaybackBackend>
        createPlayback(BackendMode mode, const PlaybackCfg& cfg, FramePool* pool);
    };

} // namespace app::media::audio
