/*
 * memory.cc - 内存管理
 */

#include "memory.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace app::tool::memory
{

    MemoryPool::MemoryPool(size_t initial_size, size_t alignment, double expansion_factor)
        : alignment_(alignment), expansion_factor_(expansion_factor)
    {
        if (alignment_ == 0 || (alignment_ & (alignment_ - 1)) != 0)
            alignment_ = alignof(std::max_align_t);

        if (expansion_factor_ < 1.0)
            expansion_factor_ = 1.0;

        init_pool(initial_size);
    }

    MemoryPool::~MemoryPool()
    {
        reset();
    }

    MemoryPool::MemoryPool(MemoryPool&& other) noexcept
        : alignment_(other.alignment_), expansion_factor_(other.expansion_factor_),
          pool_blocks_(std::move(other.pool_blocks_)), pointer_map_(std::move(other.pointer_map_)),
          free_blocks_by_size_(std::move(other.free_blocks_by_size_)),
          free_blocks_iterators_(std::move(other.free_blocks_iterators_)),
          cached_free_memory_(other.cached_free_memory_.load()),
          cached_used_memory_(other.cached_used_memory_.load()),
          cached_peak_usage_(other.cached_peak_usage_.load()),
          allocation_count_(other.allocation_count_.load()),
          dealloc_count_(other.dealloc_count_.load())
    {
        other.cached_free_memory_.store(0);
        other.cached_used_memory_.store(0);
    }

    MemoryPool& MemoryPool::operator=(MemoryPool&& other) noexcept
    {
        if (this != &other)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::lock_guard<std::mutex> other_lock(other.mutex_);

            pool_blocks_           = std::move(other.pool_blocks_);
            pointer_map_           = std::move(other.pointer_map_);
            free_blocks_by_size_   = std::move(other.free_blocks_by_size_);
            free_blocks_iterators_ = std::move(other.free_blocks_iterators_);
            alignment_             = other.alignment_;
            expansion_factor_      = other.expansion_factor_;

            cached_free_memory_.store(other.cached_free_memory_.load());
            cached_used_memory_.store(other.cached_used_memory_.load());
            cached_peak_usage_.store(other.cached_peak_usage_.load());
            allocation_count_.store(other.allocation_count_.load());
            dealloc_count_.store(other.dealloc_count_.load());

            other.cached_free_memory_.store(0);
            other.cached_used_memory_.store(0);
        }
        return *this;
    }

    void MemoryPool::init_pool(size_t size)
    {
        size_t aligned_sz  = aligned_size(size);
        size_t header_size = get_header_size();
        size_t total_size  = aligned_sz + alignment_;

        void* raw_memory = nullptr;

#if defined(_WIN32)
        raw_memory = _aligned_malloc(total_size, alignment_);
#else
        if (posix_memalign(&raw_memory, alignment_, total_size) != 0)
            raw_memory = nullptr;
#endif

        if (!raw_memory)
        {
            raw_memory = std::malloc(total_size);
            if (!raw_memory)
                return;
        }

        auto* memory = static_cast<uint8_t*>(raw_memory);

        uintptr_t addr           = reinterpret_cast<uintptr_t>(memory);
        size_t    offset         = (alignment_ - (addr % alignment_)) % alignment_;
        uint8_t*  aligned_memory = memory + offset;

        PoolBlock pool_block;
        pool_block.memory = std::unique_ptr<uint8_t[], AlignedDeleter>(memory);
        pool_block.size   = aligned_sz;

        auto* first_block       = reinterpret_cast<BlockHeader*>(aligned_memory);
        first_block->size       = aligned_sz - header_size;
        first_block->is_free    = true;
        first_block->next       = nullptr;
        first_block->prev       = nullptr;
        first_block->pool_index = pool_blocks_.size();

        pool_block.first_block = first_block;
        pool_blocks_.push_back(std::move(pool_block));

        add_to_free_list(first_block);
        cached_free_memory_.fetch_add(first_block->size, std::memory_order_relaxed);
    }

    void* MemoryPool::allocate(size_t size)
    {
        if (size == 0)
            return nullptr;

        std::lock_guard<std::mutex> lock(mutex_);

        size_t       aligned_sz = aligned_size(size);
        size_t       pool_index = 0;
        BlockHeader* block      = find_free_block(aligned_sz, pool_index);

        if (!block)
        {
            expand_pool(aligned_sz);
            block = find_free_block(aligned_sz, pool_index);
            if (!block)
                return nullptr;
        }

        remove_from_free_list(block);
        split_block(block, aligned_sz);
        block->is_free = false;

        void* user_ptr         = get_user_data_ptr(block);
        pointer_map_[user_ptr] = pool_index;

        cached_free_memory_.fetch_sub(block->size, std::memory_order_relaxed);
        cached_used_memory_.fetch_add(block->size, std::memory_order_relaxed);
        allocation_count_.fetch_add(1, std::memory_order_relaxed);

        size_t current_used = cached_used_memory_.load(std::memory_order_relaxed);
        size_t peak         = cached_peak_usage_.load(std::memory_order_relaxed);
        while (current_used > peak)
        {
            if (cached_peak_usage_.compare_exchange_weak(peak, current_used))
                break;
        }

        return user_ptr;
    }

    // ==================== 释放 ====================

    void MemoryPool::deallocate(void* ptr) noexcept
    {
        if (!ptr)
            return;

        std::lock_guard<std::mutex> lock(mutex_);

        auto it = pointer_map_.find(ptr);
        if (it == pointer_map_.end())
            return;

        size_t pool_index = it->second;
        if (pool_index >= pool_blocks_.size())
            return;

        BlockHeader* block = get_block_header(ptr);
        if (!block || block->is_free)
            return;

        cached_used_memory_.fetch_sub(block->size, std::memory_order_relaxed);
        cached_free_memory_.fetch_add(block->size, std::memory_order_relaxed);
        dealloc_count_.fetch_add(1, std::memory_order_relaxed);

        block->is_free = true;
        pointer_map_.erase(it);

        add_to_free_list(block);
        coalesce_blocks(block);
    }

    void MemoryPool::reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        pool_blocks_.clear();
        pointer_map_.clear();
        free_blocks_by_size_.clear();
        free_blocks_iterators_.clear();

        cached_free_memory_.store(0, std::memory_order_relaxed);
        cached_used_memory_.store(0, std::memory_order_relaxed);
    }

    MemoryPool::Stats MemoryPool::get_stats() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Stats stats;
        stats.peak_usage       = cached_peak_usage_.load(std::memory_order_relaxed);
        stats.allocation_count = allocation_count_.load(std::memory_order_relaxed);
        stats.dealloc_count    = dealloc_count_.load(std::memory_order_relaxed);

        for (const auto& pool_block : pool_blocks_)
        {
            stats.total_memory += pool_block.size;

            BlockHeader* current = pool_block.first_block;
            while (current)
            {
                if (current->is_free)
                {
                    stats.free_memory += current->size;
                    stats.free_blocks++;
                }
                else
                {
                    stats.used_memory += current->size;
                    stats.allocated_blocks++;
                }
                current = current->next;
            }
        }

        return stats;
    }

    void MemoryPool::expand_pool(size_t required_size)
    {
        size_t new_size = pool_blocks_.empty()
                              ? required_size
                              : static_cast<size_t>(pool_blocks_.back().size * expansion_factor_);

        new_size = std::max(new_size, required_size + get_header_size());
        init_pool(new_size);
    }

    MemoryPool::BlockHeader* MemoryPool::find_free_block(size_t size, size_t& out_pool_index)
    {
        auto it = free_blocks_by_size_.lower_bound(size);

        if (it != free_blocks_by_size_.end())
        {
            void*        user_ptr = it->second;
            BlockHeader* block    = get_block_header(user_ptr);
            if (block)
            {
                out_pool_index = block->pool_index;
                return block;
            }
        }

        return nullptr;
    }

    void MemoryPool::split_block(BlockHeader* block, size_t size)
    {
        size_t header_size    = get_header_size();
        size_t min_block_size = header_size + alignment_;

        if (block->size < size + min_block_size)
            return;

        auto* new_block_addr = reinterpret_cast<uint8_t*>(block) + header_size + size;
        auto* new_block      = reinterpret_cast<BlockHeader*>(new_block_addr);

        new_block->size       = block->size - size - header_size;
        new_block->is_free    = true;
        new_block->next       = block->next;
        new_block->prev       = block;
        new_block->pool_index = block->pool_index;

        block->size = size;
        block->next = new_block;

        if (new_block->next)
            new_block->next->prev = new_block;

        add_to_free_list(new_block);
        cached_free_memory_.fetch_add(new_block->size, std::memory_order_relaxed);
    }

    void MemoryPool::coalesce_blocks(BlockHeader* block)
    {
        remove_from_free_list(block);

        size_t merged_size = 0;

        if (block->next && block->next->is_free)
        {
            BlockHeader* next_block = block->next;
            remove_from_free_list(next_block);

            merged_size += get_header_size();
            block->size += next_block->size + get_header_size();
            block->next = next_block->next;

            if (next_block->next)
                next_block->next->prev = block;
        }

        if (block->prev && block->prev->is_free)
        {
            BlockHeader* prev_block = block->prev;
            remove_from_free_list(prev_block);

            merged_size += get_header_size();
            prev_block->size += block->size + get_header_size();
            prev_block->next = block->next;

            if (block->next)
                block->next->prev = prev_block;

            block = prev_block;
        }

        add_to_free_list(block);

        if (merged_size > 0)
            cached_free_memory_.fetch_add(merged_size, std::memory_order_relaxed);
    }

    void MemoryPool::add_to_free_list(BlockHeader* block)
    {
        if (!block)
            return;

        void* user_ptr                   = get_user_data_ptr(block);
        auto  it                         = free_blocks_by_size_.insert({block->size, user_ptr});
        free_blocks_iterators_[user_ptr] = it;
    }

    void MemoryPool::remove_from_free_list(BlockHeader* block)
    {
        if (!block)
            return;

        void* user_ptr = get_user_data_ptr(block);
        auto  iter_it  = free_blocks_iterators_.find(user_ptr);
        if (iter_it != free_blocks_iterators_.end())
        {
            free_blocks_by_size_.erase(iter_it->second);
            free_blocks_iterators_.erase(iter_it);
        }
    }

    void MemoryPool::update_cached_stats()
    {
        size_t free_mem = 0;
        size_t used_mem = 0;

        for (const auto& pool_block : pool_blocks_)
        {
            BlockHeader* current = pool_block.first_block;
            while (current)
            {
                if (current->is_free)
                    free_mem += current->size;
                else
                    used_mem += current->size;
                current = current->next;
            }
        }

        cached_free_memory_.store(free_mem, std::memory_order_relaxed);
        cached_used_memory_.store(used_mem, std::memory_order_relaxed);
    }

    size_t MemoryPool::aligned_size(size_t size) const noexcept
    {
        return (size + alignment_ - 1) & ~(alignment_ - 1);
    }

    size_t MemoryPool::get_header_size() const noexcept
    {
        return aligned_size(sizeof(BlockHeader));
    }

    void* MemoryPool::get_user_data_ptr(BlockHeader* block) const noexcept
    {
        return reinterpret_cast<uint8_t*>(block) + get_header_size();
    }

    MemoryPool::BlockHeader* MemoryPool::get_block_header(void* user_ptr) const noexcept
    {
        return reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(user_ptr) -
                                              get_header_size());
    }

} // namespace app::tool::memory
