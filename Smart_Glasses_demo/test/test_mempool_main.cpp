/* test_mempool_main.cpp - 内存池测试 */

#include "app/tool/memory/memory.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

using namespace app::tool::memory;

void testBasicAllocation()
{
    std::cout << "基本分配测试" << std::endl;

    MemoryPool pool(1024, 8, 2.0);

    void* ptr1 = pool.allocate(100);
    void* ptr2 = pool.allocate(200);
    void* ptr3 = pool.allocate(300);

    pool.deallocate(ptr2);
    void* ptr4 = pool.allocate(150);

    auto stats = pool.get_stats();
    std::cout << "统计 总" << stats.total_memory << " 用" << stats.used_memory << " 块"
              << stats.allocated_blocks << std::endl;
    pool.deallocate(ptr1);
    pool.deallocate(ptr3);
    pool.deallocate(ptr4);
}

void testMemoryExpansion()
{
    std::cout << "内存扩展测试" << std::endl;

    MemoryPool pool(512, 8, 2.0);

    auto  initial_stats  = pool.get_stats();
    void* large_ptr      = pool.allocate(1000);
    auto  expanded_stats = pool.get_stats();
    std::cout << "扩展前" << initial_stats.total_memory << " 扩展后" << expanded_stats.total_memory
              << std::endl;
    pool.deallocate(large_ptr);
}

void testAlignment()
{
    std::cout << "对齐测试" << std::endl;

    MemoryPool pool(1024, 16, 2.0);

    void* ptr1 = pool.allocate(10);
    void* ptr2 = pool.allocate(20);
    void* ptr3 = pool.allocate(30);

    std::cout << "分配的指针地址:" << std::endl;
    std::cout << "  ptr1: " << ptr1 << " (0x" << std::hex << reinterpret_cast<uintptr_t>(ptr1)
              << std::dec << ")" << std::endl;
    std::cout << "  ptr2: " << ptr2 << " (0x" << std::hex << reinterpret_cast<uintptr_t>(ptr2)
              << std::dec << ")" << std::endl;
    std::cout << "  ptr3: " << ptr3 << " (0x" << std::hex << reinterpret_cast<uintptr_t>(ptr3)
              << std::dec << ")" << std::endl;

    bool aligned1 = (reinterpret_cast<uintptr_t>(ptr1) % 16) == 0;
    bool aligned2 = (reinterpret_cast<uintptr_t>(ptr2) % 16) == 0;
    bool aligned3 = (reinterpret_cast<uintptr_t>(ptr3) % 16) == 0;

    std::cout << "对齐检查:" << std::endl;
    std::cout << "  ptr1 对齐: " << (aligned1 ? "是" : "否") << std::endl;
    std::cout << "  ptr2 对齐: " << (aligned2 ? "是" : "否") << std::endl;
    std::cout << "  ptr3 对齐: " << (aligned3 ? "是" : "否") << std::endl;

    pool.deallocate(ptr1);
    pool.deallocate(ptr2);
    pool.deallocate(ptr3);

    std::cout << "内存已释放" << std::endl << std::endl;
}

void testThreadSafety()
{
    std::cout << "=== 线程安全测试 ===" << std::endl;

    MemoryPool pool(4096, 8, 2.0);

    std::vector<std::thread> threads;
    const int                num_threads            = 4;
    const int                allocations_per_thread = 100;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(
            [&pool, i, allocations_per_thread]()
            {
                std::vector<void*> pointers;
                pointers.reserve(allocations_per_thread);

                for (int j = 0; j < allocations_per_thread; ++j)
                {
                    size_t size = 10 + (i * allocations_per_thread + j) % 100;
                    void*  ptr  = pool.allocate(size);
                    if (ptr)
                    {
                        pointers.push_back(ptr);
                        memset(ptr, i + j, std::min(size, static_cast<size_t>(10)));
                    }
                }

                for (void* ptr : pointers)
                    pool.deallocate(ptr);
            });
    }

    for (auto& t : threads)
        t.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "多线程测试完成，耗时: " << duration.count() << " 毫秒" << std::endl;

    auto stats = pool.get_stats();
    std::cout << "最终统计信息:" << std::endl;
    std::cout << "  总内存: " << stats.total_memory << " 字节" << std::endl;
    std::cout << "  已使用: " << stats.used_memory << " 字节" << std::endl;
    std::cout << "  空闲: " << stats.free_memory << " 字节" << std::endl;
    std::cout << "  已分配块: " << stats.allocated_blocks << std::endl;
    std::cout << "  空闲块: " << stats.free_blocks << std::endl;
    std::cout << "  峰值使用: " << stats.peak_usage << " 字节" << std::endl;
    std::cout << "  分配次数: " << stats.allocation_count << std::endl;

    std::cout << "线程安全测试结束" << std::endl << std::endl;
}

int main()
{
    std::cout << "内存池测试开始" << std::endl << std::endl;

    try
    {
        testBasicAllocation();
        testMemoryExpansion();
        testAlignment();
        testThreadSafety();
        std::cout << "所有测试完成!" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
