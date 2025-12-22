#ifndef MEMORY_HPP
#define MEMORY_HPP

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <memory>

namespace app
{
    namespace tool
    {
        namespace memory
        {

            constexpr size_t BYTES_PER_KILOBYTE        = 1024;
            constexpr size_t BYTES_PER_MEGABYTE        = BYTES_PER_KILOBYTE * 1024;
            constexpr size_t DEFAULT_INITIAL_POOL_SIZE = BYTES_PER_MEGABYTE; // 1 MB
            constexpr double DEFAULT_EXPANSION_FACTOR  = 2.0;

            /**
             * 内存池类
             * 提供高效的内存分配和回收机制
             */
            class MemoryPool
            {
            public:
                /**
                 * 构造函数
                 * @param initial_size 初始内存池大小（字节）
                 * @param alignment 内存对齐要求（字节）
                 * @param expansion_factor 扩展因子（当内存不足时，扩展倍数）
                 */
                explicit MemoryPool(size_t initial_size     = DEFAULT_INITIAL_POOL_SIZE,
                                    size_t alignment        = alignof(std::max_align_t),
                                    double expansion_factor = DEFAULT_EXPANSION_FACTOR);

                /**
                 * 析构函数
                 */
                ~MemoryPool();

                /**
                 * 分配内存
                 * @param size 请求的内存大小（字节）
                 * @return 分配的内存指针，失败返回nullptr
                 */
                void* allocate(size_t size);

                /**
                 * 释放内存
                 * @param ptr 要释放的内存指针
                 */
                void deallocate(void* ptr);

                /**
                 * 重置内存池（释放所有内存块）
                 */
                void reset();

                /**
                 * 获取内存池统计信息
                 */
                struct Stats
                {
                    size_t total_memory;     // 总内存大小
                    size_t used_memory;      // 已使用内存大小
                    size_t free_memory;      // 空闲内存大小
                    size_t allocated_blocks; // 已分配块数量
                    size_t free_blocks;      // 空闲块数量
                };

                /**
                 * 获取内存池统计信息
                 * @return 统计信息结构体
                 */
                Stats getStats() const;

            private:
                /**
                 * 内存块头部信息
                 */
                struct BlockHeader
                {
                    size_t       size;    // 块大小
                    bool         is_free; // 是否空闲
                    BlockHeader* next;    // 下一个块
                    BlockHeader* prev;    // 上一个块
                };

                /**
                 * 内存池块结构
                 */
                struct PoolBlock
                {
                    std::unique_ptr<uint8_t[]> memory;      // 内存块
                    size_t                     size;        // 块大小
                    BlockHeader*               first_block; // 第一个块
                };

                // 私有成员变量
                mutable std::mutex                mutex_;            // 线程安全互斥锁
                size_t                            alignment_;        // 内存对齐要求
                double                            expansion_factor_; // 扩展因子
                std::vector<PoolBlock>            pool_blocks_;      // 内存池块列表
                std::unordered_map<void*, size_t> pointer_map_; // 指针到块索引的哈希映射
                mutable std::unordered_map<void*, size_t>
                    free_blocks_; // 空闲块映射（用于快速查找）

                /**
                 * 初始化内存池
                 * @param size 初始大小
                 */
                void initializePool(size_t size);

                /**
                 * 扩展内存池
                 * @param required_size 需要的最小大小
                 */
                void expandPool(size_t required_size);

                /**
                 * 查找合适的空闲块
                 * @param size 请求大小
                 * @return 合适的块指针，未找到返回nullptr
                 */
                BlockHeader* findFreeBlock(size_t size);

                /**
                 * 分割内存块
                 * @param block 要分割的块
                 * @param size 请求大小
                 */
                void splitBlock(BlockHeader* block, size_t size);

                /**
                 * 合并相邻的空闲块
                 * @param block 要合并的块
                 */
                void coalesceBlocks(BlockHeader* block);

                /**
                 * 计算对齐后的大小
                 * @param size 原始大小
                 * @return 对齐后的大小
                 */
                size_t alignedSize(size_t size) const;

                /**
                 * 获取块头部大小（考虑对齐）
                 * @return 块头部大小
                 */
                size_t getHeaderSize() const;
            };

        } // namespace memory
    }     // namespace tool
} // namespace app

#endif // MEMORY_HPP