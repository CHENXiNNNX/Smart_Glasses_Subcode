/* capture_backend.hpp - 采集后端纯虚接口 */

#pragma once

#include "../core/types.hpp"
#include "../core/frame_pool.hpp"

#include <functional>
#include <memory>

namespace app::media::audio
{

    class ICaptureBackend
    {
    public:
        using OnFrame = std::function<void(FramePtr)>;

        virtual ~ICaptureBackend() = default;

        virtual Error init(const CaptureCfg& cfg, FramePool* pool) = 0;
        virtual void  deinit()                                     = 0;
        virtual Error start(OnFrame on_frame)                      = 0;
        virtual Error stop()                                       = 0;
        virtual bool  is_running() const                           = 0;

        virtual bool        has_builtin_vqe() const = 0;
        virtual const char* name() const            = 0;
        virtual uint32_t    drops() const           = 0;
    };

} // namespace app::media::audio
