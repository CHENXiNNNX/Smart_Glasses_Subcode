/**
 * @file face_detection.cc
 * @brief 人脸检测模块实现
 */

#include "face_detection.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>

namespace app
{
    namespace rknn
    {
        namespace face_detection
        {
            namespace
            {
                constexpr const char* LOG_TAG = "FACE_DETECTION";
            }

            FaceDetection::FaceDetection() : initialized_(false) {}

            FaceDetection::~FaceDetection()
            {
                deinit();
            }

            RKNNError FaceDetection::init(const std::string&     model_path,
                                          const DetectionConfig& config)
            {
                if (initialized_)
                {
                    LOG_WARN(LOG_TAG, "检测模型已初始化");
                    return RKNNError::INVALID_STATE;
                }

                config_ = config;

                // 创建模型
                model_ = std::make_unique<RKNNModel>();

                // 初始化模型
                RKNNError ret = model_->init(model_path);
                if (ret != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "模型初始化失败: %s", model_path.c_str());
                    model_.reset();
                    return ret;
                }

                // 验证模型输入输出
                if (model_->getInputNum() != 1 || model_->getOutputNum() != 2)
                {
                    LOG_ERROR(LOG_TAG, "模型输入输出数量不匹配: 输入=%d, 输出=%d",
                              model_->getInputNum(), model_->getOutputNum());
                    model_->deinit();
                    model_.reset();
                    return RKNNError::INVALID_PARAM;
                }

                initialized_ = true;
                LOG_INFO(LOG_TAG, "人脸检测模型初始化成功: %s (%dx%dx%d)", model_path.c_str(),
                         model_->getModelWidth(), model_->getModelHeight(),
                         model_->getModelChannel());

                return RKNNError::NONE;
            }

            void FaceDetection::deinit()
            {
                if (!initialized_)
                {
                    return;
                }

                if (model_)
                {
                    model_->deinit();
                    model_.reset();
                }

                initialized_ = false;
                LOG_INFO(LOG_TAG, "人脸检测模型已释放");
            }

            RKNNError FaceDetection::detect(const uint8_t* image_data, int width, int height,
                                            DetectionResult& result)
            {
                if (!initialized_ || !model_)
                {
                    LOG_ERROR(LOG_TAG, "模型未初始化");
                    return RKNNError::INVALID_STATE;
                }

                if (!image_data)
                {
                    LOG_ERROR(LOG_TAG, "输入数据为空");
                    return RKNNError::INVALID_PARAM;
                }

                int    model_width   = model_->getModelWidth();
                int    model_height  = model_->getModelHeight();
                int    model_channel = model_->getModelChannel();
                size_t input_size    = model_width * model_height * model_channel;

                uint8_t* preprocessed_data =
                    static_cast<uint8_t*>(model_->allocateTempBuffer(input_size));
                if (!preprocessed_data)
                {
                    LOG_ERROR(LOG_TAG, "分配预处理缓冲区失败");
                    return RKNNError::MEMORY_ALLOC_FAILED;
                }

                float scale_x = static_cast<float>(width) / static_cast<float>(model_width);
                float scale_y = static_cast<float>(height) / static_cast<float>(model_height);

                for (int y = 0; y < model_height; y++)
                {
                    for (int x = 0; x < model_width; x++)
                    {
                        int src_x = static_cast<int>(x * scale_x);
                        int src_y = static_cast<int>(y * scale_y);
                        src_x     = std::min(src_x, width - 1);
                        src_y     = std::min(src_y, height - 1);

                        preprocessed_data[y * model_width + x] = image_data[src_y * width + src_x];
                    }
                }

                RKNNError ret = detect(preprocessed_data, result);

                model_->deallocateTempBuffer(preprocessed_data);

                if (ret == RKNNError::NONE)
                {
                    float scale_x_ratio =
                        static_cast<float>(width) / static_cast<float>(model_width);
                    float scale_y_ratio =
                        static_cast<float>(height) / static_cast<float>(model_height);

                    for (auto& face : result.faces)
                    {
                        face.left   = static_cast<int>(face.left * scale_x_ratio);
                        face.top    = static_cast<int>(face.top * scale_y_ratio);
                        face.right  = static_cast<int>(face.right * scale_x_ratio);
                        face.bottom = static_cast<int>(face.bottom * scale_y_ratio);
                    }
                }

                return ret;
            }

            RKNNError FaceDetection::detect(const uint8_t* image_data, DetectionResult& result)
            {
                if (!initialized_ || !model_)
                {
                    LOG_ERROR(LOG_TAG, "模型未初始化");
                    return RKNNError::INVALID_STATE;
                }

                if (!image_data)
                {
                    LOG_ERROR(LOG_TAG, "输入数据为空");
                    return RKNNError::INVALID_PARAM;
                }

                // 清空结果
                result.faces.clear();

                // 获取模型输入尺寸
                int    model_width   = model_->getModelWidth();
                int    model_height  = model_->getModelHeight();
                int    model_channel = model_->getModelChannel();
                size_t input_size    = model_width * model_height * model_channel;

                // 设置输入
                RKNNError ret = model_->setInput(0, image_data, input_size);
                if (ret != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "设置输入失败");
                    return ret;
                }

                // 执行推理
                ret = model_->run();
                if (ret != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "推理失败");
                    return ret;
                }

                // 解码检测结果
                decodeDetections(model_width, model_height, result);

                return RKNNError::NONE;
            }

            void FaceDetection::decodeDetections(int original_width, int original_height,
                                                 DetectionResult& result)
            {
                void* locations_ptr = model_->getOutput(0);
                void* scores_ptr    = model_->getOutput(1);

                if (!locations_ptr || !scores_ptr)
                {
                    LOG_ERROR(LOG_TAG, "获取输出数据失败");
                    return;
                }

                int32_t loc_zp      = 0;
                float   loc_scale   = 0.0f;
                int32_t score_zp    = 0;
                float   score_scale = 0.0f;

                if (!model_->getOutputQuantParams(0, loc_zp, loc_scale) ||
                    !model_->getOutputQuantParams(1, score_zp, score_scale))
                {
                    LOG_ERROR(LOG_TAG, "获取量化参数失败");
                    return;
                }

                const int8_t* locations = static_cast<const int8_t*>(locations_ptr);
                const int8_t* scores    = static_cast<const int8_t*>(scores_ptr);

                const int num_anchors = 896;
                const int loc_stride  = 14;

                for (int i = 0; i < num_anchors; i++)
                {
                    float score_logit = dequantize(scores[i], score_zp, score_scale);
                    float confidence  = sigmoid(score_logit);

                    if (confidence < config_.confidence_threshold)
                    {
                        continue;
                    }

                    const int8_t* loc = &locations[i * loc_stride];

                    float val0 = dequantize(loc[0], loc_zp, loc_scale);
                    float val1 = dequantize(loc[1], loc_zp, loc_scale);
                    float val2 = dequantize(loc[2], loc_zp, loc_scale);
                    float val3 = dequantize(loc[3], loc_zp, loc_scale);

                    float xmin, ymin, xmax, ymax;

                    if (val0 >= 0.0f && val0 <= 1.0f && val2 >= 0.0f && val2 <= 1.0f &&
                        val1 >= 0.0f && val1 <= 1.0f && val3 >= 0.0f && val3 <= 1.0f &&
                        val2 > val0 && val3 > val1)
                    {
                        xmin = val0 * original_width;
                        ymin = val1 * original_height;
                        xmax = val2 * original_width;
                        ymax = val3 * original_height;
                    }
                    else
                    {
                        float cx = val0;
                        float cy = val1;
                        float w  = std::abs(val2);
                        float h  = std::abs(val3);

                        if (cx >= 0.0f && cx <= 1.0f && cy >= 0.0f && cy <= 1.0f && w > 0.0f &&
                            w <= 1.0f && h > 0.0f && h <= 1.0f)
                        {
                            constexpr float HALF = 0.5f;
                            xmin                 = (cx - w * HALF) * original_width;
                            ymin                 = (cy - h * HALF) * original_height;
                            xmax                 = (cx + w * HALF) * original_width;
                            ymax                 = (cy + h * HALF) * original_height;
                        }
                        else
                        {
                            xmin = cx - w * 0.5f;
                            ymin = cy - h * 0.5f;
                            xmax = cx + w * 0.5f;
                            ymax = cy + h * 0.5f;
                        }
                    }

                    xmin = clamp(xmin, 0.0f, static_cast<float>(original_width));
                    ymin = clamp(ymin, 0.0f, static_cast<float>(original_height));
                    xmax = clamp(xmax, 0.0f, static_cast<float>(original_width));
                    ymax = clamp(ymax, 0.0f, static_cast<float>(original_height));

                    if (xmax <= xmin || ymax <= ymin)
                    {
                        continue;
                    }

                    float box_width  = xmax - xmin;
                    float box_height = ymax - ymin;

                    constexpr float MIN_BOX_RATIO    = 0.015f;
                    constexpr float MIN_BOX_SIZE_ABS = 15.0f;
                    float min_width  = std::max(MIN_BOX_SIZE_ABS, original_width * MIN_BOX_RATIO);
                    float min_height = std::max(MIN_BOX_SIZE_ABS, original_height * MIN_BOX_RATIO);

                    if (box_width < min_width || box_height < min_height)
                    {
                        continue;
                    }

                    int box_left   = static_cast<int>(xmin);
                    int box_top    = static_cast<int>(ymin);
                    int box_right  = static_cast<int>(xmax);
                    int box_bottom = static_cast<int>(ymax);

                    box_left   = std::max(0, std::min(box_left, original_width - 1));
                    box_top    = std::max(0, std::min(box_top, original_height - 1));
                    box_right  = std::max(box_left + 1, std::min(box_right, original_width));
                    box_bottom = std::max(box_top + 1, std::min(box_bottom, original_height));

                    if (box_right <= box_left || box_bottom <= box_top)
                    {
                        continue;
                    }

                    FaceBox box;
                    box.left       = box_left;
                    box.top        = box_top;
                    box.right      = box_right;
                    box.bottom     = box_bottom;
                    box.confidence = confidence;

                    result.faces.push_back(box);
                }

                nms(result.faces);

                if (result.faces.size() > static_cast<size_t>(config_.max_detections))
                {
                    result.faces.resize(config_.max_detections);
                }
            }

            float FaceDetection::calculateIoU(const FaceBox& box1, const FaceBox& box2) const
            {
                int x1 = std::max(box1.left, box2.left);
                int y1 = std::max(box1.top, box2.top);
                int x2 = std::min(box1.right, box2.right);
                int y2 = std::min(box1.bottom, box2.bottom);

                if (x2 <= x1 || y2 <= y1)
                {
                    return 0.0f;
                }

                int intersection = (x2 - x1) * (y2 - y1);
                int area1        = (box1.right - box1.left) * (box1.bottom - box1.top);
                int area2        = (box2.right - box2.left) * (box2.bottom - box2.top);
                int union_area   = area1 + area2 - intersection;

                return union_area > 0
                           ? static_cast<float>(intersection) / static_cast<float>(union_area)
                           : 0.0f;
            }

            void FaceDetection::nms(std::vector<FaceBox>& boxes) const
            {
                if (boxes.empty())
                {
                    return;
                }

                // 按置信度排序
                std::sort(boxes.begin(), boxes.end(),
                          [](const FaceBox& a, const FaceBox& b)
                          { return a.confidence > b.confidence; });

                std::vector<bool> suppressed(boxes.size(), false);

                for (size_t i = 0; i < boxes.size(); i++)
                {
                    if (suppressed[i])
                    {
                        continue;
                    }

                    for (size_t j = i + 1; j < boxes.size(); j++)
                    {
                        if (suppressed[j])
                        {
                            continue;
                        }

                        float iou = calculateIoU(boxes[i], boxes[j]);
                        if (iou > config_.nms_threshold)
                        {
                            suppressed[j] = true;
                        }
                    }
                }

                // 移除被抑制的框
                size_t write_idx = 0;
                for (size_t i = 0; i < boxes.size(); i++)
                {
                    if (!suppressed[i])
                    {
                        boxes[write_idx++] = boxes[i];
                    }
                }
                boxes.resize(write_idx);
            }

            void* FaceDetection::allocateTempBuffer(size_t size)
            {
                if (!model_)
                    return nullptr;
                return model_->allocateTempBuffer(size);
            }

            void FaceDetection::deallocateTempBuffer(void* ptr)
            {
                if (model_ && ptr)
                    model_->deallocateTempBuffer(ptr);
            }

        } // namespace face_detection
    }     // namespace rknn
} // namespace app
