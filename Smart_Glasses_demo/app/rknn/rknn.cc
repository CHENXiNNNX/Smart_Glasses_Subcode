/**
 * @file rknn.cc
 * @brief RKNN模型基础封装类实现
 */

#include "rknn.hpp"
#include <cstring>

namespace app
{
    namespace rknn
    {
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

            // 创建内存池（用于临时缓冲区）
            mem_pool_ = std::make_unique<MemoryPool>(10 * 1024 * 1024);

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
            // 设置输入属性（RV1106 NPU只支持NHWC格式的零拷贝）
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

    } // namespace rknn
} // namespace app
