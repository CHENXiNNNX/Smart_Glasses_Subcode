/* face_detection.cc - 人脸检测 */

#include "face_detection.hpp"
#include "retinaface_anchors.hpp"
#include "../rknn_config.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

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

                model_ = std::make_unique<RKNNModel>();

                RKNNError ret = model_->init(model_path);
                if (ret != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "模型初始化失败: %s", model_path.c_str());
                    model_.reset();
                    return ret;
                }

                if (model_->getInputNum() != 1 || model_->getOutputNum() != 3)
                {
                    LOG_ERROR(LOG_TAG, "模型输入输出数量不匹配: 输入=%d, 输出=%d",
                              model_->getInputNum(), model_->getOutputNum());
                    model_->deinit();
                    model_.reset();
                    return RKNNError::INVALID_PARAM;
                }

                int model_width   = model_->getModelWidth();
                int model_height  = model_->getModelHeight();
                int model_channel = model_->getModelChannel();

                if (model_width != FaceDetectionConst::MODEL_WIDTH ||
                    model_height != FaceDetectionConst::MODEL_HEIGHT ||
                    model_channel != FaceDetectionConst::MODEL_CHANNEL)
                {
                    LOG_WARN(LOG_TAG, "模型输入尺寸: %dx%dx%d", model_width, model_height,
                             model_channel);
                }

                initialized_ = true;
                LOG_INFO(LOG_TAG, "人脸检测模型初始化成功: %s", model_path.c_str());

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
                    LOG_ERROR(LOG_TAG, "分配缓冲区失败");
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

                        for (int c = 0; c < FaceDetectionConst::MODEL_CHANNEL; c++)
                        {
                            int src_idx =
                                (src_y * width + src_x) * FaceDetectionConst::MODEL_CHANNEL + c;
                            int dst_idx =
                                (y * model_width + x) * FaceDetectionConst::MODEL_CHANNEL + c;
                            preprocessed_data[dst_idx] = image_data[src_idx];
                        }
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

                        for (int i = 0; i < FaceDetectionConst::NUM_LANDMARKS; i++)
                        {
                            face.landmarks[i].x *= scale_x_ratio;
                            face.landmarks[i].y *= scale_y_ratio;
                        }
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

                result.faces.clear();

                int    model_width   = model_->getModelWidth();
                int    model_height  = model_->getModelHeight();
                int    model_channel = model_->getModelChannel();
                size_t input_size    = model_width * model_height * model_channel;

                RKNNError ret = model_->setInput(0, image_data, input_size);
                if (ret != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "设置输入失败");
                    return ret;
                }

                ret = model_->run();
                if (ret != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "推理失败");
                    return ret;
                }

                decodeDetections(model_width, model_height, result);

                return RKNNError::NONE;
            }

            void FaceDetection::decodeDetections(int original_width, int original_height,
                                                 DetectionResult& result)
            {
                void* locations_ptr = model_->getOutput(0);
                void* scores_ptr    = model_->getOutput(1);
                void* landms_ptr    = model_->getOutput(2);

                if (!locations_ptr || !scores_ptr || !landms_ptr)
                {
                    LOG_ERROR(LOG_TAG, "获取输出数据失败");
                    return;
                }

                int32_t loc_zp       = 0;
                float   loc_scale    = 0.0f;
                int32_t scores_zp    = 0;
                float   scores_scale = 0.0f;
                int32_t landms_zp    = 0;
                float   landms_scale = 0.0f;

                if (!model_->getOutputQuantParams(0, loc_zp, loc_scale) ||
                    !model_->getOutputQuantParams(1, scores_zp, scores_scale) ||
                    !model_->getOutputQuantParams(2, landms_zp, landms_scale))
                {
                    LOG_ERROR(LOG_TAG, "获取量化参数失败");
                    return;
                }

                const int8_t* locations = static_cast<const int8_t*>(locations_ptr);
                const int8_t* scores    = static_cast<const int8_t*>(scores_ptr);
                const int8_t* landms    = static_cast<const int8_t*>(landms_ptr);

                const float(*prior_ptr)[4] = BOX_PRIORS_640;
                const int num_priors       = FaceDetectionConst::NUM_PRIORS_640;

                std::vector<FaceBox> candidate_faces;
                candidate_faces.reserve(FaceDetectionConst::CANDIDATE_RESERVE);

                for (int i = 0; i < num_priors; i++)
                {
                    float face_score = dequantize(scores[i * 2 + 1], scores_zp, scores_scale);

                    if (face_score < config_.confidence_threshold)
                    {
                        continue;
                    }

                    const int8_t* bbox = &locations[i * 4];

                    float dx = dequantize(bbox[0], loc_zp, loc_scale);
                    float dy = dequantize(bbox[1], loc_zp, loc_scale);
                    float dw = dequantize(bbox[2], loc_zp, loc_scale);
                    float dh = dequantize(bbox[3], loc_zp, loc_scale);

                    float box_x =
                        dx * FaceDetectionConst::VARIANCES[0] * prior_ptr[i][2] + prior_ptr[i][0];
                    float box_y =
                        dy * FaceDetectionConst::VARIANCES[0] * prior_ptr[i][3] + prior_ptr[i][1];
                    float box_w = std::exp(dw * FaceDetectionConst::VARIANCES[1]) * prior_ptr[i][2];
                    float box_h = std::exp(dh * FaceDetectionConst::VARIANCES[1]) * prior_ptr[i][3];

                    float xmin = box_x - box_w * 0.5f;
                    float ymin = box_y - box_h * 0.5f;
                    float xmax = xmin + box_w;
                    float ymax = ymin + box_h;

                    xmin = clamp(xmin, 0.0f, 1.0f);
                    ymin = clamp(ymin, 0.0f, 1.0f);
                    xmax = clamp(xmax, 0.0f, 1.0f);
                    ymax = clamp(ymax, 0.0f, 1.0f);

                    if (xmax <= xmin || ymax <= ymin)
                    {
                        continue;
                    }

                    const int8_t* landmark = &landms[i * FaceDetectionConst::NUM_LANDMARKS * 2];
                    LandmarkPoint landmarks[FaceDetectionConst::NUM_LANDMARKS];

                    for (int j = 0; j < FaceDetectionConst::NUM_LANDMARKS; j++)
                    {
                        float lx = dequantize(landmark[j * 2], landms_zp, landms_scale);
                        float ly = dequantize(landmark[j * 2 + 1], landms_zp, landms_scale);

                        landmarks[j].x = lx * FaceDetectionConst::VARIANCES[0] * prior_ptr[i][2] +
                                         prior_ptr[i][0];
                        landmarks[j].y = ly * FaceDetectionConst::VARIANCES[0] * prior_ptr[i][3] +
                                         prior_ptr[i][1];

                        landmarks[j].x = clamp(landmarks[j].x, 0.0f, 1.0f);
                        landmarks[j].y = clamp(landmarks[j].y, 0.0f, 1.0f);
                    }

                    FaceBox box;
                    box.left       = static_cast<int>(xmin * original_width);
                    box.top        = static_cast<int>(ymin * original_height);
                    box.right      = static_cast<int>(xmax * original_width);
                    box.bottom     = static_cast<int>(ymax * original_height);
                    box.confidence = face_score;

                    box.left   = clampInt(box.left, 0, original_width - 1);
                    box.top    = clampInt(box.top, 0, original_height - 1);
                    box.right  = clampInt(box.right, box.left + 1, original_width);
                    box.bottom = clampInt(box.bottom, box.top + 1, original_height);

                    if (box.right <= box.left || box.bottom <= box.top)
                    {
                        continue;
                    }

                    for (int j = 0; j < FaceDetectionConst::NUM_LANDMARKS; j++)
                    {
                        box.landmarks[j].x = landmarks[j].x * original_width;
                        box.landmarks[j].y = landmarks[j].y * original_height;
                    }

                    candidate_faces.push_back(box);
                }

                if (candidate_faces.empty())
                {
                    return;
                }

                std::sort(candidate_faces.begin(), candidate_faces.end(),
                          [](const FaceBox& a, const FaceBox& b)
                          { return a.confidence > b.confidence; });

                nms(candidate_faces);

                if (candidate_faces.size() > static_cast<size_t>(config_.max_detections))
                {
                    candidate_faces.resize(config_.max_detections);
                }

                result.faces = std::move(candidate_faces);
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

                size_t write_idx = 0;
                for (size_t i = 0; i < boxes.size(); i++)
                {
                    if (!suppressed[i])
                    {
                        if (write_idx != i)
                        {
                            boxes[write_idx] = boxes[i];
                        }
                        write_idx++;
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
