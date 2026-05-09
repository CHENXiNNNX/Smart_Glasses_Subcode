/* test_load_rknn.cpp - RKNN 模型加载测试 */

#include "app/rknn/rknn.hpp"
#include "app/tool/log/log.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace app::rknn;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "LOAD";

    std::vector<std::string> scanModelFiles(const std::string& dir_path)
    {
        std::vector<std::string> model_files;
        try
        {
            if (!std::filesystem::exists(dir_path))
            {
                std::cerr << "模型目录不存在: " << dir_path << std::endl;
                return model_files;
            }

            for (const auto& entry : std::filesystem::directory_iterator(dir_path))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".rknn")
                {
                    model_files.push_back(entry.path().string());
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "扫描目录失败: " << e.what() << std::endl;
        }
        return model_files;
    }

    std::string getModelNameFromPath(const std::string& file_path)
    {
        return std::filesystem::path(file_path).filename().string();
    }

    void printModelInfo(const RKNNModel& model, const std::string& model_name)
    {
        LOG_INFO(LOG_TAG, "模型 %s", model_name.c_str());
        LOG_INFO(LOG_TAG, "输入数量: %d, 输出数量: %d", model.getInputNum(), model.getOutputNum());
        LOG_INFO(LOG_TAG, "模型尺寸: %dx%dx%d", model.getModelWidth(), model.getModelHeight(),
                 model.getModelChannel());
        LOG_INFO(LOG_TAG, "是否量化: %s", model.isQuantized() ? "是" : "否");

        // 打印输入信息
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

        // 打印输出信息
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
    }
} // namespace

int main(int argc, char* argv[])
{
    // 初始化日志系统
    Logger::inst().init(LogConfig());

    std::string model_dir = "/root/bin/assets/models/";

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--path") && (i + 1) < argc)
        {
            model_dir = argv[++i];
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << "用法: " << argv[0] << " [-p 模型目录]\n";
            std::cout << "示例: " << argv[0] << " -p /root/bin/assets/models\n";
            return 0;
        }
        else
        {
            std::cerr << "未知参数: " << arg << std::endl;
            std::cerr << "使用 -h 查看帮助" << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (!model_dir.empty() && model_dir.back() != '/')
    {
        model_dir += "/";
    }

    std::cout << "开始扫描 RKNN 模型目录: " << model_dir << std::endl;

    // 扫描所有模型文件
    std::vector<std::string> model_files = scanModelFiles(model_dir);

    if (model_files.empty())
    {
        LOG_ERROR(LOG_TAG, "未找到任何模型文件 (.rknn)");
        return EXIT_FAILURE;
    }

    LOG_INFO(LOG_TAG, "共找到 %u 个模型文件", static_cast<unsigned>(model_files.size()));
    LOG_INFO(LOG_TAG, "");

    // 加载并打印每个模型的详细信息
    std::vector<std::unique_ptr<RKNNModel>> loaded_models;

    for (const auto& model_path : model_files)
    {
        std::string model_name = getModelNameFromPath(model_path);
        LOG_INFO(LOG_TAG, "正在加载模型: %s", model_path.c_str());

        auto      model = std::make_unique<RKNNModel>();
        RKNNError ret   = model->init(model_path);

        if (ret != RKNNError::NONE)
        {
            LOG_ERROR(LOG_TAG, "模型加载失败: %s (错误码: %d)", model_path.c_str(),
                      static_cast<int>(ret));
            LOG_INFO(LOG_TAG, "");
            continue;
        }

        // 打印模型信息
        printModelInfo(*model, model_name);
        LOG_INFO(LOG_TAG, "");

        // 保存已加载的模型
        loaded_models.push_back(std::move(model));
    }

    LOG_INFO(LOG_TAG, "成功加载 %u/%u 个模型", static_cast<unsigned>(loaded_models.size()),
             static_cast<unsigned>(model_files.size()));

    if (loaded_models.empty())
    {
        LOG_WARN(LOG_TAG, "没有成功加载任何模型");
    }
    else
    {
        LOG_INFO(LOG_TAG, "按 Enter 键退出...");
        std::cin.get();
    }

    // 清理资源
    for (auto& model : loaded_models)
    {
        if (model)
        {
            model->deinit();
        }
    }

    return loaded_models.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
}
