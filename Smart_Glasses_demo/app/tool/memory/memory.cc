#include "memory.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace app
{
    namespace tool
    {
        namespace memory
        {

            MemoryPool::MemoryPool(size_t initial_size, size_t alignment, double expansion_factor)
                : alignment_(alignment), expansion_factor_(expansion_factor)
            {
                // 确保对齐值是2的幂
                if (alignment_ == 0 || (alignment_ & (alignment_ - 1)) != 0)
                {
                    alignment_ = alignof(std::max_align_t);
                }

                // 确保扩展因子至少为1.0
                if (expansion_factor_ < 1.0)
                {
                    expansion_factor_ = 1.0;
                }

                initializePool(initial_size);
            }

            MemoryPool::~MemoryPool()
            {
                reset();
            }

            void MemoryPool::initializePool(size_t size)
            {
                // 计算对齐后的大小
                size_t aligned_size_value = alignedSize(size);

                // 创建新的内存池块
                PoolBlock pool_block;
                pool_block.memory = std::make_unique<uint8_t[]>(aligned_size_value);
                pool_block.size   = aligned_size_value;

                // 初始化第一个块
                auto* first_block    = reinterpret_cast<BlockHeader*>(pool_block.memory.get());
                first_block->size    = aligned_size_value - getHeaderSize();
                first_block->is_free = true;
                first_block->next    = nullptr;
                first_block->prev    = nullptr;

                pool_block.first_block = first_block;

                // 添加到池块列表
                pool_blocks_.push_back(std::move(pool_block));
            }

            void* MemoryPool::allocate(size_t size)
            {
                if (size == 0)
                {
                    return nullptr;
                }

                std::lock_guard<std::mutex> lock(mutex_);

                // 计算对齐后的大小
                size_t aligned_request_size = alignedSize(size);

                // 查找合适的空闲块，并记录池块索引
                size_t       pool_index = 0;
                BlockHeader* block      = nullptr;

                for (size_t i = 0; i < pool_blocks_.size(); i++)
                {
                    BlockHeader* current = pool_blocks_[i].first_block;
                    while (current)
                    {
                        if (current->is_free && current->size >= aligned_request_size)
                        {
                            block      = current;
                            pool_index = i;
                            goto found;
                        }
                        current = current->next;
                    }
                }

            found:
                if (!block)
                {
                    // 没有找到合适的块，需要扩展内存池
                    expandPool(aligned_request_size);
                    pool_index = pool_blocks_.size() - 1; // 新扩展的块在最后

                    // 在新扩展的块中查找
                    block = pool_blocks_[pool_index].first_block;
                    if (!block || !block->is_free || block->size < aligned_request_size)
                    {
                        // 扩展后仍然没有找到，返回nullptr
                        return nullptr;
                    }
                }

                // 分割块（如果需要）
                splitBlock(block, aligned_request_size);

                // 标记为已使用
                block->is_free = false;

                // 记录指针映射（记录正确的池块索引）
                auto* user_data         = reinterpret_cast<uint8_t*>(block) + getHeaderSize();
                pointer_map_[user_data] = pool_index;

                // 从空闲块映射中移除
                free_blocks_.erase(user_data);

                return user_data;
            }

            void MemoryPool::deallocate(void* ptr)
            {
                if (!ptr)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(mutex_);

                // 查找指针对应的块
                auto pointer_iter = pointer_map_.find(ptr);
                if (pointer_iter == pointer_map_.end())
                {
                    // 指针不在内存池中
                    return;
                }

                // 获取块信息
                size_t pool_index = pointer_iter->second;
                if (pool_index >= pool_blocks_.size())
                {
                    // 索引无效
                    return;
                }

                // 计算块头部地址
                auto* block = reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(ptr) -
                                                             getHeaderSize());

                // 标记为自由
                block->is_free = true;

                // 添加到空闲块映射
                free_blocks_[ptr] = block->size;

                // 合并相邻的空闲块
                coalesceBlocks(block);

                // 从指针映射中移除
                pointer_map_.erase(pointer_iter);
            }

            void MemoryPool::reset()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                pool_blocks_.clear();
                pointer_map_.clear();
                free_blocks_.clear();
            }

            MemoryPool::Stats MemoryPool::getStats() const
            {
                std::lock_guard<std::mutex> lock(mutex_);

                Stats stats{0, 0, 0, 0, 0};

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

            void MemoryPool::expandPool(size_t required_size)
            {
                // 计算需要的新块大小
                auto new_size = static_cast<size_t>(
                    pool_blocks_.empty() ? required_size
                                         : pool_blocks_.back().size * expansion_factor_);

                // 确保新大小足够容纳请求的大小
                new_size = std::max(new_size, required_size + getHeaderSize());

                // 初始化新块
                initializePool(new_size);
            }

            MemoryPool::BlockHeader* MemoryPool::findFreeBlock(size_t size)
            {
                // 使用哈希表优化查找
                for (const auto& free_block : free_blocks_)
                {
                    if (free_block.second >= size)
                    {
                        // 找到一个足够大的空闲块
                        void* ptr   = free_block.first;
                        auto* block = reinterpret_cast<BlockHeader*>(
                            reinterpret_cast<uint8_t*>(ptr) - getHeaderSize());
                        return block;
                    }
                }

                // 如果哈希表中没有找到，遍历所有块
                for (auto& pool_block : pool_blocks_)
                {
                    BlockHeader* current = pool_block.first_block;
                    while (current)
                    {
                        if (current->is_free && current->size >= size)
                        {
                            return current;
                        }
                        current = current->next;
                    }
                }

                return nullptr;
            }

            void MemoryPool::splitBlock(BlockHeader* block, size_t size)
            {
                // 只有当块大小比请求大小大足够的时候才分割
                size_t min_block_size = getHeaderSize() + alignment_; // 最小块大小（头部+对齐）
                if (block->size >= size + min_block_size)
                {
                    // 计算新块的位置
                    auto* new_block_address =
                        reinterpret_cast<uint8_t*>(block) + getHeaderSize() + size;
                    auto* new_block = reinterpret_cast<BlockHeader*>(new_block_address);

                    // 设置新块信息
                    new_block->size    = block->size - size - getHeaderSize();
                    new_block->is_free = true;
                    new_block->next    = block->next;
                    new_block->prev    = block;

                    // 更新原块信息
                    block->size = size;
                    block->next = new_block;

                    // 更新下一个块的prev指针
                    if (new_block->next)
                    {
                        new_block->next->prev = new_block;
                    }

                    // 添加新块到空闲映射
                    void* new_block_ptr = reinterpret_cast<uint8_t*>(new_block) + getHeaderSize();
                    free_blocks_[new_block_ptr] = new_block->size;
                }
            }

            void MemoryPool::coalesceBlocks(BlockHeader* block)
            {
                // 向后合并（与下一个块合并）
                if (block->next && block->next->is_free)
                {
                    BlockHeader* next_block = block->next;

                    // 更新大小
                    block->size += next_block->size + getHeaderSize();
                    block->next = next_block->next;

                    // 更新下一个块的prev指针
                    if (next_block->next)
                    {
                        next_block->next->prev = block;
                    }

                    // 从空闲映射中移除合并的块
                    void* next_block_ptr = reinterpret_cast<uint8_t*>(next_block) + getHeaderSize();
                    free_blocks_.erase(next_block_ptr);
                }

                // 向前合并（与前一个块合并）
                if (block->prev && block->prev->is_free)
                {
                    BlockHeader* previous_block = block->prev;

                    // 更新大小
                    previous_block->size += block->size + getHeaderSize();
                    previous_block->next = block->next;

                    // 更新下一个块的prev指针
                    if (block->next)
                    {
                        block->next->prev = previous_block;
                    }

                    // 从空闲映射中移除当前块
                    void* block_ptr = reinterpret_cast<uint8_t*>(block) + getHeaderSize();
                    free_blocks_.erase(block_ptr);
                }
            }

            size_t MemoryPool::alignedSize(size_t size) const
            {
                // 计算对齐后的大小
                return (size + alignment_ - 1) & ~(alignment_ - 1);
            }

            size_t MemoryPool::getHeaderSize() const
            {
                // 计算块头部大小（考虑对齐）
                return alignedSize(sizeof(BlockHeader));
            }

        } // namespace memory
    }     // namespace tool
} // namespace app