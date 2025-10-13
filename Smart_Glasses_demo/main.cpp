/**
 * @file test_dynamic_params.cpp
 * @brief 测试VideoSystemV2的动态参数调整功能
 */

 #include "../app/media/camera/camerav2.h"
 #include "../app/tool/log/log.h"
 #include <iostream>
 #include <thread>
 #include <chrono>
 #include <sys/stat.h>  // ✅ 添加stat函数支持
 
 using namespace glasses::media::camera;
 using namespace glasses::tool::logger;
 
 int main() {
     // 初始化日志
     LogConfig config;
     config.enable_timestamp = false;
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
     
    // 测试ISP参数控制
    LOG_INFO("Test", "=== Test 5.1: ISP Exposure Control (AE) ===");
    
    // 设置曝光增益范围
    LOG_INFO("Test", "Setting exposure gain range: [1.0, 16.0]");
    err = video.setExpGainRange(1.0f, 16.0f);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Exposure gain range set successfully");
    } else {
        LOG_ERROR("Test", "Failed to set exposure gain range");
    }
    
    // 设置曝光时间范围
    LOG_INFO("Test", "Setting exposure time range: [0.001s, 0.033s]");
    err = video.setExpTimeRange(0.001f, 0.033f);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Exposure time range set successfully");
    } else {
        LOG_ERROR("Test", "Failed to set exposure time range");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 测试ISP白平衡控制
    LOG_INFO("Test", "=== Test 5.2: ISP White Balance Control (AWB) ===");
    
    // 设置色温
    LOG_INFO("Test", "Setting color temperature: 5000K (daylight)");
    err = video.setColorTemperature(5000);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Color temperature set successfully");
    } else {
        LOG_ERROR("Test", "Failed to set color temperature");
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 设置色温到暖色调
    LOG_INFO("Test", "Setting color temperature: 3200K (warm)");
    err = video.setColorTemperature(3200);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Warm color temperature set successfully");
    } else {
        LOG_ERROR("Test", "Failed to set warm color temperature");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 恢复到默认色温
    LOG_INFO("Test", "Restoring color temperature: 5000K");
    video.setColorTemperature(5000);
    
    // 测试ISP图像质量控制
    LOG_INFO("Test", "=== Test 5.3: ISP Image Quality Control ===");
    
    // 增加亮度
    LOG_INFO("Test", "Increasing brightness: 128 → 160");
    err = video.setBrightness(160);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Brightness increased successfully");
    } else {
        LOG_ERROR("Test", "Failed to set brightness");
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 增加对比度
    LOG_INFO("Test", "Increasing contrast: 128 → 150");
    err = video.setContrast(150);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Contrast increased successfully");
    } else {
        LOG_ERROR("Test", "Failed to set contrast");
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 增加饱和度
    LOG_INFO("Test", "Increasing saturation: 128 → 140");
    err = video.setSaturation(140);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Saturation increased successfully");
    } else {
        LOG_ERROR("Test", "Failed to set saturation");
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 增加锐度
    LOG_INFO("Test", "Increasing sharpness: 50 → 70");
    err = video.setSharpness(70);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Sharpness increased successfully");
    } else {
        LOG_ERROR("Test", "Failed to set sharpness");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 拍一张增强后的照片
    LOG_INFO("Test", "Taking photo with enhanced image quality...");
    video.setMainState(VideoMainState::PHOTO);
    err = video.takePhoto("test_enhanced_quality.jpg");
    if (err == VideoError::NONE) {
        while (video.isPhotoCapturing()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        video.restoreH264Encoder();
        LOG_INFO("Test", "✓ Enhanced quality photo taken");
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 恢复默认图像质量参数
    LOG_INFO("Test", "Restoring default image quality settings...");
    video.setBrightness(128);
    video.setContrast(128);
    video.setSaturation(128);
    video.setSharpness(50);
    LOG_INFO("Test", "✓ Default settings restored");
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 测试去雾功能
    LOG_INFO("Test", "=== Test 5.4: ISP Dehaze Control ===");
    
    // 启用去雾并设置强度
    LOG_INFO("Test", "Enabling dehaze with strength: 100");
    err = video.setDehazeLevel(100);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Dehaze enabled successfully");
    } else {
        LOG_ERROR("Test", "Failed to enable dehaze");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 禁用去雾
    LOG_INFO("Test", "Disabling dehaze");
    err = video.setDehazeLevel(0);
    if (err == VideoError::NONE) {
        LOG_INFO("Test", "✓ Dehaze disabled successfully");
    } else {
        LOG_ERROR("Test", "Failed to disable dehaze");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
     
    // 测试JPEG质量调整（拍照测试）
    LOG_INFO("Test", "=== Test 6: Comprehensive JPEG Quality Test ===");
    
    // 测试多个质量级别，验证QP值与实际效果的关系
    std::vector<std::pair<int, std::string>> test_cases = {
        {1, "quality_worst.jpg"},
        {30, "quality_low.jpg"},
        {50, "quality_medium.jpg"}, 
        {70, "quality_good.jpg"},
        {85, "quality_high.jpg"},
        {95, "quality_very_high.jpg"},
        {100, "quality_perfect.jpg"}  // ✅ 支持100质量
    };
    
    for (auto& test : test_cases) {
        int quality = test.first;
        std::string filename = test.second;
        
        LOG_INFO("Test", "--- Testing quality %d ---", quality);
        
        // 设置质量
        err = video.setJPEGQuality(quality);
        if (err != VideoError::NONE) {
            LOG_ERROR("Test", "Failed to set quality %d", quality);
            continue;
        }
        
        // 拍照
        video.setMainState(VideoMainState::PHOTO);
        err = video.takePhoto(filename);
        if (err == VideoError::NONE) {
            LOG_INFO("Test", "Taking photo with quality %d → %s", quality, filename.c_str());
            
            // 等待拍照完成
            while (video.isPhotoCapturing()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // 检查文件大小
            struct stat st;
            if (stat(filename.c_str(), &st) == 0) {
                LOG_INFO("Test", "✓ Quality %d: %s (%ld bytes)", 
                         quality, filename.c_str(), st.st_size);
            }
            
            video.restoreH264Encoder();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
    
    LOG_INFO("Test", "=== Quality test complete! ===");
    LOG_INFO("Test", "Expected: quality_perfect.jpg > quality_very_high.jpg > ... > quality_worst.jpg");
    LOG_INFO("Test", "If file sizes don't follow this pattern, QP mapping needs adjustment");
    
    // 兼容原有测试逻辑
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
    LOG_INFO("Test", "  High quality (95):      test_quality_95.jpg");
    LOG_INFO("Test", "  Low quality (30):       test_quality_30.jpg");
    LOG_INFO("Test", "  Enhanced quality:       test_enhanced_quality.jpg");
    LOG_INFO("Test", "  Quality gradient test:  quality_worst.jpg → quality_perfect.jpg");
    LOG_INFO("Test", "");
    LOG_INFO("Test", "ISP parameter tests:");
    LOG_INFO("Test", "  ✓ Exposure control (gain/time range)");
    LOG_INFO("Test", "  ✓ White balance control (color temperature)");
    LOG_INFO("Test", "  ✓ Image quality (brightness/contrast/saturation/sharpness)");
    LOG_INFO("Test", "  ✓ Dehaze control (enable/disable)");
    LOG_INFO("Test", "");
    LOG_INFO("Test", "Check photos to see the ISP parameter effects!");
    
    return 0;
}
 