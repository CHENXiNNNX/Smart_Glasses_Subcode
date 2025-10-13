/**
 * @file test_videov2.cpp
 * @brief VideoSystemV2 测试程序
 */

 #include "../app/media/camera/camerav2.h"
 #include "../app/tool/log/log.h"
 #include "../app/media/sync.h"
 #include <iostream>
 #include <thread>
 #include <chrono>
 #include <csignal>
 
 using namespace glasses::media::camera;
 using namespace glasses::tool::logger;
 
 // 全局标志：控制程序退出
 std::atomic<bool> g_quit{false};
 
 void signalHandler(int signal) {
     LOG_INFO("Test", "Received signal %d, exiting...", signal);
     g_quit.store(true);
 }
 
 int main() {
     // 信号处理
     std::signal(SIGINT, signalHandler);
     std::signal(SIGTERM, signalHandler);
     
     // 1. 初始化日志系统
     LogConfig log_config;
     log_config.enable_console = true;
     log_config.enable_file = true;
     log_config.enable_color = true;
     log_config.enable_timestamp = false;
     log_config.enable_thread_id = false;
     log_config.log_file_path = "./log/videov2_test.log";
     log_config.max_file_size = 5 * 1024 * 1024;
     log_config.min_level = LogLevel::DEBUG;
     
     Logger& logger = Logger::getInstance();
     if (!logger.initialize(log_config)) {
         std::cerr << "Failed to initialize logger" << std::endl;
         return -1;
     }
     
     LOG_INFO("Test", "========================================");
     LOG_INFO("Test", "  VideoSystemV2 Test Program");
     LOG_INFO("Test", "========================================");
     
     // 2. 创建时间同步上下文
     auto sync_ctx = std::make_shared<sync_context_t>();
     if (sync_init(sync_ctx.get()) != 0) {
         LOG_ERROR("Test", "Failed to init sync");
         return -1;
     }
     LOG_INFO("Test", "✓ Sync context initialized");
     
     // 3. 配置视频系统（使用H264编码器）
     VideoConfig config;
     config.width = 1280;
     config.height = 720;
     config.fps = 30;
     config.format = EncodeFormat::H264;  // 主编码器使用H264
     config.bitrate = 5 * 1024;
     config.gop = 10;
     config.quality = 90;  // JPEG质量（拍照时使用）
     config.photo_path = "/root/picture/";
     config.record_path = "/root/video/";
     
     // 4. 创建视频系统
     LOG_INFO("Test", "\n=== Test 1: Initialize VideoSystemV2 ===");
     VideoSystemV2 video(config);
     
     if (video.initialize(sync_ctx) != VideoError::NONE) {
         LOG_ERROR("Test", "Failed to initialize video system");
         sync_deinit(sync_ctx.get());
         return -1;
     }
     LOG_INFO("Test", "✓ VideoSystemV2 initialized");
     
     // 5. 启动视频流
     LOG_INFO("Test", "\n=== Test 2: Start Video Stream ===");
     if (video.startStream() != VideoError::NONE) {
         LOG_ERROR("Test", "Failed to start video stream");
         video.shutdown();
         sync_deinit(sync_ctx.get());
         return -1;
     }
     LOG_INFO("Test", "✓ Video stream started");
     
     // 等待流稳定
     std::this_thread::sleep_for(std::chrono::seconds(2));
     LOG_INFO("Test", "Current FPS: %.2f", video.getCurrentFPS());
     
     // 6. 拍照测试（自动切换JPEG编码器）
     LOG_INFO("Test", "\n=== Test 3: Take Photo (H264→JPEG→H264) ===");
     LOG_INFO("Test", "Note: Will auto-switch to JPEG encoder, then restore H264");
     video.setMainState(VideoMainState::PHOTO);
     
     if (video.takePhoto() != VideoError::NONE) {  // 默认switch_encoder=true
         LOG_ERROR("Test", "Failed to take photo");
     } else {
         LOG_INFO("Test", "Photo capture started (auto-switching to JPEG)...");
         
         // 等待拍照完成（最多5秒）
         bool photo_done = false;
         for (int i = 0; i < 50; i++) {
             std::this_thread::sleep_for(std::chrono::milliseconds(100));
             if (!video.isPhotoCapturing()) {
                 photo_done = true;
                 break;
             }
         }
         
         if (photo_done) {
             LOG_INFO("Test", "✓ Photo saved");
         } else {
             LOG_WARN("Test", "⚠ Photo timeout, but may have completed");
         }
         
         // 无论如何都恢复H264编码器
         LOG_INFO("Test", "Restoring H264 encoder...");
         if (video.restoreH264Encoder() == VideoError::NONE) {
             LOG_INFO("Test", "✓ H264 encoder restored successfully");
         } else {
             LOG_ERROR("Test", "✗ Failed to restore H264 encoder");
         }
         
         // 等待编码器稳定
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
     }
     
     // 7. 录像测试
     LOG_INFO("Test", "\n=== Test 4: Record Video ===");
     video.setMainState(VideoMainState::RECORD);
     
     if (video.startRecord("", 5) != VideoError::NONE) {  // 录制5秒
         LOG_ERROR("Test", "Failed to start recording");
     } else {
         LOG_INFO("Test", "Recording started (5 seconds)...");
         
         // 等待录像完成
         while (video.isRecording() && !g_quit.load()) {
             std::this_thread::sleep_for(std::chrono::milliseconds(500));
             LOG_INFO("Test", "Recording... FPS: %.2f", video.getCurrentFPS());
         }
         
         LOG_INFO("Test", "✓ Recording completed");
     }
     
     // 8. WebRTC推流测试
     LOG_INFO("Test", "\n=== Test 5: WebRTC Streaming ===");
     
     // 设置WebRTC回调
     int frame_count = 0;
     video.setWebRTCCallback([&frame_count](VideoFramePtr frame) {
         frame_count++;
         if (frame_count % 30 == 0) {  // 每30帧打印一次
             LOG_DEBUG("WebRTCCallback", "Received frame: %zu bytes, PTS: %llu, keyframe: %d",
                      frame->size, (unsigned long long)frame->pts, frame->is_keyframe);
         }
     });
     
     // 使用便利函数启动WebRTC模式
     if (video.startWebRTCMode() != VideoError::NONE) {
         LOG_ERROR("Test", "Failed to start WebRTC mode");
     } else {
         LOG_INFO("Test", "✓ WebRTC mode started");
         LOG_INFO("Test", "Streaming for 5 seconds...");
         
         // 推流5秒
         for (int i = 0; i < 50 && !g_quit.load(); i++) {
             std::this_thread::sleep_for(std::chrono::milliseconds(100));
             if (i % 10 == 0) {
                 LOG_INFO("Test", "Streaming... FPS: %.2f, Frames: %d", 
                          video.getCurrentFPS(), frame_count);
             }
         }
         
         // 停止WebRTC模式
         video.stopWebRTCMode();
         LOG_INFO("Test", "✓ WebRTC mode stopped (total frames: %d)", frame_count);
     }
     
     // 9. 统计信息
     LOG_INFO("Test", "\n=== Test 6: Statistics ===");
     video.logStats();
     
     VideoSystemV2::Stats stats;
     video.getStats(stats);
     
     LOG_INFO("Test", "\nDetailed Statistics:");
     LOG_INFO("Test", "  Frames captured: %zu", stats.frames_captured.load());
     LOG_INFO("Test", "  Frames dropped:  %zu", stats.frames_dropped.load());
     LOG_INFO("Test", "  Photos taken:    %zu", stats.photos_taken.load());
     LOG_INFO("Test", "  Record duration: %zu ms", stats.record_duration_ms.load());
     
     LOG_INFO("Test", "\nMemory Pool:");
     LOG_INFO("Test", "  Total allocations: %zu", stats.mem_stats.total_allocations.load());
     LOG_INFO("Test", "  Fixed pool hits:   %zu (%.2f%%)", 
              stats.mem_stats.fixed_pool_hits.load(),
              stats.mem_stats.total_allocations.load() > 0
                 ? (double)stats.mem_stats.fixed_pool_hits.load() * 100.0 / stats.mem_stats.total_allocations.load()
                 : 0.0);
     LOG_INFO("Test", "  Dynamic pool hits: %zu (%.2f%%)", 
              stats.mem_stats.dynamic_pool_hits.load(),
              stats.mem_stats.total_allocations.load() > 0
                 ? (double)stats.mem_stats.dynamic_pool_hits.load() * 100.0 / stats.mem_stats.total_allocations.load()
                 : 0.0);
     LOG_INFO("Test", "  Allocation failures: %zu", stats.mem_stats.allocation_failures.load());
     
     // 10. 清理
     LOG_INFO("Test", "\n=== Cleanup ===");
     video.stopStream();
     video.shutdown();
     sync_deinit(sync_ctx.get());
     
     LOG_INFO("Test", "\n========================================");
     LOG_INFO("Test", "  All Tests Completed Successfully!");
     LOG_INFO("Test", "========================================");
     
     logger.shutdown();
     
     std::cout << "\nPress Enter to exit..." << std::endl;
     std::cin.get();
     
     return 0;
 }
 
 