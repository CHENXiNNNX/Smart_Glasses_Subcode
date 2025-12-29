/**
 * @file test_camera_main.cpp
 * @brief VideoSystem 完整测试程序
 * @details 测试内容：
 *          1. 单次拍照测试
 *          2. 连续拍照测试（10次）
 *          3. 不同质量参数拍照测试
 *          4. ISP参数配置测试（曝光、白平衡、亮度、对比度、饱和度、锐度、除雾）
 *          5. H264录像测试
 */

 #include "../app/media/camera/camera.hpp"
 #include "../app/tool/log/log.hpp"
 #include "../app/media/sync.hpp"
 #include <iostream>
 #include <thread>
 #include <chrono>
 #include <csignal>
 #include <sys/stat.h>
 #include <unistd.h>
 
using namespace app::media::camera;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "TEST_CAMERA";
} // namespace

// 全局标志：控制程序退出
std::atomic<bool> g_quit{false};
 
 void signalHandler(int signal) {
     LOG_INFO(LOG_TAG, "收到信号 %d，正在退出...", signal);
     g_quit.store(true);
 }
 
 // 工具函数：确保目录存在
 bool ensureDirectory(const std::string& path) {
     struct stat st;
     if (stat(path.c_str(), &st) != 0) {
         // 目录不存在，创建它
         std::string cmd = "mkdir -p " + path;
         if (system(cmd.c_str()) != 0) {
             LOG_ERROR(LOG_TAG, "无法创建目录: %s", path.c_str());
             return false;
         }
         LOG_INFO(LOG_TAG, "创建目录: %s", path.c_str());
     }
     return true;
 }
 
 // 工具函数：等待拍照完成
 bool waitForPhotoComplete(VideoSystem& video, int timeout_ms = 5000) {
     int waited = 0;
     while (video.isPhotoCapturing() && waited < timeout_ms) {
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
         waited += 100;
     }
     return !video.isPhotoCapturing();
 }
 
 // 工具函数：等待录像完成
 void waitForRecordComplete(VideoSystem& video) {
     while (video.isRecording() && !g_quit.load()) {
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
     }
 }
 
 int main() {
     // 信号处理
     std::signal(SIGINT, signalHandler);
     std::signal(SIGTERM, signalHandler);
     
     // ========================================
     // 初始化日志系统
     // ========================================
     LogConfig log_config;
     log_config.enable_console = true;
     log_config.enable_file = true;
     log_config.enable_color = true;
     log_config.enable_timestamp = false;
     log_config.enable_thread_id = false;
     log_config.log_file_path = "./log/camera_test.log";
     log_config.max_file_size = 10 * 1024 * 1024;  // 10MB
     log_config.min_level = LogLevel::INFO;
     
     Logger& logger = Logger::getInstance();
     if (!logger.initialize(log_config)) {
         std::cerr << "日志系统初始化失败" << std::endl;
         return -1;
     }
     
     LOG_INFO(LOG_TAG, "========================================");
     LOG_INFO(LOG_TAG, "  VideoSystem 完整测试程序");
     LOG_INFO(LOG_TAG, "========================================");
     
     // ========================================
     // 创建时间同步上下文
     // ========================================
     auto sync_ctx = std::make_shared<sync_context_t>();
     if (sync_init(sync_ctx.get()) != 0) {
         LOG_ERROR(LOG_TAG, "同步上下文初始化失败");
         return -1;
     }
     LOG_INFO(LOG_TAG, "同步上下文初始化成功");
     
     // ========================================
     // 创建测试目录
     // ========================================
     const std::string photo_base = "/root/test_photos/";
     const std::string video_base = "/root/test_videos/";
     
     if (!ensureDirectory(photo_base) || !ensureDirectory(video_base)) {
         LOG_ERROR(LOG_TAG, "无法创建测试目录");
         sync_deinit(sync_ctx.get());
         return -1;
     }
     
     // ========================================
     // 配置视频系统
     // ========================================
     VideoConfig config;
     config.width = 1920;           // 1080P分辨率
     config.height = 1080;
     config.fps = 30;
     config.format = EncodeFormat::H264;
     config.bitrate = 6 * 1024;     // 6Mbps
     config.gop = 30;
     config.quality = 95;           // 默认高质量
     config.photo_path = photo_base;
     config.record_path = video_base;
     config.enable_dma_zero_copy = true;
     
     // ========================================
     // 初始化视频系统
     // ========================================
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "测试 0: 初始化 VideoSystem");
     LOG_INFO(LOG_TAG, "========================================");
     
     VideoSystem video(config);
     if (video.initialize(sync_ctx) != VideoError::NONE) {
         LOG_ERROR(LOG_TAG, "视频系统初始化失败");
         sync_deinit(sync_ctx.get());
         return -1;
     }
     LOG_INFO(LOG_TAG, "VideoSystem 初始化成功");
     
     // 启动视频流
     if (video.startStream() != VideoError::NONE) {
         LOG_ERROR(LOG_TAG, "启动视频流失败");
         video.shutdown();
         sync_deinit(sync_ctx.get());
         return -1;
     }
     LOG_INFO(LOG_TAG, "视频流启动成功");
     
     // 等待流稳定
     std::this_thread::sleep_for(std::chrono::seconds(2));
     LOG_INFO(LOG_TAG, "当前FPS: %.2f", video.getCurrentFPS());
     
     // ========================================
     // 测试 1: 单次拍照
     // ========================================
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "测试 1: 单次拍照测试");
     LOG_INFO(LOG_TAG, "========================================");
     
     video.setMainState(VideoMainState::PHOTO);
     
     if (video.takePhoto() == VideoError::NONE) {
         LOG_INFO(LOG_TAG, "拍照开始...");
         if (waitForPhotoComplete(video)) {
             LOG_INFO(LOG_TAG, "单次拍照成功");
             LOG_INFO(LOG_TAG, "  照片保存路径: %s", photo_base.c_str());
         } else {
             LOG_WARN(LOG_TAG, "⚠ 拍照超时");
         }
         
         // 恢复H264编码器
         video.restoreH264Encoder();
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
     } else {
         LOG_ERROR(LOG_TAG, "✗ 单次拍照失败");
     }
     
     // ========================================
     // 测试 2: 连续10次拍照
     // ========================================
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "测试 2: 连续10次拍照测试");
     LOG_INFO(LOG_TAG, "========================================");
     
     int success_count = 0;
     for (int i = 1; i <= 10; i++) {
         LOG_INFO(LOG_TAG, "拍照 %d/10...", i);
         
         if (video.takePhoto() == VideoError::NONE) {
             if (waitForPhotoComplete(video, 3000)) {
                 success_count++;
                 LOG_INFO(LOG_TAG, "  拍照 %d 成功", i);
             } else {
                 LOG_WARN(LOG_TAG, "  ⚠ 拍照 %d 超时", i);
             }
             
             // 恢复H264编码器
             video.restoreH264Encoder();
             
             // 短暂延迟，避免过快切换
             std::this_thread::sleep_for(std::chrono::milliseconds(300));
         } else {
             LOG_ERROR(LOG_TAG, "  ✗ 拍照 %d 失败", i);
         }
     }
     
     LOG_INFO(LOG_TAG, "连续拍照测试完成: %d/10 成功", success_count);
     
     // ========================================
     // 测试 3: 不同质量参数拍照测试
     // ========================================
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "测试 3: 不同JPEG质量参数拍照测试");
     LOG_INFO(LOG_TAG, "========================================");
     
     // 创建质量测试子目录
     std::string quality_test_dir = photo_base + "quality_test/";
     ensureDirectory(quality_test_dir);
     
     struct QualityTest {
         int quality;
         std::string description;
     };
     
     std::vector<QualityTest> quality_tests = {
         {100, "质量100 - 最高质量"},
         {95,  "质量95 - 极高质量"},
         {80,  "质量80 - 高质量"},
         {50,  "质量50 - 中等质量"},
         {30,  "质量30 - 低质量"},
         {1,   "质量1 - 最低质量"}
     };
     
     for (const auto& test : quality_tests) {
         LOG_INFO(LOG_TAG, "测试 %s...", test.description.c_str());
         
         // 设置JPEG质量
         if (video.setJPEGQuality(test.quality) != VideoError::NONE) {
             LOG_ERROR(LOG_TAG, "  ✗ 设置质量失败");
             continue;
         }
         
         // 拍照
         if (video.takePhoto() == VideoError::NONE) {
             if (waitForPhotoComplete(video)) {
                 LOG_INFO(LOG_TAG, "  质量%d 拍照成功", test.quality);
             }
             video.restoreH264Encoder();
             std::this_thread::sleep_for(std::chrono::milliseconds(300));
         }
     }
     
     // 恢复默认质量
     video.setJPEGQuality(95);
     LOG_INFO(LOG_TAG, "质量测试完成，已恢复默认质量95");
     
     //  // ========================================
     //  // 测试 4: ISP参数配置测试
     //  // ========================================
     //  LOG_INFO(LOG_TAG, "\n========================================");
     //  LOG_INFO(LOG_TAG, "测试 4: ISP参数配置测试");
     //  LOG_INFO(LOG_TAG, "========================================");
     
     //  // 创建ISP测试子目录
     //  std::string isp_test_dir = photo_base + "isp_test/";
     //  ensureDirectory(isp_test_dir);
     
     //  // 4.1 基准照片（默认ISP参数）
     //  LOG_INFO(LOG_TAG, "\n4.1 基准照片（默认ISP参数）");
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "基准照片保存");
     
     //  // 4.2 曝光控制测试
     //  LOG_INFO(LOG_TAG, "\n4.2 曝光控制测试");
     
     //  // 自动曝光模式
     //  LOG_INFO(LOG_TAG, "  测试：自动曝光模式");
     //  video.setExposureMode(OP_AUTO);
     //  std::this_thread::sleep_for(std::chrono::seconds(1));  // 等待AE稳定
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  自动曝光照片保存");
     
     //  // 手动曝光 - 高曝光（增大增益范围）
     //  LOG_INFO(LOG_TAG, "  测试：手动高曝光（高增益）");
     //  video.setExposureMode(OP_MANUAL);
     //  video.setExpGainRange(4.0f, 16.0f);    // 高增益：4x-16x
     //  video.setExpTimeRange(0.01f, 0.05f);   // 较长曝光时间：10ms-50ms
     //  std::this_thread::sleep_for(std::chrono::seconds(1));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  高曝光照片保存（增益4-16x，时间10-50ms）");
     
     //  // 手动曝光 - 低曝光
     //  LOG_INFO(LOG_TAG, "  测试：手动低曝光（低增益）");
     //  video.setExpGainRange(1.0f, 2.0f);     // 低增益：1x-2x
     //  video.setExpTimeRange(0.001f, 0.005f); // 短曝光时间：1ms-5ms
     //  std::this_thread::sleep_for(std::chrono::seconds(1));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  低曝光照片保存（增益1-2x，时间1-5ms）");
     
     //  // 恢复自动曝光
     //  video.setExposureMode(OP_AUTO);
     //  std::this_thread::sleep_for(std::chrono::seconds(1));
     
     //  // 4.3 白平衡测试
     //  LOG_INFO(LOG_TAG, "\n4.3 白平衡测试");
     
     //  // 自动白平衡
     //  LOG_INFO(LOG_TAG, "  测试：自动白平衡");
     //  video.setWhiteBalanceMode(OP_AUTO);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(800));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  自动白平衡照片保存");
     
     //  // 手动白平衡 - 暖色调（色温2800K）
     //  LOG_INFO(LOG_TAG, "  测试：暖色调白平衡（2800K）");
     //  video.setWhiteBalanceMode(OP_MANUAL);
     //  video.setColorTemperature(2800);  // 暖色调
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  暖色调照片保存");
     
     //  // 手动白平衡 - 冷色调（色温6500K）
     //  LOG_INFO(LOG_TAG, "  测试：冷色调白平衡（6500K）");
     //  video.setColorTemperature(6500);  // 冷色调
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  冷色调照片保存");
     
     //  // 恢复自动白平衡
     //  video.setWhiteBalanceMode(OP_AUTO);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(800));
     
     //  // 4.4 亮度、对比度、饱和度测试
     //  LOG_INFO(LOG_TAG, "\n4.4 亮度、对比度、饱和度测试");
     
     //  // 高亮度
     //  LOG_INFO(LOG_TAG, "  测试：高亮度（80）");
     //  video.setBrightness(80);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  高亮度照片保存");
     
     //  // 低亮度
     //  LOG_INFO(LOG_TAG, "  测试：低亮度（20）");
     //  video.setBrightness(20);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  低亮度照片保存");
     
     //  // 恢复默认亮度
     //  video.setBrightness(50);
     
     //  // 高对比度
     //  LOG_INFO(LOG_TAG, "  测试：高对比度（80）");
     //  video.setContrast(80);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  高对比度照片保存");
     
     //  // 低对比度
     //  LOG_INFO(LOG_TAG, "  测试：低对比度（20）");
     //  video.setContrast(20);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  低对比度照片保存");
     
     //  // 恢复默认对比度
     //  video.setContrast(50);
     
     //  // 高饱和度
     //  LOG_INFO(LOG_TAG, "  测试：高饱和度（100）");
     //  video.setSaturation(100);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  高饱和度照片保存");
     
     //  // 低饱和度（接近黑白）
     //  LOG_INFO(LOG_TAG, "  测试：低饱和度（10）");
     //  video.setSaturation(10);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  低饱和度照片保存");
     
     //  // 恢复默认饱和度
     //  video.setSaturation(50);
     
     //  // 4.5 锐度测试
     //  LOG_INFO(LOG_TAG, "\n4.5 锐度测试");
     
     //  // 高锐度
     //  LOG_INFO(LOG_TAG, "  测试：高锐度（80）");
     //  video.setSharpness(80);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  高锐度照片保存");
     
     //  // 低锐度（柔和）
     //  LOG_INFO(LOG_TAG, "  测试：低锐度（10）");
     //  video.setSharpness(10);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(300));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  低锐度照片保存");
     
     //  // 恢复默认锐度
     //  video.setSharpness(50);
     
     //  // 4.6 除雾测试
     //  LOG_INFO(LOG_TAG, "\n4.6 除雾测试");
     
     //  // 启用除雾
     //  LOG_INFO(LOG_TAG, "  测试：启用除雾（级别5）");
     //  video.setDehazeLevel(5);
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  除雾照片保存");
     
     //  // 禁用除雾
     //  video.setDehazeLevel(0);
     
     //  // 4.7 组合效果测试（高曝光 + 高饱和度 + 高锐度）
     //  LOG_INFO(LOG_TAG, "\n4.7 组合效果测试");
     //  LOG_INFO(LOG_TAG, "  测试：高曝光 + 高饱和度 + 高锐度");
     
     //  video.setExposureMode(OP_MANUAL);
     //  video.setExpGainRange(6.0f, 12.0f);    // 高增益
     //  video.setExpTimeRange(0.02f, 0.04f);   // 较长曝光
     //  video.setSaturation(90);                // 高饱和度
     //  video.setSharpness(70);                 // 高锐度
     
     //  std::this_thread::sleep_for(std::chrono::seconds(1));
     //  video.takePhoto();
     //  waitForPhotoComplete(video);
     //  video.restoreH264Encoder();
     //  std::this_thread::sleep_for(std::chrono::milliseconds(500));
     //  LOG_INFO(LOG_TAG, "  组合效果照片保存");
     
     //  // 恢复所有默认ISP参数
     //  LOG_INFO(LOG_TAG, "\n恢复所有ISP参数为默认值...");
     //  video.setExposureMode(OP_AUTO);
     //  video.setWhiteBalanceMode(OP_AUTO);
     //  video.setBrightness(50);
     //  video.setContrast(50);
     //  video.setSaturation(50);
     //  video.setSharpness(50);
     //  video.setDehazeLevel(0);
     //  std::this_thread::sleep_for(std::chrono::seconds(1));
     //  LOG_INFO(LOG_TAG, "ISP参数测试完成，已恢复默认值");
     
     // ========================================
     // 测试 5: H264录像测试
     // ========================================
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "测试 5: H264录像测试");
     LOG_INFO(LOG_TAG, "========================================");
     
     video.setMainState(VideoMainState::RECORD);
     
     // 5.1 短录像（5秒）
     LOG_INFO(LOG_TAG, "5.1 短录像测试（5秒）");
     std::string short_video = video_base + "test_short_5s.h264";
     
     if (video.startRecord(short_video, 5) == VideoError::NONE) {
         LOG_INFO(LOG_TAG, "  开始录制...");
         waitForRecordComplete(video);
         LOG_INFO(LOG_TAG, "  短录像保存: %s", short_video.c_str());
     } else {
         LOG_ERROR(LOG_TAG, "  ✗ 短录像启动失败");
     }
     
     std::this_thread::sleep_for(std::chrono::milliseconds(500));
     
     // 5.2 中等时长录像（10秒）
     LOG_INFO(LOG_TAG, "5.2 中等时长录像测试（10秒）");
     std::string medium_video = video_base + "test_medium_10s.h264";
     
     if (video.startRecord(medium_video, 10) == VideoError::NONE) {
         LOG_INFO(LOG_TAG, "  开始录制...");
         
         // 显示录制进度
         int elapsed = 0;
         while (video.isRecording() && !g_quit.load()) {
             std::this_thread::sleep_for(std::chrono::seconds(1));
             elapsed++;
             LOG_INFO(LOG_TAG, "  录制中... %d/10秒, FPS: %.2f", elapsed, video.getCurrentFPS());
         }
         
         LOG_INFO(LOG_TAG, "  中等时长录像保存: %s", medium_video.c_str());
     } else {
         LOG_ERROR(LOG_TAG, "  ✗ 中等时长录像启动失败");
     }
     
     std::this_thread::sleep_for(std::chrono::milliseconds(500));
     
     // 5.3 不同码率录像测试
     LOG_INFO(LOG_TAG, "5.3 不同码率录像测试");
     
     struct BitrateTest {
         int bitrate_kbps;
         std::string filename;
     };
     
     std::vector<BitrateTest> bitrate_tests = {
         {2048,  "test_2mbps_5s.h264"},
         {4096,  "test_4mbps_5s.h264"},
         {8192,  "test_8mbps_5s.h264"}
     };
     
     for (const auto& test : bitrate_tests) {
         LOG_INFO(LOG_TAG, "  测试码率: %d kbps", test.bitrate_kbps);
         
         // 设置码率
         video.setEncodingParams(test.bitrate_kbps, -1);
         std::this_thread::sleep_for(std::chrono::milliseconds(200));
         
         // 录制5秒
         std::string video_path = video_base + test.filename;
         if (video.startRecord(video_path, 5) == VideoError::NONE) {
             waitForRecordComplete(video);
             LOG_INFO(LOG_TAG, "    %d kbps 录像保存: %s", test.bitrate_kbps, test.filename.c_str());
         } else {
             LOG_ERROR(LOG_TAG, "    ✗ %d kbps 录像失败", test.bitrate_kbps);
         }
         
         std::this_thread::sleep_for(std::chrono::milliseconds(300));
     }
     
     // 恢复默认码率
     video.setEncodingParams(6 * 1024, -1);
     LOG_INFO(LOG_TAG, "录像测试完成");
     
     // ========================================
     // 统计信息
     // ========================================
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "测试统计信息");
     LOG_INFO(LOG_TAG, "========================================");
     
     video.logStats();
     
     VideoSystem::Stats stats;
     video.getStats(stats);
     
     LOG_INFO(LOG_TAG, "\n性能统计:");
     LOG_INFO(LOG_TAG, "  捕获帧数:      %zu", stats.frames_captured.load());
     LOG_INFO(LOG_TAG, "  丢帧数:        %zu", stats.frames_dropped.load());
     LOG_INFO(LOG_TAG, "  拍照次数:      %zu", stats.photos_taken.load());
     LOG_INFO(LOG_TAG, "  录像总时长:    %zu ms", stats.record_duration_ms.load());
     LOG_INFO(LOG_TAG, "  当前FPS:       %.2f", video.getCurrentFPS());
     
     LOG_INFO(LOG_TAG, "\n内存池统计:");
     LOG_INFO(LOG_TAG, "  总分配次数:    %zu", stats.mem_stats.total_allocations.load());
     LOG_INFO(LOG_TAG, "  固定池命中:    %zu (%.2f%%)", 
              stats.mem_stats.fixed_pool_hits.load(),
              stats.mem_stats.total_allocations.load() > 0
                 ? (double)stats.mem_stats.fixed_pool_hits.load() * 100.0 / stats.mem_stats.total_allocations.load()
                 : 0.0);
     LOG_INFO(LOG_TAG, "  动态池命中:    %zu (%.2f%%)", 
              stats.mem_stats.dynamic_pool_hits.load(),
              stats.mem_stats.total_allocations.load() > 0
                 ? (double)stats.mem_stats.dynamic_pool_hits.load() * 100.0 / stats.mem_stats.total_allocations.load()
                 : 0.0);
     LOG_INFO(LOG_TAG, "  DMA池命中:     %zu (%.2f%%)", 
              stats.mem_stats.dma_pool_hits.load(),
              stats.mem_stats.total_allocations.load() > 0
                 ? (double)stats.mem_stats.dma_pool_hits.load() * 100.0 / stats.mem_stats.total_allocations.load()
                 : 0.0);
     LOG_INFO(LOG_TAG, "  分配失败:      %zu", stats.mem_stats.allocation_failures.load());
     
     // ========================================
     // 清理
     // ========================================
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "清理资源");
     LOG_INFO(LOG_TAG, "========================================");
     
     video.stopStream();
     video.shutdown();
     sync_deinit(sync_ctx.get());
     
     LOG_INFO(LOG_TAG, "\n========================================");
     LOG_INFO(LOG_TAG, "  所有测试完成！");
     LOG_INFO(LOG_TAG, "========================================");
     LOG_INFO(LOG_TAG, "\n测试图片保存位置:");
     LOG_INFO(LOG_TAG, "  普通拍照: %s", photo_base.c_str());
     LOG_INFO(LOG_TAG, "  质量测试: %squality_test/", photo_base.c_str());
     LOG_INFO(LOG_TAG, "  ISP测试:  %sisp_test/", photo_base.c_str());
     LOG_INFO(LOG_TAG, "\n测试视频保存位置:");
     LOG_INFO(LOG_TAG, "  %s", video_base.c_str());
     LOG_INFO(LOG_TAG, "========================================");
     
     logger.shutdown();
     
     return 0;
 }
 