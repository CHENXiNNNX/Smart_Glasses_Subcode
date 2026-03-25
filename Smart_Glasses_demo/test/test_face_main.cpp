/* test_face_main.cpp - RTSP 推流 + RetinaFace 人脸检测 */

#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/rknn/face_detection/face_detection.hpp"
#include "app/rknn/face_recognition/face_recognition.hpp"
#include "app/rknn/rknn_config.hpp"
#include "app/tool/log/log.hpp"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

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
     * @brief 从图像中裁剪人脸区域
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
     * @brief 加载特征库（从 ./faces/ 目录读取所有用户的特征图片）
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

        for (const auto& user_entry : std::filesystem::directory_iterator(faces_path))
        {
            if (!user_entry.is_directory())
                continue;

            std::string user_name   = user_entry.path().filename().string();
            std::string user_dir    = user_entry.path().string();
            int         person_id   = user_count;
            int         image_count = 0;

            for (const auto& file_entry : std::filesystem::directory_iterator(user_dir))
            {
                if (!file_entry.is_regular_file())
                    continue;

                std::string filename  = file_entry.path().filename().string();
                std::string extension = file_entry.path().extension().string();

                if (extension != ".jpg" && extension != ".jpeg" && extension != ".JPG" &&
                    extension != ".JPEG")
                    continue;
                if (filename.find(user_name + "_") != 0)
                    continue;

                std::string feature_image_path = file_entry.path().string();
                cv::Mat     image              = cv::imread(feature_image_path);
                if (image.empty())
                {
                    LOG_WARN(LOG_TAG, "无法加载图像: %s", feature_image_path.c_str());
                    continue;
                }

                cv::Mat rgb_image;
                cv::cvtColor(image, rgb_image, cv::COLOR_BGR2RGB);

                cv::Mat resized_image;
                cv::resize(rgb_image, resized_image, cv::Size(112, 112));

                FaceFeature feature;
                RKNNError   feature_err = recognizer.extractFeature(
                      resized_image.data, resized_image.cols, resized_image.rows, feature);

                if (feature_err != RKNNError::NONE)
                {
                    LOG_ERROR(LOG_TAG, "提取特征失败: %s", feature_image_path.c_str());
                    continue;
                }

                feature_db.push_back({person_id, feature});
                image_count++;
                loaded_count++;

                LOG_INFO(LOG_TAG, "加载特征 [%d]: %s -> %s 维度=%zu", person_id, user_name.c_str(),
                         filename.c_str(), feature.size());
            }

            if (image_count > 0)
            {
                if (person_id >= static_cast<int>(person_names.size()))
                    person_names.push_back(user_name);
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

    /* JPEG 帧共享缓冲区（jpeg_cb 写入，检测线程读取） */
    struct FrameBuffer
    {
        std::mutex           mtx;
        std::vector<uint8_t> data;
        std::atomic<bool>    has_new{false};
        int                  width{0};
        int                  height{0};

        void set(const uint8_t* ptr, size_t size, int w, int h)
        {
            std::lock_guard<std::mutex> lk(mtx);
            data.assign(ptr, ptr + size);
            width  = w;
            height = h;
            has_new.store(true, std::memory_order_release);
        }

        bool take(std::vector<uint8_t>& out_data, int& out_w, int& out_h)
        {
            if (!has_new.exchange(false, std::memory_order_acquire))
                return false;
            std::lock_guard<std::mutex> lk(mtx);
            out_data = data;
            out_w    = width;
            out_h    = height;
            return !out_data.empty();
        }
    };

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
    Logger::inst().init(LogConfig());
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG_INFO(LOG_TAG, "=== RTSP推流 + RetinaFace人脸检测程序启动 ===");

    /* 1. 配置并初始化摄像头 */
    CameraCfg cam_cfg;
    cam_cfg.h264.width   = CAMERA_WIDTH;
    cam_cfg.h264.height  = CAMERA_HEIGHT;
    cam_cfg.h264.fps     = CAMERA_FPS;
    cam_cfg.h264.bitrate = H264_Default_Bitrate;
    cam_cfg.h264.gop     = H264_Default_Gop;
    cam_cfg.jpeg.width   = CAMERA_WIDTH;
    cam_cfg.jpeg.height  = CAMERA_HEIGHT;
    cam_cfg.jpeg.quality = 80;
    cam_cfg.iq_file_dir  = ISP_PATH;
    cam_cfg.enable_h264  = true;
    cam_cfg.enable_jpeg  = true;

    CameraDrv cam;
    if (cam.init(cam_cfg, nullptr) != Error::OK)
    {
        LOG_ERROR(LOG_TAG, "摄像头初始化失败");
        return -1;
    }

    /* 2. 启动 RTSP */
    if (cam.rtsp().start(static_cast<uint16_t>(RTSP_PORT), RTSP_PATH) != Error::OK)
    {
        LOG_ERROR(LOG_TAG, "RTSP 启动失败");
        cam.deinit();
        return -1;
    }

    /* 3. JPEG 帧缓冲区，供人脸检测线程使用 */
    FrameBuffer frame_buf;

    cam.set_jpeg_cb(
        [&](const FramePtr& f)
        {
            if (!f || !f->data || f->size == 0)
                return;
            frame_buf.set(f->data, f->size, static_cast<int>(f->width),
                          static_cast<int>(f->height));
        });

    if (cam.start() != Error::OK)
    {
        LOG_ERROR(LOG_TAG, "摄像头启动失败");
        cam.rtsp().stop();
        cam.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "RTSP推流已启动: rtsp://<ip>:%d%s", RTSP_PORT, RTSP_PATH);

    /* 4. 初始化人脸检测 */
    FaceDetectionConfig face_config = FaceDetectionConfig::getDefault();
    face_config.model.model_path    = "./model/retinaface.rknn";

    DetectionConfig detect_config;
    detect_config.confidence_threshold = face_config.detection.confidence_threshold;
    detect_config.nms_threshold        = face_config.detection.nms_threshold;
    detect_config.max_detections       = face_config.detection.max_detections;

    FaceDetection face_detector;
    if (face_detector.init(face_config.model.model_path, detect_config) != RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "RetinaFace模型初始化失败");
        cam.stop();
        cam.rtsp().stop();
        cam.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "RetinaFace模型输入尺寸: %dx%dx%d", face_detector.getModelWidth(),
             face_detector.getModelHeight(), face_detector.getModelChannel());

    /* 5. 初始化人脸识别 */
    FaceRecognitionConfig recog_config;
    recog_config.model.model_path                 = "./model/LZ-ArcFace.rknn";
    recog_config.recognition.similarity_threshold = 0.6f;
    recog_config.recognition.feature_dim          = 128;

    FaceRecognition face_recognizer;
    if (face_recognizer.init(recog_config.model.model_path, recog_config.recognition) !=
        RKNNError::NONE)
    {
        LOG_ERROR(LOG_TAG, "人脸识别模型初始化失败");
        face_detector.deinit();
        cam.stop();
        cam.rtsp().stop();
        cam.deinit();
        return -1;
    }

    LOG_INFO(LOG_TAG, "人脸识别模型输入尺寸: %dx%dx%d, 特征维度=%d",
             face_recognizer.getModelWidth(), face_recognizer.getModelHeight(),
             face_recognizer.getModelChannel(), face_recognizer.getFeatureDim());

    /* 6. 加载特征库 */
    std::vector<std::pair<int, FaceFeature>> feature_db;
    std::vector<std::string>                 person_names;
    int loaded_count = loadFeatureDatabase(face_recognizer, feature_db, person_names);
    if (loaded_count == 0)
        LOG_WARN(LOG_TAG, "未加载任何用户特征，识别功能将不可用");

    /* 7. 人脸检测线程 */
    const int detect_interval_ms = face_config.process.detect_interval_ms;
    const int thread_sleep_ms    = face_config.process.thread_sleep_ms;

    std::atomic<bool> detect_running{true};
    std::thread       detect_thread(
        [&]()
        {
            LOG_INFO(LOG_TAG, "RetinaFace人脸检测线程启动");
            auto last_detect_time = std::chrono::steady_clock::now();

            while (detect_running.load(std::memory_order_relaxed) &&
                   g_running.load(std::memory_order_relaxed))
            {
                auto now = std::chrono::steady_clock::now();
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_detect_time)
                        .count();

                if (elapsed < detect_interval_ms)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(thread_sleep_ms));
                    continue;
                }
                last_detect_time = now;

                std::vector<uint8_t> jpeg_data;
                int                  width = 0, height = 0;
                if (!frame_buf.take(jpeg_data, width, height) || jpeg_data.empty())
                    continue;

                /* 解码 JPEG -> RGB */
                cv::Mat bgr = cv::imdecode(jpeg_data, cv::IMREAD_COLOR);
                if (bgr.empty())
                    continue;

                cv::Mat rgb;
                cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

                int w = rgb.cols;
                int h = rgb.rows;

                /* 人脸检测 */
                DetectionResult result;
                RKNNError       detect_err = face_detector.detect(rgb.data, w, h, result);

                if (detect_err != RKNNError::NONE)
                    continue;

                if (result.count() == 0)
                    continue;

                LOG_INFO(LOG_TAG, "检测到 %zu 个人脸", result.count());

                for (size_t i = 0; i < result.count(); i++)
                {
                    const auto& face = result.faces[i];

                    int face_width  = 112;
                    int face_height = 112;
                    int face_size   = face_width * face_height * 3;

                    uint8_t* face_rgb =
                        static_cast<uint8_t*>(face_recognizer.allocateTempBuffer(face_size));
                    if (!face_rgb)
                    {
                        LOG_INFO(LOG_TAG, "  人脸[%zu]: (%d,%d,%d,%d) 置信%.3f", i, face.left,
                                       face.top, face.right, face.bottom, face.confidence);
                        continue;
                    }

                    cropFace(rgb.data, w, h, face, face_rgb, face_width, face_height);

                    FaceFeature query_feature;
                    RKNNError   feature_err = face_recognizer.extractFeature(
                                face_rgb, face_width, face_height, query_feature);

                    face_recognizer.deallocateTempBuffer(face_rgb);

                    RecognitionResult recog_result;
                    if (feature_err == RKNNError::NONE && !feature_db.empty())
                        face_recognizer.recognize(query_feature, feature_db, recog_result);

                    if (recog_result.recognized && !person_names.empty() &&
                        recog_result.person_id >= 0 &&
                        recog_result.person_id < static_cast<int>(person_names.size()))
                    {
                        LOG_INFO(LOG_TAG, "  人脸[%zu]: (%d,%d,%d,%d) 置信%.3f 识别:%s 相似%.3f", i,
                                       face.left, face.top, face.right, face.bottom, face.confidence,
                                       person_names[recog_result.person_id].c_str(),
                                       recog_result.similarity);
                    }
                    else
                    {
                        LOG_INFO(LOG_TAG, "  人脸[%zu]: (%d,%d,%d,%d) 置信%.3f 未知", i, face.left,
                                       face.top, face.right, face.bottom, face.confidence);
                    }

                    if (i == 0)
                    {
                        LOG_DEBUG(LOG_TAG,
                                        "    关键点: 左眼=(%.1f,%.1f), 右眼=(%.1f,%.1f), "
                                              "鼻尖=(%.1f,%.1f), 左嘴角=(%.1f,%.1f), 右嘴角=(%.1f,%.1f)",
                                        face.landmarks[0].x, face.landmarks[0].y, face.landmarks[1].x,
                                        face.landmarks[1].y, face.landmarks[2].x, face.landmarks[2].y,
                                        face.landmarks[3].x, face.landmarks[3].y, face.landmarks[4].x,
                                        face.landmarks[4].y);
                    }
                }
            }

            LOG_INFO(LOG_TAG, "RetinaFace人脸检测线程退出");
        });

    LOG_INFO(LOG_TAG, "程序运行中，按 Ctrl+C 退出...");

    while (g_running.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(
            std::chrono::milliseconds(face_config.process.main_loop_sleep_ms));

    LOG_INFO(LOG_TAG, "正在退出...");

    detect_running.store(false, std::memory_order_release);
    if (detect_thread.joinable())
        detect_thread.join();

    cam.stop();
    cam.rtsp().stop();
    cam.deinit();
    face_recognizer.deinit();
    face_detector.deinit();

    LOG_INFO(LOG_TAG, "程序已退出");
    return 0;
}
