/**
 * @file camera.cc
 * @brief 视频系统实现
 */

 #include "camera.hpp"
 #include "../../tool/log/log.hpp"
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
         LOG_WARN("VideoBuffer", "块数量限制为 %zu", MAX_BLOCKS);
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
     
     LOG_INFO("VideoBuffer", "固定池已初始化: %zu 块 × %zu 字节 = %zu MB",
              block_count, block_size, (block_count * block_size) / (1024 * 1024));
 }
 
 VideoMemoryPool::FixedPool::~FixedPool() {
     LOG_INFO("VideoBuffer", "固定池已销毁");
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
         LOG_ERROR("VideoBuffer", "无效的块索引: %d", index);
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
     LOG_INFO("VideoBuffer", "初始化DMA池...");
     
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
                 LOG_ERROR("VideoBuffer", "获取DMA块 %zu 的虚拟地址失败", i);
                 RK_MPI_SYS_Free(mb_blk);
                 blocks[i].mb_blk = nullptr;
             }
         } else {
             LOG_WARN("VideoBuffer", "分配DMA块 %zu 失败 (ret: 0x%x)", i, ret);
             break;
         }
     }
     
     LOG_INFO("VideoBuffer", "DMA池已初始化，共 %zu 块", MAX_DMA_BLOCKS);
 }
 
 VideoMemoryPool::DMAPool::~DMAPool() {
     LOG_INFO("VideoBuffer", "销毁DMA池...");
     
     std::lock_guard<std::mutex> lock(mutex);
     for (size_t i = 0; i < MAX_DMA_BLOCKS; i++) {
         if (blocks[i].mb_blk) {
             if (blocks[i].in_use) {
                 LOG_WARN("VideoBuffer", "DMA块 %zu 在销毁时仍在使用", i);
             }
             RK_MPI_SYS_Free(blocks[i].mb_blk);  
             blocks[i].mb_blk = nullptr;
             blocks[i].vir_addr = nullptr;
         }
     }
     
     LOG_INFO("VideoBuffer", "DMA池已销毁");
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
                     LOG_DEBUG("VideoBuffer", "DMA块 %d 已通过删除器释放", block_idx);
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
         LOG_DEBUG("VideoBuffer", "DMA块 %d 已释放", block_idx);
     } else {
         LOG_ERROR("VideoBuffer", "无效的DMA块索引: %d", block_idx);
     }
 }
 
 // VideoMemoryPool 主类实现
 VideoMemoryPool::VideoMemoryPool(const VideoMemoryPoolConfig& config)
     : config_(config) {
     
     LOG_INFO("VideoBuffer", "===========================================");
     LOG_INFO("VideoBuffer", "  初始化视频内存池");
     LOG_INFO("VideoBuffer", "===========================================");
     
     // 初始化固定池
     fixed_pool_ = std::make_unique<FixedPool>(config.fixed_block_size, config.fixed_block_count);
     
     // 初始化动态池
     dynamic_pool_ = std::make_unique<tool::memory::MemoryPool>(
         config.dynamic_pool_size,  // 初始大小
         config.alignment,           // 对齐
         1.5                         // 扩展因子
     );
     
     LOG_INFO("VideoBuffer", "动态池: %zu MB, 对齐: %zu 字节",
              config.dynamic_pool_size / (1024 * 1024), config.alignment);
     
     // 初始化DMA池（如果启用）
     if (config.enable_dma) {
         dma_pool_ = std::make_unique<DMAPool>(config.fixed_block_size);
         LOG_INFO("VideoBuffer", "DMA池: %zu 块 × %zu KB = %zu KB",
                  DMAPool::MAX_DMA_BLOCKS, config.fixed_block_size / 1024,
                  (DMAPool::MAX_DMA_BLOCKS * config.fixed_block_size) / 1024);
     }
     
     // DMA零拷贝状态
     if (config.enable_dma) {
         LOG_INFO("VideoBuffer", "DMA零拷贝: 已启用 ✅");
         LOG_INFO("VideoBuffer", "  性能: 无memcpy开销");
         LOG_INFO("VideoBuffer", "  注意: 固定/动态池作为后备");
     } else {
         LOG_INFO("VideoBuffer", "DMA零拷贝: 已禁用");
     }
     
     LOG_INFO("VideoBuffer", "===========================================");
 }
 
 VideoMemoryPool::~VideoMemoryPool() {
     LOG_INFO("VideoBuffer", "视频内存池正在关闭...");
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
     LOG_ERROR("VideoBuffer", "内存分配失败，大小: %zu", size);
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
     
     LOG_INFO("VideoBuffer", "=== 内存池统计 ===");
     LOG_INFO("VideoBuffer", "  总分配次数: %zu", total);
     
     if (total > 0) {
         LOG_INFO("VideoBuffer", "  固定池命中:   %zu (%.2f%%)", 
                  fixed_hits, (double)fixed_hits * 100.0 / total);
         LOG_INFO("VideoBuffer", "  动态池命中: %zu (%.2f%%)", 
                  dynamic_hits, (double)dynamic_hits * 100.0 / total);
         
         // DMA池统计（如果启用）
         if (dma_pool_) {
             LOG_INFO("VideoBuffer", "  DMA池命中:     %zu (%.2f%%) 🚀", 
                      dma_hits, (double)dma_hits * 100.0 / total);
         }
     }
     
     LOG_INFO("VideoBuffer", "  分配失败:          %zu", failures);
     
     // 性能分析和建议
     if (total > 100) {
         uint64_t pool_hits = fixed_hits + dynamic_hits + dma_hits;
         double total_hit_rate = (double)pool_hits * 100.0 / total;
         
         if (fixed_hits * 100 / total < 70) {
             LOG_WARN("VideoBuffer", "固定池命中率偏低，建议增加池大小");
         }
         
         if (dma_pool_ && dma_hits > 0) {
             LOG_INFO("VideoBuffer", "DMA零拷贝优化已激活！🔥");
         }
         
         LOG_INFO("VideoBuffer", "总体命中率: %.2f%%", total_hit_rate);
     }
 }
 
 // ============================================================================
 // RAII 资源包装器实现
 // ============================================================================
 
 // ISPWrapper 实现
 ISPWrapper::ISPWrapper(int camera_id, const std::string& iq_dir)
     : camera_id_(camera_id) {
     
     LOG_INFO("Camera", "使用直接AIQ API为相机 %d 初始化ISP", camera_id);
     
     // 停止RkLunch服务
     system("RkLunch-stop.sh 2>/dev/null");
     
     // 步骤1：枚举相机静态信息，获取sensor entity name
     rk_aiq_static_info_t static_info;
     memset(&static_info, 0, sizeof(static_info));
     
     if (rk_aiq_uapi2_sysctl_enumStaticMetas(camera_id, &static_info) != 0) {
         LOG_ERROR("Camera", "枚举相机静态元数据失败");
         return;
     }
     
     const char* sns_ent_name = static_info.sensor_info.sensor_name;
     LOG_INFO("Camera", "发现传感器: %s", sns_ent_name);
     
     // 步骤2：直接使用AIQ API初始化（不使用SAMPLE_COMM_ISP）
     aiq_ctx_ = rk_aiq_uapi2_sysctl_init(sns_ent_name, iq_dir.c_str(), nullptr, nullptr);
     
     if (!aiq_ctx_) {
         LOG_ERROR("Camera", "AIQ上下文初始化失败");
         return;
     }
     
     LOG_INFO("Camera", "✓ AIQ上下文创建成功");
     
     // 步骤3：准备并启动AIQ
     rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
     
     if (rk_aiq_uapi2_sysctl_prepare(aiq_ctx_, 0, 0, hdr_mode) != 0) {
         LOG_ERROR("Camera", "AIQ准备失败");
         rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
         aiq_ctx_ = nullptr;
         return;
     }
     
     if (rk_aiq_uapi2_sysctl_start(aiq_ctx_) != 0) {
         LOG_ERROR("Camera", "AIQ启动失败");
         rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
         aiq_ctx_ = nullptr;
         return;
     }
     
     valid_ = true;
     LOG_INFO("Camera", "✓ ISP已成功初始化，完整AIQ控制可用");
     LOG_INFO("Camera", "✓ ISP参数控制现已可用");
 }
 
 ISPWrapper::~ISPWrapper() {
     if (aiq_ctx_) {
         rk_aiq_uapi2_sysctl_stop(aiq_ctx_, false);
         rk_aiq_uapi2_sysctl_deinit(aiq_ctx_);
         LOG_INFO("Camera", "ISP/AIQ已停止并去初始化");
     }
 }
 
 // ========================================================================
 // ISP参数控制实现
 // ========================================================================
 
 VideoError ISPWrapper::setExposureMode(opMode_t mode) {
     if (!aiq_ctx_) {
         LOG_ERROR("ISP", "AIQ上下文未初始化");
         return VideoError::NOT_INITIALIZED;
     }
     
     XCamReturn ret = rk_aiq_uapi2_setExpMode(aiq_ctx_, mode);
     if (ret == XCAM_RETURN_NO_ERROR) {
         LOG_INFO("ISP", "✓ 曝光模式设置为: %s", mode == OP_AUTO ? "自动" : "手动");
         return VideoError::NONE;
     }
     
     LOG_ERROR("ISP", "设置曝光模式失败: %d", ret);
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
         LOG_INFO("ISP", "✓ 曝光增益范围设置为: [%.2f, %.2f]", min_gain, max_gain);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置曝光增益范围失败");
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
         LOG_INFO("ISP", "✓ 曝光时间范围设置为: [%.4f, %.4f]s", min_time, max_time);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置曝光时间范围失败");
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
         LOG_INFO("ISP", "✓ AE %s", lock ? "已锁定" : "已解锁");
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "%s AE失败", lock ? "锁定" : "解锁");
     return VideoError::RKMPI_ERROR;
 }
 
 VideoError ISPWrapper::setWhiteBalanceMode(opMode_t mode) {
     if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
     XCamReturn ret = rk_aiq_uapi2_setWBMode(aiq_ctx_, mode);
     if (ret == XCAM_RETURN_NO_ERROR) {
         LOG_INFO("ISP", "✓ 白平衡模式设置为: %s", mode == OP_AUTO ? "自动" : "手动");
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置白平衡模式失败");
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
         LOG_INFO("ISP", "✓ WB增益设置为: R=%.2f, B=%.2f", r_gain, b_gain);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置WB增益失败");
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
         LOG_INFO("ISP", "✓ 色温设置为: %uK", ct);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置色温失败");
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
         LOG_INFO("ISP", "✓ AWB %s", lock ? "已锁定" : "已解锁");
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "%s AWB失败", lock ? "锁定" : "解锁");
     return VideoError::RKMPI_ERROR;
 }
 
 VideoError ISPWrapper::setBrightness(unsigned int level) {
     if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
     XCamReturn ret = rk_aiq_uapi2_setBrightness(aiq_ctx_, level);
     if (ret == XCAM_RETURN_NO_ERROR) {
         LOG_INFO("ISP", "✓ 亮度设置为: %u", level);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置亮度失败");
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
         LOG_INFO("ISP", "✓ 对比度设置为: %u", level);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置对比度失败");
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
         LOG_INFO("ISP", "✓ 饱和度设置为: %u", level);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置饱和度失败");
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
         LOG_INFO("ISP", "✓ 色调设置为: %u", level);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置色调失败");
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
         LOG_INFO("ISP", "✓ 锐度设置为: %u", level);
         return VideoError::NONE;
     }
     LOG_ERROR("ISP", "设置锐度失败");
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
         LOG_ERROR("ISP", "启用/禁用去雾失败");
         return VideoError::RKMPI_ERROR;
     }
     if (level > 0) {
         ret = rk_aiq_uapi2_setMDehazeStrth(aiq_ctx_, level);
         if (ret == XCAM_RETURN_NO_ERROR) {
             LOG_INFO("ISP", "✓ 去雾强度设置为: %u", level);
             return VideoError::NONE;
         }
         LOG_ERROR("ISP", "设置去雾强度失败");
         return VideoError::RKMPI_ERROR;
     }
     LOG_INFO("ISP", "✓ 去雾已禁用");
     return VideoError::NONE;
 }
 
 VideoError ISPWrapper::getDehazeLevel(unsigned int& level) const {
     if (!aiq_ctx_) return VideoError::NOT_INITIALIZED;
     XCamReturn ret = rk_aiq_uapi2_getMDehazeStrth(aiq_ctx_, &level);
     return (ret == XCAM_RETURN_NO_ERROR) ? VideoError::NONE : VideoError::RKMPI_ERROR;
 }
 
 // VIDeviceWrapper 实现
 VIDeviceWrapper::VIDeviceWrapper(int dev_id) : dev_id_(dev_id) {
     LOG_INFO("Camera", "初始化VI设备 %d", dev_id);
     
     VI_DEV_ATTR_S stDevAttr;
     VI_DEV_BIND_PIPE_S stBindPipe;
     std::memset(&stDevAttr, 0, sizeof(stDevAttr));
     std::memset(&stBindPipe, 0, sizeof(stBindPipe));
     
     // 检查设备配置状态
     RK_S32 ret = RK_MPI_VI_GetDevAttr(dev_id, &stDevAttr);
     if (ret == RK_ERR_VI_NOT_CONFIG) {
         ret = RK_MPI_VI_SetDevAttr(dev_id, &stDevAttr);
         if (ret != RK_SUCCESS) {
             LOG_ERROR("Camera", "RK_MPI_VI_SetDevAttr 失败: 0x%x", ret);
             return;
         }
     }
     
     // 检查设备使能状态
     ret = RK_MPI_VI_GetDevIsEnable(dev_id);
     if (ret != RK_SUCCESS) {
         // 使能设备
         ret = RK_MPI_VI_EnableDev(dev_id);
         if (ret != RK_SUCCESS) {
             LOG_ERROR("Camera", "RK_MPI_VI_EnableDev 失败: 0x%x", ret);
             return;
         }
         
         // 绑定设备与管道
         stBindPipe.u32Num = 1;
         stBindPipe.PipeId[0] = dev_id;
         ret = RK_MPI_VI_SetDevBindPipe(dev_id, &stBindPipe);
         if (ret != RK_SUCCESS) {
             LOG_ERROR("Camera", "RK_MPI_VI_SetDevBindPipe 失败: 0x%x", ret);
             RK_MPI_VI_DisableDev(dev_id);
             return;
         }
     }
     
     valid_ = true;
     LOG_INFO("Camera", "✓ VI设备初始化成功");
 }
 
 VIDeviceWrapper::~VIDeviceWrapper() {
     if (valid_) {
         RK_MPI_VI_DisableDev(dev_id_);
         LOG_INFO("Camera", "VI设备 %d 已禁用", dev_id_);
     }
 }
 
 // VIChannelWrapper 实现
 VIChannelWrapper::VIChannelWrapper(int dev_id, int chn_id, int width, int height)
     : dev_id_(dev_id), chn_id_(chn_id) {
     
     LOG_INFO("Camera", "初始化VI通道 %d (%dx%d)", chn_id, width, height);
     
     VI_CHN_ATTR_S vi_chn_attr;
     std::memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
     
     vi_chn_attr.stIspOpt.u32BufCount = 2;
     vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
     vi_chn_attr.stSize.u32Width = width;
     vi_chn_attr.stSize.u32Height = height;
     vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
     vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
     vi_chn_attr.u32Depth = 0;      // 绑定模式为0(自动采帧)，如果不为0则为采取指定帧数(0代表无限)
     
     RK_S32 ret = RK_MPI_VI_SetChnAttr(dev_id, chn_id, &vi_chn_attr);
     if (ret != RK_SUCCESS) {
         LOG_ERROR("Camera", "RK_MPI_VI_SetChnAttr 失败: 0x%x", ret);
         return;
     }
     
     ret = RK_MPI_VI_EnableChn(dev_id, chn_id);
     if (ret != RK_SUCCESS) {
         LOG_ERROR("Camera", "RK_MPI_VI_EnableChn 失败: 0x%x", ret);
         return;
     }
     
     valid_ = true;
     LOG_INFO("Camera", "✓ VI通道初始化成功");
 }
 
 VIChannelWrapper::~VIChannelWrapper() {
     if (valid_) {
         RK_MPI_VI_DisableChn(dev_id_, chn_id_);
         LOG_INFO("Camera", "VI通道 %d 已禁用", chn_id_);
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
     
     LOG_INFO("Camera", "初始化VENC通道 %d (%dx%d, 格式=%d)", 
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
         LOG_ERROR("Camera", "RK_MPI_VENC_CreateChn 失败: 0x%x", ret);
         return;
     }
     
     VENC_RECV_PIC_PARAM_S stRecvParam;
     std::memset(&stRecvParam, 0, sizeof(stRecvParam));
     stRecvParam.s32RecvPicNum = -1;
     
     ret = RK_MPI_VENC_StartRecvFrame(chn_id, &stRecvParam);
     if (ret != RK_SUCCESS) {
         LOG_ERROR("Camera", "RK_MPI_VENC_StartRecvFrame 失败: 0x%x", ret);
         RK_MPI_VENC_DestroyChn(chn_id);
         return;
     }
     
     valid_ = true;
     LOG_INFO("Camera", "✓ VENC初始化成功");
 }
 
 VENCWrapper::~VENCWrapper() {
     if (valid_) {
         RK_MPI_VENC_StopRecvFrame(chn_id_);
         RK_MPI_VENC_DestroyChn(chn_id_);
         LOG_INFO("Camera", "VENC通道 %d 已销毁", chn_id_);
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
         LOG_WARN("VideoBuffer", "多包帧 (%u 包)，无法使用零拷贝", 
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
             LOG_ERROR("DMABuffer", "RK_MPI_VENC_ReleaseStream 失败: 0x%x", ret);
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
     
     LOG_INFO("Camera", "设置码率为 %d kbps", bitrate_kbps);
     
     // 获取当前编码器属性
     VENC_CHN_ATTR_S stVencChnAttr;
     RK_S32 ret = RK_MPI_VENC_GetChnAttr(chn_id_, &stVencChnAttr);
     if (ret != RK_SUCCESS) {
         LOG_ERROR("Camera", "获取VENC通道属性失败: 0x%x", ret);
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
             LOG_WARN("Camera", "JPEG格式不支持设置码率");
             return VideoError::INVALID_PARAM;
     }
     
     // 应用新属性
     ret = RK_MPI_VENC_SetChnAttr(chn_id_, &stVencChnAttr);
     if (ret == RK_SUCCESS) {
         current_bitrate_ = bitrate_kbps;
         LOG_INFO("Camera", "✓ 码率已更新为 %d kbps", bitrate_kbps);
         return VideoError::NONE;
     } else {
         LOG_ERROR("Camera", "设置码率失败: 0x%x", ret);
         return VideoError::ENCODE_FAILED;
     }
 }
 
 VideoError VENCWrapper::setGOP(int gop) {
     if (!valid_) {
         return VideoError::NOT_INITIALIZED;
     }
     
     LOG_INFO("Camera", "设置GOP为 %d", gop);
     
     // 获取当前编码器属性
     VENC_CHN_ATTR_S stVencChnAttr;
     RK_S32 ret = RK_MPI_VENC_GetChnAttr(chn_id_, &stVencChnAttr);
     if (ret != RK_SUCCESS) {
         LOG_ERROR("Camera", "获取VENC通道属性失败: 0x%x", ret);
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
             LOG_INFO("Camera", "JPEG格式始终使用GOP=1");
             current_gop_ = 1;
             return VideoError::NONE;
     }
     
     // 应用新属性
     ret = RK_MPI_VENC_SetChnAttr(chn_id_, &stVencChnAttr);
     if (ret == RK_SUCCESS) {
         current_gop_ = gop;
         LOG_INFO("Camera", "✓ GOP已更新为 %d", gop);
         return VideoError::NONE;
     } else {
         LOG_ERROR("Camera", "设置GOP失败: 0x%x", ret);
         return VideoError::ENCODE_FAILED;
     }
 }
 
 VideoError VENCWrapper::setJPEGQuality(int quality) {
     if (!valid_) {
         return VideoError::NOT_INITIALIZED;
     }
     
     // 限制JPEG质量范围 [1, 100]
     quality = std::max(1, std::min(100, quality));
     
     LOG_INFO("Camera", "设置JPEG质量为 %d", quality);
     
     if (current_format_ != EncodeFormat::JPEG) {
         LOG_WARN("Camera", "当前格式不是JPEG，质量设置已保存用于下次JPEG编码");
         current_jpeg_quality_ = quality;
         return VideoError::NONE;
     }
     
     // ✅ 使用通用的VENC_CHN_ATTR_S方法设置JPEG质量
     VENC_CHN_ATTR_S stVencChnAttr;
     RK_S32 ret = RK_MPI_VENC_GetChnAttr(chn_id_, &stVencChnAttr);
     if (ret != RK_SUCCESS) {
         LOG_ERROR("Camera", "获取VENC通道属性失败: 0x%x", ret);
         return VideoError::ENCODE_FAILED;
     }
     
     // 设置JPEG质量参数
     if (stVencChnAttr.stRcAttr.enRcMode == VENC_RC_MODE_MJPEGFIXQP) {
         int qp = std::max(1, std::min(99, (quality * 99 + 50) / 100));
         stVencChnAttr.stRcAttr.stMjpegFixQp.u32Qfactor = qp;
         
         ret = RK_MPI_VENC_SetChnAttr(chn_id_, &stVencChnAttr);
         if (ret == RK_SUCCESS) {
             current_jpeg_quality_ = quality;
             LOG_INFO("Camera", "✓ JPEG质量已更新为 %d (QP: %d)", quality, qp);
             return VideoError::NONE;
         } else {
             LOG_ERROR("Camera", "通过ChnAttr设置JPEG质量失败: 0x%x", ret);
         }
     } else {
         LOG_WARN("Camera", "JPEG编码器不在FixQP模式，无法调整质量");
     }
     
     current_jpeg_quality_ = quality;
     LOG_INFO("Camera", "✓ JPEG质量设置已保存: %d", quality);
     return VideoError::NONE;
 }
 
 // FileWrapper 实现
 FileWrapper::FileWrapper(const std::string& filename, bool write)
     : filename_(filename) {
     
     const char* mode = write ? "wb" : "rb";
     file_ = fopen(filename.c_str(), mode);
     
     if (file_) {
         valid_ = true;
         LOG_INFO("Camera", "文件已打开: %s", filename.c_str());
     } else {
         LOG_ERROR("Camera", "打开文件失败: %s", filename.c_str());
     }
 }
 
 FileWrapper::~FileWrapper() {
     if (file_) {
         fclose(file_);
         LOG_INFO("Camera", "文件已关闭: %s", filename_.c_str());
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
 // VideoSystem::Impl 实现类（Pimpl模式）
 // ============================================================================
 
 class VideoSystem::Impl {
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
         LOG_INFO("Camera", "VideoSystem::Impl 已构造 (DMA: %s)", 
                  config.enable_dma_zero_copy ? "已启用" : "已禁用");
     }
     
     ~Impl() {
         LOG_INFO("Camera", "VideoSystem::Impl 正在析构...");
         shutdown();
     }
     
     // ========================================================================
     // 初始化和关闭
     // ========================================================================
     
     VideoError initialize(std::shared_ptr<sync_context_t> sync_ctx) {
         std::lock_guard<std::mutex> lock(mutex_);
         
         LOG_INFO("Camera", "========================================");
         LOG_INFO("Camera", "  初始化视频系统");
         LOG_INFO("Camera", "========================================");
         LOG_INFO("Camera", "配置:");
         LOG_INFO("Camera", "  分辨率: %dx%d @ %d fps", 
                  config_.width, config_.height, config_.fps);
         LOG_INFO("Camera", "  格式: %d, 码率: %d kbps, GOP: %d",
                  static_cast<int>(config_.format), config_.bitrate, config_.gop);
         
         sync_ctx_ = sync_ctx;
         
         // 创建输出目录
         mkdir(config_.photo_path.c_str(), 0755);
         mkdir(config_.record_path.c_str(), 0755);
         
         // 步骤1: 初始化RKMPI系统
         LOG_INFO("Camera", "步骤1: 初始化RKMPI系统...");
         if (RK_MPI_SYS_Init() != RK_SUCCESS) {
             LOG_ERROR("Camera", "RK_MPI_SYS_Init 失败");
             return VideoError::INIT_FAILED;
         }
         rkmpi_initialized_ = true;
         
         // 步骤2: 初始化ISP
         LOG_INFO("Camera", "步骤2: 初始化ISP...");
         isp_ = std::make_unique<ISPWrapper>(0, ISP_PATH);
         if (!isp_->isValid()) {
             LOG_ERROR("Camera", "ISP初始化失败");
             return VideoError::INIT_FAILED;
         }
         
         // 步骤3: 初始化VI设备
         LOG_INFO("Camera", "步骤3: 初始化VI设备...");
         vi_dev_ = std::make_unique<VIDeviceWrapper>(0);
         if (!vi_dev_->isValid()) {
             LOG_ERROR("Camera", "VI设备初始化失败");
             return VideoError::INIT_FAILED;
         }
         
         // 步骤4: 初始化VI通道
         LOG_INFO("Camera", "步骤4: 初始化VI通道...");
         vi_chn_ = std::make_unique<VIChannelWrapper>(0, 0, config_.width, config_.height);
         if (!vi_chn_->isValid()) {
             LOG_ERROR("Camera", "VI通道初始化失败");
             return VideoError::INIT_FAILED;
         }
         
         // 步骤5: 初始化VENC编码器
         LOG_INFO("Camera", "步骤5: 初始化VENC编码器...");
         venc_ = std::make_unique<VENCWrapper>(
             0, config_.width, config_.height, 
             config_.format, config_.bitrate, config_.gop
         );
         if (!venc_->isValid()) {
             LOG_ERROR("Camera", "VENC初始化失败");
             return VideoError::INIT_FAILED;
         }
         
         // 步骤6: 绑定VI到VENC
         LOG_INFO("Camera", "步骤6: 绑定VI到VENC...");
         MPP_CHN_S stSrcChn, stDestChn;
         stSrcChn.enModId = RK_ID_VI;
         stSrcChn.s32DevId = 0;
         stSrcChn.s32ChnId = 0;
         
         stDestChn.enModId = RK_ID_VENC;
         stDestChn.s32DevId = 0;
         stDestChn.s32ChnId = 0;
         
         if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn) != RK_SUCCESS) {
             LOG_ERROR("Camera", "模块绑定失败");
             return VideoError::INIT_FAILED;
         }
         modules_bound_ = true;
         
         LOG_INFO("Camera", "========================================");
         LOG_INFO("Camera", "视频系统初始化成功！");
         LOG_INFO("Camera", "========================================");
         
         return VideoError::NONE;
     }
     
     void shutdown() {
         std::lock_guard<std::mutex> lock(mutex_);
         
         LOG_INFO("Camera", "关闭视频系统...");
         
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
         
         LOG_INFO("Camera", "视频系统关闭完成");
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
             LOG_WARN("Camera", "流已启动");
             return VideoError::ALREADY_STARTED;
         }
         
         LOG_INFO("Camera", "启动视频流...");
         
         quit_flag_.store(false);
         stream_thread_ = std::make_unique<std::thread>(&Impl::streamProcessThread, this);
         
         LOG_INFO("Camera", "✓ 视频流已启动");
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
         
         LOG_INFO("Camera", "停止视频流...");
         
         quit_flag_.store(true);
         if (stream_thread_->joinable()) {
             stream_thread_->join();
         }
         stream_thread_.reset();
         
         LOG_INFO("Camera", "✓ 视频流已停止");
         return VideoError::NONE;
     }
     
     void streamProcessThread() {
         LOG_INFO("Camera", "流处理线程已启动 (DMA: %s)", 
                  config_.enable_dma_zero_copy ? "已启用" : "已禁用");
         
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
                     LOG_ERROR("Camera", "获取流失败: %d", static_cast<int>(err));
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
                 LOG_INFO("Camera", "当前FPS: %.2f", current_fps_);
                 #endif
             }
         }
         
         LOG_INFO("Camera", "流处理线程已停止");
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
                     LOG_INFO("Camera", "照片已保存: %s (%zu 字节)", 
                              filename.c_str(), frame->size);
                     stats_.photos_taken.fetch_add(1, std::memory_order_relaxed);
                 } else {
                     LOG_ERROR("Camera", "写入照片文件失败");
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
             LOG_INFO("Camera", "拍照完成，将恢复H264编码器");
         }
     }
     
     VideoError switchToJPEGEncoder() {
         if (!venc_) {
             return VideoError::INVALID_STATE;
         }
         
         LOG_INFO("Camera", "切换到JPEG编码器...");
         
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
             LOG_ERROR("Camera", "创建JPEG编码器失败");
             return VideoError::INIT_FAILED;
         }
         
         // 应用JPEG质量设置
         VideoError quality_err = venc_->setJPEGQuality(config_.quality);
         if (quality_err != VideoError::NONE) {
             LOG_WARN("Camera", "设置JPEG质量失败");
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
             LOG_ERROR("Camera", "绑定模块失败");
             return VideoError::INIT_FAILED;
         }
         modules_bound_ = true;
         
         LOG_INFO("Camera", "✓ 已切换到JPEG编码器");
         return VideoError::NONE;
     }
     
     VideoError switchToH264Encoder() {
         if (!venc_) {
             return VideoError::INVALID_STATE;
         }
         
         LOG_INFO("Camera", "切换回H264编码器...");
         
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
             LOG_ERROR("Camera", "创建H264编码器失败");
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
             LOG_ERROR("Camera", "绑定模块失败");
             return VideoError::INIT_FAILED;
         }
         modules_bound_ = true;
         
         LOG_INFO("Camera", "✓ 已切换回H264编码器");
         return VideoError::NONE;
     }
     
     bool needsRestoreEncoder() const {
         return photo_need_restore_encoder_;
     }
     
     VideoError restoreH264Encoder() {
         if (!photo_need_restore_encoder_) {
             return VideoError::NONE;
         }
         
         LOG_INFO("Camera", "恢复H264编码器...");
         
         bool was_streaming = stream_thread_ && stream_thread_->joinable();
         
         // 停止视频流
         if (was_streaming) {
             stopStreamInternal();
         }
         
         // 切换回H264编码器
         VideoError err = switchToH264Encoder();
         if (err != VideoError::NONE) {
             LOG_ERROR("Camera", "恢复H264编码器失败");
             return err;
         }
         
         // 重新启动视频流
         if (was_streaming) {
             startStreamInternal();
         }
         
         photo_need_restore_encoder_ = false;
         LOG_INFO("Camera", "✓ H264编码器已恢复");
         
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
                 LOG_INFO("Camera", "为编码器切换停止流...");
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
                 LOG_INFO("Camera", "使用JPEG编码器重启流...");
                 startStreamInternal();
                 // 等待流稳定
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
             }
         }
         
         photo_capturing_.store(true, std::memory_order_release);
         
         LOG_INFO("Camera", "拍照已开始");
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
                     LOG_INFO("Camera", "录像时长已达到，正在停止...");
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
         
         LOG_INFO("Camera", "录像已启动: %s", record_filename.c_str());
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
             LOG_INFO("Camera", "录像已停止: %s", record_file_->getFilename().c_str());
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
     
     void setWebRTCVideoCallback(WebRTCVideoCallback callback) {
         std::lock_guard<std::mutex> lock(webrtc_mutex_);
         webrtc_callback_ = callback;
         LOG_INFO("Camera", "WebRTC回调已设置");
     }
     
     VideoError startWebRTCStream() {
         if (is_webrtc_streaming_.load(std::memory_order_acquire)) {
             return VideoError::ALREADY_STARTED;
         }
         
         std::lock_guard<std::mutex> lock(webrtc_mutex_);
         
         if (!webrtc_callback_) {
             LOG_ERROR("Camera", "WebRTC回调未设置");
             return VideoError::INVALID_STATE;
         }
         
         is_webrtc_streaming_.store(true, std::memory_order_release);
         LOG_INFO("Camera", "WebRTC流已启动");
         
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
         LOG_INFO("Camera", "WebRTC流已停止");
         
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
         
         LOG_INFO("Camera", "主状态: %d → %d", 
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
     
     void getStats(VideoSystem::Stats& out_stats) const {
         memory_pool_.getStats(out_stats.mem_stats);
         out_stats.frames_captured = stats_.frames_captured.load(std::memory_order_relaxed);
         out_stats.frames_dropped = stats_.frames_dropped.load(std::memory_order_relaxed);
         out_stats.photos_taken = stats_.photos_taken.load(std::memory_order_relaxed);
         out_stats.record_duration_ms = stats_.record_duration_ms.load(std::memory_order_relaxed);
     }
     
     void logStats() const {
         uint64_t total_frames = stats_.frames_captured.load(std::memory_order_relaxed);
         uint64_t dma_frames = stats_.dma_frames.load(std::memory_order_relaxed);
         
         LOG_INFO("Camera", "=== 视频系统统计 ===");
         LOG_INFO("Camera", "  已采集帧数: %zu", total_frames);
         
         // DMA零拷贝统计
         if (config_.enable_dma_zero_copy && total_frames > 0) {
             double dma_rate = (double)dma_frames * 100.0 / total_frames;
             LOG_INFO("Camera", "  DMA零拷贝:   %zu (%.2f%%) 🚀", dma_frames, dma_rate);
             LOG_INFO("Camera", "  内存拷贝:     %zu (%.2f%%)", 
                      total_frames - dma_frames, 100.0 - dma_rate);
         }
         
         LOG_INFO("Camera", "  丢弃帧数:  %zu", 
                  stats_.frames_dropped.load(std::memory_order_relaxed));
         LOG_INFO("Camera", "  拍照次数:    %zu", 
                  stats_.photos_taken.load(std::memory_order_relaxed));
         LOG_INFO("Camera", "  录像时长: %zu ms", 
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
             LOG_ERROR("Camera", "VENC未初始化");
             return VideoError::NOT_INITIALIZED;
         }
         
         LOG_INFO("Camera", "设置编码参数: 码率=%d kbps, GOP=%d", bitrate, gop);
         
         // 设置码率
         VideoError err = venc_->setBitrate(bitrate);
         if (err != VideoError::NONE) {
             LOG_ERROR("Camera", "设置码率失败");
             return err;
         }
         
         // 设置GOP
         err = venc_->setGOP(gop);
         if (err != VideoError::NONE) {
             LOG_ERROR("Camera", "设置GOP失败");
             return err;
         }
         
         // 更新配置
         config_.bitrate = bitrate;
         config_.gop = gop;
         
         LOG_INFO("Camera", "✓ 编码参数更新成功");
         return VideoError::NONE;
     }
     
     VideoError setBitrate(int bitrate_kbps) {
         std::lock_guard<std::mutex> lock(mutex_);
         
         if (!venc_ || !venc_->isValid()) {
             LOG_ERROR("Camera", "VENC未初始化");
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
             LOG_ERROR("Camera", "VENC未初始化");
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
         LOG_INFO("Camera", "JPEG质量设置为 %d", quality);
         
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
 // VideoSystem 公开接口实现
 // ============================================================================
 
 VideoSystem::VideoSystem(const VideoConfig& config)
     : pImpl_(std::make_unique<Impl>(config, photo_capturing_, is_recording_, is_webrtc_streaming_)) {
 }
 
 VideoSystem::~VideoSystem() {
     shutdown();
 }
 
 VideoError VideoSystem::initialize(std::shared_ptr<sync_context_t> sync_ctx) {
     VideoError err = pImpl_->initialize(sync_ctx);
     if (err == VideoError::NONE) {
         is_initialized_.store(true, std::memory_order_release);
     }
     return err;
 }
 
 void VideoSystem::shutdown() {
     if (is_initialized_.load(std::memory_order_acquire)) {
         pImpl_->shutdown();
         is_initialized_.store(false, std::memory_order_release);
     }
 }
 
 VideoError VideoSystem::startStream() {
     VideoError err = pImpl_->startStream();
     if (err == VideoError::NONE) {
         is_streaming_.store(true, std::memory_order_release);
     }
     return err;
 }
 
 VideoError VideoSystem::stopStream() {
     VideoError err = pImpl_->stopStream();
     if (err == VideoError::NONE) {
         is_streaming_.store(false, std::memory_order_release);
     }
     return err;
 }
 
 VideoError VideoSystem::takePhoto(const std::string& filename, bool switch_encoder) {
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
 
 VideoError VideoSystem::restoreH264Encoder() {
     if (!isInitialized()) {
         return VideoError::NOT_INITIALIZED;
     }
     
     return pImpl_->restoreH264Encoder();
 }
 
 VideoError VideoSystem::startRecord(const std::string& filename, int duration_sec) {
     if (!isInitialized()) {
         return VideoError::NOT_INITIALIZED;
     }
     
     VideoError err = pImpl_->startRecord(filename, duration_sec);
     if (err == VideoError::NONE) {
         is_recording_.store(true, std::memory_order_release);
     }
     return err;
 }
 
 VideoError VideoSystem::stopRecord() {
     VideoError err = pImpl_->stopRecord();
     if (err == VideoError::NONE) {
         is_recording_.store(false, std::memory_order_release);
     }
     return err;
 }
 
 void VideoSystem::setWebRTCVideoCallback(WebRTCVideoCallback callback) {
     pImpl_->setWebRTCVideoCallback(callback);
 }
 
 VideoError VideoSystem::startWebRTCStream() {
     VideoError err = pImpl_->startWebRTCStream();
     if (err == VideoError::NONE) {
         is_webrtc_streaming_.store(true, std::memory_order_release);
     }
     return err;
 }
 
 VideoError VideoSystem::stopWebRTCStream() {
     VideoError err = pImpl_->stopWebRTCStream();
     if (err == VideoError::NONE) {
         is_webrtc_streaming_.store(false, std::memory_order_release);
     }
     return err;
 }
 
 VideoError VideoSystem::startWebRTCMode() {
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
     
     LOG_INFO("Camera", "WebRTC模式已启动");
     return VideoError::NONE;
 }
 
 VideoError VideoSystem::stopWebRTCMode() {
     stopWebRTCStream();
     setMainState(VideoMainState::NONE);
     
     LOG_INFO("Camera", "WebRTC模式已停止");
     return VideoError::NONE;
 }
 
 VideoError VideoSystem::setMainState(VideoMainState state) {
     VideoError err = pImpl_->setMainState(state);
     if (err == VideoError::NONE) {
         main_state_.store(state, std::memory_order_release);
     }
     return err;
 }
 
 void VideoSystem::setMainStateCallback(StateChangeCallback<VideoMainState> callback) {
     pImpl_->setMainStateCallback(callback);
 }
 
 void VideoSystem::setControlStateCallback(StateChangeCallback<VideoControlState> callback) {
     pImpl_->setControlStateCallback(callback);
 }
 
 VideoError VideoSystem::setEncodingParams(int bitrate, int gop) {
     if (!isInitialized()) {
         return VideoError::NOT_INITIALIZED;
     }
     
     return pImpl_->setEncodingParams(bitrate, gop);
 }
 
 VideoError VideoSystem::setBitrate(int bitrate_kbps) {
     if (!isInitialized()) {
         return VideoError::NOT_INITIALIZED;
     }
     
     return pImpl_->setBitrate(bitrate_kbps);
 }
 
 VideoError VideoSystem::setGOP(int gop) {
     if (!isInitialized()) {
         return VideoError::NOT_INITIALIZED;
     }
     
     return pImpl_->setGOP(gop);
 }
 
 VideoError VideoSystem::setJPEGQuality(int quality) {
     if (!isInitialized()) {
         return VideoError::NOT_INITIALIZED;
     }
     
     return pImpl_->setJPEGQuality(quality);
 }
 
 float VideoSystem::getCurrentFPS() const {
     return pImpl_->getCurrentFPS();
 }
 
 void VideoSystem::getStats(Stats& out_stats) const {
     pImpl_->getStats(out_stats);
 }
 
 void VideoSystem::resetStats() {
     pImpl_->stats_.frames_captured.store(0, std::memory_order_relaxed);
     pImpl_->stats_.frames_dropped.store(0, std::memory_order_relaxed);
     pImpl_->stats_.photos_taken.store(0, std::memory_order_relaxed);
     pImpl_->stats_.record_duration_ms.store(0, std::memory_order_relaxed);
     pImpl_->stats_.dma_frames.store(0, std::memory_order_relaxed);
     pImpl_->memory_pool_.resetStats();
     LOG_INFO("Camera", "统计数据已重置");
 }
 
 void VideoSystem::logStats() const {
     pImpl_->logStats();
 }
 
 // ========================================================================
 // ISP参数控制代理实现（转发到ISPWrapper）
 // ========================================================================
 
 VideoError VideoSystem::setExposureMode(opMode_t mode) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setExposureMode(mode);
 }
 
 VideoError VideoSystem::setExpGainRange(float min_gain, float max_gain) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setExpGainRange(min_gain, max_gain);
 }
 
 VideoError VideoSystem::setExpTimeRange(float min_time, float max_time) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setExpTimeRange(min_time, max_time);
 }
 
 VideoError VideoSystem::lockAE(bool lock) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->lockAE(lock);
 }
 
 VideoError VideoSystem::setWhiteBalanceMode(opMode_t mode) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setWhiteBalanceMode(mode);
 }
 
 VideoError VideoSystem::setWhiteBalanceGain(float r_gain, float b_gain) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setWhiteBalanceGain(r_gain, b_gain);
 }
 
 VideoError VideoSystem::setColorTemperature(unsigned int ct) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setColorTemperature(ct);
 }
 
 VideoError VideoSystem::lockAWB(bool lock) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->lockAWB(lock);
 }
 
 VideoError VideoSystem::setBrightness(unsigned int level) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setBrightness(level);
 }
 
 VideoError VideoSystem::setContrast(unsigned int level) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setContrast(level);
 }
 
 VideoError VideoSystem::setSaturation(unsigned int level) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setSaturation(level);
 }
 
 VideoError VideoSystem::setHue(unsigned int level) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setHue(level);
 }
 
 VideoError VideoSystem::setSharpness(unsigned int level) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setSharpness(level);
 }
 
 VideoError VideoSystem::setDehazeLevel(unsigned int level) {
     if (!isInitialized() || !pImpl_->isp_ || !pImpl_->isp_->isValid()) {
         return VideoError::NOT_INITIALIZED;
     }
     return pImpl_->isp_->setDehazeLevel(level);
 }
 
 } // namespace camera
 } // namespace media
 } // namespace app
 
 