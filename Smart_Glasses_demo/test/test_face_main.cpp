/* test_face_main.cpp - RTSP 推流 + RetinaFace 人脸检测 */

#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/rknn/face_detection/face_detection.hpp"
#include "app/rknn/face_recognition/face_recognition.hpp"
#include "app/rknn/rknn_config.hpp"
#include "app/tool/log/log.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <thread>

using namespace app::media::camera;
using namespace app::rknn::face_detection;
using namespace app::rknn::face_recognition;
using namespace app::rknn;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "MAIN";
    std::atomic<bool>     g_running{true};

    void signal_handler(int sig)
    {
        (void)sig;
        g_running.store(false);
        LOG_INFO(LOG_TAG, "收到退出信号");
    }

    /**
     * @brief YUV420SP转RGB
     * @param yuv_data YUV420SP数据
     * @param width 图像宽度
     * @param height 图像高度
     * @param rgb_data 输出RGB数据（需要预先分配 width*height*3 字节）
     */
    void yuv420sp_to_rgb(const uint8_t* yuv_data, int width, int height, uint8_t* rgb_data)
    {
        const uint8_t* y_plane  = yuv_data;
        const uint8_t* uv_plane = yuv_data + width * height;

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int y_idx  = y * width + x;
                int uv_idx = (y / 2) * width + (x & ~1);

                int Y = static_cast<int>(y_plane[y_idx]);
                int U = static_cast<int>(uv_plane[uv_idx]) - 128;
                int V = static_cast<int>(uv_plane[uv_idx + 1]) - 128;

                // YUV转RGB公式
                int R = Y + static_cast<int>(1.402f * V);
                int G = Y - static_cast<int>(0.344f * U + 0.714f * V);
                int B = Y + static_cast<int>(1.772f * U);

                // 限制在[0,255]范围内
                R = std::max(0, std::min(255, R));
                G = std::max(0, std::min(255, G));
                B = std::max(0, std::min(255, B));

                int rgb_idx           = (y * width + x) * 3;
                rgb_data[rgb_idx + 0] = static_cast<uint8_t>(R);
                rgb_data[rgb_idx + 1] = static_cast<uint8_t>(G);
                rgb_data[rgb_idx + 2] = static_cast<uint8_t>(B);
            }
        }
    }

    /**
     * @brief 从图像中裁剪人脸区域
     * @param rgb_data 原始RGB图像数据
     * @param width 图像宽度
     * @param height 图像高度
     * @param face_box 人脸检测框
     * @param face_rgb 输出的人脸RGB数据（需要预先分配）
     * @param face_width 输出人脸宽度
     * @param face_height 输出人脸高度
     */
    void cropFace(const uint8_t* rgb_data, int width, int height, const FaceBox& face_box,
                  uint8_t* face_rgb, int face_width, int face_height)
    {
        int face_left   = std::max(0, face_box.left);
        int face_top    = std::max(0, face_box.top);
        int face_right  = std::min(width - 1, face_box.right);
        int face_bottom = std::min(height - 1, face_box.bottom);

        float scale_x = static_cast<float>(face_right - face_left) / static_cast<float>(face_width);
        float scale_y =
            static_cast<float>(face_bottom - face_top) / static_cast<float>(face_height);

        for (int y = 0; y < face_height; y++)
        {
            for (int x = 0; x < face_width; x++)
            {
                int src_x = face_left + static_cast<int>(x * scale_x);
                int src_y = face_top + static_cast<int>(y * scale_y);
                src_x     = std::min(src_x, face_right);
                src_y     = std::min(src_y, face_bottom);

                int src_idx = (src_y * width + src_x) * 3;
                int dst_idx = (y * face_width + x) * 3;

                face_rgb[dst_idx + 0] = rgb_data[src_idx + 0];
                face_rgb[dst_idx + 1] = rgb_data[src_idx + 1];
                face_rgb[dst_idx + 2] = rgb_data[src_idx + 2];
            }
        }
    }

    /**
     * @brief 加载特征库（从./faces/目录读取所有用户的特征图片）
     * @param recognizer 人脸识别器
     * @param feature_db 输出特征库
     * @param person_names 输出人员名称列表
     * @return 成功加载的特征图片数量
     */
    int loadFeatureDatabase(FaceRecognition&                          recognizer,
                            std::vector<std::pair<int, FaceFeature>>& feature_db,
                            std::vector<std::string>&                 person_names)
    {
        const std::string faces_dir    = "./faces/";
        int               loaded_count = 0;
        int               user_count   = 0;

        std::filesystem::path faces_path(faces_dir);
        if (!std::filesystem::exists(faces_path))
        {
            LOG_WARN(LOG_TAG, "特征图片目录不存在: %s", faces_dir.c_str());
            return 0;
        }

        // 遍历所有用户目录
        for (const auto& user_entry : std::filesystem::directory_iterator(faces_path))
        {
            if (!user_entry.is_directory())
            {
                continue;
            }

            std::string user_name   = user_entry.path().filename().string();
            std::string user_dir    = user_entry.path().string();
            int         person_id   = user_count;
            int         image_count = 0;

            // 查找该用户目录下的所有特征图片（格式：名字_*.jpg）
            std::filesystem::path user_path(user_dir);
            for (const auto& file_entry : std::filesystem::directory_iterator(user_path))
            {
                if (!file_entry.is_regular_file())
                {
                    continue;
                }

                std::string filename  = file_entry.path().filename().string();
                std::string extension = file_entry.path().extension().string();

                // 检查文件名格式：用户名_*.jpg
                if (extension != ".jpg" && extension != ".jpeg" && extension != ".JPG" &&
                    extension != ".JPEG")
                {
                    continue;
                }

                // 检查文件名是否以用户名开头
                if (filename.find(user_name + "_") != 0)
                {
                    continue;
                }

                std::string feature_image_path = file_entry.path().string();

                // 使用OpenCV加载图像
                cv::Mat image = cv::imread(feature_image_path);
                if (image.empty())
                {
                    LOG_WARN(LOG_TAG, "无法加载图像: %s", feature_image_path.c_str());
                    continue;
                }

                // 转换为RGB格式
                cv::Mat rgb_image;
                cv::cvtColor(image, rgb_image, cv::COLOR_BGR2RGB);

                // 缩放到112x112（识别模型输入尺寸）
                cv::Mat resized_image;
                cv::resize(rgb_image, resized_image, cv::Size(112, 112));

                // 提取特征
                FaceFeature feature;
                RKNNError   feature_err = recognizer.extractFeature(
                      resized_image.data, resized_image.cols, resized_image.rows, feature);

                if (feature_err != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "提取特征失败: %s", feature_image_path.c_str());
                    continue;
                }

                // 添加到特征库（同一个用户的所有特征使用相同的person_id）
                feature_db.push_back({person_id, feature});
                image_count++;
                loaded_count++;

                LOG_INFO(LOG_TAG, "加载特征 [%d]: %s -> %s 维度=%u", person_id, user_name.c_str(),
                         filename.c_str(), static_cast<unsigned>(feature.size()));
            }

            if (image_count > 0)
            {
                // 只在成功加载至少一张图片时才添加用户名
                if (person_id >= static_cast<int>(person_names.size()))
                {
                    person_names.push_back(user_name);
                }
                user_count++;
                LOG_INFO(LOG_TAG, "用户 %s 共加载 %d 张特征图片", user_name.c_str(), image_count);
            }
            else
            {
                LOG_WARN(LOG_TAG, "用户 %s 目录下未找到有效的特征图片", user_name.c_str());
            }
        }

        LOG_INFO(LOG_TAG, "特征库加载完成，共 %d 个用户，%d 张特征图片", user_count, loaded_count);
        return loaded_count;
    }
} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    // 初始化日志系统
    Logger::inst().init(LogConfig());

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG_INFO(LOG_TAG, "=== RTSP推流 + RetinaFace人脸检测程序启动 ===");

    // 配置视频系统
    VideoConfig video_config;
    video_config.width   = CAMERA_WIDTH;
    video_config.height  = CAMERA_HEIGHT;
    video_config.fps     = CAMERA_FPS;
    video_config.format  = EncodeFormat::H264;
    video_config.bitrate = H264_Default_Bitrate;
    video_config.gop     = H264_Default_Gop;

    VideoSystem video_system(video_config);
    VideoError  ret = video_system.init();
    if (ret != VideoError::NONE)
    {
        LOG_ERROR(LOG_TAG, "视频系统初始化失败");
        return -1;
    }

    // 配置人脸检测
    FaceDetectionConfig face_config = FaceDetectionConfig::getDefault();
    // 使用RetinaFace模型
    face_config.model.model_path = "./model/retinaface.rknn";

    DetectionConfig detect_config;
    detect_config.confidence_threshold = face_config.detection.confidence_threshold;
    detect_config.nms_threshold        = face_config.detection.nms_threshold;
    detect_config.max_detections       = face_config.detection.max_detections;

    FaceDetection face_detector;
    RKNNError     rknn_ret = face_detector.init(face_config.model.model_path, detect_config);
    if (rknn_ret != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "RetinaFace模型初始化失败");
        video_system.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "RetinaFace模型输入尺寸: %dx%dx%d", face_detector.getModelWidth(),
             face_detector.getModelHeight(), face_detector.getModelChannel());

    // 初始化人脸识别
    FaceRecognitionConfig recog_config            = FaceRecognitionConfig();
    recog_config.model.model_path                 = "./model/LZ-ArcFace.rknn";
    recog_config.recognition.similarity_threshold = 0.6f;
    recog_config.recognition.feature_dim          = 128;

    FaceRecognition face_recognizer;
    RKNNError       recog_ret =
        face_recognizer.init(recog_config.model.model_path, recog_config.recognition);
    if (recog_ret != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "人脸识别模型初始化失败");
        face_detector.deinit();
        video_system.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "人脸识别模型输入尺寸: %dx%dx%d, 特征维度=%d",
             face_recognizer.getModelWidth(), face_recognizer.getModelHeight(),
             face_recognizer.getModelChannel(), face_recognizer.getFeatureDim());

    // 加载特征库
    std::vector<std::pair<int, FaceFeature>> feature_db;
    std::vector<std::string>                 person_names;
    int loaded_count = loadFeatureDatabase(face_recognizer, feature_db, person_names);
    if (loaded_count == 0)
    {
        LOG_WARN(LOG_TAG, "未加载任何用户特征，识别功能将不可用");
    }

    // 启动视频流
    ret = video_system.startStream();
    if (ret != VideoError::NONE)
    {
        LOG_ERROR(LOG_TAG, "启动视频流失败");
        face_detector.deinit();
        video_system.deinit();
        return -1;
    }

    // 启动RTSP推流
    ret = video_system.startRTSPMode(554, "/live/0");
    if (ret != VideoError::NONE)
    {
        LOG_ERROR(LOG_TAG, "启动RTSP推流失败");
        video_system.stopStream();
        face_detector.deinit();
        video_system.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "RTSP推流已启动: rtsp://<ip>:554/live/0");

    // 启动人脸检测线程
    std::atomic<bool> detect_running{true};
    std::thread       detect_thread(
        [&]()
        {
            LOG_INFO(LOG_TAG, "RetinaFace人脸检测线程启动");

            const int detect_interval_ms = face_config.process.detect_interval_ms;
            auto      last_detect_time   = std::chrono::steady_clock::now();

            while (detect_running.load() && g_running.load())
            {
                auto now = std::chrono::steady_clock::now();
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_detect_time)
                        .count();

                if (elapsed < detect_interval_ms)
                {
                    std::this_thread::sleep_for(
                              std::chrono::milliseconds(face_config.process.thread_sleep_ms));
                    continue;
                }

                last_detect_time = now;

                // 获取原始视频帧
                RawVideoFramePtr raw_frame;
                VideoError       frame_err =
                    video_system.getRawFrame(raw_frame, face_config.process.frame_timeout_ms);
                if (frame_err != VideoError::NONE || !raw_frame)
                {
                    continue;
                }

                int width    = static_cast<int>(raw_frame->width);
                int height   = static_cast<int>(raw_frame->height);
                int rgb_size = width * height * 3; // RGB图像大小

                // 分配RGB缓冲区
                uint8_t* rgb_data =
                    static_cast<uint8_t*>(face_detector.allocateTempBuffer(rgb_size));
                if (!rgb_data)
                {
                    LOG_WARN(LOG_TAG, "分配RGB缓冲区失败");
                    raw_frame.reset();
                    continue;
                }

                // YUV420SP转RGB
                yuv420sp_to_rgb(raw_frame->data, width, height, rgb_data);

                // 执行人脸检测
                DetectionResult result;
                RKNNError       detect_err = face_detector.detect(rgb_data, width, height, result);

                face_detector.deallocateTempBuffer(rgb_data);
                raw_frame.reset();

                if (detect_err == RKNNError::NONE)
                {
                    if (result.count() > 0)
                    {
                        LOG_INFO(LOG_TAG, "检测到 %u 个人脸",
                                       static_cast<unsigned>(result.count()));
                        for (size_t i = 0; i < result.count(); i++)
                        {
                            const auto& face = result.faces[i];

                            // 裁剪人脸区域用于识别
                            int face_width  = 112;
                            int face_height = 112;
                            int face_size   = face_width * face_height * 3;

                            uint8_t* face_rgb = static_cast<uint8_t*>(
                                face_recognizer.allocateTempBuffer(face_size));
                            if (face_rgb)
                            {
                                cropFace(rgb_data, width, height, face, face_rgb, face_width,
                                               face_height);

                                // 提取特征
                                FaceFeature query_feature;
                                RKNNError   feature_err = face_recognizer.extractFeature(
                                            face_rgb, face_width, face_height, query_feature);

                                face_recognizer.deallocateTempBuffer(face_rgb);

                                // 识别
                                RecognitionResult recog_result;
                                if (feature_err == RKNNError::NONE && !feature_db.empty())
                                {
                                    face_recognizer.recognize(query_feature, feature_db,
                                                                    recog_result);
                                }

                                // 打印检测和识别结果
                                if (recog_result.recognized && !person_names.empty() &&
                                    recog_result.person_id >= 0 &&
                                    recog_result.person_id < static_cast<int>(person_names.size()))
                                {
                                    LOG_INFO(LOG_TAG,
                                                   "  人脸[%u]: (%d,%d,%d,%d) 置信%.3f 识别:%s 相似%.3f",
                                                   static_cast<unsigned>(i), face.left, face.top,
                                                   face.right, face.bottom, face.confidence,
                                                   person_names[recog_result.person_id].c_str(),
                                                   recog_result.similarity);
                                }
                                else
                                {
                                    LOG_INFO(LOG_TAG, "  人脸[%u]: (%d,%d,%d,%d) 置信%.3f 未知",
                                                   static_cast<unsigned>(i), face.left, face.top,
                                                   face.right, face.bottom, face.confidence);
                                }

                                // 打印关键点信息（仅第一个）
                                if (i == 0)
                                {
                                    LOG_DEBUG(LOG_TAG,
                                                    "    关键点: 左眼=(%.1f,%.1f), 右眼=(%.1f,%.1f), "
                                                          "鼻尖=(%.1f,%.1f), 左嘴角=(%.1f,%.1f), "
                                                          "右嘴角=(%.1f,%.1f)",
                                                    face.landmarks[0].x, face.landmarks[0].y,
                                                    face.landmarks[1].x, face.landmarks[1].y,
                                                    face.landmarks[2].x, face.landmarks[2].y,
                                                    face.landmarks[3].x, face.landmarks[3].y,
                                                    face.landmarks[4].x, face.landmarks[4].y);
                                }
                            }
                            else
                            {
                                LOG_INFO(LOG_TAG, "  人脸[%u]: (%d,%d,%d,%d) 置信%.3f",
                                               static_cast<unsigned>(i), face.left, face.top, face.right,
                                               face.bottom, face.confidence);
                            }
                        }
                    }
                }
            }

            LOG_INFO(LOG_TAG, "RetinaFace人脸检测线程退出");
        });

    LOG_INFO(LOG_TAG, "程序运行中，按 Ctrl+C 退出...");
    while (g_running.load())
    {
        // 主循环：可以在这里处理其他任务
        std::this_thread::sleep_for(
            std::chrono::milliseconds(face_config.process.main_loop_sleep_ms));
    }

    LOG_INFO(LOG_TAG, "正在退出...");

    // 停止检测线程
    detect_running.store(false);
    if (detect_thread.joinable())
    {
        detect_thread.join();
    }

    // 清理资源
    video_system.stopRTSPMode();
    video_system.stopStream();
    face_recognizer.deinit();
    face_detector.deinit();
    video_system.deinit();

    LOG_INFO(LOG_TAG, "程序已退出");
    return 0;
}
