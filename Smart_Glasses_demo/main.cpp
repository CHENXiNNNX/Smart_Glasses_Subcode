/**
 * @file main.cpp
 * @brief 主程序：RTSP推流 + RetinaFace人脸检测
 */

#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/tool/log/log.hpp"
#include "app/rknn/face_detection/face_detection.hpp"
#include "app/rknn/rknn_config.hpp"
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <algorithm>

using namespace app::media::camera;
using namespace app::rknn::face_detection;
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
} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    // 初始化日志系统
    Logger::getInstance().init(LogConfig());

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
                        LOG_INFO(LOG_TAG, "检测到 %zu 个人脸:", result.count());
                        for (size_t i = 0; i < result.count(); i++)
                        {
                            const auto& face = result.faces[i];
                            LOG_INFO(LOG_TAG, "  人脸[%zu]: 位置=(%d,%d,%d,%d), 置信度=%.3f", i,
                                           face.left, face.top, face.right, face.bottom, face.confidence);

                            // 打印关键点信息
                            if (i == 0) // 只打印第一个人的关键点，避免日志过多
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
    face_detector.deinit();
    video_system.deinit();

    LOG_INFO(LOG_TAG, "程序已退出");
    return 0;
}
