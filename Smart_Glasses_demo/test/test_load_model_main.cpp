#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>
#include "app/rknn/rknn.hpp"
#include "app/tool/log/log.hpp"

using namespace app::rknn;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "MAIN";

    std::string getModelPath(const std::string& model_name)
    {
        return "./model/" + model_name;
    }

    void printModelInfo(const RKNNModel& model, const std::string& model_name)
    {
        LOG_INFO(LOG_TAG, "========== %s 模型信息 ==========", model_name.c_str());
        LOG_INFO(LOG_TAG, "输入数量: %d, 输出数量: %d", model.getInputNum(), model.getOutputNum());
        LOG_INFO(LOG_TAG, "模型尺寸: %dx%dx%d", model.getModelWidth(), model.getModelHeight(),
                 model.getModelChannel());
        LOG_INFO(LOG_TAG, "是否量化: %s", model.isQuantized() ? "是" : "否");

        for (uint32_t i = 0; i < model.getInputNum(); i++)
        {
            const auto* attr = model.getInputAttr(i);
            if (attr)
            {
                LOG_INFO(LOG_TAG, "输入[%d]: %s, 尺寸=[%d,%d,%d,%d], 大小=%d 字节", i, attr->name,
                         attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
                         attr->size_with_stride);
            }
        }

        for (uint32_t i = 0; i < model.getOutputNum(); i++)
        {
            const auto* attr = model.getOutputAttr(i);
            if (attr)
            {
                int32_t zp        = 0;
                float   scale     = 0.0f;
                bool    has_quant = model.isQuantized() && model.getOutputQuantParams(i, zp, scale);

                if (has_quant)
                {
                    LOG_INFO(
                        LOG_TAG,
                        "输出[%d]: %s, 尺寸=[%d,%d,%d,%d], 大小=%d 字节, 量化参数: zp=%d, scale=%f",
                        i, attr->name, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
                        attr->size_with_stride, zp, scale);
                }
                else
                {
                    LOG_INFO(LOG_TAG, "输出[%d]: %s, 尺寸=[%d,%d,%d,%d], 大小=%d 字节", i,
                             attr->name, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
                             attr->size_with_stride);
                }
            }
        }
        LOG_INFO(LOG_TAG, "==========================================");
    }
} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    Logger::getInstance().initialize(LogConfig());

    std::unique_ptr<RKNNModel> detection_model = std::make_unique<RKNNModel>();
    RKNNError                  ret = detection_model->init(getModelPath("detection.rknn"));
    if (ret != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "检测模型加载失败");
        return EXIT_FAILURE;
    }
    printModelInfo(*detection_model, "检测");

    std::unique_ptr<RKNNModel> recognition_model = std::make_unique<RKNNModel>();
    ret = recognition_model->init(getModelPath("recognition.rknn"));
    if (ret != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "识别模型加载失败");
        detection_model->deinit();
        return EXIT_FAILURE;
    }
    printModelInfo(*recognition_model, "识别");

    LOG_INFO(LOG_TAG, "模型加载完成，按 Enter 退出...");
    std::cin.get();

    detection_model->deinit();
    recognition_model->deinit();
    return EXIT_SUCCESS;
}
