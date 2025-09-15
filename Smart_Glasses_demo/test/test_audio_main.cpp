#include "app/media/audio/audio.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>

// 音频系统测试函数
void run_audio_test() {
    std::cout << "===== 智能眼镜音频系统测试程序 =====" << std::endl;
    
    try {
        // 创建音频系统实例
        audio_system_t audio_system;
        
        // 初始化音频系统
        audio_error_t error = audio_system_init(&audio_system);
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "音频系统初始化失败: 错误代码 " << error << std::endl;
            return;
        }
        std::cout << "音频系统初始化成功。" << std::endl;
        
        // 设置音频模式为AI模式
        error = set_audio_mode(&audio_system, AUDIO_MODE_AI);
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "设置音频模式失败: 错误代码 " << error << std::endl;
            audio_system_deinit(&audio_system);
            return;
        }
        std::cout << "音频模式已设置为AI模式。" << std::endl;
        
        // 开始录音
        std::cout << "开始录音测试，请对着麦克风说话..." << std::endl;
        error = start_recording(&audio_system);
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "开始录音失败: 错误代码 " << error << std::endl;
            audio_system_deinit(&audio_system);
            return;
        }
        
        // 录音5秒钟
        std::cout << "录音中...(5秒)" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 停止录音
        error = stop_recording(&audio_system);
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "停止录音失败: 错误代码 " << error << std::endl;
            audio_system_deinit(&audio_system);
            return;
        }
        std::cout << "录音已停止。" << std::endl;
        
        // 保存录音到文件
        error = save_audio(&audio_system, "smart_glasses_recording.raw");
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "保存音频失败: 错误代码 " << error << std::endl;
        } else {
            std::cout << "音频已保存到 smart_glasses_recording.raw" << std::endl;
        }
        
        // 创建一个简单的播放测试
        std::thread playback_thread([&audio_system]() {
            audio_error_t play_error = start_playback(&audio_system);
            if (play_error != AUDIO_ERROR_NONE) {
                std::cerr << "开始播放失败: 错误代码 " << play_error << std::endl;
                return;
            }
            
            std::cout << "开始播放测试，您应该能听到自己的录音..." << std::endl;
            
            // 从录音队列获取数据并播放
            std::vector<int16_t> recorded_data;
            int frames_played = 0;
            bool has_played_data = false;
            
            // 先尝试立即获取所有可用的录音数据
            while (get_recorded_audio(&audio_system, recorded_data)) {
                // 将录音数据添加到播放队列
                add_frame_to_playback_queue(&audio_system, recorded_data);
                frames_played++;
                has_played_data = true;
            }
            
            // 如果没有播放任何数据，尝试等待一小段时间再检查一次
            if (!has_played_data) {
                std::cout << "等待录音数据加载..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // 再次尝试获取数据
                while (get_recorded_audio(&audio_system, recorded_data)) {
                    add_frame_to_playback_queue(&audio_system, recorded_data);
                    frames_played++;
                    has_played_data = true;
                }
            }
            
            if (frames_played > 0) {
                std::cout << "已添加 " << frames_played << " 帧录音数据到播放队列" << std::endl;
                
                // 等待播放完成
                std::this_thread::sleep_for(std::chrono::seconds(5)); // 等待更长时间以确保播放完成
            } else {
                std::cout << "没有可播放的录音数据" << std::endl;
            }
            
            // 停止播放
            play_error = stop_playback(&audio_system);
            if (play_error != AUDIO_ERROR_NONE) {
                std::cerr << "停止播放失败: 错误代码 " << play_error << std::endl;
            } else {
                std::cout << "播放已停止。" << std::endl;
            }
        });
        
        // 等待播放线程完成
        if (playback_thread.joinable()) {
            playback_thread.join();
        }
        
        // 创建编解码测试
        std::cout << "开始编解码测试..." << std::endl;
        
        // 创建测试音频数据（正弦波）
        const int num_samples = 16000; // 1秒，16kHz采样率
        std::vector<int16_t> test_audio(num_samples);
        const float frequency = 440.0f; // A4音符
        const float amplitude = 0.5f * 32767.0f; // 50%音量
        
        for (int i = 0; i < num_samples; i++) {
            float t = static_cast<float>(i) / 16000.0f;
            test_audio[i] = static_cast<int16_t>(amplitude * sinf(2.0f * static_cast<float>(M_PI) * frequency * t));
        }
        
        // 分帧处理 - 按照官方实现，使用960样本/帧
        const int FRAME_SIZE = 960; // 40ms帧，16kHz采样率
        std::vector<std::vector<uint8_t>> encoded_frames;
        
        // 编码测试 - 逐帧编码
        bool encode_success = true;
        for (size_t frame_start = 0; frame_start < test_audio.size(); frame_start += FRAME_SIZE) {
            size_t frame_samples = std::min(static_cast<size_t>(FRAME_SIZE), test_audio.size() - frame_start);
            uint8_t encoded_frame[2048]; // 与官方实现保持一致，使用2048字节缓冲区
            size_t encoded_frame_size = sizeof(encoded_frame);
            
            error = encode_opus(&audio_system, 
                               reinterpret_cast<uint8_t*>(&test_audio[frame_start]), 
                               frame_samples * sizeof(int16_t), 
                               encoded_frame, 
                               &encoded_frame_size);
            
            if (error != AUDIO_ERROR_NONE) {
                std::cerr << "Opus编码第" << (frame_start / FRAME_SIZE + 1) << "帧失败: 错误代码 " << error << std::endl;
                encode_success = false;
                break;
            } else {
                // 保存编码后的帧
                encoded_frames.push_back(std::vector<uint8_t>(encoded_frame, encoded_frame + encoded_frame_size));
            }
        }
        
        if (encode_success) {
            std::cout << "Opus编码成功。共编码" << encoded_frames.size() << "帧" << std::endl;
        }
        
        // 解码测试 - 逐帧解码
        if (encode_success) {
            std::vector<int16_t> decoded_audio;
            bool decode_success = true;
            
            for (size_t i = 0; i < encoded_frames.size(); i++) {
                uint8_t decoded_frame[FRAME_SIZE * sizeof(int16_t)];
                size_t decoded_frame_size = sizeof(decoded_frame);
                
                error = decode_opus(&audio_system, 
                                   encoded_frames[i].data(), 
                                   encoded_frames[i].size(), 
                                   decoded_frame, 
                                   &decoded_frame_size);
                
                if (error != AUDIO_ERROR_NONE) {
                    std::cerr << "Opus解码第" << (i + 1) << "帧失败: 错误代码 " << error << std::endl;
                    decode_success = false;
                    break;
                } else {
                    // 添加解码后的PCM数据到结果中
                    int16_t* pcm_data = reinterpret_cast<int16_t*>(decoded_frame);
                    int num_pcm_samples = decoded_frame_size / sizeof(int16_t);
                    decoded_audio.insert(decoded_audio.end(), pcm_data, pcm_data + num_pcm_samples);
                }
            }
            
            if (decode_success) {
                std::cout << "Opus解码成功。解码后样本数: " << decoded_audio.size() << std::endl;
                
                // 播放解码后的音频
                error = start_playback(&audio_system);
                if (error == AUDIO_ERROR_NONE) {
                    add_frame_to_playback_queue(&audio_system, decoded_audio);
                    std::cout << "正在播放解码后的音频（440Hz正弦波）..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    stop_playback(&audio_system);
                }
            }
        }
        
        // 二进制协议封装测试
        std::cout << "开始二进制协议封装测试..." << std::endl;
        if (!encoded_frames.empty()) {
            BinProtocol* bin_frame = pack_bin_frame(&audio_system, 
                                                  encoded_frames[0].data(), 
                                                  encoded_frames[0].size(), 
                                                  1); // 版本号为1
        
            if (!bin_frame) {
                std::cerr << "二进制协议封装失败" << std::endl;
            } else {
                std::cout << "二进制协议封装成功" << std::endl;
                
                // 二进制协议解包测试
                BinProtocolInfo protocol_info;
                std::vector<uint8_t> unpacked_opus_data;
                bool unpack_success = unpack_bin_frame(&audio_system, 
                                                     reinterpret_cast<uint8_t*>(bin_frame), 
                                                     sizeof(BinProtocol) + encoded_frames[0].size(), 
                                                     protocol_info, 
                                                     unpacked_opus_data);
            
            if (!unpack_success) {
                std::cerr << "二进制协议解包失败" << std::endl;
            } else {
                std::cout << "二进制协议解包成功。版本: " << protocol_info.version 
                          << ", 类型: " << protocol_info.type 
                          << ", 负载大小: " << unpacked_opus_data.size() << " 字节" << std::endl;
            }
            
            // 释放二进制帧内存
            free(bin_frame);
        }
        }
        
        // 测试切换音频模式
        std::cout << "测试切换音频模式..." << std::endl;
        error = set_audio_mode(&audio_system, AUDIO_MODE_WEBRTC);
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "切换到WebRTC模式失败: 错误代码 " << error << std::endl;
        } else {
            std::cout << "已切换到WebRTC模式。" << std::endl;
            
            // 获取当前模式
            audio_mode_t current_mode = get_current_mode(&audio_system);
            std::cout << "当前模式: " << (current_mode == AUDIO_MODE_WEBRTC ? "WebRTC" : "未知") << std::endl;
        }
        
        // 释放音频系统资源
        error = audio_system_deinit(&audio_system);
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "音频系统释放失败: 错误代码 " << error << std::endl;
        } else {
            std::cout << "音频系统资源已成功释放。" << std::endl;
        }
        
        std::cout << "===== 智能眼镜音频系统测试完成 =====" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "测试失败，出现异常: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "测试失败，出现未知异常" << std::endl;
    }
}

int main() {
    std::cout << "智能眼镜演示程序启动中..." << std::endl;
    
    try {
        // 运行音频系统测试
        run_audio_test();
        
        std::cout << "演示程序已完成所有测试。" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "程序运行异常: " << e.what() << std::endl;
        return 1;
    }
}