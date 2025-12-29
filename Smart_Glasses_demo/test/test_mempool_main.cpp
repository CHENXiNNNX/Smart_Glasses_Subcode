#include "mem_pool.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>

using namespace glasses::tool::memory;

void testBasicAllocation()
{
    std::cout << "=== 基本内存分配测试 ===" << std::endl;

    MemoryPool pool(1024, 8, 2.0); // 1KB初始大小，8字节对齐，2倍扩展

    // 分配一些内存
    void* ptr1 = pool.allocate(100);
    void* ptr2 = pool.allocate(200);
    void* ptr3 = pool.allocate(300);

    std::cout << "分配了3块内存: " << ptr1 << ", " << ptr2 << ", " << ptr3 << std::endl;

    // 释放内存
    pool.deallocate(ptr2);
    std::cout << "释放了中间块内存" << std::endl;

    // 再次分配
    void* ptr4 = pool.allocate(150);
    std::cout << "重新分配内存: " << ptr4 << std::endl;

    // 获取统计信息
    auto stats = pool.getStats();
    std::cout << "统计信息:" << std::endl;
    std::cout << "  总内存: " << stats.totalMemory << " 字节" << std::endl;
    std::cout << "  已使用: " << stats.usedMemory << " 字节" << std::endl;
    std::cout << "  空闲: " << stats.freeMemory << " 字节" << std::endl;
    std::cout << "  已分配块: " << stats.allocatedBlocks << std::endl;
    std::cout << "  空闲块: " << stats.freeBlocks << std::endl;

    // 释放剩余内存
    pool.deallocate(ptr1);
    pool.deallocate(ptr3);
    pool.deallocate(ptr4);

    std::cout << "所有内存已释放" << std::endl << std::endl;
}

void testMemoryExpansion()
{
    std::cout << "=== 内存扩展测试 ===" << std::endl;

    MemoryPool pool(512, 8, 2.0); // 512字节初始大小

    std::cout << "初始统计信息:" << std::endl;
    auto initialStats = pool.getStats();
    std::cout << "  总内存: " << initialStats.totalMemory << " 字节" << std::endl;

    // 分配超过初始大小的内存
    void* largePtr = pool.allocate(1000);
    std::cout << "分配了大块内存: " << largePtr << std::endl;

    std::cout << "扩展后统计信息:" << std::endl;
    auto expandedStats = pool.getStats();
    std::cout << "  总内存: " << expandedStats.totalMemory << " 字节" << std::endl;

    pool.deallocate(largePtr);
    std::cout << "大块内存已释放" << std::endl << std::endl;
}

void testAlignment()
{
    std::cout << "=== 内存对齐测试 ===" << std::endl;

    MemoryPool pool(1024, 16, 2.0); // 16字节对齐

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

    // 检查对齐
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

    // 创建多个线程同时分配和释放内存
    std::vector<std::thread> threads;
    const int                numThreads           = 4;
    const int                allocationsPerThread = 100;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(
            [&pool, i, allocationsPerThread]()
            {
                std::vector<void*> pointers;
                pointers.reserve(allocationsPerThread);

                // 分配内存
                for (int j = 0; j < allocationsPerThread; ++j)
                {
                    size_t size = 10 + (i * allocationsPerThread + j) % 100; // 10-110字节
                    void*  ptr  = pool.allocate(size);
                    if (ptr)
                    {
                        pointers.push_back(ptr);

                        // 写入一些数据以验证内存可用性
                        memset(ptr, i + j, std::min(size, static_cast<size_t>(10)));
                    }
                }

                // 释放内存
                for (void* ptr : pointers)
                {
                    pool.deallocate(ptr);
                }
            });
    }

    // 等待所有线程完成
    for (auto& thread : threads)
    {
        thread.join();
    }

    auto endTime  = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "多线程测试完成，耗时: " << duration.count() << " 毫秒" << std::endl;

    auto stats = pool.getStats();
    std::cout << "最终统计信息:" << std::endl;
    std::cout << "  总内存: " << stats.totalMemory << " 字节" << std::endl;
    std::cout << "  已使用: " << stats.usedMemory << " 字节" << std::endl;
    std::cout << "  空闲: " << stats.freeMemory << " 字节" << std::endl;
    std::cout << "  已分配块: " << stats.allocatedBlocks << std::endl;
    std::cout << "  空闲块: " << stats.freeBlocks << std::endl;

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