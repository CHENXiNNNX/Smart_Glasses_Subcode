/* frame_pool.hpp - 帧内存池 */

#pragma once

#include "types.hpp"
#include <memory>

namespace app::media::audio
{

    class FramePool
    {
    public:
        FramePool();
        ~FramePool();

        FramePool(const FramePool&)            = delete;
        FramePool& operator=(const FramePool&) = delete;

        Error init(const MemoryCfg& cfg);
        void  deinit();

        FramePtr alloc(size_t size);

        size_t used() const;
        size_t total() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
