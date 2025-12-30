/**
 * @file rknn.cc
 * @brief RKNN模型基础封装类实现
 */

#include "rknn.hpp"
#include "rknn_config.hpp"
#include <cstring>

namespace app
{
    namespace rknn
    {
        namespace
        {
            constexpr const char* LOG_TAG_RKNN_POOL  = "RKNN_MEM_POOL";
            constexpr double      BYTES_PER_MEGABYTE = 1024.0 * 1024.0;
        } // namespace

        // ============================================================================
        // RKNNMemoryPool::FixedPool 实现
        // ============================================================================

        RKNNMemoryPool::FixedPool::FixedPool(size_t block_sz, size_t block_cnt)
            : block_size(block_sz), block_count(block_cnt)
        {
            if (block_count > MemoryPoolConfig::MAX_BLOCKS)
            {
                block_count = MemoryPoolConfig::MAX_BLOCKS;
                LOG_WARN(LOG_TAG_RKNN_POOL, "块数量限制为 %zu", MemoryPoolConfig::MAX_BLOCKS);
            }

            for (auto& bitmap_word : allocation_bitmap_)
                bitmap_word.store(0, std::memory_order_relaxed);

            buffer.resize(block_size * block_count);

            LOG_INFO(LOG_TAG_RKNN_POOL, "固定池: %zu块 × %zu字节 = %.2fMB", block_count, block_size,
                     static_cast<double>(block_count * block_size) / BYTES_PER_MEGABYTE);
        }

        RKNNMemoryPool::FixedPool::~FixedPool() = default;

        int RKNNMemoryPool::FixedPool::allocateBlock()
        {
            int bitmap_count =
                static_cast<int>((block_count + MemoryPoolConfig::BITS_PER_WORD - 1) /
                                 MemoryPoolConfig::BITS_PER_WORD);

            for (int bitmap_index = 0; bitmap_index < bitmap_count; bitmap_index++)
            {
                auto&    bitmap_atomic = allocation_bitmap_[bitmap_index];
                uint64_t bitmap        = bitmap_atomic.load(std::memory_order_acquire);
                int base_index = bitmap_index * static_cast<int>(MemoryPoolConfig::BITS_PER_WORD);
                int max_blocks = std::min(static_cast<int>(MemoryPoolConfig::BITS_PER_WORD),
                                          static_cast<int>(block_count) - base_index);

                if (max_blocks <= 0)
                    break;

                while (bitmap != UINT64_MAX)
                {
                    uint64_t inverted = ~bitmap;
                    if (inverted == 0)
                        break;

                    int free_bit = __builtin_ctzll(inverted);
                    if (free_bit >= max_blocks)
                        break;

                    uint64_t new_bitmap = bitmap | (1ULL << free_bit);
                    if (bitmap_atomic.compare_exchange_weak(bitmap, new_bitmap,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
                        return base_index + free_bit;
                }
            }
            return -1;
        }

        void RKNNMemoryPool::FixedPool::deallocateBlock(int index)
        {
            if (index < 0 || index >= static_cast<int>(block_count))
            {
                LOG_ERROR(LOG_TAG_RKNN_POOL, "无效的块索引: %d", index);
                return;
            }

            int      bitmap_index = index / static_cast<int>(MemoryPoolConfig::BITS_PER_WORD);
            int      bit_position = index % static_cast<int>(MemoryPoolConfig::BITS_PER_WORD);
            uint64_t mask         = ~(1ULL << bit_position);

            allocation_bitmap_[bitmap_index].fetch_and(mask, std::memory_order_release);
        }

        uint8_t* RKNNMemoryPool::FixedPool::getBlockPtr(int index) const
        {
            if (index < 0 || index >= static_cast<int>(block_count))
                return nullptr;
            return const_cast<uint8_t*>(buffer.data() + (static_cast<size_t>(index) * block_size));
        }

        // ============================================================================
        // RKNNMemoryPool 实现
        // ============================================================================

        RKNNMemoryPool::RKNNMemoryPool(const Config& config) : config_(config)
        {
            LOG_INFO(LOG_TAG_RKNN_POOL, "初始化RKNN内存池...");

            fixed_pool_ =
                std::make_unique<FixedPool>(config.fixed_block_size, config.fixed_block_count);
            dynamic_pool_ = std::make_unique<MemoryPool>(config.dynamic_pool_size, config.alignment,
                                                         config.expansion_factor);

            LOG_INFO(LOG_TAG_RKNN_POOL,
                     "RKNN内存池初始化完成 (固定池: %zu块×%zu字节, 动态池: %.2fMB)",
                     config.fixed_block_count, config.fixed_block_size,
                     static_cast<double>(config.dynamic_pool_size) / BYTES_PER_MEGABYTE);
        }

        RKNNMemoryPool::~RKNNMemoryPool()
        {
            logStats();
        }

        void* RKNNMemoryPool::allocate(size_t size)
        {
            stats_.total_allocations.fetch_add(1, std::memory_order_relaxed);

            // 固定池
            if (size <= config_.fixed_block_size)
            {
                int block_index = fixed_pool_->allocateBlock();
                if (block_index >= 0)
                {
                    stats_.fixed_pool_hits.fetch_add(1, std::memory_order_relaxed);
                    return fixed_pool_->getBlockPtr(block_index);
                }
            }

            // 动态池
            void* mem = dynamic_pool_->allocate(size);
            if (mem)
            {
                stats_.dynamic_pool_hits.fetch_add(1, std::memory_order_relaxed);
                return mem;
            }

            stats_.allocation_failures.fetch_add(1, std::memory_order_relaxed);
            LOG_ERROR(LOG_TAG_RKNN_POOL, "内存分配失败: %zu字节", size);
            return nullptr;
        }

        void RKNNMemoryPool::deallocate(void* ptr)
        {
            if (!ptr)
                return;

            // 检查是否属于固定池
            uint8_t* buffer_start = fixed_pool_->buffer.data();
            uint8_t* buffer_end   = buffer_start + fixed_pool_->buffer.size();
            uint8_t* ptr_byte     = static_cast<uint8_t*>(ptr);

            if (ptr_byte >= buffer_start && ptr_byte < buffer_end)
            {
                // 属于固定池
                size_t offset = ptr_byte - buffer_start;
                int    index  = static_cast<int>(offset / config_.fixed_block_size);
                if (index >= 0 && index < static_cast<int>(fixed_pool_->block_count))
                {
                    fixed_pool_->deallocateBlock(index);
                    return;
                }
            }

            // 属于动态池
            dynamic_pool_->deallocate(ptr);
        }

        void RKNNMemoryPool::getStats(Stats& out_stats) const
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            out_stats.fixed_pool_hits   = stats_.fixed_pool_hits.load(std::memory_order_relaxed);
            out_stats.dynamic_pool_hits = stats_.dynamic_pool_hits.load(std::memory_order_relaxed);
            out_stats.total_allocations = stats_.total_allocations.load(std::memory_order_relaxed);
            out_stats.allocation_failures =
                stats_.allocation_failures.load(std::memory_order_relaxed);
        }

        void RKNNMemoryPool::resetStats()
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.fixed_pool_hits.store(0);
            stats_.dynamic_pool_hits.store(0);
            stats_.total_allocations.store(0);
            stats_.allocation_failures.store(0);
        }

        void RKNNMemoryPool::logStats() const
        {
            uint64_t total = stats_.total_allocations.load(std::memory_order_relaxed);
            if (total == 0)
                return;

            uint64_t fixed   = stats_.fixed_pool_hits.load();
            uint64_t dynamic = stats_.dynamic_pool_hits.load();
            uint64_t fail    = stats_.allocation_failures.load();

            LOG_INFO(LOG_TAG_RKNN_POOL,
                     "内存池统计: 总=%zu, 固定=%zu(%.1f%%), 动态=%zu(%.1f%%), 失败=%zu", total,
                     fixed, (double)fixed * 100.0 / total, dynamic, (double)dynamic * 100.0 / total,
                     fail);
        }

        // ============================================================================
        // RKNNModel 实现
        // ============================================================================

        RKNNModel::RKNNModel()
            : ctx_(0), model_width_(0), model_height_(0), model_channel_(0), is_quant_(false),
              initialized_(false)
        {
            std::memset(&io_num_, 0, sizeof(io_num_));
        }

        RKNNModel::~RKNNModel()
        {
            deinit();
        }

        RKNNError RKNNModel::init(const std::string& model_path)
        {
            if (initialized_)
            {
                LOG_WARN(LOG_TAG, "模型已初始化，先调用deinit()");
                return RKNNError::INVALID_STATE;
            }

            if (model_path.empty())
            {
                LOG_ERROR(LOG_TAG, "模型路径为空");
                return RKNNError::INVALID_PARAM;
            }

            // 初始化RKNN上下文
            int ret = rknn_init(&ctx_, const_cast<char*>(model_path.c_str()), 0, 0, nullptr);
            if (ret != RKNN_SUCC)
            {
                LOG_ERROR(LOG_TAG, "rknn_init失败: ret=%d", ret);
                return RKNNError::INIT_FAILED;
            }

            // 查询模型信息
            RKNNError err = queryModelInfo();
            if (err != RKNNError::NONE)
            {
                rknn_destroy(ctx_);
                ctx_ = 0;
                return err;
            }

            // 创建输入输出内存
            err = createIOMemory();
            if (err != RKNNError::NONE)
            {
                destroyIOMemory();
                rknn_destroy(ctx_);
                ctx_ = 0;
                return err;
            }

            // 创建内存池
            MemoryPoolConfig pool_config;
            size_t           model_input_size = model_width_ * model_height_ * model_channel_;
            pool_config.fixed_block_size      = model_input_size;

            mem_pool_ = std::make_unique<RKNNMemoryPool>(pool_config);

            initialized_ = true;
            LOG_INFO(LOG_TAG, "模型初始化成功: %s (输入:%dx%dx%d, 输出:%d个)", model_path.c_str(),
                     model_width_, model_height_, model_channel_, io_num_.n_output);

            return RKNNError::NONE;
        }

        void RKNNModel::deinit()
        {
            if (!initialized_)
            {
                return;
            }

            // 释放输入输出内存
            destroyIOMemory();

            // 释放RKNN上下文
            if (ctx_ != 0)
            {
                rknn_destroy(ctx_);
                ctx_ = 0;
            }

            // 释放内存池
            mem_pool_.reset();

            // 清空属性
            input_attrs_.clear();
            output_attrs_.clear();

            initialized_ = false;
            LOG_INFO(LOG_TAG, "模型资源已释放");
        }

        RKNNError RKNNModel::queryModelInfo()
        {
            // 查询输入输出数量
            int ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
            if (ret != RKNN_SUCC)
            {
                LOG_ERROR(LOG_TAG, "查询输入输出数量失败: ret=%d", ret);
                return RKNNError::QUERY_FAILED;
            }

            LOG_INFO(LOG_TAG, "模型输入数量: %d, 输出数量: %d", io_num_.n_input, io_num_.n_output);

            // 查询输入属性
            input_attrs_.resize(io_num_.n_input);
            for (uint32_t i = 0; i < io_num_.n_input; i++)
            {
                input_attrs_[i].index = i;
                ret = rknn_query(ctx_, RKNN_QUERY_NATIVE_INPUT_ATTR, &input_attrs_[i],
                                 sizeof(rknn_tensor_attr));
                if (ret != RKNN_SUCC)
                {
                    LOG_ERROR(LOG_TAG, "查询输入属性失败: index=%d, ret=%d", i, ret);
                    return RKNNError::QUERY_FAILED;
                }

                LOG_DEBUG(LOG_TAG,
                          "输入[%d]: name=%s, dims=[%d,%d,%d,%d], fmt=%s, type=%s, size=%d", i,
                          input_attrs_[i].name, input_attrs_[i].dims[0], input_attrs_[i].dims[1],
                          input_attrs_[i].dims[2], input_attrs_[i].dims[3],
                          get_format_string(input_attrs_[i].fmt),
                          get_type_string(input_attrs_[i].type), input_attrs_[i].size_with_stride);
            }

            // 查询输出属性
            output_attrs_.resize(io_num_.n_output);
            for (uint32_t i = 0; i < io_num_.n_output; i++)
            {
                output_attrs_[i].index = i;
                ret = rknn_query(ctx_, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, &output_attrs_[i],
                                 sizeof(rknn_tensor_attr));
                if (ret != RKNN_SUCC)
                {
                    LOG_ERROR(LOG_TAG, "查询输出属性失败: index=%d, ret=%d", i, ret);
                    return RKNNError::QUERY_FAILED;
                }

                LOG_DEBUG(LOG_TAG,
                          "输出[%d]: name=%s, dims=[%d,%d,%d,%d], fmt=%s, type=%s, size=%d, "
                          "qnt_type=%s, zp=%d, scale=%f",
                          i, output_attrs_[i].name, output_attrs_[i].dims[0],
                          output_attrs_[i].dims[1], output_attrs_[i].dims[2],
                          output_attrs_[i].dims[3], get_format_string(output_attrs_[i].fmt),
                          get_type_string(output_attrs_[i].type), output_attrs_[i].size_with_stride,
                          get_qnt_type_string(output_attrs_[i].qnt_type), output_attrs_[i].zp,
                          output_attrs_[i].scale);
            }

            // 解析模型输入尺寸
            if (input_attrs_[0].fmt == RKNN_TENSOR_NCHW)
            {
                model_channel_ = input_attrs_[0].dims[1];
                model_height_  = input_attrs_[0].dims[2];
                model_width_   = input_attrs_[0].dims[3];
            }
            else // NHWC
            {
                model_height_  = input_attrs_[0].dims[1];
                model_width_   = input_attrs_[0].dims[2];
                model_channel_ = input_attrs_[0].dims[3];
            }

            // 检查是否为量化模型
            is_quant_ = (output_attrs_[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC);

            return RKNNError::NONE;
        }

        RKNNError RKNNModel::createIOMemory()
        {
            // 设置输入属性
            input_attrs_[0].type = RKNN_TENSOR_UINT8;
            input_attrs_[0].fmt  = RKNN_TENSOR_NHWC;

            // 创建输入内存
            input_mems_.resize(io_num_.n_input);
            for (uint32_t i = 0; i < io_num_.n_input; i++)
            {
                input_mems_[i] = rknn_create_mem(ctx_, input_attrs_[i].size_with_stride);
                if (!input_mems_[i])
                {
                    LOG_ERROR(LOG_TAG, "创建输入内存失败: index=%d", i);
                    return RKNNError::MEMORY_ALLOC_FAILED;
                }

                // 设置输入内存
                int ret = rknn_set_io_mem(ctx_, input_mems_[i], &input_attrs_[i]);
                if (ret != RKNN_SUCC)
                {
                    LOG_ERROR(LOG_TAG, "设置输入内存失败: index=%d, ret=%d", i, ret);
                    return RKNNError::SET_IO_MEM_FAILED;
                }
            }

            // 创建输出内存
            output_mems_.resize(io_num_.n_output);
            for (uint32_t i = 0; i < io_num_.n_output; i++)
            {
                output_mems_[i] = rknn_create_mem(ctx_, output_attrs_[i].size_with_stride);
                if (!output_mems_[i])
                {
                    LOG_ERROR(LOG_TAG, "创建输出内存失败: index=%d", i);
                    return RKNNError::MEMORY_ALLOC_FAILED;
                }

                // 设置输出内存
                int ret = rknn_set_io_mem(ctx_, output_mems_[i], &output_attrs_[i]);
                if (ret != RKNN_SUCC)
                {
                    LOG_ERROR(LOG_TAG, "设置输出内存失败: index=%d, ret=%d", i, ret);
                    return RKNNError::SET_IO_MEM_FAILED;
                }
            }

            return RKNNError::NONE;
        }

        void RKNNModel::destroyIOMemory()
        {
            // 释放输入内存
            for (auto* mem : input_mems_)
            {
                if (mem)
                {
                    rknn_destroy_mem(ctx_, mem);
                }
            }
            input_mems_.clear();

            // 释放输出内存
            for (auto* mem : output_mems_)
            {
                if (mem)
                {
                    rknn_destroy_mem(ctx_, mem);
                }
            }
            output_mems_.clear();
        }

        const rknn_tensor_attr* RKNNModel::getInputAttr(uint32_t index) const
        {
            if (!initialized_ || index >= input_attrs_.size())
            {
                return nullptr;
            }
            return &input_attrs_[index];
        }

        const rknn_tensor_attr* RKNNModel::getOutputAttr(uint32_t index) const
        {
            if (!initialized_ || index >= output_attrs_.size())
            {
                return nullptr;
            }
            return &output_attrs_[index];
        }

        RKNNError RKNNModel::setInput(uint32_t index, const void* data, uint32_t size)
        {
            if (!initialized_)
            {
                LOG_ERROR(LOG_TAG, "模型未初始化");
                return RKNNError::INVALID_STATE;
            }

            if (index >= input_mems_.size() || !input_mems_[index])
            {
                LOG_ERROR(LOG_TAG, "无效的输入索引: %d", index);
                return RKNNError::INVALID_PARAM;
            }

            // 检查数据大小
            uint32_t required_size = input_attrs_[index].size_with_stride;
            uint32_t copy_size     = size;
            if (copy_size > required_size)
            {
                LOG_WARN(LOG_TAG, "输入数据大小(%d)超过要求(%d)，将截断", copy_size, required_size);
                copy_size = required_size;
            }

            // 复制数据到输入内存
            std::memcpy(input_mems_[index]->virt_addr, data, copy_size);

            return RKNNError::NONE;
        }

        RKNNError RKNNModel::run()
        {
            if (!initialized_)
            {
                LOG_ERROR(LOG_TAG, "模型未初始化");
                return RKNNError::INVALID_STATE;
            }

            int ret = rknn_run(ctx_, nullptr);
            if (ret != RKNN_SUCC)
            {
                LOG_ERROR(LOG_TAG, "rknn_run失败: ret=%d", ret);
                return RKNNError::RUN_FAILED;
            }

            return RKNNError::NONE;
        }

        void* RKNNModel::getOutput(uint32_t index) const
        {
            if (!initialized_ || index >= output_mems_.size() || !output_mems_[index])
            {
                return nullptr;
            }
            return output_mems_[index]->virt_addr;
        }

        uint32_t RKNNModel::getOutputSize(uint32_t index) const
        {
            if (!initialized_ || index >= output_attrs_.size())
            {
                return 0;
            }
            return output_attrs_[index].size_with_stride;
        }

        bool RKNNModel::getOutputQuantParams(uint32_t index, int32_t& zp, float& scale) const
        {
            if (!initialized_ || index >= output_attrs_.size())
            {
                return false;
            }

            const auto& attr = output_attrs_[index];
            if (attr.qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC)
            {
                return false;
            }

            zp    = attr.zp;
            scale = attr.scale;
            return true;
        }

        void* RKNNModel::allocateTempBuffer(size_t size)
        {
            if (!mem_pool_)
            {
                return nullptr;
            }
            return mem_pool_->allocate(size);
        }

        void RKNNModel::deallocateTempBuffer(void* ptr)
        {
            if (mem_pool_ && ptr)
            {
                mem_pool_->deallocate(ptr);
            }
        }

        void RKNNModel::getMemoryPoolStats(RKNNMemoryPool::Stats& stats) const
        {
            if (mem_pool_)
            {
                mem_pool_->getStats(stats);
            }
        }

    } // namespace rknn
} // namespace app
