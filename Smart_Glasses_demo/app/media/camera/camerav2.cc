/**
 * @file camerav2.cc
 * @brief VideoSystemV2实现
 */

#include "camerav2.h"
#include "../../tool/log/log.h"
#include "../media_config.h"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <sys/stat.h>

namespace app {
namespace media {
namespace camera {

using namespace tool::log;

// ============================================================================
// VideoMemoryPool 实现
// ============================================================================

// FixedPool 实现
VideoMemoryPool::FixedPool::FixedPool(size_t block_sz, size_t block_cnt)
    : block_size(block_sz), block_count(block_cnt) {
    
    if (block_count > MAX_BLOCKS) {
        block_count = MAX_BLOCKS;
        LOG_WARN("VideoBuffer", "Block count limited to %zu", MAX_BLOCKS);
    }
    
    // 初始化位图（所有位为0表示空闲）
    for (size_t i = 0; i < 8; i++) {
        allocation_bitmap_[i].store(0, std::memory_order_relaxed);
    }
    
    // 预分配连续内存（64字节对齐）
    buffer.resize(block_size * block_count);
    
    // 初始化帧对象
    for (size_t i = 0; i < block_count; i++) {
        frame_objects[i].frame_index = i;
    }
    
    LOG_INFO("VideoBuffer", "Fixed pool initialized: %zu blocks × %zu bytes = %zu MB",
             block_count, block_size, (block_count * block_size) / (1024 * 1024));
}

VideoMemoryPool::FixedPool::~FixedPool() {
    LOG_INFO("VideoBuffer", "Fixed pool destroyed");
}

int VideoMemoryPool::FixedPool::allocateBlock() {
    int bitmap_count = (block_count + 63) / 64;
    
    for (int bitmap_index = 0; bitmap_index < bitmap_count; bitmap_index++) {
        uint64_t bitmap = allocation_bitmap_[bitmap_index].load(std::memory_order_acquire);
        int base_index = bitmap_index * 64;
        int max_blocks = std::min(64, static_cast<int>(block_count) - base_index);
        
        if (max_blocks <= 0) break;
        
        while (bitmap != UINT64_MAX) {
            uint64_t inverted = ~bitmap;
            if (inverted == 0) break;
            
            int free_bit = __builtin_ctzll(inverted);
            if (free_bit >= max_blocks) break;
            
            uint64_t new_bitmap = bitmap | (1ULL << free_bit);
            if (allocation_bitmap_[bitmap_index].compare_exchange_weak(bitmap, new_bitmap,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
                return base_index + free_bit;
            }
        }
    }
    
    return -1;  // 分配失败
}

void VideoMemoryPool::FixedPool::deallocateBlock(int index) {
    if (index < 0 || index >= static_cast<int>(block_count)) {
        LOG_ERROR("VideoBuffer", "Invalid block index: %d", index);
        return;
    }
    
    int bitmap_index = index / 64;
    int bit_position = index % 64;
    uint64_t mask = ~(1ULL << bit_position);
    
    allocation_bitmap_[bitmap_index].fetch_and(mask, std::memory_order_release);
}

uint8_t* VideoMemoryPool::FixedPool::getBlockPtr(int index) {
    if (index < 0 || index >= static_cast<int>(block_count)) {
        return nullptr;
    }
    return buffer.data() + (index * block_size);
}

// ============================================================================
// RKMPI DMA缓冲区池
// ============================================================================

VideoMemoryPool::DMAPool::DMAPool(size_t dma_block_size) : block_size(dma_block_size) {
    LOG_INFO("VideoBuffer", "Initializing DMA pool...");
    
    // 预分配DMA缓冲区
    for (size_t i = 0; i < MAX_DMA_BLOCKS; i++) {
        // 使用RKMPI分配DMA缓冲区（正确的API调用方式）
        MB_BLK mb_blk = nullptr;
        RK_S32 ret = RK_MPI_SYS_Malloc(&mb_blk, static_cast<RK_U32>(block_size));
        if (ret == RK_SUCCESS && mb_blk) {
            blocks[i].mb_blk = mb_blk;
            blocks[i].vir_addr = RK_MPI_MB_Handle2VirAddr(mb_blk);
            blocks[i].size = block_size;
            blocks[i].in_use = false;
            
            if (!blocks[i].vir_addr) {
                LOG_ERROR("VideoBuffer", "Failed to get DMA virtual address for block %zu", i);
                RK_MPI_SYS_Free(mb_blk);
                blocks[i].mb_blk = nullptr;
            }
        } else {
            LOG_WARN("VideoBuffer", "Failed to allocate DMA block %zu (ret: 0x%x)", i, ret);
            break;
        }
    }
    
    LOG_INFO("VideoBuffer", "DMA pool initialized with %zu blocks", MAX_DMA_BLOCKS);
}

VideoMemoryPool::DMAPool::~DMAPool() {
    LOG_INFO("VideoBuffer", "Destroying DMA pool...");
    
    std::lock_guard<std::mutex> lock(mutex);
    for (size_t i = 0; i < MAX_DMA_BLOCKS; i++) {
        if (blocks[i].mb_blk) {
            if (blocks[i].in_use) {
                LOG_WARN("VideoBuffer", "DMA block %zu still in use during destruction", i);
            }
            RK_MPI_SYS_Free(blocks[i].mb_blk);  
            blocks[i].mb_blk = nullptr;
            blocks[i].vir_addr = nullptr;
        }
    }
    
    LOG_INFO("VideoBuffer", "DMA pool destroyed");
}

VideoFrame* VideoMemoryPool::DMAPool::allocateDMAFrame(size_t size) {
    if (size > block_size) {
        return nullptr;  // 超出DMA块大小
    }
    
    std::lock_guard<std::mutex> lock(mutex);
    
    // 查找空闲的DMA块
    for (size_t i = 0; i < MAX_DMA_BLOCKS; i++) {
        if (blocks[i].mb_blk && !blocks[i].in_use) {
            blocks[i].in_use = true;
            
            // 创建VideoFrame对象（堆分配）
            VideoFrame* frame = new VideoFrame();
            frame->data = static_cast<uint8_t*>(blocks[i].vir_addr);
            frame->size = size;
            frame->frame_index = static_cast<int>(i);  // 存储DMA块索引
            frame->is_dma_buffer = true;
            frame->dma_mb_blk = blocks[i].mb_blk;
            
            // 设置删除器（释放时标记为未使用）
            frame->deleter = [this](int block_idx) {
                std::lock_guard<std::mutex> lock(mutex);
                if (block_idx >= 0 && block_idx < static_cast<int>(MAX_DMA_BLOCKS)) {
                    blocks[block_idx].in_use = false;
                    LOG_DEBUG("VideoBuffer", "DMA block %d freed via deleter", block_idx);
                }
            };
            
            return frame;
        }
    }
    
    return nullptr;  // 无可用DMA块
}

void VideoMemoryPool::DMAPool::deallocateDMAFrame(VideoFrame* frame) {
    if (!frame) return;
    
    std::lock_guard<std::mutex> lock(mutex);
    
    int block_idx = frame->frame_index;
    if (block_idx >= 0 && block_idx < static_cast<int>(MAX_DMA_BLOCKS)) {
        blocks[block_idx].in_use = false;
        LOG_DEBUG("VideoBuffer", "DMA block %d freed", block_idx);
    } else {
        LOG_ERROR("VideoBuffer", "Invalid DMA block index: %d", block_idx);
    }
}

// VideoMemoryPool 主类实现
VideoMemoryPool::VideoMemoryPool(const VideoMemoryPoolConfig& config)
    : config_(config) {
    
    LOG_INFO("VideoBuffer", "===========================================");
    LOG_INFO("VideoBuffer", "  Initializing Video Memory Pool");
    LOG_INFO("VideoBuffer", "===========================================");
    
    // 初始化固定池
    fixed_pool_ = std::make_unique<FixedPool>(config.fixed_block_size, config.fixed_block_count);
    
    // 初始化动态池
    dynamic_pool_ = std::make_unique<tool::memory::MemoryPool>(
        config.dynamic_pool_size,  // 初始大小
        config.alignment,           // 对齐
        1.5                         // 扩展因子
    );
    
    LOG_INFO("VideoBuffer", "Dynamic pool: %zu MB, alignment: %zu bytes",
             config.dynamic_pool_size / (1024 * 1024), config.alignment);
    
    // 初始化DMA池（如果启用）
    if (config.enable_dma) {
        dma_pool_ = std::make_unique<DMAPool>(config.fixed_block_size);
        LOG_INFO("VideoBuffer", "DMA pool: %zu blocks × %zu KB = %zu KB",
                 DMAPool::MAX_DMA_BLOCKS, config.fixed_block_size / 1024,
                 (DMAPool::MAX_DMA_BLOCKS * config.fixed_block_size) / 1024);
    }
    
    // DMA零拷贝状态
    if (config.enable_dma) {
        LOG_INFO("VideoBuffer", "DMA zero-copy: ENABLED ✅");
        LOG_INFO("VideoBuffer", "  Performance: No memcpy overhead");
        LOG_INFO("VideoBuffer", "  Note: Fixed/Dynamic pools used as fallback");
    } else {
        LOG_INFO("VideoBuffer", "DMA zero-copy: DISABLED");
    }
    
    LOG_INFO("VideoBuffer", "===========================================");
}

VideoMemoryPool::~VideoMemoryPool() {
    LOG_INFO("VideoBuffer", "Video memory pool shutting down...");
    logStats();
}

VideoFramePtr VideoMemoryPool::allocate(size_t size) {
    stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);
    
    // 第一级：尝试固定池（适合小帧：JPEG、IDR帧）
    if (size <= config_.fixed_block_size) {
        int block_index = fixed_pool_->allocateBlock();
        if (block_index >= 0) {
            stats_.fixed_pool_hits.fetch_add(1, std::memory_order_relaxed);
            
            // 使用对象池中的帧对象
            VideoFrame* frame_obj = &fixed_pool_->frame_objects[block_index];
            frame_obj->data = fixed_pool_->getBlockPtr(block_index);
            frame_obj->size = size;
            frame_obj->frame_index = block_index;
            
            // 设置删除器（释放时归还到固定池）
            auto pool_ptr = fixed_pool_.get();
            frame_obj->deleter = [pool_ptr](int idx) {
                pool_ptr->deallocateBlock(idx);
            };
            
            // 返回共享指针（使用自定义删除器）
            return VideoFramePtr(frame_obj, [](VideoFrame* f) {
                if (f && f->deleter && f->frame_index >= 0) {
                    f->deleter(f->frame_index);
                }
                // 注意：不delete f，因为它在对象池中
            });
        }
    }
    
    // 第二级：动态池（大帧：H.264/H.265 P帧）
    void* mem = dynamic_pool_->allocate(size + sizeof(VideoFrame));
    if (mem) {
        stats_.dynamic_pool_hits.fetch_add(1, std::memory_order_relaxed);
        
        // 在分配的内存开头放置VideoFrame对象
        VideoFrame* frame = new (mem) VideoFrame();
        frame->data = static_cast<uint8_t*>(mem) + sizeof(VideoFrame);
        frame->size = size;
        frame->frame_index = -1;  // 动态池没有索引
        
        // 设置删除器（释放到动态池）
        auto pool_ptr = dynamic_pool_.get();
        frame->deleter = [pool_ptr, mem](int) {
            pool_ptr->deallocate(mem);
        };
        
        return VideoFramePtr(frame, [mem](VideoFrame* f) {
            if (f && f->deleter) {
                f->~VideoFrame();  // 显式调用析构函数
                f->deleter(0);     // 调用删除器
            }
        });
    }
    
    // 第三级：DMA零拷贝内存池
    if (dma_pool_) {
        VideoFrame* dma_frame = dma_pool_->allocateDMAFrame(size);
        if (dma_frame) {
            stats_.dma_pool_hits.fetch_add(1, std::memory_order_relaxed);
            
            // 返回智能指针（使用自定义删除器）
            return VideoFramePtr(dma_frame, [](VideoFrame* f) {
                if (f) {
                    // 通过frame->deleter释放DMA块
                    if (f->deleter && f->frame_index >= 0) {
                        f->deleter(f->frame_index);
                    }
                    delete f;  // 删除VideoFrame对象
                }
            });
        }
    }
    
    // 分配失败
    stats_.allocation_failures.fetch_add(1, std::memory_order_relaxed);
    LOG_ERROR("VideoBuffer", "Memory allocation failed for size: %zu", size);
    return nullptr;
}

void VideoMemoryPool::getStats(Stats& out_stats) const {
    out_stats.fixed_pool_hits = stats_.fixed_pool_hits.load(std::memory_order_relaxed);
    out_stats.dynamic_pool_hits = stats_.dynamic_pool_hits.load(std::memory_order_relaxed);
    out_stats.dma_pool_hits = stats_.dma_pool_hits.load(std::memory_order_relaxed);
    out_stats.total_allocations = stats_.total_allocations.load(std::memory_order_relaxed);
    out_stats.allocation_failures = stats_.allocation_failures.load(std::memory_order_relaxed);
}

void VideoMemoryPool::resetStats() {
    stats_.fixed_pool_hits.store(0, std::memory_order_relaxed);
    stats_.dynamic_pool_hits.store(0, std::memory_order_relaxed);
    stats_.dma_pool_hits.store(0, std::memory_order_relaxed);
    stats_.total_allocations.store(0, std::memory_order_relaxed);
    stats_.allocation_failures.store(0, std::memory_order_relaxed);
}

void VideoMemoryPool::logStats() const {
    uint64_t total = stats_.total_allocations.load(std::memory_order_relaxed);
    uint64_t fixed_hits = stats_.fixed_pool_hits.load(std::memory_order_relaxed);
    uint64_t dynamic_hits = stats_.dynamic_pool_hits.load(std::memory_order_relaxed);
    uint64_t dma_hits = stats_.dma_pool_hits.load(std::memory_order_relaxed);
    uint64_t failures = stats_.allocation_failures.load(std::memory_order_relaxed);
    
    LOG_INFO("VideoBuffer", "=== Memory Pool Statistics ===");
    LOG_INFO("VideoBuffer", "  Total allocations: %zu", total);
    
    if (total > 0) {
        LOG_INFO("VideoBuffer", "  Fixed pool hits:   %zu (%.2f%%)", 
                 fixed_hits, (double)fixed_hits * 100.0 / total);
        LOG_INFO("VideoBuffer", "  Dynamic pool hits: %zu (%.2f%%)", 
                 dynamic_hits, (double)dynamic_hits * 100.0 / total);
        
        // DMA池统计（如果启用）
        if (dma_pool_) {
            LOG_INFO("VideoBuffer", "  DMA pool hits:     %zu (%.2f%%) 🚀", 
                     dma_hits, (double)dma_hits * 100.0 / total);
        }
    }
    
    LOG_INFO("VideoBuffer", "  Failures:          %zu", failures);
    
    // 性能分析和建议
    if (total > 100) {
        uint64_t pool_hits = fixed_hits + dynamic_hits + dma_hits;
        double total_hit_rate = (double)pool_hits * 100.0 / total;
        
        if (fixed_hits * 100 / total < 70) {
            LOG_WARN("VideoBuffer", "Fixed pool hit rate low, consider increasing pool size");
        }
        
        if (dma_pool_ && dma_hits > 0) {
            LOG_INFO("VideoBuffer", "DMA zero-copy optimization active! 🔥");
        }
        
        LOG_INFO("VideoBuffer", "Overall hit rate: %.2f%%", total_hit_rate);
    }
}

// ============================================================================
// RAII 资源包装器实现
// ============================================================================

// ISPWrapper 实现
ISPWrapper::ISPWrapper(int camera_id, const std::string& iq_dir)
    : camera_id_(camera_id) {
    
    LOG_INFO("Camera", "Initializing ISP for camera %d with direct AIQ API", camera_id);
    
    // 停止RkLunch服务
    system("RkLunch-stop.sh 2>/dev/null");
    
    // 步骤1：枚举相机静态信息，获取sensor entity name
    rk_aiq_static_info_t static_info;
    memset(&static_info, 0, sizeof(static_info));
    
    if (rk_aiq_uapi2_sysctl_enumStaticMetas(camera_id, &static_info) != 0) {
        LOG_ERROR("Camera", "Failed to enumerate camera static metas");
        return;
    }
    
    const char* sns_ent_name = static_info.sensor_info.sensor_name;
    LOG_INFO("Camera", "Found sensor: %s", sns_ent_name);
    
    // 步骤2：直接使用AIQ API初始化（不使用SAMPLE_COMM_ISP）
    aiq_ctx_ = rk_aiq_uapi2_sysctl_init(sns_ent_name, iq_dir.c_str(), nullptr, nullptr);
    
    if (!aiq_ctx_) {
        LOG_ERROR("Camera", "Failed to initialize AIQ context");
        return;
    }
    
    LOG_INFO("Camera", "✓ AIQ context created successfully");
    
    // 步骤3：准备并启动AIQ
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
    
    if (rk_aiq_uapi2_sysctl_prepare(aiq_ctx_, 0, 0, hdr_mode) != 0) {
        LOG_ERROR("Camera", "Failed to prepare AIQ");
        rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
        aiq_ctx_ = nullptr;
        return;
    }
    
    if (rk_aiq_uapi2_sysctl_start(aiq_ctx_) != 0) {
        LOG_ERROR("Camera", "Failed to start AIQ");
        rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
        aiq_ctx_ = nullptr;
        return;
    }
    
    valid_ = true;
    LOG_INFO("Camera", "✓ ISP initialized successfully with full AIQ control");
    LOG_INFO("Camera", "✓ ISP parameter control is now AVAILABLE");
}

ISPWrapper::~ISPWrapper() {
    if (aiq_ctx_) {
        rk_aiq_uapi2_sysctl_stop(aiq_ctx_, false);
        rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
        LOG_INFO("Camera", "ISP/AIQ stopped and deinitialized");
    }
}

// ========================================================================
// ISP参数控制实现
// ========================================================================

VideoError ISPWrapper::setExposureMode(opMode_t mode) {
    if (!aiq_ctx_) {
        LOG_ERROR("ISP", "AIQ context not initialized");
        return VideoError::NOT_INITIALIZED;
    }
    
    XCamReturn ret = rk_aiq_uapi2_setExpMode(aiq_ctx_, mode);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Exposure mode set to: %s", mode == OP_AUTO ? "AUTO" : "MANUAL");
        return VideoError::NONE;
    }
    
    LOG_ERROR("ISP", "Failed to set exposure mode: %d", ret);
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getExposureMode(opMode_t& mode) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getExpMode(aiq_ctx_, &mode);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setExpGainRange(float min_gain, float max_gain) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    
    paRange_t gain;
    gain.min = min_gain;
    gain.max = max_gain;
    
    XCamReturn ret = rk_aiq_uapi2_setExpGainRange(aiq_ctx_, &gain);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Exposure gain range set to: [%.2f, %.2f]", min_gain, max_gain);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set exposure gain range");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getExpGainRange(float& min_gain, float& max_gain) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    paRange_t gain;
    XCamReturn ret = rk_aiq_uapi2_getExpGainRange(aiq_ctx_, &gain);
    if (ret == XCAM_RETURN_NO_ERROR) {
        min_gain = gain.min;
        max_gain = gain.max;
        return VideoError::NONE;
    }
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setExpTimeRange(float min_time, float max_time) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    paRange_t time;
    time.min = min_time;
    time.max = max_time;
    XCamReturn ret = rk_aiq_uapi2_setExpTimeRange(aiq_ctx_, &time);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Exposure time range set to: [%.4f, %.4f]s", min_time, max_time);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set exposure time range");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getExpTimeRange(float& min_time, float& max_time) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    paRange_t time;
    XCamReturn ret = rk_aiq_uapi2_getExpTimeRange(aiq_ctx_, &time);
    if (ret == XCAM_RETURN_NO_ERROR) {
        min_time = time.min;
        max_time = time.max;
        return VideoError::NONE;
    }
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::lockAE(bool lock) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setAeLock(aiq_ctx_, lock);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ AE %s", lock ? "locked" : "unlocked");
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to %s AE", lock ? "lock" : "unlock");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setWhiteBalanceMode(opMode_t mode) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setWBMode(aiq_ctx_, mode);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ White balance mode set to: %s", mode == OP_AUTO ? "AUTO" : "MANUAL");
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set white balance mode");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getWhiteBalanceMode(opMode_t& mode) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getWBMode(aiq_ctx_, &mode);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setWhiteBalanceGain(float r_gain, float b_gain) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    rk_aiq_wb_gain_t gain;
    gain.rgain = r_gain;
    gain.bgain = b_gain;
    gain.grgain = 1.0f;
    gain.gbgain = 1.0f;
    XCamReturn ret = rk_aiq_uapi2_setMWBGain(aiq_ctx_, &gain);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ WB gain set to: R=%.2f, B=%.2f", r_gain, b_gain);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set WB gain");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getWhiteBalanceGain(float& r_gain, float& b_gain) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    rk_aiq_wb_gain_t gain;
    XCamReturn ret = rk_aiq_uapi2_getWBGain(aiq_ctx_, &gain);
    if (ret == XCAM_RETURN_NO_ERROR) {
        r_gain = gain.rgain;
        b_gain = gain.bgain;
        return VideoError::NONE;
    }
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setColorTemperature(unsigned int ct) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setMWBCT(aiq_ctx_, ct);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Color temperature set to: %uK", ct);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set color temperature");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getColorTemperature(unsigned int& ct) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getWBCT(aiq_ctx_, &ct);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::lockAWB(bool lock) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = lock ? rk_aiq_uapi2_lockAWB(aiq_ctx_) : rk_aiq_uapi2_unlockAWB(aiq_ctx_);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ AWB %s", lock ? "locked" : "unlocked");
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to %s AWB", lock ? "lock" : "unlock");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setBrightness(unsigned int level) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setBrightness(aiq_ctx_, level);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Brightness set to: %u", level);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set brightness");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getBrightness(unsigned int& level) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getBrightness(aiq_ctx_, &level);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setContrast(unsigned int level) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setContrast(aiq_ctx_, level);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Contrast set to: %u", level);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set contrast");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getContrast(unsigned int& level) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getContrast(aiq_ctx_, &level);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setSaturation(unsigned int level) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setSaturation(aiq_ctx_, level);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Saturation set to: %u", level);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set saturation");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getSaturation(unsigned int& level) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getSaturation(aiq_ctx_, &level);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setHue(unsigned int level) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setHue(aiq_ctx_, level);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Hue set to: %u", level);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set hue");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getHue(unsigned int& level) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getHue(aiq_ctx_, &level);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setSharpness(unsigned int level) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setSharpness(aiq_ctx_, level);
    if (ret == XCAM_RETURN_NO_ERROR) {
        LOG_INFO("ISP", "✓ Sharpness set to: %u", level);
        return VideoError::NONE;
    }
    LOG_ERROR("ISP", "Failed to set sharpness");
    return VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::getSharpness(unsigned int& level) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getSharpness(aiq_ctx_, &level);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

VideoError ISPWrapper::setDehazeLevel(unsigned int level) {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_setDehazeEnable(aiq_ctx_, level > 0);
    if (ret != XCAM_RETURN_NO_ERROR) {
        LOG_ERROR("ISP", "Failed to enable/disable dehaze");
        return VideoError::RKMPI_ERROR;
    }
    if (level > 0) {
        ret = rk_aiq_uapi2_setMDehazeStrth(aiq_ctx_, level);
        if (ret == XCAM_RETURN_NO_ERROR) {
            LOG_INFO("ISP", "✓ Dehaze level set to: %u", level);
            return VideoError::NONE;
        }
        LOG_ERROR("ISP", "Failed to set dehaze level");
        return VideoError::RKMPI_ERROR;
    }
    LOG_INFO("ISP", "✓ Dehaze disabled");
    return VideoError::NONE;
}

VideoError ISPWrapper::getDehazeLevel(unsigned int& level) const {
    if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
    XCamReturn ret = rk_aiq_uapi2_getMDehazeStrth(aiq_ctx_, &level);
    return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
}

// VIDeviceWrapper 实现
VIDeviceWrapper::VIDeviceWrapper(int dev_id) : dev_id_(dev_id) {
    LOG_INFO("Camera", "Initializing VI device %d", dev_id);
    
    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    std::memset(&stDevAttr, 0, sizeof(stDevAttr));
    std::memset(&stBindPipe, 0, sizeof(stBindPipe));
    
    // 检查设备配置状态
    RK_S32 ret = RK_MPI_VI_GetDevAttr(dev_id, &stDevAttr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(dev_id, &stDevAttr);
        if (ret != RK_SUCCESS) {
            LOG_ERROR("Camera", "RK_MPI_VI_SetDevAttr failed: 0x%x", ret);
            return;
        }
    }
    
    // 检查设备使能状态
    ret = RK_MPI_VI_GetDevIsEnable(dev_id);
    if (ret != RK_SUCCESS) {
        // 使能设备
        ret = RK_MPI_VI_EnableDev(dev_id);
        if (ret != RK_SUCCESS) {
            LOG_ERROR("Camera", "RK_MPI_VI_EnableDev failed: 0x%x", ret);
            return;
        }
        
        // 绑定设备与管道
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = dev_id;
        ret = RK_MPI_VI_SetDevBindPipe(dev_id, &stBindPipe);
        if (ret != RK_SUCCESS) {
            LOG_ERROR("Camera", "RK_MPI_VI_SetDevBindPipe failed: 0x%x", ret);
            RK_MPI_VI_DisableDev(dev_id);
            return;
        }
    }
    
    valid_ = true;
    LOG_INFO("Camera", "✓ VI device initialized successfully");
}

VIDeviceWrapper::~VIDeviceWrapper() {
    if (valid_) {
        RK_MPI_VI_DisableDev(dev_id_);
        LOG_INFO("Camera", "VI device %d disabled", dev_id_);
    }
}

// VIChannelWrapper 实现
VIChannelWrapper::VIChannelWrapper(int dev_id, int chn_id, int width, int height)
    : dev_id_(dev_id), chn_id_(chn_id) {
    
    LOG_INFO("Camera", "Initializing VI channel %d (%dx%d)", chn_id, width, height);
    
    VI_CHN_ATTR_S vi_chn_attr;
    std::memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
    
    vi_chn_attr.stIspOpt.u32BufCount = 2;
    vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    vi_chn_attr.stSize.u32Width = width;
    vi_chn_attr.stSize.u32Height = height;
    vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
    vi_chn_attr.u32Depth = 2;
    
    RK_S32 ret = RK_MPI_VI_SetChnAttr(dev_id, chn_id, &vi_chn_attr);
    if (ret != RK_SUCCESS) {
        LOG_ERROR("Camera", "RK_MPI_VI_SetChnAttr failed: 0x%x", ret);
        return;
    }
    
    ret = RK_MPI_VI_EnableChn(dev_id, chn_id);
    if (ret != RK_SUCCESS) {
        LOG_ERROR("Camera", "RK_MPI_VI_EnableChn failed: 0x%x", ret);
        return;
    }
    
    valid_ = true;
    LOG_INFO("Camera", "✓ VI channel initialized successfully");
}

VIChannelWrapper::~VIChannelWrapper() {
    if (valid_) {
        RK_MPI_VI_DisableChn(dev_id_, chn_id_);
        LOG_INFO("Camera", "VI channel %d disabled", chn_id_);
    }
}

// VENCWrapper 实现
VENCWrapper::VENCWrapper(int chn_id, int width, int height, EncodeFormat format, 
                         int bitrate, int gop)
    : chn_id_(chn_id)
    , current_format_(format)
    , current_width_(width) 
    , current_height_(height)
    , current_bitrate_(bitrate)
    , current_gop_(gop)
    , current_jpeg_quality_(77) {  // 默认JPEG质量
    
    LOG_INFO("Camera", "Initializing VENC channel %d (%dx%d, format=%d)", 
             chn_id, width, height, static_cast<int>(format));
    
    VENC_CHN_ATTR_S stAttr;
    std::memset(&stAttr, 0, sizeof(stAttr));
    
    // 根据编码类型设置参数
    RK_CODEC_ID_E codec_type;
    switch (format) {
        case EncodeFormat::H264:
            codec_type = RK_VIDEO_ID_AVC;
            stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
            stAttr.stRcAttr.stH264Cbr.u32BitRate = bitrate;
            stAttr.stRcAttr.stH264Cbr.u32Gop = gop;
            stAttr.stVencAttr.u32Profile = H264E_PROFILE_BASELINE;
            stAttr.stVencAttr.u32BufSize = bitrate * 2 * 1024;
            break;
            
        case EncodeFormat::H265:
            codec_type = RK_VIDEO_ID_HEVC;
            stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
            stAttr.stRcAttr.stH265Cbr.u32BitRate = bitrate;
            stAttr.stRcAttr.stH265Cbr.u32Gop = gop;
            stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
            break;
            
        case EncodeFormat::JPEG:
            codec_type = RK_VIDEO_ID_MJPEG;
            stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
            stAttr.stRcAttr.stMjpegFixQp.u32Qfactor = 70;
            stAttr.stVencAttr.stAttrJpege.bSupportDCF = RK_FALSE;
            stAttr.stVencAttr.stAttrJpege.stMPFCfg.u8LargeThumbNailNum = 0;
            stAttr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
            stAttr.stVencAttr.u32BufSize = width * height * 3;
            break;
    }
    
    stAttr.stVencAttr.enType = codec_type;
    stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    stAttr.stVencAttr.u32PicWidth = width;
    stAttr.stVencAttr.u32PicHeight = height;
    stAttr.stVencAttr.u32VirWidth = width;
    stAttr.stVencAttr.u32VirHeight = height;
    stAttr.stVencAttr.u32StreamBufCnt = 3;
    stAttr.stVencAttr.enMirror = MIRROR_NONE;
    
    RK_S32 ret = RK_MPI_VENC_CreateChn(chn_id, &stAttr);
    if (ret != RK_SUCCESS) {
        LOG_ERROR("Camera", "RK_MPI_VENC_CreateChn failed: 0x%x", ret);
        return;
    }
    
    VENC_RECV_PIC_PARAM_S stRecvParam;
    std::memset(&stRecvParam, 0, sizeof(stRecvParam));
    stRecvParam.s32RecvPicNum = -1;
    
    ret = RK_MPI_VENC_StartRecvFrame(chn_id, &stRecvParam);
    if (ret != RK_SUCCESS) {
        LOG_ERROR("Camera", "RK_MPI_VENC_StartRecvFrame failed: 0x%x", ret);
        RK_MPI_VENC_DestroyChn(chn_id);
        return;
    }
    
    valid_ = true;
    LOG_INFO("Camera", "✓ VENC initialized successfully");
}

VENCWrapper::~VENCWrapper() {
    if (valid_) {
        RK_MPI_VENC_StopRecvFrame(chn_id_);
        RK_MPI_VENC_DestroyChn(chn_id_);
        LOG_INFO("Camera", "VENC channel %d destroyed", chn_id_);
    }
}

VideoError VENCWrapper::getStream(VideoFramePtr& frame, VideoMemoryPool& pool, int timeout_ms) {
    if (!valid_) {
        return VideoError::NOT_INITIALIZED;
    }
    
    VENC_STREAM_S stStream;
    stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
    if (!stStream.pstPack) {
        return VideoError::ALLOC_FAILED;
    }
    
    RK_S32 ret = RK_MPI_VENC_GetStream(chn_id_, &stStream, timeout_ms);
    if (ret != RK_SUCCESS) {
        free(stStream.pstPack);
        return (ret == RK_ERR_VENC_BUSY) ? VideoError::TIMEOUT : VideoError::ENCODE_FAILED;
    }
    
    if (stStream.u32PackCount == 0 || !stStream.pstPack) {
        RK_MPI_VENC_ReleaseStream(chn_id_, &stStream);
        free(stStream.pstPack);
        return VideoError::ENCODE_FAILED;
    }
    
    // 计算总大小
    size_t total_size = 0;
    for (uint32_t i = 0; i < stStream.u32PackCount; i++) {
        total_size += stStream.pstPack[i].u32Len;
    }
    
    // 从内存池分配帧
    frame = pool.allocate(total_size);
    if (!frame) {
        RK_MPI_VENC_ReleaseStream(chn_id_, &stStream);
        free(stStream.pstPack);
        return VideoError::ALLOC_FAILED;
    }
    
    // 复制数据（合并所有pack）
    size_t offset = 0;
    for (uint32_t i = 0; i < stStream.u32PackCount; i++) {
        void* pData = RK_MPI_MB_Handle2VirAddr(stStream.pstPack[i].pMbBlk);
        if (pData) {
            std::memcpy(frame->data + offset, pData, stStream.pstPack[i].u32Len);
            offset += stStream.pstPack[i].u32Len;
        }
    }
    
    // 设置帧属性
    frame->size = total_size;
    frame->pts = stStream.pstPack[0].u64PTS;
    frame->is_keyframe = (stStream.pstPack[0].DataType.enH264EType == H264E_NALU_IDRSLICE ||
                          stStream.pstPack[0].DataType.enH265EType == H265E_NALU_IDRSLICE);
    frame->is_dma_buffer = false;
    
    // 释放编码流
    RK_MPI_VENC_ReleaseStream(chn_id_, &stStream);
    free(stStream.pstPack);
    
    return VideoError::NONE;
}

VideoError VENCWrapper::getStreamZeroCopy(VideoFramePtr& frame, int timeout_ms) {
    if (!valid_) {
        return VideoError::NOT_INITIALIZED;
    }
    
    // 分配VENC_STREAM_S结构（需要手动管理）
    VENC_STREAM_S* stStream = new VENC_STREAM_S();
    stStream->pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
    if (!stStream->pstPack) {
        delete stStream;
        return VideoError::ALLOC_FAILED;
    }
    
    RK_S32 ret = RK_MPI_VENC_GetStream(chn_id_, stStream, timeout_ms);
    if (ret != RK_SUCCESS) {
        free(stStream->pstPack);
        delete stStream;
        return (ret == RK_ERR_VENC_BUSY) ? VideoError::TIMEOUT : VideoError::ENCODE_FAILED;
    }
    
    if (stStream->u32PackCount == 0 || !stStream->pstPack) {
        RK_MPI_VENC_ReleaseStream(chn_id_, stStream);
        free(stStream->pstPack);
        delete stStream;
        return VideoError::ENCODE_FAILED;
    }
    
    // 计算总大小
    size_t total_size = 0;
    for (uint32_t i = 0; i < stStream->u32PackCount; i++) {
        total_size += stStream->pstPack[i].u32Len;
    }
    
    // 创建VideoFrame对象（堆分配）
    VideoFrame* video_frame = new VideoFrame();
    
    // ✅ 关键：直接使用DMA地址，零拷贝！
    if (stStream->u32PackCount == 1) {
        // 单包：直接使用DMA地址
        video_frame->data = static_cast<uint8_t*>(
            RK_MPI_MB_Handle2VirAddr(stStream->pstPack[0].pMbBlk)
        );
        video_frame->dma_mb_blk = stStream->pstPack[0].pMbBlk;
    } else {
        // 多包：需要合并，仍然需要memcpy（极少情况）
        // 这种情况下回退到普通分配
        LOG_WARN("VideoBuffer", "Multi-pack frame (%u packs), cannot use zero-copy", 
                 stStream->u32PackCount);
        
        // 释放资源
        RK_MPI_VENC_ReleaseStream(chn_id_, stStream);
        free(stStream->pstPack);
        delete stStream;
        delete video_frame;
        return VideoError::ENCODE_FAILED;
    }
    
    // 设置帧属性
    video_frame->size = total_size;
    video_frame->pts = stStream->pstPack[0].u64PTS;
    video_frame->is_keyframe = (stStream->pstPack[0].DataType.enH264EType == H264E_NALU_IDRSLICE ||
                                stStream->pstPack[0].DataType.enH265EType == H265E_NALU_IDRSLICE);
    video_frame->is_dma_buffer = true;  // ✅ 标记为DMA缓冲区
    
    // 释放系统资源
    int venc_chn = chn_id_;
    video_frame->deleter = [venc_chn, stStream](int) {
        // 释放VENC流
        RK_S32 ret = RK_MPI_VENC_ReleaseStream(venc_chn, stStream);
        if (ret != RK_SUCCESS) {
            LOG_ERROR("DMABuffer", "RK_MPI_VENC_ReleaseStream failed: 0x%x", ret);
            // 即使失败也要继续释放其他资源
        }
        
        // 确保释放所有分配的资源
        if (stStream) {
            if (stStream->pstPack) {
                free(stStream->pstPack);
                stStream->pstPack = nullptr;
            }
            delete stStream;
        }
    };
    
    // 返回智能指针
    frame = VideoFramePtr(video_frame, [](VideoFrame* f) {
        if (f) {
            if (f->deleter) {
                f->deleter(0);  // 调用删除器释放RKMPI资源
            }
            delete f;  // 删除VideoFrame对象
        }
    });
    
    return VideoError::NONE;
}

// 动态参数调整实现
VideoError VENCWrapper::setBitrate(int bitrate_kbps) {
    if (!valid_) {
        return VideoError::NOT_INITIALIZED;
    }
    
    LOG_INFO("Camera", "Setting bitrate to %d kbps", bitrate_kbps);
    
    // 获取当前编码器属性
    VENC_CHN_ATTR_S stVencChnAttr;
    RK_S32 ret = RK_MPI_VENC_GetChnAttr(chn_id_, &stVencChnAttr);
    if (ret != RK_SUCCESS) {
        LOG_ERROR("Camera", "Failed to get VENC channel attr: 0x%x", ret);
        return VideoError::ENCODE_FAILED;
    }
    
    // 根据编码格式设置码率
    switch (current_format_) {
        case EncodeFormat::H264:
            stVencChnAttr.stRcAttr.stH264Cbr.u32BitRate = bitrate_kbps;
            break;
            
        case EncodeFormat::H265:
            stVencChnAttr.stRcAttr.stH265Cbr.u32BitRate = bitrate_kbps;
            break;
            
        case EncodeFormat::JPEG:
            LOG_WARN("Camera", "JPEG format does not support bitrate setting");
            return VideoError::INVALID_PARAM;
    }
    
    // 应用新属性
    ret = RK_MPI_VENC_SetChnAttr(chn_id_, &stVencChnAttr);
    if (ret == RK_SUCCESS) {
        current_bitrate_ = bitrate_kbps;
        LOG_INFO("Camera", "✓ Bitrate updated to %d kbps", bitrate_kbps);
        return VideoError::NONE;
    } else {
        LOG_ERROR("Camera", "Failed to set bitrate: 0x%x", ret);
        return VideoError::ENCODE_FAILED;
    }
}

VideoError VENCWrapper::setGOP(int gop) {
    if (!valid_) {
        return VideoError::NOT_INITIALIZED;
    }
    
    LOG_INFO("Camera", "Setting GOP to %d", gop);
    
    // 获取当前编码器属性
    VENC_CHN_ATTR_S stVencChnAttr;
    RK_S32 ret = RK_MPI_VENC_GetChnAttr(chn_id_, &stVencChnAttr);
    if (ret != RK_SUCCESS) {
        LOG_ERROR("Camera", "Failed to get VENC channel attr: 0x%x", ret);
        return VideoError::ENCODE_FAILED;
    }
    
    // 根据编码格式设置GOP
    switch (current_format_) {
        case EncodeFormat::H264:
            stVencChnAttr.stRcAttr.stH264Cbr.u32Gop = gop;
            break;
            
        case EncodeFormat::H265:
            stVencChnAttr.stRcAttr.stH265Cbr.u32Gop = gop;
            break;
            
        case EncodeFormat::JPEG:
            LOG_INFO("Camera", "JPEG format always uses GOP=1");
            current_gop_ = 1;
            return VideoError::NONE;
    }
    
    // 应用新属性
    ret = RK_MPI_VENC_SetChnAttr(chn_id_, &stVencChnAttr);
    if (ret == RK_SUCCESS) {
        current_gop_ = gop;
        LOG_INFO("Camera", "✓ GOP updated to %d", gop);
        return VideoError::NONE;
    } else {
        LOG_ERROR("Camera", "Failed to set GOP: 0x%x", ret);
        return VideoError::ENCODE_FAILED;
    }
}

VideoError VENCWrapper::setJPEGQuality(int quality) {
    if (!valid_) {
        return VideoError::NOT_INITIALIZED;
    }
    
    // 限制JPEG质量范围 [1, 100]
    quality = std::max(1, std::min(100, quality));
    
    LOG_INFO("Camera", "Setting JPEG quality to %d", quality);
    
    if (current_format_ != EncodeFormat::JPEG) {
        LOG_WARN("Camera", "Current format is not JPEG, quality setting saved for next JPEG encode");
        current_jpeg_quality_ = quality;
        return VideoError::NONE;
    }
    
    // ✅ 使用通用的VENC_CHN_ATTR_S方法设置JPEG质量
    VENC_CHN_ATTR_S stVencChnAttr;
    RK_S32 ret = RK_MPI_VENC_GetChnAttr(chn_id_, &stVencChnAttr);
    if (ret != RK_SUCCESS) {
        LOG_ERROR("Camera", "Failed to get VENC channel attr: 0x%x", ret);
        return VideoError::ENCODE_FAILED;
    }
    
    // 设置JPEG质量参数
    if (stVencChnAttr.stRcAttr.enRcMode == VENC_RC_MODE_MJPEGFIXQP) {
        int qp = std::max(1, std::min(99, (quality * 99 + 50) / 100));
        stVencChnAttr.stRcAttr.stMjpegFixQp.u32Qfactor = qp;
        
        ret = RK_MPI_VENC_SetChnAttr(chn_id_, &stVencChnAttr);
        if (ret == RK_SUCCESS) {
            current_jpeg_quality_ = quality;
            LOG_INFO("Camera", "✓ JPEG quality updated to %d (QP: %d)", quality, qp);
            return VideoError::NONE;
        } else {
            LOG_ERROR("Camera", "Failed to set JPEG quality via ChnAttr: 0x%x", ret);
        }
    } else {
        LOG_WARN("Camera", "JPEG encoder not in FixQP mode, cannot adjust quality");
    }
    
    current_jpeg_quality_ = quality;
    LOG_INFO("Camera", "✓ JPEG quality setting saved: %d", quality);
    return VideoError::NONE;
}

// FileWrapper 实现
FileWrapper::FileWrapper(const std::string& filename, bool write)
    : filename_(filename) {
    
    const char* mode = write ? "wb" : "rb";
    file_ = fopen(filename.c_str(), mode);
    
    if (file_) {
        valid_ = true;
        LOG_INFO("Camera", "File opened: %s", filename.c_str());
    } else {
        LOG_ERROR("Camera", "Failed to open file: %s", filename.c_str());
    }
}

FileWrapper::~FileWrapper() {
    if (file_) {
        fclose(file_);
        LOG_INFO("Camera", "File closed: %s", filename_.c_str());
    }
}

bool FileWrapper::write(const void* data, size_t size) {
    if (!valid_ || !file_) {
        return false;
    }
    
    size_t written = fwrite(data, 1, size, file_);
    return written == size;
}

void FileWrapper::flush() {
    if (file_) {
        fflush(file_);
    }
}

// ============================================================================
// VideoSystemV2::Impl 实现类（Pimpl模式）
// ============================================================================

class VideoSystemV2::Impl {
public:
    Impl(const VideoConfig& config, 
         std::atomic<bool>& photo_capturing_ref,
         std::atomic<bool>& is_recording_ref,
         std::atomic<bool>& is_webrtc_streaming_ref)
        : config_(config)
        , memory_pool_(VideoMemoryPool::VideoMemoryPoolConfig{
            config.fixed_block_size,
            config.fixed_pool_size,
            config.dynamic_pool_size, 
            64,  // 对齐
            config.enable_dma_zero_copy  // DMA零拷贝开关
          })
        , quit_flag_(false)
        , current_fps_(0.0f)
        , photo_id_(0)
        , record_id_(0)
        , photo_capturing_external_(photo_capturing_ref)
        , is_recording_external_(is_recording_ref)
        , is_webrtc_streaming_external_(is_webrtc_streaming_ref)
    {
        LOG_INFO("Camera", "VideoSystemV2::Impl constructed (DMA: %s)", 
                 config.enable_dma_zero_copy ? "enabled" : "disabled");
    }
    
    ~Impl() {
        LOG_INFO("Camera", "VideoSystemV2::Impl destructing...");
        shutdown();
    }
    
    // ========================================================================
    // 初始化和关闭
    // ========================================================================
    
    VideoError initialize(std::shared_ptr<sync_context_t> sync_ctx) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        LOG_INFO("Camera", "========================================");
        LOG_INFO("Camera", "  Initializing VideoSystemV2");
        LOG_INFO("Camera", "========================================");
        LOG_INFO("Camera", "Configuration:");
        LOG_INFO("Camera", "  Resolution: %dx%d @ %d fps", 
                 config_.width, config_.height, config_.fps);
        LOG_INFO("Camera", "  Format: %d, Bitrate: %d kbps, GOP: %d",
                 static_cast<int>(config_.format), config_.bitrate, config_.gop);
        
        sync_ctx_ = sync_ctx;
        
        // 创建输出目录
        mkdir(config_.photo_path.c_str(), 0755);
        mkdir(config_.record_path.c_str(), 0755);
        
        // 步骤1: 初始化RKMPI系统
        LOG_INFO("Camera", "Step 1: Initializing RKMPI system...");
        if (RK_MPI_SYS_Init() != RK_SUCCESS) {
            LOG_ERROR("Camera", "RK_MPI_SYS_Init failed");
            return VideoError::INIT_FAILED;
        }
        rkmpi_initialized_ = true;
        
        // 步骤2: 初始化ISP
        LOG_INFO("Camera", "Step 2: Initializing ISP...");
        isp_ = std::make_unique<ISPWrapper>(0, ISP_PATH);
        if (!isp_->isValid()) {
            LOG_ERROR("Camera", "ISP initialization failed");
            return VideoError::INIT_FAILED;
        }
        
        // 步骤3: 初始化VI设备
        LOG_INFO("Camera", "Step 3: Initializing VI device...");
        vi_dev_ = std::make_unique<VIDeviceWrapper>(0);
        if (!vi_dev_->isValid()) {
            LOG_ERROR("Camera", "VI device initialization failed");
            return VideoError::INIT_FAILED;
        }
        
        // 步骤4: 初始化VI通道
        LOG_INFO("Camera", "Step 4: Initializing VI channel...");
        vi_chn_ = std::make_unique<VIChannelWrapper>(0, 0, config_.width, config_.height);
        if (!vi_chn_->isValid()) {
            LOG_ERROR("Camera", "VI channel initialization failed");
            return VideoError::INIT_FAILED;
        }
        
        // 步骤5: 初始化VENC编码器
        LOG_INFO("Camera", "Step 5: Initializing VENC encoder...");
        venc_ = std::make_unique<VENCWrapper>(
            0, config_.width, config_.height, 
            config_.format, config_.bitrate, config_.gop
        );
        if (!venc_->isValid()) {
            LOG_ERROR("Camera", "VENC initialization failed");
            return VideoError::INIT_FAILED;
        }
        
        // 步骤6: 绑定VI到VENC
        LOG_INFO("Camera", "Step 6: Binding VI to VENC...");
        MPP_CHN_S stSrcChn, stDestChn;
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = 0;
        
        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        
        if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn) != RK_SUCCESS) {
            LOG_ERROR("Camera", "Module bind failed");
            return VideoError::INIT_FAILED;
        }
        modules_bound_ = true;
        
        LOG_INFO("Camera", "========================================");
        LOG_INFO("Camera", "VideoSystemV2 initialized successfully!");
        LOG_INFO("Camera", "========================================");
        
        return VideoError::NONE;
    }
    
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        LOG_INFO("Camera", "Shutting down VideoSystemV2...");
        
        // 停止流处理
        stopStreamInternal();
        
        // 停止所有功能
        stopRecordInternal();
        stopWebRTCInternal();
        
        // 解绑模块
        if (modules_bound_) {
            MPP_CHN_S stSrcChn, stDestChn;
            stSrcChn.enModId = RK_ID_VI;
            stSrcChn.s32DevId = 0;
            stSrcChn.s32ChnId = 0;
            
            stDestChn.enModId = RK_ID_VENC;
            stDestChn.s32DevId = 0;
            stDestChn.s32ChnId = 0;
            
            RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
            modules_bound_ = false;
        }
        
        // RAII自动释放资源（按构造的逆序）
        venc_.reset();
        vi_chn_.reset();
        vi_dev_.reset();
        isp_.reset();
        
        // 退出RKMPI
        if (rkmpi_initialized_) {
            RK_MPI_SYS_Exit();
            rkmpi_initialized_ = false;
        }
        
        LOG_INFO("Camera", "VideoSystemV2 shutdown complete");
    }
    
    // ========================================================================
    // 视频流处理
    // ========================================================================
    
    VideoError startStream() {
        std::lock_guard<std::mutex> lock(mutex_);
        return startStreamInternal();
    }
    
    VideoError startStreamInternal() {
        if (stream_thread_ && stream_thread_->joinable()) {
            LOG_WARN("Camera", "Stream already started");
            return VideoError::ALREADY_STARTED;
        }
        
        LOG_INFO("Camera", "Starting video stream...");
        
        quit_flag_.store(false);
        stream_thread_ = std::make_unique<std::thread>(&Impl::streamProcessThread, this);
        
        LOG_INFO("Camera", "✓ Video stream started");
        return VideoError::NONE;
    }
    
    VideoError stopStream() {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopStreamInternal();
    }
    
    VideoError stopStreamInternal() {
        if (!stream_thread_ || !stream_thread_->joinable()) {
            return VideoError::NOT_STARTED;
        }
        
        LOG_INFO("Camera", "Stopping video stream...");
        
        quit_flag_.store(true);
        if (stream_thread_->joinable()) {
            stream_thread_->join();
        }
        stream_thread_.reset();
        
        LOG_INFO("Camera", "✓ Video stream stopped");
        return VideoError::NONE;
    }
    
    void streamProcessThread() {
        LOG_INFO("Camera", "Stream process thread started (DMA: %s)", 
                 config_.enable_dma_zero_copy ? "enabled" : "disabled");
        
        auto last_stat_time = std::chrono::steady_clock::now();
        int frame_count = 0;
        
        while (!quit_flag_.load(std::memory_order_acquire)) {
            VideoFramePtr frame;
            VideoError err;
            
            // 根据配置选择获取方式
            if (config_.enable_dma_zero_copy) {
                // ✅ DMA零拷贝：直接使用RKMPI缓冲区
                err = venc_->getStreamZeroCopy(frame, 100);
            } else {
                // 普通方式：内存池分配+memcpy
                err = venc_->getStream(frame, memory_pool_, 100);
            }
            
            if (err == VideoError::TIMEOUT) {
                continue;  // 超时，继续循环
            }
            
            if (err != VideoError::NONE || !frame) {
                if (err != VideoError::TIMEOUT) {
                    LOG_ERROR("Camera", "Failed to get stream: %d", static_cast<int>(err));
                }
                continue;
            }
            
            frame_count++;
            stats_.frames_captured.fetch_add(1, std::memory_order_relaxed);
            
            // 统计DMA使用情况
            if (frame->is_dma_buffer) {
                stats_.dma_frames.fetch_add(1, std::memory_order_relaxed);
            }
            
            // 时间同步
            if (sync_ctx_) {
                frame->timestamp = sync_get_timestamp(sync_ctx_.get(), frame->pts, false);
            } else {
                frame->timestamp = frame->pts;
            }
            
            // 根据主状态分发帧
            VideoMainState main_state = main_state_.load(std::memory_order_acquire);
            
            switch (main_state) {
                case VideoMainState::PHOTO:
                    handlePhotoFrame(frame);
                    break;
                    
                case VideoMainState::RECORD:
                    handleRecordFrame(frame);
                    break;
                    
                case VideoMainState::WEBRTC:
                    handleWebRTCFrame(frame);
                    break;
                    
                default:
                    break;
            }
            
            // 计算FPS
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                current_time - last_stat_time).count();
            
            if (elapsed >= 1000000) {  // 1秒
                current_fps_ = static_cast<float>(frame_count) * 1000000.0f / elapsed;
                last_stat_time = current_time;
                frame_count = 0;
                
                #if DISPLAY_FPS
                LOG_INFO("Camera", "Current FPS: %.2f", current_fps_);
                #endif
            }
        }
        
        LOG_INFO("Camera", "Stream process thread stopped");
    }
    
    // ========================================================================
    // 拍照处理
    // ========================================================================
    
    void handlePhotoFrame(VideoFramePtr& frame) {
        if (!photo_capturing_.load(std::memory_order_acquire)) {
            return;
        }
        
        photo_capture_count_++;
        
        // 只取最后一帧
        if (photo_capture_count_ >= config_.photo_capture_frames) {
            std::string filename = photo_filename_.empty() 
                ? config_.photo_path + "photo_" + std::to_string(photo_id_++) + ".jpg"
                : photo_filename_;
            
            // 使用FileWrapper保存
            FileWrapper file(filename, true);
            if (file.isValid()) {
                if (file.write(frame->data, frame->size)) {
                    LOG_INFO("Camera", "Photo saved: %s (%zu bytes)", 
                             filename.c_str(), frame->size);
                    stats_.photos_taken.fetch_add(1, std::memory_order_relaxed);
                } else {
                    LOG_ERROR("Camera", "Failed to write photo file");
                }
            }
            
            // 重置拍照状态
            photo_capture_count_ = 0;
            
            // 确保状态同步
            bool expected = true;
            if (photo_capturing_.compare_exchange_strong(expected, false, std::memory_order_release)) {
                photo_capturing_external_.store(false, std::memory_order_release);
                photo_filename_.clear();
            }
            
            // 设置标志：通知需要恢复编码器（外部检测）
            // 不能在流线程中调用stopStreamInternal()，会导致死锁
            LOG_INFO("Camera", "Photo complete, will restore H264 encoder");
        }
    }
    
    VideoError switchToJPEGEncoder() {
        if (!venc_) {
            return VideoError::INVALID_STATE;
        }
        
        LOG_INFO("Camera", "Switching to JPEG encoder...");
        
        // 解绑模块
        if (modules_bound_) {
            MPP_CHN_S stSrcChn, stDestChn;
            stSrcChn.enModId = RK_ID_VI;
            stSrcChn.s32DevId = 0;
            stSrcChn.s32ChnId = 0;
            
            stDestChn.enModId = RK_ID_VENC;
            stDestChn.s32DevId = 0;
            stDestChn.s32ChnId = 0;
            
            RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
            modules_bound_ = false;
        }
        
        // 销毁H264编码器
        venc_.reset();
        
        // 创建JPEG编码器
        venc_ = std::make_unique<VENCWrapper>(
            0, config_.width, config_.height,
            EncodeFormat::JPEG, config_.bitrate, config_.gop
        );
        
        if (!venc_->isValid()) {
            LOG_ERROR("Camera", "Failed to create JPEG encoder");
            return VideoError::INIT_FAILED;
        }
        
        // 应用JPEG质量设置
        VideoError quality_err = venc_->setJPEGQuality(config_.quality);
        if (quality_err != VideoError::NONE) {
            LOG_WARN("Camera", "Failed to set JPEG quality");
        }
        
        // 重新绑定
        MPP_CHN_S stSrcChn, stDestChn;
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = 0;
        
        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        
        if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn) != RK_SUCCESS) {
            LOG_ERROR("Camera", "Failed to bind modules");
            return VideoError::INIT_FAILED;
        }
        modules_bound_ = true;
        
        LOG_INFO("Camera", "✓ Switched to JPEG encoder");
        return VideoError::NONE;
    }
    
    VideoError switchToH264Encoder() {
        if (!venc_) {
            return VideoError::INVALID_STATE;
        }
        
        LOG_INFO("Camera", "Switching back to H264 encoder...");
        
        // 解绑模块
        if (modules_bound_) {
            MPP_CHN_S stSrcChn, stDestChn;
            stSrcChn.enModId = RK_ID_VI;
            stSrcChn.s32DevId = 0;
            stSrcChn.s32ChnId = 0;
            
            stDestChn.enModId = RK_ID_VENC;
            stDestChn.s32DevId = 0;
            stDestChn.s32ChnId = 0;
            
            RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
            modules_bound_ = false;
        }
        
        // 销毁JPEG编码器
        venc_.reset();
        
        // 重新创建H264编码器
        venc_ = std::make_unique<VENCWrapper>(
            0, config_.width, config_.height,
            config_.format, config_.bitrate, config_.gop
        );
        
        if (!venc_->isValid()) {
            LOG_ERROR("Camera", "Failed to create H264 encoder");
            return VideoError::INIT_FAILED;
        }
        
        // 重新绑定
        MPP_CHN_S stSrcChn, stDestChn;
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = 0;
        
        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        
        if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn) != RK_SUCCESS) {
            LOG_ERROR("Camera", "Failed to bind modules");
            return VideoError::INIT_FAILED;
        }
        modules_bound_ = true;
        
        LOG_INFO("Camera", "✓ Switched back to H264 encoder");
        return VideoError::NONE;
    }
    
    bool needsRestoreEncoder() const {
        return photo_need_restore_encoder_;
    }
    
    VideoError restoreH264Encoder() {
        if (!photo_need_restore_encoder_) {
            return VideoError::NONE;
        }
        
        LOG_INFO("Camera", "Restoring H264 encoder...");
        
        bool was_streaming = stream_thread_ && stream_thread_->joinable();
        
        // 停止视频流
        if (was_streaming) {
            stopStreamInternal();
        }
        
        // 切换回H264编码器
        VideoError err = switchToH264Encoder();
        if (err != VideoError::NONE) {
            LOG_ERROR("Camera", "Failed to restore H264 encoder");
            return err;
        }
        
        // 重新启动视频流
        if (was_streaming) {
            startStreamInternal();
        }
        
        photo_need_restore_encoder_ = false;
        LOG_INFO("Camera", "✓ H264 encoder restored");
        
        return VideoError::NONE;
    }
    
    VideoError takePhoto(const std::string& filename, bool switch_encoder) {
        if (photo_capturing_.load(std::memory_order_acquire)) {
            return VideoError::ALREADY_STARTED;
        }
        
        photo_filename_ = filename;
        photo_capture_count_ = 0;
        photo_need_restore_encoder_ = false;
        
        // 如果需要切换编码器且当前不是JPEG编码器
        if (switch_encoder && config_.format != EncodeFormat::JPEG) {
            bool was_streaming = stream_thread_ && stream_thread_->joinable();
            
            // 停止视频流
            if (was_streaming) {
                LOG_INFO("Camera", "Stopping stream for encoder switch...");
                stopStreamInternal();
            }
            
            // 切换到JPEG编码器
            VideoError err = switchToJPEGEncoder();
            if (err != VideoError::NONE) {
                // 切换失败，恢复流
                if (was_streaming) {
                    startStreamInternal();
                }
                return err;
            }
            
            photo_need_restore_encoder_ = true;
            
            // 重新启动视频流
            if (was_streaming) {
                LOG_INFO("Camera", "Restarting stream with JPEG encoder...");
                startStreamInternal();
                // 等待流稳定
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        photo_capturing_.store(true, std::memory_order_release);
        
        LOG_INFO("Camera", "Photo capture started");
        return VideoError::NONE;
    }
    
    // ========================================================================
    // 录像处理
    // ========================================================================
    
    void handleRecordFrame(VideoFramePtr& frame) {
        if (!is_recording_.load(std::memory_order_acquire)) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(record_mutex_);
        
        if (record_file_ && record_file_->isValid()) {
            if (record_file_->write(frame->data, frame->size)) {
                record_file_->flush();
                
                // 更新录像时长
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - record_start_time_).count();
                stats_.record_duration_ms.store(elapsed, std::memory_order_relaxed);
                
                // 如果设置了时长限制，检查是否到达
                if (record_duration_sec_ > 0 && elapsed >= record_duration_sec_ * 1000) {
                    LOG_INFO("Camera", "Record duration reached, stopping...");
                    stopRecordInternal();
                }
            }
        }
    }
    
    VideoError startRecord(const std::string& filename, int duration_sec) {
        std::lock_guard<std::mutex> lock(record_mutex_);
        
        if (is_recording_.load(std::memory_order_acquire)) {
            return VideoError::ALREADY_STARTED;
        }
        
        std::string record_filename = filename.empty()
            ? config_.record_path + "record_" + std::to_string(record_id_++) + ".h264"
            : filename;
        
        record_file_ = std::make_unique<FileWrapper>(record_filename, true);
        if (!record_file_->isValid()) {
            record_file_.reset();
            return VideoError::FILE_OPEN_FAILED;
        }
        
        record_duration_sec_ = duration_sec;
        record_start_time_ = std::chrono::steady_clock::now();
        is_recording_.store(true, std::memory_order_release);
        
        LOG_INFO("Camera", "Recording started: %s", record_filename.c_str());
        return VideoError::NONE;
    }
    
    VideoError stopRecord() {
        std::lock_guard<std::mutex> lock(record_mutex_);
        return stopRecordInternal();
    }
    
    VideoError stopRecordInternal() {
        if (!is_recording_.load(std::memory_order_acquire)) {
            return VideoError::NOT_STARTED;
        }
        
        is_recording_.store(false, std::memory_order_release);
        is_recording_external_.store(false, std::memory_order_release);  // ✅ 同步外部状态
        
        if (record_file_) {
            record_file_->flush();
            LOG_INFO("Camera", "Recording stopped: %s", record_file_->getFilename().c_str());
            record_file_.reset();
        }
        
        return VideoError::NONE;
    }
    
    // ========================================================================
    // WebRTC处理
    // ========================================================================
    
    void handleWebRTCFrame(VideoFramePtr& frame) {
        if (!is_webrtc_streaming_.load(std::memory_order_acquire)) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(webrtc_mutex_);
        
        if (webrtc_callback_) {
            webrtc_callback_(frame);
        }
    }
    
    void setWebRTCCallback(WebRTCVideoCallback callback) {
        std::lock_guard<std::mutex> lock(webrtc_mutex_);
        webrtc_callback_ = callback;
        LOG_INFO("Camera", "WebRTC callback set");
    }
    
    VideoError startWebRTCStream() {
        if (is_webrtc_streaming_.load(std::memory_order_acquire)) {
            return VideoError::ALREADY_STARTED;
        }
        
        std::lock_guard<std::mutex> lock(webrtc_mutex_);
        
        if (!webrtc_callback_) {
            LOG_ERROR("Camera", "WebRTC callback not set");
            return VideoError::INVALID_STATE;
        }
        
        is_webrtc_streaming_.store(true, std::memory_order_release);
        LOG_INFO("Camera", "WebRTC stream started");
        
        return VideoError::NONE;
    }
    
    VideoError stopWebRTCStream() {
        std::lock_guard<std::mutex> lock(webrtc_mutex_);
        return stopWebRTCInternal();
    }
    
    VideoError stopWebRTCInternal() {
        if (!is_webrtc_streaming_.load(std::memory_order_acquire)) {
            return VideoError::NOT_STARTED;
        }
        
        is_webrtc_streaming_.store(false, std::memory_order_release);
        is_webrtc_streaming_external_.store(false, std::memory_order_release);  // ✅ 同步外部状态
        LOG_INFO("Camera", "WebRTC stream stopped");
        
        return VideoError::NONE;
    }
    
    // ========================================================================
    // 状态机
    // ========================================================================
    
    VideoError setMainState(VideoMainState new_state) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        VideoMainState old_state = main_state_.load(std::memory_order_acquire);
        if (old_state == new_state) {
            return VideoError::NONE;
        }
        
        main_state_.store(new_state, std::memory_order_release);
        
        LOG_INFO("Camera", "Main State: %d → %d", 
                 static_cast<int>(old_state), static_cast<int>(new_state));
        
        // 触发回调
        if (main_state_callback_) {
            main_state_callback_(old_state, new_state);
        }
        
        return VideoError::NONE;
    }
    
    void setMainStateCallback(StateChangeCallback<VideoMainState> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        main_state_callback_ = callback;
    }
    
    void setControlStateCallback(StateChangeCallback<VideoControlState> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        control_state_callback_ = callback;
    }
    
    // ========================================================================
    // 统计信息
    // ========================================================================
    
    void getStats(VideoSystemV2::Stats& out_stats) const {
        memory_pool_.getStats(out_stats.mem_stats);
        out_stats.frames_captured = stats_.frames_captured.load(std::memory_order_relaxed);
        out_stats.frames_dropped = stats_.frames_dropped.load(std::memory_order_relaxed);
        out_stats.photos_taken = stats_.photos_taken.load(std::memory_order_relaxed);
        out_stats.record_duration_ms = stats_.record_duration_ms.load(std::memory_order_relaxed);
    }
    
    void logStats() const {
        uint64_t total_frames = stats_.frames_captured.load(std::memory_order_relaxed);
        uint64_t dma_frames = stats_.dma_frames.load(std::memory_order_relaxed);
        
        LOG_INFO("Camera", "=== Video System V2 Statistics ===");
        LOG_INFO("Camera", "  Frames captured: %zu", total_frames);
        
        // DMA零拷贝统计
        if (config_.enable_dma_zero_copy && total_frames > 0) {
            double dma_rate = (double)dma_frames * 100.0 / total_frames;
            LOG_INFO("Camera", "  DMA zero-copy:   %zu (%.2f%%) 🚀", dma_frames, dma_rate);
            LOG_INFO("Camera", "  Memory copy:     %zu (%.2f%%)", 
                     total_frames - dma_frames, 100.0 - dma_rate);
        }
        
        LOG_INFO("Camera", "  Frames dropped:  %zu", 
                 stats_.frames_dropped.load(std::memory_order_relaxed));
        LOG_INFO("Camera", "  Photos taken:    %zu", 
                 stats_.photos_taken.load(std::memory_order_relaxed));
        LOG_INFO("Camera", "  Record duration: %zu ms", 
                 stats_.record_duration_ms.load(std::memory_order_relaxed));
        
        memory_pool_.logStats();
    }
    
    float getCurrentFPS() const {
        return current_fps_;
    }
    
    // ========================================================================
    // 动态参数调整
    // ========================================================================
    
    VideoError setEncodingParams(int bitrate, int gop) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!venc_ || !venc_->isValid()) {
            LOG_ERROR("Camera", "VENC not initialized");
            return VideoError::NOT_INITIALIZED;
        }
        
        LOG_INFO("Camera", "Setting encoding params: bitrate=%d kbps, GOP=%d", bitrate, gop);
        
        // 设置码率
        VideoError err = venc_->setBitrate(bitrate);
        if (err != VideoError::NONE) {
            LOG_ERROR("Camera", "Failed to set bitrate");
            return err;
        }
        
        // 设置GOP
        err = venc_->setGOP(gop);
        if (err != VideoError::NONE) {
            LOG_ERROR("Camera", "Failed to set GOP");
            return err;
        }
        
        // 更新配置
        config_.bitrate = bitrate;
        config_.gop = gop;
        
        LOG_INFO("Camera", "✓ Encoding params updated successfully");
        return VideoError::NONE;
    }
    
    VideoError setBitrate(int bitrate_kbps) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!venc_ || !venc_->isValid()) {
            LOG_ERROR("Camera", "VENC not initialized");
            return VideoError::NOT_INITIALIZED;
        }
        
        VideoError err = venc_->setBitrate(bitrate_kbps);
        if (err == VideoError::NONE) {
            config_.bitrate = bitrate_kbps;
        }
        return err;
    }
    
    VideoError setGOP(int gop) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!venc_ || !venc_->isValid()) {
            LOG_ERROR("Camera", "VENC not initialized");
            return VideoError::NOT_INITIALIZED;
        }
        
        VideoError err = venc_->setGOP(gop);
        if (err == VideoError::NONE) {
            config_.gop = gop;
        }
        return err;
    }
    
    VideoError setJPEGQuality(int quality) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 限制质量范围
        quality = std::max(1, std::min(100, quality));
        
        // 更新配置
        config_.quality = quality;
        LOG_INFO("Camera", "JPEG quality set to %d", quality);
        
        // 如果VENC已初始化，立即应用
        if (venc_ && venc_->isValid()) {
            return venc_->setJPEGQuality(quality);
        }
        
        return VideoError::NONE;
    }
    
    // 成员变量
    VideoConfig config_;
    VideoMemoryPool memory_pool_;
    
    // RAII资源包装器
    std::unique_ptr<ISPWrapper> isp_;
    std::unique_ptr<VIDeviceWrapper> vi_dev_;
    std::unique_ptr<VIChannelWrapper> vi_chn_;
    std::unique_ptr<VENCWrapper> venc_;
    
    // 时间同步
    std::shared_ptr<sync_context_t> sync_ctx_;
    
    // 流处理线程
    std::unique_ptr<std::thread> stream_thread_;
    std::atomic<bool> quit_flag_;
    std::atomic<float> current_fps_;
    
    // 拍照状态
    std::atomic<bool> photo_capturing_{false};
    int photo_capture_count_ = 0;
    int photo_id_;
    std::string photo_filename_;
    bool photo_need_restore_encoder_ = false;  // 拍照后是否需要恢复编码器
    
    // 录像状态
    std::atomic<bool> is_recording_{false};
    std::unique_ptr<FileWrapper> record_file_;
    int record_id_;
    int record_duration_sec_ = 0;
    std::chrono::steady_clock::time_point record_start_time_;
    std::mutex record_mutex_;
    
    // WebRTC状态
    std::atomic<bool> is_webrtc_streaming_{false};
    WebRTCVideoCallback webrtc_callback_;
    std::mutex webrtc_mutex_;
    
    // 主状态和控制状态
    std::atomic<VideoMainState> main_state_{VideoMainState::NONE};
    std::atomic<VideoControlState> control_state_{VideoControlState::IDLE};
    
    // 状态变化回调
    StateChangeCallback<VideoMainState> main_state_callback_;
    StateChangeCallback<VideoControlState> control_state_callback_;
    
    // 系统状态
    bool rkmpi_initialized_ = false;
    bool modules_bound_ = false;
    
    // 统计数据
    struct {
        std::atomic<uint64_t> frames_captured{0};
        std::atomic<uint64_t> frames_dropped{0};
        std::atomic<uint64_t> photos_taken{0};
        std::atomic<uint64_t> record_duration_ms{0};
        std::atomic<uint64_t> dma_frames{0};  // DMA零拷贝帧数
    } stats_;
    
    // 外部状态引用（用于同步Impl内部状态到外部）
    std::atomic<bool>& photo_capturing_external_;
    std::atomic<bool>& is_recording_external_;
    std::atomic<bool>& is_webrtc_streaming_external_;
    
    // 互斥锁
    mutable std::mutex mutex_;
};

// ============================================================================
// VideoSystemV2 公开接口实现
// ============================================================================

VideoSystemV2::VideoSystemV2(const VideoConfig& config)
    : pImpl_(std::make_unique<Impl>(config, photo_capturing_, is_recording_, is_webrtc_streaming_)) {
}

VideoSystemV2::~VideoSystemV2() {
    shutdown();
}

VideoError VideoSystemV2::initialize(std::shared_ptr<sync_context_t> sync_ctx) {
    VideoError err = pImpl_->initialize(sync_ctx);
    if (err == VideoError::NONE) {
        is_initialized_.store(true, std::memory_order_release);
    }
    return err;
}

void VideoSystemV2::shutdown() {
    if (is_initialized_.load(std::memory_order_acquire)) {
        pImpl_->shutdown();
        is_initialized_.store(false, std::memory_order_release);
    }
}

VideoError VideoSystemV2::startStream() {
    VideoError err = pImpl_->startStream();
    if (err == VideoError::NONE) {
        is_streaming_.store(true, std::memory_order_release);
    }
    return err;
}

VideoError VideoSystemV2::stopStream() {
    VideoError err = pImpl_->stopStream();
    if (err == VideoError::NONE) {
        is_streaming_.store(false, std::memory_order_release);
    }
    return err;
}

VideoError VideoSystemV2::takePhoto(const std::string& filename, bool switch_encoder) {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    VideoError err = pImpl_->takePhoto(filename, switch_encoder);
    if (err == VideoError::NONE) {
        photo_capturing_.store(true, std::memory_order_release);
        // 注意：拍照完成后，Impl会自动重置photo_capturing_标志
    }
    return err;
}

VideoError VideoSystemV2::restoreH264Encoder() {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    return pImpl_->restoreH264Encoder();
}

VideoError VideoSystemV2::startRecord(const std::string& filename, int duration_sec) {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    VideoError err = pImpl_->startRecord(filename, duration_sec);
    if (err == VideoError::NONE) {
        is_recording_.store(true, std::memory_order_release);
    }
    return err;
}

VideoError VideoSystemV2::stopRecord() {
    VideoError err = pImpl_->stopRecord();
    if (err == VideoError::NONE) {
        is_recording_.store(false, std::memory_order_release);
    }
    return err;
}

void VideoSystemV2::setWebRTCCallback(WebRTCVideoCallback callback) {
    pImpl_->setWebRTCCallback(callback);
}

VideoError VideoSystemV2::startWebRTCStream() {
    VideoError err = pImpl_->startWebRTCStream();
    if (err == VideoError::NONE) {
        is_webrtc_streaming_.store(true, std::memory_order_release);
    }
    return err;
}

VideoError VideoSystemV2::stopWebRTCStream() {
    VideoError err = pImpl_->stopWebRTCStream();
    if (err == VideoError::NONE) {
        is_webrtc_streaming_.store(false, std::memory_order_release);
    }
    return err;
}

VideoError VideoSystemV2::startWebRTCMode() {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    // 设置主状态为WebRTC
    VideoError err = setMainState(VideoMainState::WEBRTC);
    if (err != VideoError::NONE) {
        return err;
    }
    
    // 启动视频流
    if (!isStreaming()) {
        err = startStream();
        if (err != VideoError::NONE) {
            return err;
        }
    }
    
    // 启动WebRTC推流
    err = startWebRTCStream();
    if (err != VideoError::NONE) {
        return err;
    }
    
    LOG_INFO("Camera", "WebRTC mode started");
    return VideoError::NONE;
}

VideoError VideoSystemV2::stopWebRTCMode() {
    stopWebRTCStream();
    setMainState(VideoMainState::NONE);
    
    LOG_INFO("Camera", "WebRTC mode stopped");
    return VideoError::NONE;
}

VideoError VideoSystemV2::setMainState(VideoMainState state) {
    VideoError err = pImpl_->setMainState(state);
    if (err == VideoError::NONE) {
        main_state_.store(state, std::memory_order_release);
    }
    return err;
}

void VideoSystemV2::setMainStateCallback(StateChangeCallback<VideoMainState> callback) {
    pImpl_->setMainStateCallback(callback);
}

void VideoSystemV2::setControlStateCallback(StateChangeCallback<VideoControlState> callback) {
    pImpl_->setControlStateCallback(callback);
}

VideoError VideoSystemV2::setEncodingParams(int bitrate, int gop) {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    return pImpl_->setEncodingParams(bitrate, gop);
}

VideoError VideoSystemV2::setBitrate(int bitrate_kbps) {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    return pImpl_->setBitrate(bitrate_kbps);
}

VideoError VideoSystemV2::setGOP(int gop) {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    return pImpl_->setGOP(gop);
}

VideoError VideoSystemV2::setJPEGQuality(int quality) {
    if (!isInitialized()) {
        return VideoError::NOT_INITIALIZED;
    }
    
    return pImpl_->setJPEGQuality(quality);
}

float VideoSystemV2::getCurrentFPS() const {
    return pImpl_->getCurrentFPS();
}

void VideoSystemV2::getStats(Stats& out_stats) const {
    pImpl_->getStats(out_stats);
}

void VideoSystemV2::resetStats() {
    pImpl_->stats_.frames_captured.store(0, std::memory_order_relaxed);
    pImpl_->stats_.frames_dropped.store(0, std::memory_order_relaxed);
    pImpl_->stats_.photos_taken.store(0, std::memory_order_relaxed);
    pImpl_->stats_.record_duration_ms.store(0, std::memory_order_relaxed);
    pImpl_->stats_.dma_frames.store(0, std::memory_order_relaxed);
    pImpl_->memory_pool_.resetStats();
    LOG_INFO("Camera", "Statistics reset");
}

void VideoSystemV2::logStats() const {
    pImpl_->logStats();
}

// ========================================================================
// ISP参数控制代理实现（转发到ISPWrapper）
// ========================================================================

VideoError VideoSystemV2::setExposureMode(opMode_t mode) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setExposureMode(mode);
}

VideoError VideoSystemV2::setExpGainRange(float min_gain, float max_gain) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setExpGainRange(min_gain, max_gain);
}

VideoError VideoSystemV2::setExpTimeRange(float min_time, float max_time) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setExpTimeRange(min_time, max_time);
}

VideoError VideoSystemV2::lockAE(bool lock) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->lockAE(lock);
}

VideoError VideoSystemV2::setWhiteBalanceMode(opMode_t mode) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setWhiteBalanceMode(mode);
}

VideoError VideoSystemV2::setWhiteBalanceGain(float r_gain, float b_gain) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setWhiteBalanceGain(r_gain, b_gain);
}

VideoError VideoSystemV2::setColorTemperature(unsigned int ct) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setColorTemperature(ct);
}

VideoError VideoSystemV2::lockAWB(bool lock) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->lockAWB(lock);
}

VideoError VideoSystemV2::setBrightness(unsigned int level) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setBrightness(level);
}

VideoError VideoSystemV2::setContrast(unsigned int level) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setContrast(level);
}

VideoError VideoSystemV2::setSaturation(unsigned int level) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setSaturation(level);
}

VideoError VideoSystemV2::setHue(unsigned int level) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setHue(level);
}

VideoError VideoSystemV2::setSharpness(unsigned int level) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setSharpness(level);
}

VideoError VideoSystemV2::setDehazeLevel(unsigned int level) {
    if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
        return VideoError::NOT_INITIALIZED;
    }
    return pImpl_->isp_->setDehazeLevel(level);
}

} // namespace camera
} // namespace media
} // namespace glasses

