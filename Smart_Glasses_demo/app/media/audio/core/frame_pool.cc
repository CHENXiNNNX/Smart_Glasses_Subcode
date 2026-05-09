/* frame_pool.cc - 帧内存池实现 */

#include "frame_pool.hpp"
#include "../../../tool/log/log.hpp"
#include "../../../tool/memory/memory.hpp"
#include "../../../tool/time/time.hpp"

#include <mutex>

namespace app::media::audio
{

    using namespace tool::log;
    using namespace tool::time;

#define TAG "AudioPool"

    class FramePool::Impl
    {
    public:
        std::unique_ptr<tool::memory::MemoryPool> pool_;
        std::mutex                                mtx_;
        size_t                                    total_ = 0;

        Error init(const MemoryCfg& cfg)
        {
            size_t sz = cfg.fixed_block_size * cfg.fixed_block_count + cfg.dynamic_max_size;
            pool_     = std::make_unique<tool::memory::MemoryPool>(sz);
            total_    = sz;
            LOG_INFO(TAG, "帧池: %uKB", (unsigned)(sz / 1024));
            return Error::OK;
        }

        void deinit()
        {
            pool_.reset();
            total_ = 0;
        }

        FramePtr alloc(size_t size)
        {
            if (!pool_ || !pool_->valid())
                return nullptr;

            void* mem = nullptr;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                mem = pool_->allocate(size);
            }
            if (!mem)
                return nullptr;

            Frame* f     = new Frame();
            f->data      = static_cast<uint8_t*>(mem);
            f->size      = size;
            f->capacity  = size;
            f->timestamp = uptime_us();

            return FramePtr(f,
                            [this, mem](Frame* p)
                            {
                                std::lock_guard<std::mutex> lk(mtx_);
                                if (pool_)
                                    pool_->deallocate(mem);
                                delete p;
                            });
        }

        size_t used() const
        {
            return pool_ ? pool_->get_used_memory_fast() : 0;
        }
        size_t total() const
        {
            return total_;
        }
    };

    FramePool::FramePool() : impl_(std::make_unique<Impl>()) {}
    FramePool::~FramePool()
    {
        deinit();
    }

    Error FramePool::init(const MemoryCfg& cfg)
    {
        return impl_->init(cfg);
    }
    void FramePool::deinit()
    {
        impl_->deinit();
    }

    FramePtr FramePool::alloc(size_t size)
    {
        return impl_->alloc(size);
    }

    size_t FramePool::used() const
    {
        return impl_->used();
    }
    size_t FramePool::total() const
    {
        return impl_->total();
    }

} // namespace app::media::audio
