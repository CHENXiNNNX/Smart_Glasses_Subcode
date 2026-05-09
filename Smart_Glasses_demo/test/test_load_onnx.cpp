/* test_load_onnx.cpp - ONNX 模型加载测试 */

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "onnxruntime_cxx_api.h"

namespace
{
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
                if (entry.is_regular_file() && entry.path().extension() == ".onnx")
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

    std::string shapeToString(const std::vector<int64_t>& dims)
    {
        std::string out = "[";
        for (size_t i = 0; i < dims.size(); ++i)
        {
            if (i > 0)
            {
                out += ", ";
            }
            out += std::to_string(dims[i]);
        }
        out += "]";
        return out;
    }

    bool printOneModelInfo(const std::string& model_path)
    {
        try
        {
            Ort::Env            env(ORT_LOGGING_LEVEL_WARNING, "test-load-onnx");
            Ort::SessionOptions session_options;
            session_options.SetIntraOpNumThreads(1);
            session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

            Ort::Session                     session(env, model_path.c_str(), session_options);
            Ort::AllocatorWithDefaultOptions allocator;

            std::cout << "模型加载成功: " << model_path << std::endl;
            std::cout << "输入数量: " << session.GetInputCount()
                      << ", 输出数量: " << session.GetOutputCount() << std::endl;

            for (size_t i = 0; i < session.GetInputCount(); ++i)
            {
                Ort::AllocatedStringPtr name = session.GetInputNameAllocated(i, allocator);
                auto type_info = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
                std::cout << "输入[" << i << "]: " << name.get()
                          << ", shape=" << shapeToString(type_info.GetShape()) << std::endl;
            }

            for (size_t i = 0; i < session.GetOutputCount(); ++i)
            {
                Ort::AllocatedStringPtr name = session.GetOutputNameAllocated(i, allocator);
                auto type_info = session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
                std::cout << "输出[" << i << "]: " << name.get()
                          << ", shape=" << shapeToString(type_info.GetShape()) << std::endl;
            }

            std::cout << std::endl;
            return true;
        }
        catch (const Ort::Exception& e)
        {
            std::cerr << "ONNX Runtime 错误: " << e.what() << std::endl;
            return false;
        }
    }
} // namespace

int main(int argc, char* argv[])
{
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
            return 1;
        }
    }

    if (!model_dir.empty() && model_dir.back() != '/')
    {
        model_dir += "/";
    }

    std::cout << "开始扫描 ONNX 模型目录: " << model_dir << std::endl;
    auto model_files = scanModelFiles(model_dir);
    if (model_files.empty())
    {
        std::cerr << "未找到任何 .onnx 模型文件" << std::endl;
        return 1;
    }

    int success = 0;
    for (const auto& model_path : model_files)
    {
        if (printOneModelInfo(model_path))
        {
            success++;
        }
    }

    std::cout << "成功加载 " << success << "/" << model_files.size() << " 个 ONNX 模型"
              << std::endl;
    return success > 0 ? 0 : 1;
}
