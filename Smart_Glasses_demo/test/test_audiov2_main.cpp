/**
 * AudioSystemV2 完整功能测试程序
 * 对应V1版本的test_audio_main.cpp所有测试功能
 */

 #include "../app/media/audio/audiov2.h"
 #include "../app/tool/log/log.h"
 #include "../app/media/sync.h"
 #include <iostream>
 #include <thread>
 #include <chrono>
 #include <cmath>
 #include <fstream>
 #include <vector>
 #include <memory>
 
 using namespace glasses::media::audio;
 using namespace glasses::tool::logger;
 
 // 全局变量：存储录音帧
 std::vector<AudioFramePtr> g_recorded_frames;
 std::mutex g_frames_mutex;
 
 // AI音频回调
 void onAIAudioCallback(AudioFramePtr frame) {
     std::lock_guard<std::mutex> lock(g_frames_mutex);
     g_recorded_frames.push_back(frame);
     LOG_DEBUG("AICallback", "收到AI音频帧: %zu 字节", frame->size);
 }
 
 // WebRTC音频回调
 void onWebRTCAudioCallback(AudioFramePtr frame) {
     LOG_DEBUG("WebRTCCallback", "收到WebRTC音频帧: %zu 字节 (时间戳: %lld)", 
               frame->size, (long long)frame->timestamp);
 }
 
 // 唤醒词回调
 void onWakewordCallback(const int16_t* data, size_t length) {
     LOG_DEBUG("WakewordCallback", "唤醒词音频: %zu 样本", length);
 }
 
 int main() {
     std::cout << "========================================" << std::endl;
     std::cout << "  AudioSystemV2 完整功能测试程序" << std::endl;
     std::cout << "  (对应V1版本所有测试)" << std::endl;
     std::cout << "========================================" << std::endl;
     
     // 1. 初始化日志系统
     LogConfig log_config;
     log_config.enable_console = true;
     log_config.enable_file = true;
     log_config.enable_color = true;
     log_config.enable_timestamp = false;
     log_config.enable_thread_id = false;
     log_config.log_file_path = "./log/audiov2_full_test.log";
     log_config.max_file_size = 5 * 1024 * 1024;
     log_config.min_level = LogLevel::DEBUG;
     
     Logger& logger = Logger::getInstance();
     if (!logger.initialize(log_config)) {
         std::cerr << "日志系统初始化失败" << std::endl;
         return -1;
     }
     
     LOG_INFO("Test", "========================================");
     LOG_INFO("Test", "  智能眼镜音频系统V2测试程序");
     LOG_INFO("Test", "========================================");
     
     // 2. 创建时间同步上下文
     auto sync_ctx = std::make_shared<sync_context_t>();
     if (sync_init(sync_ctx.get()) != 0) {
         LOG_ERROR("Test", "时间同步初始化失败");
         return -1;
     }
     LOG_INFO("Test", "✓ 时间同步上下文已初始化");
     
     // 3. 配置音频系统
     AudioConfig config;
     config.sample_rate = 48000;
     config.channels = 1;
     config.frame_duration_ms = 20;
     config.output_volume = 0.8f;
     config.enable_denoise = true;
     config.enable_agc = true;
     config.enable_vad = true;
     config.max_record_queue_size = 300;
     config.max_playback_queue_size = 300;
     
     // 4. 创建音频系统
     LOG_INFO("Test", "\n=== 步骤1: 初始化AudioSystemV2 ===");
     AudioSystemV2 audio(config);
     
     if (audio.initialize(sync_ctx) != AudioError::NONE) {
         LOG_ERROR("Test", "音频系统初始化失败");
         sync_deinit(sync_ctx.get());
         return -1;
     }
     LOG_INFO("Test", "✓ 音频系统初始化成功");
     
     // 5. 设置回调函数
     LOG_INFO("Test", "\n=== 步骤2: 设置回调函数 ===");
     audio.setAIAudioCallback(onAIAudioCallback);
     audio.setWebRTCAudioCallback(onWebRTCAudioCallback);
     audio.setWakewordCallback(onWakewordCallback);
     LOG_INFO("Test", "✓ 回调函数已设置");
     
     // ========================================
     // 测试1: 基本录音测试
     // ========================================
     LOG_INFO("Test", "\n=== 测试1: 基本录音测试 (5秒) ===");
     audio.setMainState(AudioMainState::NONE);
     
     if (audio.startRecord() != AudioError::NONE) {
         LOG_ERROR("Test", "开始录音失败");
         audio.shutdown();
         sync_deinit(sync_ctx.get());
         logger.shutdown();
         return -1;
     }
     LOG_INFO("Test", "✓ 录音已开始，请对着麦克风说话...");
     
     // 录音5秒
     std::this_thread::sleep_for(std::chrono::seconds(5));
     
     audio.stopRecord();
     LOG_INFO("Test", "✓ 录音已停止");
     
     // 获取录音数据并保存到文件
     {
         std::vector<int16_t> all_recorded_pcm;
         AudioFramePtr frame;
         int frame_count = 0;
         
         // 从录音队列获取所有帧
         while ((frame = audio.getRecordedFrame(std::chrono::milliseconds(10)))) {
             int16_t* pcm_data = reinterpret_cast<int16_t*>(frame->data);
             size_t sample_count = frame->size / sizeof(int16_t);
             all_recorded_pcm.insert(all_recorded_pcm.end(), pcm_data, pcm_data + sample_count);
             frame_count++;
         }
         
         LOG_INFO("Test", "✓ 获取到 %d 帧录音，共 %zu 样本", frame_count, all_recorded_pcm.size());
         
         // 保存到文件
         if (!all_recorded_pcm.empty()) {
             std::ofstream file("smart_glasses_recording_v2.raw", std::ios::binary);
             if (file) {
                 file.write(reinterpret_cast<const char*>(all_recorded_pcm.data()), 
                           all_recorded_pcm.size() * sizeof(int16_t));
                 file.close();
                 LOG_INFO("Test", "✓ 录音已保存到 smart_glasses_recording_v2.raw");
                 LOG_INFO("Test", "  播放命令: aplay -f S16_LE -r 48000 -c 1 smart_glasses_recording_v2.raw");
             }
         }
     }
     
     // ========================================
     // 测试2: AI模式录音和编码
     // ========================================
     LOG_INFO("Test", "\n=== 测试2: AI模式录音和编码 (5秒) ===");
     g_recorded_frames.clear();
     
     // 使用便利函数启动AI模式
     if (audio.startAIMode() != AudioError::NONE) {
         LOG_ERROR("Test", "启动AI模式失败");
         audio.shutdown();
         sync_deinit(sync_ctx.get());
         logger.shutdown();
         return -1;
     }
     LOG_INFO("Test", "✓ AI模式已启动（录音+AI流），请说话...");
     
     // 录音5秒
     std::this_thread::sleep_for(std::chrono::seconds(5));
     
     // 停止AI模式
     audio.stopAIMode();
     LOG_INFO("Test", "✓ AI模式已停止");
     
     // 统计AI编码帧
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         size_t total_opus_size = 0;
         for (const auto& frame : g_recorded_frames) {
             total_opus_size += frame->size;
         }
         LOG_INFO("Test", "✓ AI编码统计: %zu 帧, 总大小 %zu 字节", 
                  g_recorded_frames.size(), total_opus_size);
         
         // 保存AI编码数据
         if (!g_recorded_frames.empty()) {
             std::ofstream opus_file("ai_encoded_16k.opus", std::ios::binary);
             if (opus_file) {
                 for (const auto& frame : g_recorded_frames) {
                     opus_file.write(reinterpret_cast<const char*>(frame->data), frame->size);
                 }
                 opus_file.close();
                 LOG_INFO("Test", "✓ AI编码数据已保存到 ai_encoded_16k.opus");
             }
         }
     }
     
     // ========================================
     // 测试3: WebRTC模式录音和编码
     // ========================================
     LOG_INFO("Test", "\n=== 测试3: WebRTC模式录音和编码 (3秒) ===");
     
     // 使用便利函数启动WebRTC模式
     if (audio.startWebRTCMode() != AudioError::NONE) {
         LOG_ERROR("Test", "启动WebRTC模式失败");
         audio.shutdown();
         sync_deinit(sync_ctx.get());
         logger.shutdown();
         return -1;
     }
     LOG_INFO("Test", "✓ WebRTC模式已启动（录音+WebRTC流）");
     
     // 录音3秒
     std::this_thread::sleep_for(std::chrono::seconds(3));
     
     // 停止WebRTC模式
     audio.stopWebRTCMode();
     LOG_INFO("Test", "✓ WebRTC模式已停止");
     
     // ========================================
     // 测试4: 播放录音（回放测试）
     // ========================================
     LOG_INFO("Test", "\n=== 测试4: 播放录音回放测试 ===");
     audio.setMainState(AudioMainState::NONE);
     
     if (audio.startRecord() != AudioError::NONE) {
         LOG_ERROR("Test", "开始录音失败");
         audio.shutdown();
         sync_deinit(sync_ctx.get());
         logger.shutdown();
         return -1;
     }
     LOG_INFO("Test", "录音3秒用于回放测试...");
     std::this_thread::sleep_for(std::chrono::seconds(3));
     audio.stopRecord();
     
     // 收集录音帧
     {
         std::vector<AudioFramePtr> playback_frames;
         AudioFramePtr frame;
         
         while ((frame = audio.getRecordedFrame(std::chrono::milliseconds(10)))) {
             playback_frames.push_back(frame);
         }
         
         LOG_INFO("Test", "✓ 收集到 %zu 帧录音，准备播放...", playback_frames.size());
         
         if (!playback_frames.empty()) {
             if (audio.startPlayback() != AudioError::NONE) {
                 LOG_ERROR("Test", "开始播放失败");
             } else {
                 LOG_INFO("Test", "✓ 播放已开始，您应该能听到自己的声音...");
                 
                 // 推送所有帧到播放队列（零拷贝）
                 for (const auto& frame : playback_frames) {
                     audio.pushPlaybackFrame(frame);
                     std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 实时播放
                 }
                 
                 // 等待播放完成
                 std::this_thread::sleep_for(std::chrono::milliseconds(500));
                 audio.stopPlayback();
                 LOG_INFO("Test", "✓ 播放完成");
             }
         }
     }
     
     // ========================================
     // 测试5: Opus编解码测试
     // ========================================
     LOG_INFO("Test", "\n=== 测试5: Opus编解码测试 ===");
     
     // 生成测试音频（440Hz正弦波，1秒）
     {
         const int test_sample_rate = 48000;
         const int test_duration_sec = 1;
         std::vector<int16_t> test_pcm(test_sample_rate * test_duration_sec);
         
         for (int i = 0; i < test_pcm.size(); i++) {
             double t = static_cast<double>(i) / test_sample_rate;
             test_pcm[i] = static_cast<int16_t>(16000.0 * std::sin(2.0 * M_PI * 440.0 * t));
         }
         
         LOG_INFO("Test", "生成测试音频: %zu 样本 (440Hz正弦波, 1秒)", test_pcm.size());
         
         // 使用分帧编码函数
         std::vector<AudioFramePtr> encoded_frames;
         size_t encoded_count = audio.encodeOpusFrames(test_pcm.data(), 
                                                        test_pcm.size() * sizeof(int16_t), 
                                                        encoded_frames);
         
         if (encoded_count == 0) {
             LOG_ERROR("Test", "Opus编码失败");
         } else {
             size_t total_opus_size = 0;
             for (const auto& frame : encoded_frames) {
                 total_opus_size += frame->size;
             }
             
             LOG_INFO("Test", "✓ Opus编码成功: %zu 帧, %zu 字节 (压缩比: %.2f:1)", 
                      encoded_count, total_opus_size,
                      (double)(test_pcm.size() * sizeof(int16_t)) / total_opus_size);
             
             // 解码测试
             std::vector<AudioFramePtr> decoded_frames;
             int decoded_frame_count = 0;
             
             for (const auto& opus_frame : encoded_frames) {
                 auto decoded_frame = audio.decodeOpus(opus_frame->data, opus_frame->size);
                 if (decoded_frame) {
                     decoded_frames.push_back(decoded_frame);
                     decoded_frame_count++;
                 }
             }
             
             size_t total_decoded_samples = 0;
             for (const auto& frame : decoded_frames) {
                 total_decoded_samples += frame->size / sizeof(int16_t);
             }
             
             LOG_INFO("Test", "✓ Opus解码成功: %d 帧, %zu 样本", 
                      decoded_frame_count, total_decoded_samples);
             
             // 播放解码后的音频（直接使用解码后的帧，零拷贝）
             if (!decoded_frames.empty()) {
                 LOG_INFO("Test", "播放解码后的音频（440Hz正弦波）...");
                 
                 if (audio.startPlayback() == AudioError::NONE) {
                     // 直接推送解码后的帧（零拷贝）
                     for (const auto& frame : decoded_frames) {
                         audio.pushPlaybackFrame(frame);
                         std::this_thread::sleep_for(std::chrono::milliseconds(20));
                     }
                     
                     std::this_thread::sleep_for(std::chrono::milliseconds(500));
                     audio.stopPlayback();
                     LOG_INFO("Test", "✓ 播放完成");
                 }
             }
         }
     }
     
     // ========================================
     // 测试6: 音量控制测试
     // ========================================
     LOG_INFO("Test", "\n=== 测试6: 音量控制测试 ===");
     
     // 生成测试音频并编码
     {
         std::vector<int16_t> volume_test_pcm(48000); // 1秒
         for (int i = 0; i < volume_test_pcm.size(); i++) {
             double t = static_cast<double>(i) / 48000.0;
             volume_test_pcm[i] = static_cast<int16_t>(20000.0 * std::sin(2.0 * M_PI * 440.0 * t));
         }
         
         // 编码测试音频
         std::vector<AudioFramePtr> volume_test_frames;
         audio.encodeOpusFrames(volume_test_pcm.data(), 
                               volume_test_pcm.size() * sizeof(int16_t), 
                               volume_test_frames);
         
         // 解码以备播放
         std::vector<AudioFramePtr> volume_playback_frames;
         for (const auto& opus_frame : volume_test_frames) {
             auto decoded = audio.decodeOpus(opus_frame->data, opus_frame->size);
             if (decoded) {
                 volume_playback_frames.push_back(decoded);
             }
         }
         
         // 测试不同音量
         std::vector<float> volume_levels = {0.3f, 0.5f, 0.8f, 1.0f, 1.5f};
         
         for (float vol : volume_levels) {
             audio.setOutputVolume(vol);
             LOG_INFO("Test", "音量设置为: %.2f", audio.getOutputVolume());
             
             if (audio.startPlayback() == AudioError::NONE && !volume_playback_frames.empty()) {
                 // 推送帧到播放队列
                 for (const auto& frame : volume_playback_frames) {
                     audio.pushPlaybackFrame(frame);
                     std::this_thread::sleep_for(std::chrono::milliseconds(20));
                 }
                 
                 std::this_thread::sleep_for(std::chrono::milliseconds(200));
                 audio.stopPlayback();
             }
             
             std::this_thread::sleep_for(std::chrono::milliseconds(300));
         }
         
         LOG_INFO("Test", "✓ 音量控制测试完成");
     }
     
     // ========================================
     // 测试7: 状态切换测试
     // ========================================
     LOG_INFO("Test", "\n=== 测试7: 状态切换测试 ===");
     
     audio.setMainState(AudioMainState::NONE);
     LOG_INFO("Test", "主状态: NONE");
     std::this_thread::sleep_for(std::chrono::milliseconds(500));
     
     audio.setMainState(AudioMainState::AI);
     LOG_INFO("Test", "主状态: AI");
     std::this_thread::sleep_for(std::chrono::milliseconds(500));
     
     audio.setMainState(AudioMainState::WEBRTC);
     LOG_INFO("Test", "主状态: WEBRTC");
     std::this_thread::sleep_for(std::chrono::milliseconds(500));
     
     audio.setMainState(AudioMainState::NONE);
     LOG_INFO("Test", "主状态: NONE");
     LOG_INFO("Test", "✓ 状态切换测试完成");
     
     // ========================================
     // 测试8: 性能统计
     // ========================================
     LOG_INFO("Test", "\n=== 测试8: 性能统计 ===");
     audio.logStats();
     
     AudioSystemV2::Stats stats;
     audio.getStats(stats);
     
     LOG_INFO("Test", "\n详细统计:");
     LOG_INFO("Test", "  已录制帧数: %zu", stats.frames_recorded.load());
     LOG_INFO("Test", "  已播放帧数: %zu", stats.frames_played.load());
     LOG_INFO("Test", "  丢弃帧数: %zu", stats.frames_dropped.load());
     LOG_INFO("Test", "  编码次数: %zu", stats.encode_count.load());
     LOG_INFO("Test", "  解码次数: %zu", stats.decode_count.load());
     LOG_INFO("Test", "\n内存池统计:");
     LOG_INFO("Test", "  总分配次数: %zu", stats.mem_stats.total_allocations.load());
     LOG_INFO("Test", "  固定池命中: %zu (%.2f%%)", 
              stats.mem_stats.fixed_pool_hits.load(),
              stats.mem_stats.total_allocations.load() > 0 
                 ? (double)stats.mem_stats.fixed_pool_hits.load() * 100.0 / stats.mem_stats.total_allocations.load() 
                 : 0.0);
     LOG_INFO("Test", "  动态池命中: %zu (%.2f%%)", 
              stats.mem_stats.dynamic_pool_hits.load(),
              stats.mem_stats.total_allocations.load() > 0 
                 ? (double)stats.mem_stats.dynamic_pool_hits.load() * 100.0 / stats.mem_stats.total_allocations.load() 
                 : 0.0);
     LOG_INFO("Test", "  分配失败: %zu", stats.mem_stats.allocation_failures.load());
     
     // ========================================
     // 清理资源
     // ========================================
     LOG_INFO("Test", "\n=== 清理资源 ===");
     audio.shutdown();
     sync_deinit(sync_ctx.get());
     
     LOG_INFO("Test", "\n========================================");
     LOG_INFO("Test", "  所有测试完成！");
     LOG_INFO("Test", "========================================");
     
     // 关闭日志
     logger.shutdown();
     
     std::cout << "\n程序执行完成，按Enter退出..." << std::endl;
     std::cin.get();
     
     return 0;
 }
 
 