/**
 * @file rknn_config.hpp
 * @brief RKNN检测配置
 */

#ifndef RKNN_CONFIG_HPP
#define RKNN_CONFIG_HPP

#include <string>
#include <cstdint>

namespace app
{
    namespace rknn
    {
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
                // RKNN模型文件路径
                std::string model_path = "./model/detection.rknn";
            } model;

            /**
             * @brief 检测参数配置
             */
            struct DetectionConfig
            {
                // 置信度阈值，范围0.0-1.0，值越高要求检测结果越可靠
                float confidence_threshold = 0.65f;
                // NMS（非极大值抑制）阈值，范围0.0-1.0，用于去除重叠检测框
                float nms_threshold = 0.4f;
                // 最大检测数量，单次检测最多返回的人脸数量
                int max_detections = 10;
            } detection;

            /**
             * @brief 检测流程配置
             */
            struct ProcessConfig
            {
                // 检测间隔时间（毫秒），控制检测频率，值越小检测越频繁
                int detect_interval_ms = 200;
                // 帧获取超时时间（毫秒），获取原始帧时的超时设置
                int frame_timeout_ms = 100;
                // 检测线程休眠时间（毫秒），用于控制CPU占用
                int thread_sleep_ms = 5;
                // 主循环休眠时间（毫秒），用于控制主线程处理频率
                int main_loop_sleep_ms = 20;
            } process;

            /**
             * @brief 队列配置
             */
            struct QueueConfig
            {
                // 队列最大容量，最多缓存的检测结果数量
                size_t max_size = 10;
                // 检测结果有效期（毫秒），超过此时间的旧结果会被自动丢弃
                int result_ttl_ms = 500;
            } queue;

            /**
             * @brief 获取默认配置
             */
            static FaceDetectionConfig getDefault()
            {
                return FaceDetectionConfig();
            }
        };

    } // namespace rknn
} // namespace app

#endif // RKNN_CONFIG_HPP
