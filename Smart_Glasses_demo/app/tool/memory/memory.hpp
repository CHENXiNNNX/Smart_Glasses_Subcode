/*
 * memory.hpp - 内存管理
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace app::tool::memory
{

    constexpr size_t BYTES_PER_KILOBYTE        = 1024;
    constexpr size_t BYTES_PER_MEGABYTE        = BYTES_PER_KILOBYTE * 1024;
    constexpr size_t DEFAULT_INITIAL_POOL_SIZE = BYTES_PER_MEGABYTE;
    constexpr double DEFAULT_EXPANSION_FACTOR  = 2.0;

    /* 对齐内存删除器 */
    struct AlignedDeleter
    {
        void operator()(uint8_t* ptr) const noexcept
        {
            if (ptr)
                std::free(ptr);
        }
    };

    /* 内存池 */
    class MemoryPool
    {
    public:
        /* 统计 */
        struct Stats
        {
            size_t total_memory{0};
            size_t used_memory{0};
            size_t free_memory{0};
            size_t allocated_blocks{0};
            size_t free_blocks{0};
            size_t peak_usage{0};
            size_t allocation_count{0};
            size_t dealloc_count{0};
        };

        explicit MemoryPool(size_t initial_size     = DEFAULT_INITIAL_POOL_SIZE,
                            size_t alignment        = alignof(std::max_align_t),
                            double expansion_factor = DEFAULT_EXPANSION_FACTOR);
        ~MemoryPool();

        MemoryPool(const MemoryPool&)            = delete;
        MemoryPool& operator=(const MemoryPool&) = delete;
        MemoryPool(MemoryPool&&) noexcept;
        MemoryPool& operator=(MemoryPool&&) noexcept;

        [[nodiscard]] void* allocate(size_t size);
        void                deallocate(void* ptr) noexcept;
        void                reset();

        [[nodiscard]] Stats  get_stats() const;
        [[nodiscard]] size_t get_free_memory_fast() const noexcept
        {
            return cached_free_memory_.load(std::memory_order_acquire);
        }
        [[nodiscard]] size_t get_used_memory_fast() const noexcept
        {
            return cached_used_memory_.load(std::memory_order_acquire);
        }
        [[nodiscard]] bool valid() const noexcept
        {
            return !pool_blocks_.empty();
        }
        [[nodiscard]] size_t get_alignment() const noexcept
        {
            return alignment_;
        }

    private:
        struct BlockHeader
        {
            size_t       size;
            bool         is_free;
            BlockHeader* next;
            BlockHeader* prev;
            size_t       pool_index;
        };

        struct PoolBlock
        {
            std::unique_ptr<uint8_t[], AlignedDeleter> memory;
            size_t                                     size;
            BlockHeader*                               first_block;
        };

        mutable std::mutex     mutex_;
        size_t                 alignment_;
        double                 expansion_factor_;
        std::vector<PoolBlock> pool_blocks_;

        std::unordered_map<void*, size_t>                                 pointer_map_;
        std::multimap<size_t, void*>                                      free_blocks_by_size_;
        std::unordered_map<void*, std::multimap<size_t, void*>::iterator> free_blocks_iterators_;

        std::atomic<size_t> cached_free_memory_{0};
        std::atomic<size_t> cached_used_memory_{0};
        std::atomic<size_t> cached_peak_usage_{0};
        std::atomic<size_t> allocation_count_{0};
        std::atomic<size_t> dealloc_count_{0};

        void         init_pool(size_t size);
        void         expand_pool(size_t required_size);
        BlockHeader* find_free_block(size_t size, size_t& out_pool_index);
        void         split_block(BlockHeader* block, size_t size);
        void         coalesce_blocks(BlockHeader* block);
        void         add_to_free_list(BlockHeader* block);
        void         remove_from_free_list(BlockHeader* block);
        void         update_cached_stats();

        [[nodiscard]] size_t       aligned_size(size_t size) const noexcept;
        [[nodiscard]] size_t       get_header_size() const noexcept;
        [[nodiscard]] void*        get_user_data_ptr(BlockHeader* block) const noexcept;
        [[nodiscard]] BlockHeader* get_block_header(void* user_ptr) const noexcept;
    };

} // namespace app::tool::memory
