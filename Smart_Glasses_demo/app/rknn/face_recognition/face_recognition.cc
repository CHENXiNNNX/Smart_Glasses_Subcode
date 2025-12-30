/**
 * @file face_recognition.cc
 * @brief 人脸识别模块实现
 */

#include "face_recognition.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace app
{
    namespace rknn
    {
        namespace face_recognition
        {
            namespace
            {
                constexpr const char* LOG_TAG = "FACE_RECOGNITION";
            }

            FaceRecognition::FaceRecognition() : initialized_(false) {}

            FaceRecognition::~FaceRecognition()
            {
                deinit();
            }

            RKNNError FaceRecognition::init(const std::string&       model_path,
                                            const RecognitionConfig& config)
            {
                if (initialized_)
                {
                    LOG_WARN(LOG_TAG, "识别模型已初始化");
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

                if (model_->getInputNum() != 1 || model_->getOutputNum() != 1)
                {
                    LOG_ERROR(LOG_TAG, "模型输入输出数量不匹配: 输入=%d, 输出=%d",
                              model_->getInputNum(), model_->getOutputNum());
                    model_->deinit();
                    model_.reset();
                    return RKNNError::INVALID_PARAM;
                }

                const auto* output_attr = model_->getOutputAttr(0);
                if (output_attr)
                {
                    int output_size = output_attr->size_with_stride;
                    if (output_size == FaceRecognitionConst::FEATURE_DIM_128)
                    {
                        config_.feature_dim = FaceRecognitionConst::FEATURE_DIM_128;
                    }
                    else if (output_size == FaceRecognitionConst::FEATURE_DIM_512)
                    {
                        config_.feature_dim = FaceRecognitionConst::FEATURE_DIM_512;
                    }
                }

                initialized_ = true;
                LOG_INFO(LOG_TAG, "人脸识别模型初始化成功: %s", model_path.c_str());

                return RKNNError::NONE;
            }

            void FaceRecognition::deinit()
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

            RKNNError FaceRecognition::extractFeature(const uint8_t* image_data, int width,
                                                      int height, FaceFeature& feature)
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

                preprocessImage(image_data, width, height, preprocessed_data, model_width,
                                model_height);

                RKNNError ret = extractFeature(preprocessed_data, feature);

                model_->deallocateTempBuffer(preprocessed_data);

                return ret;
            }

            RKNNError FaceRecognition::extractFeature(const uint8_t* image_data,
                                                      FaceFeature&   feature)
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

                void* output_ptr = model_->getOutput(0);
                if (!output_ptr)
                {
                    LOG_ERROR(LOG_TAG, "获取输出数据失败");
                    return RKNNError::INVALID_STATE;
                }

                int32_t zp    = 0;
                float   scale = 0.0f;
                bool    is_quantized =
                    model_->isQuantized() && model_->getOutputQuantParams(0, zp, scale);

                feature.data.clear();
                feature.data.reserve(config_.feature_dim);

                if (is_quantized)
                {
                    const int8_t* output_data = static_cast<const int8_t*>(output_ptr);
                    for (int i = 0; i < config_.feature_dim; i++)
                    {
                        float val = dequantize(output_data[i], zp, scale);
                        feature.data.push_back(val);
                    }
                }
                else
                {
                    const float* output_data = static_cast<const float*>(output_ptr);
                    for (int i = 0; i < config_.feature_dim; i++)
                    {
                        feature.data.push_back(output_data[i]);
                    }
                }

                feature.normalize();

                return RKNNError::NONE;
            }

            float FaceRecognition::calculateSimilarity(const FaceFeature& feature1,
                                                       const FaceFeature& feature2)
            {
                if (feature1.size() != feature2.size() || feature1.size() == 0)
                {
                    return 0.0f;
                }

                float dot_product = 0.0f;
                for (size_t i = 0; i < feature1.size(); i++)
                {
                    dot_product += feature1.data[i] * feature2.data[i];
                }

                return (dot_product + 1.0f) * FaceRecognitionConst::SIMILARITY_SCALE;
            }

            RKNNError
            FaceRecognition::recognize(const FaceFeature& query_feature,
                                       const std::vector<std::pair<int, FaceFeature>>& feature_db,
                                       RecognitionResult&                              result)
            {
                if (!initialized_)
                {
                    LOG_ERROR(LOG_TAG, "模型未初始化");
                    return RKNNError::INVALID_STATE;
                }

                if (feature_db.empty())
                {
                    result.recognized = false;
                    result.person_id  = -1;
                    result.similarity = 0.0f;
                    return RKNNError::NONE;
                }

                float max_similarity = 0.0f;
                int   best_match_id  = -1;

                for (const auto& [person_id, feature] : feature_db)
                {
                    float similarity = calculateSimilarity(query_feature, feature);
                    if (similarity > max_similarity)
                    {
                        max_similarity = similarity;
                        best_match_id  = person_id;
                    }
                }

                result.similarity = max_similarity;
                result.person_id  = best_match_id;
                result.recognized = (max_similarity >= config_.similarity_threshold);

                return RKNNError::NONE;
            }

            void FaceRecognition::preprocessImage(const uint8_t* src_data, int src_width,
                                                  int src_height, uint8_t* dst_data, int dst_width,
                                                  int dst_height)
            {
                float scale_x = static_cast<float>(src_width) / static_cast<float>(dst_width);
                float scale_y = static_cast<float>(src_height) / static_cast<float>(dst_height);

                for (int y = 0; y < dst_height; y++)
                {
                    for (int x = 0; x < dst_width; x++)
                    {
                        int src_x = static_cast<int>(x * scale_x);
                        int src_y = static_cast<int>(y * scale_y);
                        src_x     = std::min(src_x, src_width - 1);
                        src_y     = std::min(src_y, src_height - 1);

                        for (int c = 0; c < FaceRecognitionConst::RGB_CHANNELS; c++)
                        {
                            int src_idx =
                                (src_y * src_width + src_x) * FaceRecognitionConst::RGB_CHANNELS +
                                c;
                            int dst_idx =
                                (y * dst_width + x) * FaceRecognitionConst::RGB_CHANNELS + c;
                            dst_data[dst_idx] = src_data[src_idx];
                        }
                    }
                }
            }

            void* FaceRecognition::allocateTempBuffer(size_t size)
            {
                if (!model_)
                    return nullptr;
                return model_->allocateTempBuffer(size);
            }

            void FaceRecognition::deallocateTempBuffer(void* ptr)
            {
                if (model_ && ptr)
                    model_->deallocateTempBuffer(ptr);
            }

        } // namespace face_recognition
    }     // namespace rknn
} // namespace app
