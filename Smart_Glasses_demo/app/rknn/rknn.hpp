/**
 * @file rknn.hpp
 * @brief RKNN模型基础封装类
 * @details 提供RKNN模型的加载、推理、内存管理等基础功能
 */

#ifndef RKNN_HPP
#define RKNN_HPP

#include <cstdint>
#include <memory>
#include <string>
#include "rknn_api.h"
#include "../../tool/memory/mem_pool.hpp"
#include "../../tool/log/log.hpp"

namespace app
{
    namespace rknn
    {
        using namespace tool::log;
        using namespace tool::memory;

        /**
         * @brief RKNN模型错误码
         */
        enum class RKNNError
        {
            NONE                = 0,  // 成功
            INIT_FAILED         = -1, // 初始化失败
            QUERY_FAILED        = -2, // 查询失败
            MEMORY_ALLOC_FAILED = -3, // 内存分配失败
            SET_IO_MEM_FAILED   = -4, // 设置IO内存失败
            RUN_FAILED          = -5, // 推理失败
            INVALID_STATE       = -6, // 无效状态
            INVALID_PARAM       = -7  // 无效参数
        };

        /**
         * @brief RKNN模型基础封装类
         */
        class RKNNModel
        {
        public:
            /**
             * @brief 构造函数
             */
            RKNNModel();

            /**
             * @brief 析构函数
             */
            ~RKNNModel();

            /**
             * @brief 禁用拷贝构造和赋值
             */
            RKNNModel(const RKNNModel&)            = delete;
            RKNNModel& operator=(const RKNNModel&) = delete;

            /**
             * @brief 初始化模型
             * @param model_path 模型文件路径
             * @return RKNNError::NONE 成功，其他值表示失败
             */
            RKNNError init(const std::string& model_path);

            /**
             * @brief 释放模型资源
             */
            void deinit();

            /**
             * @brief 检查模型是否已初始化
             * @return true 已初始化，false 未初始化
             */
            bool isInitialized() const
            {
                return initialized_;
            }

            /**
             * @brief 获取模型输入数量
             * @return 输入数量
             */
            uint32_t getInputNum() const
            {
                return io_num_.n_input;
            }

            /**
             * @brief 获取模型输出数量
             * @return 输出数量
             */
            uint32_t getOutputNum() const
            {
                return io_num_.n_output;
            }

            /**
             * @brief 获取输入属性
             * @param index 输入索引
             * @return 输入属性指针，失败返回nullptr
             */
            const rknn_tensor_attr* getInputAttr(uint32_t index) const;

            /**
             * @brief 获取输出属性
             * @param index 输出索引
             * @return 输出属性指针，失败返回nullptr
             */
            const rknn_tensor_attr* getOutputAttr(uint32_t index) const;

            /**
             * @brief 获取模型输入宽度
             * @return 输入宽度
             */
            int getModelWidth() const
            {
                return model_width_;
            }

            /**
             * @brief 获取模型输入高度
             * @return 输入高度
             */
            int getModelHeight() const
            {
                return model_height_;
            }

            /**
             * @brief 获取模型输入通道数
             * @return 输入通道数
             */
            int getModelChannel() const
            {
                return model_channel_;
            }

            /**
             * @brief 设置输入数据
             * @param index 输入索引
             * @param data 输入数据指针
             * @param size 数据大小（字节）
             * @return RKNNError::NONE 成功，其他值表示失败
             */
            RKNNError setInput(uint32_t index, const void* data, uint32_t size);

            /**
             * @brief 执行推理
             * @return RKNNError::NONE 成功，其他值表示失败
             */
            RKNNError run();

            /**
             * @brief 获取输出数据
             * @param index 输出索引
             * @return 输出数据指针，失败返回nullptr
             */
            void* getOutput(uint32_t index) const;

            /**
             * @brief 获取输出数据大小
             * @param index 输出索引
             * @return 输出数据大小（字节）
             */
            uint32_t getOutputSize(uint32_t index) const;

            /**
             * @brief 获取输出量化参数（如果是量化模型）
             * @param index 输出索引
             * @param zp 零点值（输出参数）
             * @param scale 缩放值（输出参数）
             * @return true 成功获取，false 失败或非量化模型
             */
            bool getOutputQuantParams(uint32_t index, int32_t& zp, float& scale) const;

            /**
             * @brief 检查输出是否为量化模型
             * @return true 是量化模型，false 不是
             */
            bool isQuantized() const
            {
                return is_quant_;
            }

            /**
             * @brief 从内存池分配临时缓冲区（用于预处理等）
             * @param size 缓冲区大小（字节）
             * @return 缓冲区指针，失败返回nullptr
             */
            void* allocateTempBuffer(size_t size);

            /**
             * @brief 释放临时缓冲区
             * @param ptr 缓冲区指针
             */
            void deallocateTempBuffer(void* ptr);

        private:
            /**
             * @brief 查询模型信息
             * @return RKNNError::NONE 成功，其他值表示失败
             */
            RKNNError queryModelInfo();

            /**
             * @brief 创建输入输出内存
             * @return RKNNError::NONE 成功，其他值表示失败
             */
            RKNNError createIOMemory();

            /**
             * @brief 释放输入输出内存
             */
            void destroyIOMemory();

            /**
             * @brief 记录日志标签
             */
            static constexpr const char* LOG_TAG = "RKNN";

            // RKNN上下文
            rknn_context ctx_;

            // 输入输出数量
            rknn_input_output_num io_num_;

            // 输入输出属性
            std::vector<rknn_tensor_attr> input_attrs_;
            std::vector<rknn_tensor_attr> output_attrs_;

            // 输入输出内存
            std::vector<rknn_tensor_mem*> input_mems_;
            std::vector<rknn_tensor_mem*> output_mems_;

            // 模型信息
            int  model_width_;
            int  model_height_;
            int  model_channel_;
            bool is_quant_;

            // 初始化状态
            bool initialized_;

            // 内存池（用于临时缓冲区）
            std::unique_ptr<MemoryPool> mem_pool_;
        };

    } // namespace rknn
} // namespace app

#endif // RKNN_HPP
