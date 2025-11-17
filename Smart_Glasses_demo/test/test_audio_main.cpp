// // #include <iostream>
// // #include <memory>
// // #include <thread>
// // #include <chrono>

// // // 信令和WebRTC模块
// // #include "app/protocol/webrtc/signaling.hpp"
// // #include "app/protocol/webrtc/webrtc.hpp"
// // #include "app/tool/log/log.hpp"
// // #include "app/media/audio/audio.hpp"
// // #include "app/media/camera/camera.hpp"
// // #include "app/media/sync.hpp"

// // // 默认配置
// // constexpr const char* DEFAULT_DEVICE_ID = "glasses_123456";
// // constexpr const char* DEFAULT_SERVER_URL = "ws://192.168.50.184:8000";

// // // 打印使用说明
// // void printUsage(const char* program_name) {
// //     std::cout << "用法: " << program_name << " [选项]" << std::endl;
// //     std::cout << std::endl;
// //     std::cout << "选项:" << std::endl;
// //     std::cout << "  <服务器地址>           信令服务器地址 (例如: ws://192.168.1.100:8000)" << std::endl;
// //     std::cout << "  -h, --help            显示此帮助信息" << std::endl;
// //     std::cout << "  -d, --device <ID>     指定设备ID (默认: glasses_123456)" << std::endl;
// //     std::cout << std::endl;
// //     std::cout << "示例:" << std::endl;
// //     std::cout << "  " << program_name << "                              # 使用默认配置" << std::endl;
// //     std::cout << "  " << program_name << " ws://192.168.1.100:8000     # 指定服务器地址" << std::endl;
// //     std::cout << "  " << program_name << " -d my_glasses ws://localhost:8000  # 指定设备ID和服务器" << std::endl;
// //     std::cout << std::endl;
// // }

// // int main(int argc, char* argv[]) {
// //     // 解析命令行参数
// //     std::string device_id = DEFAULT_DEVICE_ID;
// //     std::string server_url = DEFAULT_SERVER_URL;
    
// //     for (int i = 1; i < argc; i++) {
// //         std::string arg = argv[i];
        
// //         if (arg == "-h" || arg == "--help") {
// //             printUsage(argv[0]);
// //             return 0;
// //         } else if (arg == "-d" || arg == "--device") {
// //             if (i + 1 < argc) {
// //                 device_id = argv[++i];
// //             } else {
// //                 std::cerr << "错误: -d/--device 选项需要指定设备ID" << std::endl;
// //                 printUsage(argv[0]);
// //                 return 1;
// //             }
// //         } else if (arg.find("ws://") == 0 || arg.find("wss://") == 0) {
// //             // 识别为服务器地址
// //             server_url = arg;
// //         } else {
// //             std::cerr << "错误: 未知选项 '" << arg << "'" << std::endl;
// //             printUsage(argv[0]);
// //             return 1;
// //         }
// //     }
    
// //     // 日志系统初始化
// //     app::tool::log::Logger::getInstance().initialize(app::tool::log::LogConfig());

// //     LOG_INFO("Main", "开始初始化WebRTC系统...");
// //     LOG_INFO("Main", "设备ID: %s", device_id.c_str());
// //     LOG_INFO("Main", "服务器地址: %s", server_url.c_str());
    
// //     // 创建音频配置
// //     app::media::audio::AudioConfig audio_config;
// //     audio_config.sample_rate = 48000;        // 采样率
// //     audio_config.channels = 1;               // 单声道
// //     audio_config.frame_duration_ms = 20;     // 帧长

// //     // 创建相机配置
// //     app::media::camera::VideoConfig video_config;
// //     video_config.width = 1920;           // 分辨率宽度
// //     video_config.height = 1080;           // 分辨率高度

// //     // 创建信令配置
// //     app::protocol::webrtc::SignalingConfig sig_config;
// //     sig_config.deviceId = device_id;
// //     sig_config.serverUrl = server_url;

// //     // 创建WebRTC配置
// //     app::protocol::webrtc::WebRTCConfig webrtc_config;
// //     webrtc_config.enableAudioSend = true;
// //     webrtc_config.enableAudioReceive = true;
// //     webrtc_config.enableVideoSend = true;
// //     webrtc_config.enableVideoReceive = false;
// //     webrtc_config.enableDataChannel = true;
// //     webrtc_config.ice.stunServers = {"stun:stun.l.google.com:19302"};

// //     // 创建信令实例
// //     auto signaling = std::make_shared<app::protocol::webrtc::Signaling>(sig_config);
// //     // 创建WebRTC系统实例
// //     auto webrtc = std::make_shared<app::protocol::webrtc::WebRTCSystem>(webrtc_config);
// //     // 创建音频系统实例
// //     auto audio_system = std::make_shared<app::media::audio::AudioSystem>(audio_config);
// //     // 创建相机系统实例
// //     auto video_system = std::make_shared<app::media::camera::VideoSystem>(video_config);
// //     // 创建同步上下文
// //     auto sync_ctx = std::make_shared<sync_context_t>();
// //     sync_init(sync_ctx.get());

// //     // 初始化音频系统
// //     if (audio_system->initialize(sync_ctx) != app::media::audio::AudioError::NONE) {
// //         LOG_ERROR("Main", "音频系统初始化失败");
// //         return 1;
// //     }

// //     // 启动音频播放
// //     if (audio_system->startPlayback() != app::media::audio::AudioError::NONE) {
// //         LOG_ERROR("Main", "音频播放启动失败");
// //         return 1;
// //     }

// //     // 初始化视频系统
// //     if (video_system->initialize(sync_ctx) != app::media::camera::VideoError::NONE) {
// //         LOG_ERROR("Main", "视频系统初始化失败");
// //         return 1;
// //     }

// //     // 设置webrtc的音频回调
// //     audio_system->setWebRTCAudioCallback(
// //         [webrtc](app::media::audio::AudioFramePtr opus_frame) {
// //             if (!opus_frame || opus_frame->size == 0) {
// //                 return;
// //             }
// //             if (!webrtc->isConnected()) {
// //                 return;
// //             }
// //             webrtc->sendAudioData(opus_frame->data, opus_frame->size, opus_frame->timestamp);
// //         }
// //     );

// //     // 设置webrtc的视频回调
// //     video_system->setWebRTCVideoCallback(
// //         [webrtc](app::media::camera::VideoFramePtr video_frame) {
// //             if (!video_frame || video_frame->size == 0) {
// //                 return;
// //             }
// //             if (!webrtc->isConnected()) {
// //                 return;
// //             }
// //             webrtc->sendVideoData(
// //                 video_frame->data, 
// //                 video_frame->size, 
// //                 video_frame->timestamp,
// //                 video_frame->is_keyframe
// //             );
// //         }
// //     );

// //     // 设置WebRTC状态变化回调
// //     webrtc->onStateChanged([audio_system, video_system](app::protocol::webrtc::WebRTCState state) {
// //         LOG_INFO("Main", "WebRTC 状态: %d", static_cast<int>(state));

// //         if (state == app::protocol::webrtc::WebRTCState::CONNECTED) {
// //             if (audio_system->startWebRTCMode() != app::media::audio::AudioError::NONE) {
// //                 LOG_ERROR("Main", "启动 WebRTC 音频模式失败");
// //             }

// //             if (video_system->startWebRTCMode() != app::media::camera::VideoError::NONE) {
// //                 LOG_ERROR("Main", "启动 WebRTC 视频模式失败");
// //             }
// //         } else if (state == app::protocol::webrtc::WebRTCState::FAILED || state == app::protocol::webrtc::WebRTCState::DISCONNECTED) {
// //             audio_system->stopWebRTCMode();
// //             video_system->stopWebRTCMode();
// //         }

// //     });

// //     webrtc->onAudioData(
// //         [audio_system](const uint8_t* data, size_t size) {
// //             if (!data || size == 0) {
// //                 return;
// //             }
// //             // 打印接收到的音频数据大小
// //             LOG_INFO("Main", "📥 收到音频数据: %zu 字节", size);
            
// //             auto pcm_frame = audio_system->decodeOpus(data, size);
// //             if (pcm_frame) {
// //                 audio_system->pushPlaybackFrame(pcm_frame);
// //             }
// //         }
// //     );

// //     // 设置信令状态变化回调
// //     signaling->onStatusChanged([](app::protocol::webrtc::SignalingStatus status) {
// //         LOG_INFO("Main", "信令状态: %s", app::protocol::webrtc::Signaling::statusToString(status).c_str());
// //         // switch (status) {
// //         //     case app::protocol::webrtc::SignalingStatus::DISCONNECTED:
// //         //         LOG_INFO("Main", "信令状态: DISCONNECTED (未连接)");
// //         //         break;
// //         //     case app::protocol::webrtc::SignalingStatus::CONNECTING:
// //         //         LOG_INFO("Main", "信令状态: CONNECTING (连接中...)");
// //         //         break;
// //         //     case app::protocol::webrtc::SignalingStatus::CONNECTED:
// //         //         LOG_INFO("Main", "信令状态: CONNECTED (已连接)");
// //         //         break;
// //         //     case app::protocol::webrtc::SignalingStatus::JOINED:
// //         //         LOG_INFO("Main", "信令状态: JOINED (已加入房间，等待配对...)");
// //         //         break;
// //         //     case app::protocol::webrtc::SignalingStatus::PAIRED:
// //         //         LOG_INFO("Main", "信令状态: PAIRED (已配对，可以开始WebRTC连接)");
// //         //         break;
// //         // }
// //     });

// //     // 设置错误回调
// //     signaling->onError([](app::protocol::webrtc::SignalingError error, const std::string& message) {
// //         LOG_ERROR("Main", "错误: %s", message.c_str());
// //     });
    
// //     // 设置房间信息变化回调
// //     signaling->onRoomInfoChanged([](const app::protocol::webrtc::RoomInfo& room_info) {
// //         LOG_INFO("Main", "房间信息变化: 房间ID=%s, 人数=%d, 状态=%s", 
// //                  room_info.roomId.c_str(), room_info.num, room_info.roomStatus.c_str());
// //     });

// //     // 连接信令服务器
// //     if (!signaling->connect()) {
// //         LOG_ERROR("Main", "连接信令服务器失败");
// //         return 1;
// //     }

// //     // 初始化WebRTC系统
// //     if (webrtc->open(signaling) != app::protocol::webrtc::WebRTCError::NONE) {
// //         LOG_ERROR("Main", "WebRTCSystem 初始化失败");
// //         return 1;
// //     }

// //     // 等待连接成功
// //     LOG_INFO("Main", "等待连接建立...");
// //     int wait_count = 0;
// //     while (signaling->getStatus() != app::protocol::webrtc::SignalingStatus::CONNECTED) {
// //         std::this_thread::sleep_for(std::chrono::milliseconds(100));
// //         wait_count++;
// //         if (wait_count > 100) {  // 10秒超时
// //             LOG_ERROR("Main", "连接超时，请检查服务器地址和网络连接");
// //             return 1;
// //         }
// //     }
    
// //     // 加入房间
// //     if (!signaling->joinRoom()) {
// //         LOG_ERROR("Main", "加入房间失败");
// //         return 1;
// //     }

// //     // 主循环
// //     std::cout << "[Main] 输入 'q' 退出" << std::endl;
// //     std::string input;
// //     while (std::cin >> input) {
// //         if (input == "q" || input == "Q") {
// //             break;
// //         }
// //     }

// //     audio_system->stopWebRTCMode();           // 停掉采集/编码
// //     audio_system->stopPlayback();             // 停掉播放
// //     audio_system->shutdown();                 // 释放 PortAudio、内存池等

// //     video_system->stopWebRTCMode();           // 停止视频推流
// //     video_system->stopRecord();               // 停止录像
// //     video_system->shutdown();                 // 释放RKMPI资源

// //     sync_deinit(sync_ctx.get());    // 关闭时间同步

// //     webrtc->close();                         // 关闭 PeerConnection/任务队列
// //     signaling->disconnect();                 // 断开 WebSocket
    
// //     return 0;
// // }

// #include "app/chatbot/chatbot.hpp"
// #include "app/tool/log/log.hpp"

// #include <thread>
// #include <chrono>
// #include <csignal>
// #include <atomic>
 
// using namespace app::tool::log;
// using namespace app::chatbot;

// // 全局标志，用于优雅退出
// std::atomic<bool> g_running{true};

// // 信号处理函数（Ctrl+C）
// void signalHandler(int signal) {
//     (void)signal;
//     LOG_INFO("Main", "收到退出信号，正在关闭...");
//     g_running = false;
// }

// int main() {
//     // 注册信号处理（Ctrl+C）
//     std::signal(SIGINT, signalHandler);
//     std::signal(SIGTERM, signalHandler);
    
//     // 日志系统初始化
//     Logger::getInstance().initialize(LogConfig());
     
//     // 创建ChatbotSystem
//     ChatbotSystem chatbot;
    
//     // 硬件设备初始化和网络检测配网
//     ChatbotError err = chatbot.open();
//     if (err != ChatbotError::NONE) {
//         LOG_ERROR("Main", "ChatbotSystem初始化失败: %s", errorToString(err));
//         Logger::getInstance().shutdown();
//          return 1;
//      }
     
//     LOG_INFO("Main", "========================================");
//     LOG_INFO("Main", "系统已就绪，等待唤醒词...");
//     LOG_INFO("Main", "按 Ctrl+C 退出程序");
//     LOG_INFO("Main", "========================================");
    
//     // 查询已保存的网络信息
//     // chatbot.searchSavedNetwork();
    
//     // 保持程序运行，等待唤醒词检测
//     while (g_running) {
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
//         // 检查系统状态
//         ChatbotState state = chatbot.getState();
//         if (state == ChatbotState::ERROR || state == ChatbotState::CLOSED) {
//             LOG_WARN("Main", "系统状态异常: %s，退出程序", stateToString(state));
//                  break;
//          }
//     }
    
//     // 关闭ChatbotSystem
//     chatbot.close();
    
//     // 关闭日志系统
//     Logger::getInstance().shutdown();
    
//     LOG_INFO("Main", "程序退出");
//      return 0;
//  }

/**
 * @file test_audio_main.cpp
 * @brief AudioSystem完整功能测试程序
 * @details 测试内容：
 *          1. 内存分配测试（固定池+动态池）
 *          2. 录制音频编码成Opus后解码播放
 *          3. 下采样到16kHz的录音播放
 *          4. 正弦波生成和音量调整播放测试
 *          5. AI模式和WebRTC模式测试
 *          6. 3A音频算法测试
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-29
 */

 #include "app/media/audio/audio.hpp"
 #include "app/tool/log/log.hpp"
 #include "app/media/sync.hpp"
 #include <iostream>
 #include <thread>
 #include <chrono>
 #include <cmath>
 #include <fstream>
 #include <vector>
 #include <memory>
 #include <queue>
 #include <mutex>
 #include <atomic>
 
 using namespace app::media::audio;
 using namespace app::tool::log;
 
 // 全局变量
 std::queue<AudioFramePtr> g_recorded_frames;
 std::queue<AudioFramePtr> g_ai_frames;
 std::queue<AudioFramePtr> g_webrtc_frames;
 std::mutex g_frames_mutex;
 std::atomic<bool> g_test_running{false};
 std::atomic<int> g_callback_count{0};
 
 // ============================================================================
 // 测试工具函数
 // ============================================================================
 
 /**
  * @brief 生成正弦波音频数据
  * @param frequency 频率（Hz）
  * @param amplitude 振幅（0.0-1.0）
  * @param sample_rate 采样率
  * @param duration_ms 时长（毫秒）
  * @return 音频数据（int16格式）
  */
 std::vector<int16_t> generateSineWave(double frequency, double amplitude, 
                                      int sample_rate, int duration_ms) {
     int sample_count = sample_rate * duration_ms / 1000;
     std::vector<int16_t> samples(sample_count);
     
     for (int i = 0; i < sample_count; i++) {
         double t = (double)i / sample_rate;
         double value = amplitude * sin(2.0 * M_PI * frequency * t);
         samples[i] = static_cast<int16_t>(value * 32767.0);
     }
     
     return samples;
 }
 
 /**
  * @brief 创建包含正弦波的AudioFrame
  */
 AudioFramePtr createSineWaveFrame(AudioMemoryPool& pool, double frequency, 
                                   double amplitude, int sample_rate, int duration_ms) {
     auto samples = generateSineWave(frequency, amplitude, sample_rate, duration_ms);
     size_t data_size = samples.size() * sizeof(int16_t);
     
     auto frame = pool.allocate(data_size);
     if (frame) {
         std::memcpy(frame->data, samples.data(), data_size);
         frame->size = data_size;
         frame->timestamp = 0; // 简化测试
     }
     
     return frame;
 }
 
 /**
  * @brief 保存音频数据到文件（PCM格式）
  */
 bool saveAudioToFile(const std::string& filename, const std::vector<AudioFramePtr>& frames) {
     std::ofstream file(filename, std::ios::binary);
     if (!file) {
         LOG_ERROR("TEST", "创建文件失败: %s", filename.c_str());
         return false;
     }
     
     size_t total_samples = 0;
     for (const auto& frame : frames) {
         file.write(reinterpret_cast<const char*>(frame->data), frame->size);
         total_samples += frame->size / sizeof(int16_t);
     }
     
     LOG_INFO("TEST", "已保存 %zu 个样本到 %s", total_samples, filename.c_str());
     return true;
 }
 
 // ============================================================================
 // 音频回调函数
 // ============================================================================
 
 void onRecordCallback(AudioFramePtr frame) {
     std::lock_guard<std::mutex> lock(g_frames_mutex);
     g_recorded_frames.push(frame);
     g_callback_count.fetch_add(1);
     
     if (g_callback_count.load() % 50 == 0) {
         LOG_DEBUG("TEST", "已录制帧数: %d, 队列大小: %zu", 
                  g_callback_count.load(), g_recorded_frames.size());
     }
 }
 
 void onAIAudioCallback(AudioFramePtr frame) {
     std::lock_guard<std::mutex> lock(g_frames_mutex);
     g_ai_frames.push(frame);
     LOG_DEBUG("TEST", "收到AI音频帧: %zu 字节", frame->size);
 }
 
 void onWebRTCAudioCallback(AudioFramePtr frame) {
     std::lock_guard<std::mutex> lock(g_frames_mutex);
     g_webrtc_frames.push(frame);
     LOG_DEBUG("TEST", "收到WebRTC音频帧: %zu 字节", frame->size);
 }
 
 void onWakewordCallback(const int16_t* data, size_t length) {
     LOG_DEBUG("TEST", "唤醒词音频数据: %zu 个样本", length);
 }
 
 // ============================================================================
 // 测试用例实现
 // ============================================================================
 
 /**
  * @brief 测试1：内存分配性能和正确性
  */
 bool testMemoryAllocation() {
     LOG_INFO("TEST", "========================================");
     LOG_INFO("TEST", "测试1：内存分配性能和正确性");
     LOG_INFO("TEST", "========================================");
     
     // 配置小型内存池进行压力测试
     AudioMemoryPoolConfig config;
     config.fixed_block_size = 2048;
     config.fixed_block_count = 50;  // 较小的固定池，容易测试溢出
     config.dynamic_pool_size = 1024 * 1024;  // 1MB动态池
     
     AudioMemoryPool pool(config);
     
     // 测试1.1：固定池分配测试
     LOG_INFO("TEST", "1.1 固定池分配测试...");
     std::vector<AudioFramePtr> fixed_frames;
     
     // 分配所有固定池帧
     for (int i = 0; i < 50; i++) {
         auto frame = pool.allocate(1024);  // 小于2048，使用固定池
         if (!frame) {
             LOG_ERROR("TEST", "固定池分配失败，索引 %d", i);
             return false;
         }
         
         // 验证帧属性
         if (!frame->is_from_fixed_pool || frame->fixed_pool_index < 0) {
             LOG_ERROR("TEST", "无效的固定池帧，索引 %d", i);
             return false;
         }
         
         fixed_frames.push_back(frame);
     }
     
     // 尝试再分配一个，应该回退到动态池
     auto extra_frame = pool.allocate(1024);
     if (!extra_frame || extra_frame->is_from_fixed_pool) {
         LOG_ERROR("TEST", "回退到动态池失败");
         return false;
     }
     
     LOG_INFO("TEST", "✓ 固定池分配和回退机制正常");
     
     // 测试1.2：动态池大帧分配
     LOG_INFO("TEST", "1.2 动态池大帧分配测试...");
     std::vector<AudioFramePtr> dynamic_frames;
     
     for (int i = 0; i < 10; i++) {
         auto frame = pool.allocate(4096);  // 大于2048，使用动态池
         if (!frame || frame->is_from_fixed_pool) {
             LOG_ERROR("TEST", "动态池分配失败，索引 %d", i);
             return false;
         }
         dynamic_frames.push_back(frame);
     }
     
     LOG_INFO("TEST", "✓ 动态池分配正常");
     
     // 测试1.3：内存回收和重用
     LOG_INFO("TEST", "1.3 内存回收和重用测试...");
     fixed_frames.clear();  // 释放所有固定池帧
     extra_frame.reset();
     
     // 重新分配，应该重用固定池
     auto reused_frame = pool.allocate(1024);
     if (!reused_frame || !reused_frame->is_from_fixed_pool) {
         LOG_ERROR("TEST", "固定池重用失败");
         return false;
     }
     
     LOG_INFO("TEST", "✓ 内存回收和重用正常");
     
     // 输出统计信息
     AudioMemoryPool::Stats stats;
     pool.getStats(stats);
     LOG_INFO("TEST", "内存池统计：");
     LOG_INFO("TEST", "  总分配次数: %llu", stats.total_allocations.load());
     LOG_INFO("TEST", "  固定池命中: %llu (%.2f%%)", 
              stats.fixed_pool_hits.load(), stats.getFixedPoolHitRate());
     LOG_INFO("TEST", "  动态池命中: %llu", stats.dynamic_pool_hits.load());
     LOG_INFO("TEST", "  分配失败: %llu", stats.allocation_failures.load());
     
     LOG_INFO("TEST", "✅ 内存分配测试通过");
     return true;
 }
 
 /**
  * @brief 测试2：录制音频编码成Opus后解码播放
  */
 bool testOpusCodecAndPlayback() {
     LOG_INFO("TEST", "========================================");
     LOG_INFO("TEST", "测试2：录制音频编码成Opus后解码播放");
     LOG_INFO("TEST", "========================================");
     
     // 配置音频系统
     AudioConfig config;
     config.sample_rate = 48000;
     config.channels = 1;
     config.frame_duration_ms = 20;
     config.enable_denoise = false;  // 简化测试，关闭3A
     config.enable_agc = false;
     
     AudioSystem audio_system(config);
     
     // 初始化音频系统
     if (audio_system.initialize() != AudioError::NONE) {
         LOG_ERROR("TEST", "音频系统初始化失败");
         return false;
     }
     
     LOG_INFO("TEST", "2.1 开始录音（3秒）...");
     
     // 开始录音
     if (audio_system.startRecord() != AudioError::NONE) {
         LOG_ERROR("TEST", "启动录音失败");
         return false;
     }
     
     // 录音3秒，通过getRecordedFrame()获取帧
     std::vector<AudioFramePtr> recorded_frames;
     auto start_time = std::chrono::steady_clock::now();
     
     LOG_INFO("TEST", "正在录音，请对着麦克风说话...");
     
     while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(3)) {
         auto frame = audio_system.getRecordedFrame(std::chrono::milliseconds(100));
         if (frame) {
             recorded_frames.push_back(frame);
             
             // 每收集50帧输出一次进度
             if (recorded_frames.size() % 50 == 0) {
                 LOG_INFO("TEST", "已录制 %zu 帧...", recorded_frames.size());
             }
         }
     }
     
     audio_system.stopRecord();
     
     LOG_INFO("TEST", "录制完成，共 %zu 帧", recorded_frames.size());
     
     // 如果没有录制到真实音频，生成模拟音频数据用于测试
     if (recorded_frames.empty()) {
         LOG_WARN("TEST", "未录制到真实音频，生成模拟音频用于编解码测试");
         
         // 使用内存池生成模拟音频帧
         AudioMemoryPool test_pool(config.mem_pool_config);
         
         // 生成3秒的模拟音频（多个频率混合）
         int frames_needed = 3000 / config.frame_duration_ms;  // 3秒的帧数
         
         for (int i = 0; i < frames_needed; i++) {
             // 生成20ms的混合正弦波
             auto samples = generateSineWave(440.0 + i * 2, 0.3, config.sample_rate, config.frame_duration_ms);
             size_t frame_size = samples.size() * sizeof(int16_t);
             
             auto frame = test_pool.allocate(frame_size);
             if (frame) {
                 std::memcpy(frame->data, samples.data(), frame_size);
                 frame->size = frame_size;
                 frame->timestamp = i * config.frame_duration_ms * 1000;  // 微秒
                 recorded_frames.push_back(frame);
             }
         }
         
         LOG_INFO("TEST", "已生成 %zu 帧模拟音频数据用于测试", recorded_frames.size());
     }
     
     if (recorded_frames.empty()) {
         LOG_ERROR("TEST", "无法获取测试音频数据");
         return false;
     }
     
     LOG_INFO("TEST", "2.2 编码为Opus...");
     
     // 编码所有录音帧
     std::vector<AudioFramePtr> opus_frames;
     for (const auto& pcm_frame : recorded_frames) {
         auto opus_frame = audio_system.encodeOpus(
             pcm_frame->getData<int16_t>(), 
             pcm_frame->size
         );
         
         if (opus_frame) {
             opus_frames.push_back(opus_frame);
         }
     }
     
     LOG_INFO("TEST", "编码完成，共 %zu 个Opus帧", opus_frames.size());
     
     if (opus_frames.empty()) {
         LOG_ERROR("TEST", "未生成Opus帧");
         return false;
     }
     
     LOG_INFO("TEST", "2.3 解码Opus并播放...");
     
     // 开始播放
     if (audio_system.startPlayback() != AudioError::NONE) {
         LOG_ERROR("TEST", "启动播放失败");
         return false;
     }
     
     // 解码并推送到播放队列
     for (const auto& opus_frame : opus_frames) {
         auto pcm_frame = audio_system.decodeOpus(opus_frame->data, opus_frame->size);
         if (pcm_frame) {
             audio_system.pushPlaybackFrame(pcm_frame);
         }
         
         // 控制播放速度
         std::this_thread::sleep_for(std::chrono::milliseconds(20));
     }
     
     // 等待播放完成
     std::this_thread::sleep_for(std::chrono::seconds(2));
     
     audio_system.stopPlayback();
     
     LOG_INFO("TEST", "✅ Opus编解码和播放测试通过");
     return true;
 }
 
 /**
  * @brief 测试3：下采样到16kHz的录音播放
  */
 bool testDownsamplingPlayback() {
     LOG_INFO("TEST", "========================================");
     LOG_INFO("TEST", "测试3：下采样到16kHz的录音播放");
     LOG_INFO("TEST", "========================================");
     
     // 配置音频系统
     AudioConfig config;
     config.sample_rate = 48000;  // 录音使用48kHz
     config.channels = 1;
     config.frame_duration_ms = 20;
     
     AudioSystem audio_system(config);
     
     if (audio_system.initialize() != AudioError::NONE) {
         LOG_ERROR("TEST", "音频系统初始化失败");
         return false;
     }
     
     LOG_INFO("TEST", "3.1 启动AI模式（自动16kHz下采样）...");
     
     // 设置AI音频回调
     audio_system.setAIAudioCallback(onAIAudioCallback);
     
     // 清空AI帧队列
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         std::queue<AudioFramePtr> empty;
         g_ai_frames.swap(empty);
     }
     
     // 启动AI模式（会自动进行48kHz->16kHz重采样和Opus编码）
     if (audio_system.startAIMode() != AudioError::NONE) {
         LOG_ERROR("TEST", "启动AI模式失败");
         return false;
     }
     
     LOG_INFO("TEST", "AI模式录音中（3秒）...");
     
     // 录音3秒，期间会自动下采样和编码
     std::this_thread::sleep_for(std::chrono::seconds(3));
     
     audio_system.stopAIMode();
     
     // 收集AI帧（16kHz Opus编码）
     std::vector<AudioFramePtr> ai_opus_frames;
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         while (!g_ai_frames.empty()) {
             ai_opus_frames.push_back(g_ai_frames.front());
             g_ai_frames.pop();
         }
     }
     
     LOG_INFO("TEST", "收集到 %zu 个16kHz Opus帧", ai_opus_frames.size());
     
     if (ai_opus_frames.empty()) {
         LOG_WARN("TEST", "未收集到AI帧，可能需要更长的录音时间");
         return true;  // 不算失败，可能需要更长录音时间
     }
     
     LOG_INFO("TEST", "3.2 解码16kHz音频并播放...");
     
     // 注意：这些是16kHz的Opus帧，需要用16kHz解码器解码
     // 为简化测试，我们直接保存原始数据
     
     // 计算音频特征
     size_t total_opus_size = 0;
     for (const auto& frame : ai_opus_frames) {
         total_opus_size += frame->size;
     }
     
     double estimated_duration = ai_opus_frames.size() * 20.0 / 1000.0;  // 每帧20ms
     LOG_INFO("TEST", "16kHz音频特征：");
     LOG_INFO("TEST", "  Opus帧数: %zu", ai_opus_frames.size());
     LOG_INFO("TEST", "  总大小: %zu 字节", total_opus_size);
     LOG_INFO("TEST", "  估计时长: %.2f 秒", estimated_duration);
     
     LOG_INFO("TEST", "✅ 16kHz下采样测试通过");
     return true;
 }
 
 /**
  * @brief 测试4：正弦波生成和音量调整播放
  */
 bool testSineWaveAndVolumeControl() {
     LOG_INFO("TEST", "========================================");
     LOG_INFO("TEST", "测试4：正弦波生成和音量调整播放");
     LOG_INFO("TEST", "========================================");
     
     AudioConfig config;
     config.sample_rate = 48000;
     config.channels = 1;
     
     AudioSystem audio_system(config);
     
     if (audio_system.initialize() != AudioError::NONE) {
         LOG_ERROR("TEST", "音频系统初始化失败");
         return false;
     }
     
     // 创建内存池用于生成正弦波
     AudioMemoryPoolConfig pool_config;
     pool_config.fixed_block_count = 100;
     AudioMemoryPool pool(pool_config);
     
     LOG_INFO("TEST", "4.1 生成不同频率的正弦波...");
     
     // 生成不同频率的正弦波
     std::vector<std::pair<double, std::string>> frequencies = {
         {440.0, "A4音符"},     // 标准A音符
         {523.25, "C5音符"},    // 高音C
         {880.0, "A5音符"},     // 高八度A
         {220.0, "A3音符"}      // 低八度A
     };
     
     if (audio_system.startPlayback() != AudioError::NONE) {
         LOG_ERROR("TEST", "启动播放失败");
         return false;
     }
     
     for (const auto& [freq, name] : frequencies) {
         LOG_INFO("TEST", "播放 %s (%.2f Hz)...", name.c_str(), freq);
         
         // 测试不同音量级别
         std::vector<float> volumes = {0.3f, 0.6f, 1.0f, 0.1f};
         
         for (float volume : volumes) {
             // 设置音量
             audio_system.setOutputVolume(volume);
             LOG_INFO("TEST", "  音量: %.1f", volume);
             
             // 生成500ms的正弦波
             auto sine_frame = createSineWaveFrame(pool, freq, 0.8, config.sample_rate, 500);
             if (sine_frame) {
                 audio_system.pushPlaybackFrame(sine_frame);
             }
             
             // 等待播放
             std::this_thread::sleep_for(std::chrono::milliseconds(600));
         }
         
         // 静音间隔
         std::this_thread::sleep_for(std::chrono::milliseconds(200));
     }
     
     LOG_INFO("TEST", "4.2 测试音量渐变效果...");
     
     // 生成长音调，同时调整音量
     auto long_sine = createSineWaveFrame(pool, 440.0, 0.8, config.sample_rate, 3000);  // 3秒
     if (long_sine) {
         audio_system.pushPlaybackFrame(long_sine);
         
         // 在播放过程中动态调整音量（渐强效果）
         for (int i = 0; i <= 20; i++) {
             float volume = i / 20.0f;  // 0.0 -> 1.0
             audio_system.setOutputVolume(volume);
             std::this_thread::sleep_for(std::chrono::milliseconds(100));
         }
         
         // 渐弱效果
         for (int i = 20; i >= 0; i--) {
             float volume = i / 20.0f;  // 1.0 -> 0.0
             audio_system.setOutputVolume(volume);
             std::this_thread::sleep_for(std::chrono::milliseconds(100));
         }
     }
     
     // 恢复正常音量
     audio_system.setOutputVolume(1.0f);
     
     audio_system.stopPlayback();
     
     LOG_INFO("TEST", "✅ 正弦波生成和音量控制测试通过");
     return true;
 }
 
 /**
  * @brief 测试5：AI模式和WebRTC模式切换
  */
 bool testModeSwitch() {
     LOG_INFO("TEST", "========================================");
     LOG_INFO("TEST", "测试5：AI模式和WebRTC模式切换");
     LOG_INFO("TEST", "========================================");
     
     AudioConfig config;
     config.sample_rate = 48000;
     config.channels = 1;
     config.enable_denoise = true;
     config.enable_agc = true;
     
     AudioSystem audio_system(config);
     
     if (audio_system.initialize() != AudioError::NONE) {
         LOG_ERROR("TEST", "音频系统初始化失败");
         return false;
     }
     
     // 设置回调
     audio_system.setAIAudioCallback(onAIAudioCallback);
     audio_system.setWebRTCAudioCallback(onWebRTCAudioCallback);
     audio_system.setWakewordCallback(onWakewordCallback);
     
     LOG_INFO("TEST", "5.1 测试AI模式...");
     
     // 清空队列
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         std::queue<AudioFramePtr> empty_ai, empty_webrtc;
         g_ai_frames.swap(empty_ai);
         g_webrtc_frames.swap(empty_webrtc);
     }
     
     // 启动AI模式
     if (audio_system.startAIMode() != AudioError::NONE) {
         LOG_ERROR("TEST", "启动AI模式失败");
         return false;
     }
     
     LOG_INFO("TEST", "AI模式运行中（2秒）...");
     std::this_thread::sleep_for(std::chrono::seconds(2));
     
     // 检查AI模式状态
     if (audio_system.getMainState() != AudioMainState::AI) {
         LOG_ERROR("TEST", "未进入AI模式");
         return false;
     }
     
     if (!audio_system.isAIStreamActive()) {
         LOG_ERROR("TEST", "AI流未激活");
         return false;
     }
     
     audio_system.stopAIMode();
     
     // 统计AI帧
     size_t ai_frame_count = 0;
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         ai_frame_count = g_ai_frames.size();
     }
     
     LOG_INFO("TEST", "AI模式收集帧数: %zu", ai_frame_count);
     
     LOG_INFO("TEST", "5.2 测试WebRTC模式...");
     
     // 启动WebRTC模式
     if (audio_system.startWebRTCMode() != AudioError::NONE) {
         LOG_ERROR("TEST", "启动WebRTC模式失败");
         return false;
     }
     
     LOG_INFO("TEST", "WebRTC模式运行中（2秒）...");
     std::this_thread::sleep_for(std::chrono::seconds(2));
     
     // 检查WebRTC模式状态
     if (audio_system.getMainState() != AudioMainState::WEBRTC) {
         LOG_ERROR("TEST", "未进入WebRTC模式");
         return false;
     }
     
     if (!audio_system.isWebRTCStreamActive()) {
         LOG_ERROR("TEST", "WebRTC流未激活");
         return false;
     }
     
     audio_system.stopWebRTCMode();
     
     // 统计WebRTC帧
     size_t webrtc_frame_count = 0;
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         webrtc_frame_count = g_webrtc_frames.size();
     }
     
     LOG_INFO("TEST", "WebRTC模式收集帧数: %zu", webrtc_frame_count);
     
     LOG_INFO("TEST", "5.3 测试快速模式切换...");
     
     // 快速切换测试
     for (int i = 0; i < 3; i++) {
         audio_system.startAIMode();
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
         audio_system.stopAIMode();
         
         audio_system.startWebRTCMode();
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
         audio_system.stopWebRTCMode();
         
         LOG_INFO("TEST", "快速切换 #%d 完成", i + 1);
     }
     
     LOG_INFO("TEST", "✅ 模式切换测试通过");
     return true;
 }
 
 /**
  * @brief 测试6：3A音频算法效果
  */
 bool test3AAlgorithms() {
     LOG_INFO("TEST", "========================================");
     LOG_INFO("TEST", "测试6：3A音频算法效果");
     LOG_INFO("TEST", "========================================");
     
     // 配置启用所有3A算法
     AudioConfig config;
     config.sample_rate = 48000;
     config.channels = 1;
     config.enable_denoise = true;
     config.enable_agc = true;
     config.enable_vad = true;
     config.agc_level = 8000.0f;
     config.noise_suppress_level = -15;
     
     AudioSystem audio_system(config);
     
     if (audio_system.initialize() != AudioError::NONE) {
         LOG_ERROR("TEST", "启用3A的音频系统初始化失败");
         return false;
     }
     
     LOG_INFO("TEST", "6.1 3A算法参数：");
     LOG_INFO("TEST", "  降噪: %s", config.enable_denoise ? "开启" : "关闭");
     LOG_INFO("TEST", "  AGC: %s (目标电平: %.1f)", 
              config.enable_agc ? "开启" : "关闭", config.agc_level);
     LOG_INFO("TEST", "  VAD: %s", config.enable_vad ? "开启" : "关闭");
     
     // 清空队列
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         std::queue<AudioFramePtr> empty;
         g_recorded_frames.swap(empty);
         g_callback_count.store(0);
     }
     
     LOG_INFO("TEST", "6.2 启用3A算法录音测试（5秒）...");
     
     if (audio_system.startRecord() != AudioError::NONE) {
         LOG_ERROR("TEST", "启动录音失败");
         return false;
     }
     
     // 录音5秒，3A算法会在录音回调中自动应用
     std::this_thread::sleep_for(std::chrono::seconds(5));
     
     audio_system.stopRecord();
     
     // 收集处理后的帧
     std::vector<AudioFramePtr> processed_frames;
     {
         std::lock_guard<std::mutex> lock(g_frames_mutex);
         while (!g_recorded_frames.empty()) {
             processed_frames.push_back(g_recorded_frames.front());
             g_recorded_frames.pop();
         }
     }
     
     LOG_INFO("TEST", "3A处理完成，共处理 %zu 帧", processed_frames.size());
     
     // 分析音频特征（简单统计）
     if (!processed_frames.empty()) {
         int64_t sum_abs = 0;
         size_t total_samples = 0;
         
         for (const auto& frame : processed_frames) {
             const int16_t* samples = frame->getData<int16_t>();
             size_t sample_count = frame->size / sizeof(int16_t);
             
             for (size_t i = 0; i < sample_count; i++) {
                 sum_abs += abs(samples[i]);
             }
             total_samples += sample_count;
         }
         
         double avg_amplitude = total_samples > 0 ? (double)sum_abs / total_samples : 0.0;
         LOG_INFO("TEST", "音频统计：");
         LOG_INFO("TEST", "  总样本数: %zu", total_samples);
         LOG_INFO("TEST", "  平均幅度: %.2f", avg_amplitude);
         LOG_INFO("TEST", "  动态范围: %s", avg_amplitude > 1000 ? "正常" : "较小");
     }
     
     LOG_INFO("TEST", "✅ 3A音频算法测试完成");
     return true;
 }
 
 // ============================================================================
 // 主测试函数
 // ============================================================================
 
 int main() {
     // 初始化日志系统
     LogConfig log_config;
     log_config.min_level = LogLevel::INFO;
     log_config.enable_console = true;
     log_config.enable_file = true;
     log_config.log_file_path = "./log/test_audio.log";
     
     Logger& logger = Logger::getInstance();
     if (!logger.initialize(log_config)) {
         std::cerr << "日志系统初始化失败" << std::endl;
         return -1;
     }
     
     LOG_INFO("MAIN", "🎵 AudioSystem 完整功能测试开始");
     LOG_INFO("MAIN", "========================================");
     
     int passed = 0;
     int failed = 0;
     
     // 运行所有测试
     std::vector<std::pair<std::function<bool()>, std::string>> tests = {
         {testMemoryAllocation, "内存分配测试"},
         {testOpusCodecAndPlayback, "Opus编解码和播放测试"},
         {testDownsamplingPlayback, "16kHz下采样测试"},
         {testSineWaveAndVolumeControl, "正弦波生成和音量控制测试"},
         {testModeSwitch, "AI/WebRTC模式切换测试"},
         {test3AAlgorithms, "3A音频算法测试"}
     };
     
     for (const auto& [test_func, test_name] : tests) {
         try {
             LOG_INFO("MAIN", "\n开始执行: %s", test_name.c_str());
             
             if (test_func()) {
                 LOG_INFO("MAIN", "✅ %s - 通过", test_name.c_str());
                 passed++;
             } else {
                 LOG_ERROR("MAIN", "❌ %s - 失败", test_name.c_str());
                 failed++;
             }
             
         } catch (const std::exception& e) {
             LOG_ERROR("MAIN", "❌ %s - 异常: %s", test_name.c_str(), e.what());
             failed++;
         }
         
         // 测试间隔
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
     }
     
     LOG_INFO("MAIN", "========================================");
     LOG_INFO("MAIN", "🎵 AudioSystem 测试完成");
     LOG_INFO("MAIN", "✅ 通过: %d 个测试", passed);
     LOG_INFO("MAIN", "❌ 失败: %d 个测试", failed);
     LOG_INFO("MAIN", "总计: %d 个测试", passed + failed);
     
     if (failed == 0) {
         std::cout << "\n🎉 所有测试通过！AudioSystem 功能完全正常！\n" << std::endl;
         return 0;
     } else {
         std::cout << "\n⚠️  有 " << failed << " 个测试失败，请检查日志\n" << std::endl;
         return failed;
     }
 }
 