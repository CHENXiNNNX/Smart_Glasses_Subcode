/* playback_backend.hpp - 播放后端纯虚接口 */

#pragma once

#include "../core/types.hpp"
#include "../core/frame_pool.hpp"

#include <memory>

namespace app::media::audio
{

    class IPlaybackBackend
    {
    public:
        virtual ~IPlaybackBackend() = default;

        virtual Error init(const PlaybackCfg& cfg, FramePool* pool) = 0;
        virtual void  deinit()                                      = 0;
        virtual Error start()                                       = 0;
        virtual Error stop()                                        = 0;
        virtual bool  is_running() const                            = 0;

        virtual Error push(const FramePtr& frame)               = 0;
        virtual Error push(const int16_t* data, size_t samples) = 0;
        virtual void  clear()                                   = 0;

        virtual void        set_volume(uint8_t vol) = 0;
        virtual uint8_t     volume() const          = 0;
        virtual const char* name() const            = 0;
        virtual size_t      queue_size() const      = 0;
        virtual uint32_t    drops() const           = 0;
    };

} // namespace app::media::audio
