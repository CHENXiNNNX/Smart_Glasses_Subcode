/**
 * @file main.cpp
 * @brief 主程序：RTSP推流 + 人脸检测
 */

#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/tool/log/log.hpp"
#include "app/rknn/face_detection/face_detection.hpp"
#include "app/rknn/face_detection/detection_queue.hpp"
#include "app/rknn/rknn_config.hpp"
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>

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
     * @brief YUV420SP转灰度图
     * @param yuv_data YUV420SP数据（Y平面在前）
     * @param width 图像宽度
     * @param height 图像高度
     * @param gray_data 输出灰度图数据（需要预先分配 width*height 字节）
     */
    void yuv420sp_to_gray(const uint8_t* yuv_data, int width, int height, uint8_t* gray_data)
    {
        const uint8_t* y_plane = yuv_data;
        std::memcpy(gray_data, y_plane, static_cast<size_t>(width * height));
    }
} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    // 初始化日志系统
    Logger::getInstance().init(LogConfig());

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG_INFO(LOG_TAG, "=== RTSP推流 + 人脸检测程序启动 ===");

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

    FaceDetectionConfig face_config = FaceDetectionConfig::getDefault();

    DetectionConfig detect_config;
    detect_config.confidence_threshold = face_config.detection.confidence_threshold;
    detect_config.nms_threshold        = face_config.detection.nms_threshold;
    detect_config.max_detections       = face_config.detection.max_detections;

    FaceDetection face_detector;
    RKNNError     rknn_ret = face_detector.init(face_config.model.model_path, detect_config);
    if (rknn_ret != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "人脸检测模型初始化失败");
        video_system.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "模型输入尺寸: %dx%dx%d", face_detector.getModelWidth(),
             face_detector.getModelHeight(), face_detector.getModelChannel());

    ret = video_system.startStream();
    if (ret != VideoError::NONE)
    {
        LOG_ERROR(LOG_TAG, "启动视频流失败");
        face_detector.deinit();
        video_system.deinit();
        return -1;
    }

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

    DetectionResultQueue detection_queue(face_config.queue.max_size,
                                         face_config.queue.result_ttl_ms);

    std::atomic<bool> detect_running{true};
    std::thread       detect_thread(
        [&]()
        {
            LOG_INFO(LOG_TAG, "人脸检测线程启动");

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

                RawVideoFramePtr raw_frame;
                VideoError       frame_err =
                    video_system.getRawFrame(raw_frame, face_config.process.frame_timeout_ms);
                if (frame_err != VideoError::NONE || !raw_frame)
                {
                    continue;
                }

                int width     = static_cast<int>(raw_frame->width);
                int height    = static_cast<int>(raw_frame->height);
                int gray_size = width * height;

                uint8_t* gray_data =
                    static_cast<uint8_t*>(face_detector.allocateTempBuffer(gray_size));
                if (!gray_data)
                {
                    raw_frame.reset();
                    continue;
                }

                yuv420sp_to_gray(raw_frame->data, width, height, gray_data);

                DetectionResult result;
                RKNNError       detect_err = face_detector.detect(gray_data, width, height, result);

                face_detector.deallocateTempBuffer(gray_data);
                raw_frame.reset();

                if (detect_err == RKNNError::NONE)
                {
                    detection_queue.push(result);

                    if (result.count() > 0)
                    {
                        LOG_INFO(LOG_TAG, "检测到 %zu 个人脸:", result.count());
                        for (size_t i = 0; i < result.count(); i++)
                        {
                            const auto& face = result.faces[i];
                            LOG_INFO(LOG_TAG, "  人脸[%zu]: 位置=(%d,%d,%d,%d), 置信度=%.3f", i,
                                           face.left, face.top, face.right, face.bottom, face.confidence);
                        }
                    }
                }
            }

            LOG_INFO(LOG_TAG, "人脸检测线程退出");
        });

    LOG_INFO(LOG_TAG, "程序运行中，按 Ctrl+C 退出...");
    while (g_running.load())
    {
        auto latest_result = detection_queue.tryPeek();
        if (latest_result.has_value() && latest_result->count() > 0)
        {
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(face_config.process.main_loop_sleep_ms));
    }

    LOG_INFO(LOG_TAG, "正在退出...");

    detect_running.store(false);
    if (detect_thread.joinable())
    {
        detect_thread.join();
    }

    video_system.stopRTSPMode();
    video_system.stopStream();
    face_detector.deinit();
    video_system.deinit();

    LOG_INFO(LOG_TAG, "程序已退出");
    return 0;
}
