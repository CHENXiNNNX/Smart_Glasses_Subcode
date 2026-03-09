/*
 * frame_pool.cc - 帧内存池
 */

#include "frame_pool.hpp"
#include "tool/log/log.hpp"
#include "tool/memory/memory.hpp"

#include <mutex>

namespace app::media::camera
{

    using namespace tool::log;

#define TAG "Camera"

    class FramePool::Impl
    {
    public:
        std::unique_ptr<tool::memory::MemoryPool> pool_;
        std::mutex                                mtx_;
        size_t                                    total_ = 0;

        Error init(const MemoryCfg& cfg)
        {
            size_t pool_size = cfg.fixed_block_size * cfg.fixed_block_count + cfg.dynamic_max_size;
            pool_            = std::make_unique<tool::memory::MemoryPool>(pool_size);
            total_           = pool_size;
            LOG_INFO(TAG, "帧池: %uKB", static_cast<unsigned>(pool_size / 1024));
            return Error::OK;
        }

        void deinit() { pool_.reset(); }

        FramePtr alloc(size_t size)
        {
            if (!pool_)
                return nullptr;
            std::lock_guard<std::mutex> lk(mtx_);
            void* mem = pool_->allocate(size);
            if (!mem)
                return nullptr;
            auto frame     = std::make_shared<Frame>();
            frame->data    = static_cast<uint8_t*>(mem);
            frame->size    = size;
            frame->priv    = mem;
            frame->release = [this, mem]()
            {
                std::lock_guard<std::mutex> lk2(mtx_);
                if (pool_)
                    pool_->deallocate(mem);
            };
            return frame;
        }

        size_t used() const { return pool_ ? pool_->get_used_memory_fast() : 0; }
        size_t total() const { return total_; }
    };

    FramePool::FramePool() : impl_(std::make_unique<Impl>()) {}
    FramePool::~FramePool() { deinit(); }
    Error FramePool::init(const MemoryCfg& cfg) { return impl_->init(cfg); }
    void  FramePool::deinit() { impl_->deinit(); }
    FramePtr FramePool::alloc(size_t size) { return impl_->alloc(size); }
    size_t   FramePool::used() const { return impl_->used(); }
    size_t   FramePool::total() const { return impl_->total(); }

} // namespace app::media::camera
