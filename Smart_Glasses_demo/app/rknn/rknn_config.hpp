/* rknn_config.hpp - RKNN配置 */

#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

namespace app
{
    namespace rknn
    {
        /**
         * @brief 内存池配置
         */
        struct MemoryPoolConfig
        {
            size_t fixed_block_size;  // 固定池块大小
            size_t fixed_block_count; // 固定池块数量
            size_t dynamic_pool_size; // 动态池大小
            size_t alignment;         // 内存对齐
            double expansion_factor;  // 动态池扩展因子

            // 固定池常量
            static constexpr size_t MAX_BLOCKS        = 32; // 最大支持块数
            static constexpr size_t BITMAP_WORD_COUNT = 1;  // 位图字数
            static constexpr size_t BITS_PER_WORD     = 64; // 每字位数

            MemoryPoolConfig()
                : fixed_block_size(1 * 1024 * 1024), fixed_block_count(4),
                  dynamic_pool_size(8 * 1024 * 1024), alignment(64), expansion_factor(1.5)
            {
            }
        };

        /**
         * @brief 人脸检测常量配置
         */
        namespace FaceDetectionConst
        {
            static constexpr int   NUM_LANDMARKS     = 5;            // 关键点数量
            static constexpr int   NUM_PRIORS_640    = 16800;        // 640x640输入的anchor数量
            static constexpr float VARIANCES[2]      = {0.1f, 0.2f}; // 方差参数
            static constexpr int   MODEL_WIDTH       = 640;          // 模型输入宽度
            static constexpr int   MODEL_HEIGHT      = 640;          // 模型输入高度
            static constexpr int   MODEL_CHANNEL     = 3;            // 模型输入通道数
            static constexpr int   CANDIDATE_RESERVE = 256; // 候选检测框预分配数量
        }                                                   // namespace FaceDetectionConst

        /**
         * @brief 人脸检测配置
         */
        struct FaceDetectionConfig
        {
            /**
             * @brief 模型配置
             */
            struct ModelConfig
            {
                std::string model_path = "./model/retinaface.rknn";
            } model;

            /**
             * @brief 检测参数配置
             */
            struct DetectionConfig
            {
                float confidence_threshold = 0.5f; // 置信度阈值
                float nms_threshold        = 0.2f; // NMS阈值
                int   max_detections       = 128;  // 最大检测数量
            } detection;

            /**
             * @brief 检测流程配置
             */
            struct ProcessConfig
            {
                int detect_interval_ms = 200; // 检测间隔时间（毫秒）
                int frame_timeout_ms   = 100; // 帧获取超时时间（毫秒）
                int thread_sleep_ms    = 5;   // 检测线程休眠时间（毫秒）
                int main_loop_sleep_ms = 20;  // 主循环休眠时间（毫秒）
            } process;

            /**
             * @brief 队列配置
             */
            struct QueueConfig
            {
                size_t max_size      = 10;  // 队列最大容量
                int    result_ttl_ms = 500; // 检测结果有效期（毫秒）
            } queue;

            /**
             * @brief 获取默认配置
             */
            static FaceDetectionConfig getDefault()
            {
                return FaceDetectionConfig();
            }
        };

        /**
         * @brief 人脸识别常量配置
         */
        namespace FaceRecognitionConst
        {
            static constexpr int   FEATURE_DIM_128     = 128;   // 128维特征
            static constexpr int   FEATURE_DIM_512     = 512;   // 512维特征
            static constexpr float NORMALIZE_THRESHOLD = 1e-6f; // L2归一化阈值
            static constexpr float SIMILARITY_SCALE    = 0.5f;  // 相似度转换系数
            static constexpr int   RGB_CHANNELS        = 3;     // RGB通道数
        }                                                       // namespace FaceRecognitionConst

        /**
         * @brief 人脸识别配置
         */
        struct FaceRecognitionConfig
        {
            /**
             * @brief 模型配置
             */
            struct ModelConfig
            {
                std::string model_path = "./model/LZ-ArcFace.rknn";
            } model;

            /**
             * @brief 识别参数配置
             */
            struct RecognitionConfig
            {
                float similarity_threshold = 0.6f;
                int   feature_dim          = 128;
            } recognition;
        };

    } // namespace rknn
} // namespace app
