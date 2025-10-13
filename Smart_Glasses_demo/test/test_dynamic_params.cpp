/**
 * @file test_dynamic_params.cpp
 * @brief 测试VideoSystemV2的动态参数调整功能
 */

#include "../app/media/camera/camerav2.h"
#include "../app/tool/log/log.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace glasses::media::camera;
using namespace glasses::tool::logger;

int main() {
    // 初始化日志
    LogConfig config;
    config.enable_timestamp = true;
    config.enable_thread_id = false;
    Logger::getInstance().initialize(config);
    
    LOG_INFO("Test", "========================================");
    LOG_INFO("Test", "  Testing Dynamic Encoding Parameters  ");
    LOG_INFO("Test", "========================================");
    
    // 配置VideoSystemV2
    VideoConfig video_config;
    video_config.width = 1280;
    video_config.height = 720;
    video_config.format = EncodeFormat::H264;
    video_config.bitrate = 2000;  // 初始码率：2Mbps
    video_config.gop = 30;        // 初始GOP：30
    video_config.quality = 85;    // 初始JPEG质量：85
    video_config.enable_dma_zero_copy = true;
    
    VideoSystemV2 video(video_config);
    
    // 初始化视频系统
    LOG_INFO("Test", "=== Test 1: Initialize VideoSystem ===");
    VideoError err = video.initialize();
    if (err != VideoError::NONE) {
        LOG_ERROR("Test", "Failed to initialize video system");
        return -1;
    }
    LOG_INFO("Test", "✓ VideoSystem initialized");
    
    // 启动视频流
    LOG_INFO("Test", "=== Test 2: Start Video Stream ===");
    err = video.startStream();
    if (err != VideoError::NONE) {
        LOG_ERROR("Test", "Failed to start video stream");
        return -1;
    }
    LOG_INFO("Test", "✓ Video stream started");
    
    // 等待流稳定
    std::this_thread::sleep_for(std::chrono::seconds(2));
    LOG_INFO("Test", "Current FPS: %.2f", video.getCurrentFPS());
    
    // 测试动态码率调整
    LOG_INFO("Test", "=== Test 3: Dynamic Bitrate Adjustment ===");
    
    // 降低码率到1Mbps
    LOG_INFO("Test", "Changing bitrate: 2000 kbps → 1000 kbps");
    err = video.setBitrate(1000);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Bitrate changed successfully");
    } else {
        LOG_ERROR("Test", "Failed to change bitrate");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 提高码率到4Mbps
    LOG_INFO("Test", "Changing bitrate: 1000 kbps → 4000 kbps");
    err = video.setBitrate(4000);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Bitrate changed successfully");
    } else {
        LOG_ERROR("Test", "Failed to change bitrate");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 测试GOP调整
    LOG_INFO("Test", "=== Test 4: Dynamic GOP Adjustment ===");
    
    // 改变GOP到60
    LOG_INFO("Test", "Changing GOP: 30 → 60");
    err = video.setGOP(60);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ GOP changed successfully");
    } else {
        LOG_ERROR("Test", "Failed to change GOP");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 改变GOP到15（更频繁的I帧）
    LOG_INFO("Test", "Changing GOP: 60 → 15");
    err = video.setGOP(15);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ GOP changed successfully");
    } else {
        LOG_ERROR("Test", "Failed to change GOP");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 测试组合参数调整
    LOG_INFO("Test", "=== Test 5: Combined Parameter Adjustment ===");
    LOG_INFO("Test", "Setting bitrate=3000 kbps, GOP=25");
    err = video.setEncodingParams(3000, 25);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Combined parameters set successfully");
    } else {
        LOG_ERROR("Test", "Failed to set combined parameters");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 测试JPEG质量调整（拍照测试）
    LOG_INFO("Test", "=== Test 6: JPEG Quality Adjustment ===");
    
    // 设置高质量JPEG
    LOG_INFO("Test", "Setting JPEG quality to 95 (high quality)");
    err = video.setJPEGQuality(95);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ JPEG quality set to 95");
    } else {
        LOG_ERROR("Test", "Failed to set JPEG quality");
    }
    
    // 切换到拍照模式并拍照
    video.setMainState(VideoMainState::PHOTO);
    err = video.takePhoto("test_quality_95.jpg");
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "Taking photo with quality 95...");
        
        // 等待拍照完成
        while (video.isPhotoCapturing()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        video.restoreH264Encoder();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        LOG_INFO("Test", "✓ High quality photo taken");
    }
    
    // 设置低质量JPEG
    LOG_INFO("Test", "Setting JPEG quality to 30 (low quality)");
    err = video.setJPEGQuality(30);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ JPEG quality set to 30");
    } else {
        LOG_ERROR("Test", "Failed to set JPEG quality");
    }
    
    // 再次拍照
    err = video.takePhoto("test_quality_30.jpg");
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "Taking photo with quality 30...");
        
        // 等待拍照完成
        while (video.isPhotoCapturing()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        video.restoreH264Encoder();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        LOG_INFO("Test", "✓ Low quality photo taken");
    }
    
    // 显示统计信息
    LOG_INFO("Test", "=== Test 7: Final Statistics ===");
    video.logStats();
    
    LOG_INFO("Test", "Final FPS: %.2f", video.getCurrentFPS());
    
    // 清理
    LOG_INFO("Test", "=== Cleanup ===");
    video.stopStream();
    
    LOG_INFO("Test", "");
    LOG_INFO("Test", "========================================");
    LOG_INFO("Test", "  Dynamic Parameter Tests Completed!  ");
    LOG_INFO("Test", "========================================");
    
    // 检查生成的文件大小对比
    LOG_INFO("Test", "");
    LOG_INFO("Test", "Photo quality comparison:");
    LOG_INFO("Test", "  High quality (95): test_quality_95.jpg");
    LOG_INFO("Test", "  Low quality (30):  test_quality_30.jpg");
    LOG_INFO("Test", "Check file sizes to see the difference!");
    
    return 0;
}
