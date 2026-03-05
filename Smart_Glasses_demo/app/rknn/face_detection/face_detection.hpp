/* face_detection.hpp - 人脸检测 */

#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include "../rknn.hpp"
#include "../rknn_config.hpp"
#include "../../../tool/log/log.hpp"

namespace app
{
    namespace rknn
    {
        namespace face_detection
        {
            using namespace tool::log;

            /**
             * @brief 关键点坐标
             */
            struct LandmarkPoint
            {
                float x; // X坐标
                float y; // Y坐标
            };

            /**
             * @brief 人脸检测框（包含关键点）
             */
            struct FaceBox
            {
                int           left;       // 左边界
                int           top;        // 上边界
                int           right;      // 右边界
                int           bottom;     // 下边界
                float         confidence; // 置信度（模型输出的原始值）
                LandmarkPoint landmarks[FaceDetectionConst::NUM_LANDMARKS]; // 关键点
            };

            /**
             * @brief 人脸检测结果列表
             */
            struct DetectionResult
            {
                std::vector<FaceBox> faces; // 检测到的人脸列表
                size_t               count() const
                {
                    return faces.size();
                }
            };

            /**
             * @brief 人脸检测配置
             */
            using DetectionConfig = FaceDetectionConfig::DetectionConfig;

            /**
             * @brief 人脸检测类
             */
            class FaceDetection
            {
            public:
                /**
                 * @brief 构造函数
                 */
                FaceDetection();

                /**
                 * @brief 析构函数
                 */
                ~FaceDetection();

                // 禁用拷贝和移动
                FaceDetection(const FaceDetection&)            = delete;
                FaceDetection& operator=(const FaceDetection&) = delete;
                FaceDetection(FaceDetection&&)                 = delete;
                FaceDetection& operator=(FaceDetection&&)      = delete;

                /**
                 * @brief 初始化检测模型
                 * @param model_path 模型文件路径
                 * @param config 检测配置
                 * @return 错误码
                 */
                RKNNError init(const std::string&     model_path,
                               const DetectionConfig& config = DetectionConfig());

                /**
                 * @brief 反初始化
                 */
                void deinit();

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 执行检测
                 * @param image_data 输入图像数据
                 * @param width 图像宽度
                 * @param height 图像高度
                 * @param result 检测结果输出
                 * @return 错误码
                 */
                RKNNError detect(const uint8_t* image_data, int width, int height,
                                 DetectionResult& result);

                /**
                 * @brief 执行检测
                 * @param image_data 输入图像数据
                 * @param result 检测结果输出
                 * @return 错误码
                 */
                RKNNError detect(const uint8_t* image_data, DetectionResult& result);

                /**
                 * @brief 获取模型输入宽度
                 */
                int getModelWidth() const
                {
                    return model_ ? model_->getModelWidth() : 0;
                }

                /**
                 * @brief 获取模型输入高度
                 */
                int getModelHeight() const
                {
                    return model_ ? model_->getModelHeight() : 0;
                }

                /**
                 * @brief 获取模型输入通道数
                 */
                int getModelChannel() const
                {
                    return model_ ? model_->getModelChannel() : 0;
                }

                /**
                 * @brief 设置检测配置
                 */
                void setConfig(const DetectionConfig& config)
                {
                    config_ = config;
                }

                /**
                 * @brief 获取检测配置
                 */
                const DetectionConfig& getConfig() const
                {
                    return config_;
                }

                /**
                 * @brief 分配临时缓冲区
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
                void decodeDetections(int original_width, int original_height,
                                      DetectionResult& result);

                /**
                 * @brief 计算IoU（交并比）
                 */
                float calculateIoU(const FaceBox& box1, const FaceBox& box2) const;

                /**
                 * @brief NMS（非极大值抑制）
                 */
                void nms(std::vector<FaceBox>& boxes) const;

                /**
                 * @brief 量化值转浮点数
                 */
                inline float dequantize(int8_t qnt, int32_t zp, float scale) const
                {
                    return (static_cast<float>(qnt) - static_cast<float>(zp)) * scale;
                }

                /**
                 * @brief 限制值在范围内
                 */
                inline float clamp(float val, float min_val, float max_val) const
                {
                    return std::max(min_val, std::min(max_val, val));
                }

                /**
                 * @brief 限制整数在范围内
                 */
                inline int clampInt(int val, int min_val, int max_val) const
                {
                    return std::max(min_val, std::min(max_val, val));
                }

            private:
                std::unique_ptr<RKNNModel> model_;
                DetectionConfig            config_;
                bool                       initialized_;
            };

        } // namespace face_detection
    }     // namespace rknn
} // namespace app
