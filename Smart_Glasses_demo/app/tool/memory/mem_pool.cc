#include "mem_pool.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace app {
namespace tool {
namespace memory {

MemoryPool::MemoryPool(size_t initialSize, size_t alignment, double expansionFactor)
    : alignment_(alignment), expansionFactor_(expansionFactor) {
    // 确保对齐值是2的幂
    if (alignment_ == 0 || (alignment_ & (alignment_ - 1)) != 0) {
        alignment_ = alignof(std::max_align_t);
    }
    
    // 确保扩展因子至少为1.0
    if (expansionFactor_ < 1.0) {
        expansionFactor_ = 1.0;
    }
    
    initializePool(initialSize);
}

MemoryPool::~MemoryPool() {
    reset();
}

void MemoryPool::initializePool(size_t size) {
    // 计算对齐后的大小
    size_t alignedSizeValue = alignedSize(size);
    
    // 创建新的内存池块
    PoolBlock block;
    block.memory = std::make_unique<uint8_t[]>(alignedSizeValue);
    block.size = alignedSizeValue;
    
    // 初始化第一个块
    BlockHeader* firstBlock = reinterpret_cast<BlockHeader*>(block.memory.get());
    firstBlock->size = alignedSizeValue - getHeaderSize();
    firstBlock->isFree = true;
    firstBlock->next = nullptr;
    firstBlock->prev = nullptr;
    
    block.firstBlock = firstBlock;
    
    // 添加到池块列表
    poolBlocks_.push_back(std::move(block));
}

void* MemoryPool::allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 计算对齐后的大小
    size_t alignedRequestSize = alignedSize(size);
    
    // 查找合适的空闲块，并记录池块索引
    size_t poolIndex = 0;
    BlockHeader* block = nullptr;
    
    for (size_t i = 0; i < poolBlocks_.size(); i++) {
        BlockHeader* current = poolBlocks_[i].firstBlock;
        while (current) {
            if (current->isFree && current->size >= alignedRequestSize) {
                block = current;
                poolIndex = i;
                goto found;
            }
            current = current->next;
        }
    }
    
found:
    if (!block) {
        // 没有找到合适的块，需要扩展内存池
        expandPool(alignedRequestSize);
        poolIndex = poolBlocks_.size() - 1;  // 新扩展的块在最后
        
        // 在新扩展的块中查找
        block = poolBlocks_[poolIndex].firstBlock;
        if (!block || !block->isFree || block->size < alignedRequestSize) {
            // 扩展后仍然没有找到，返回nullptr
            return nullptr;
        }
    }
    
    // 分割块（如果需要）
    splitBlock(block, alignedRequestSize);
    
    // 标记为已使用
    block->isFree = false;
    
    // 记录指针映射（记录正确的池块索引）
    void* userData = reinterpret_cast<uint8_t*>(block) + getHeaderSize();
    ptrMap_[userData] = poolIndex;
    
    // 从空闲块映射中移除
    freeBlocks_.erase(userData);
    
    return userData;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 查找指针对应的块
    auto it = ptrMap_.find(ptr);
    if (it == ptrMap_.end()) {
        // 指针不在内存池中
        return;
    }
    
    // 获取块信息
    size_t poolIndex = it->second;
    if (poolIndex >= poolBlocks_.size()) {
        // 索引无效
        return;
    }
    
    // 计算块头部地址
    BlockHeader* block = reinterpret_cast<BlockHeader*>(
        reinterpret_cast<uint8_t*>(ptr) - getHeaderSize());
    
    // 标记为自由
    block->isFree = true;
    
    // 添加到空闲块映射
    freeBlocks_[ptr] = block->size;
    
    // 合并相邻的空闲块
    coalesceBlocks(block);
    
    // 从指针映射中移除
    ptrMap_.erase(it);
}

void MemoryPool::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    poolBlocks_.clear();
    ptrMap_.clear();
    freeBlocks_.clear();
}

MemoryPool::Stats MemoryPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Stats stats = {0, 0, 0, 0, 0};
    
    for (const auto& poolBlock : poolBlocks_) {
        stats.totalMemory += poolBlock.size;
        
        BlockHeader* current = poolBlock.firstBlock;
        while (current) {
            if (current->isFree) {
                stats.freeMemory += current->size;
                stats.freeBlocks++;
            } else {
                stats.usedMemory += current->size;
                stats.allocatedBlocks++;
            }
            current = current->next;
        }
    }
    
    return stats;
}

void MemoryPool::expandPool(size_t requiredSize) {
    // 计算需要的新块大小
    size_t newSize = static_cast<size_t>(poolBlocks_.empty() ? requiredSize : 
                                         poolBlocks_.back().size * expansionFactor_);
    
    // 确保新大小足够容纳请求的大小
    newSize = std::max(newSize, requiredSize + getHeaderSize());
    
    // 初始化新块
    initializePool(newSize);
}

MemoryPool::BlockHeader* MemoryPool::findFreeBlock(size_t size) {
    // 使用哈希表优化查找
    for (const auto& freeBlock : freeBlocks_) {
        if (freeBlock.second >= size) {
            // 找到一个足够大的空闲块
            void* ptr = freeBlock.first;
            BlockHeader* block = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<uint8_t*>(ptr) - getHeaderSize());
            return block;
        }
    }
    
    // 如果哈希表中没有找到，遍历所有块
    for (auto& poolBlock : poolBlocks_) {
        BlockHeader* current = poolBlock.firstBlock;
        while (current) {
            if (current->isFree && current->size >= size) {
                return current;
            }
            current = current->next;
        }
    }
    
    return nullptr;
}

void MemoryPool::splitBlock(BlockHeader* block, size_t size) {
    // 只有当块大小比请求大小大足够的时候才分割
    size_t minBlockSize = getHeaderSize() + alignment_; // 最小块大小（头部+对齐）
    if (block->size >= size + minBlockSize) {
        // 计算新块的位置
        uint8_t* newBlockAddr = reinterpret_cast<uint8_t*>(block) + getHeaderSize() + size;
        BlockHeader* newBlock = reinterpret_cast<BlockHeader*>(newBlockAddr);
        
        // 设置新块信息
        newBlock->size = block->size - size - getHeaderSize();
        newBlock->isFree = true;
        newBlock->next = block->next;
        newBlock->prev = block;
        
        // 更新原块信息
        block->size = size;
        block->next = newBlock;
        
        // 更新下一个块的prev指针
        if (newBlock->next) {
            newBlock->next->prev = newBlock;
        }
        
        // 添加新块到空闲映射
        void* newBlockPtr = reinterpret_cast<uint8_t*>(newBlock) + getHeaderSize();
        freeBlocks_[newBlockPtr] = newBlock->size;
    }
}

void MemoryPool::coalesceBlocks(BlockHeader* block) {
    // 向后合并（与下一个块合并）
    if (block->next && block->next->isFree) {
        BlockHeader* nextBlock = block->next;
        
        // 更新大小
        block->size += nextBlock->size + getHeaderSize();
        block->next = nextBlock->next;
        
        // 更新下一个块的prev指针
        if (nextBlock->next) {
            nextBlock->next->prev = block;
        }
        
        // 从空闲映射中移除合并的块
        void* nextBlockPtr = reinterpret_cast<uint8_t*>(nextBlock) + getHeaderSize();
        freeBlocks_.erase(nextBlockPtr);
    }
    
    // 向前合并（与前一个块合并）
    if (block->prev && block->prev->isFree) {
        BlockHeader* prevBlock = block->prev;
        
        // 更新大小
        prevBlock->size += block->size + getHeaderSize();
        prevBlock->next = block->next;
        
        // 更新下一个块的prev指针
        if (block->next) {
            block->next->prev = prevBlock;
        }
        
        // 从空闲映射中移除当前块
        void* blockPtr = reinterpret_cast<uint8_t*>(block) + getHeaderSize();
        freeBlocks_.erase(blockPtr);
    }
}

size_t MemoryPool::alignedSize(size_t size) const {
    // 计算对齐后的大小
    return (size + alignment_ - 1) & ~(alignment_ - 1);
}

size_t MemoryPool::getHeaderSize() const {
    // 计算块头部大小（考虑对齐）
    return alignedSize(sizeof(BlockHeader));
}

} // namespace memory
} // namespace tool
} // namespace app