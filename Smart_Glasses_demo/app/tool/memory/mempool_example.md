#include "mem_pool.h"
#include <iostream>

using namespace glasses::tool::memory;

int main() {
    // 创建内存池
    MemoryPool pool(1024 * 1024, 8, 2.0);  // 1MB初始大小，8字节对齐，2倍扩展因子
    
    // 分配内存
    void* ptr1 = pool.allocate(100);
    void* ptr2 = pool.allocate(200);
    
    if (ptr1 && ptr2) {
        std::cout << "成功分配内存" << std::endl;
        
        // 使用内存...
        
        // 释放内存
        pool.deallocate(ptr1);
        pool.deallocate(ptr2);
    }
    
    // 获取统计信息
    auto stats = pool.getStats();
    std::cout << "总内存: " << stats.totalMemory << " 字节" << std::endl;
    std::cout << "已使用: " << stats.usedMemory << " 字节" << std::endl;
    
    return 0;
}
