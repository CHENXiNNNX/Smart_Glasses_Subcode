#include "app/media/audio/audio.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <fstream>

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
        const int num_samples = AUDIO_SAMPLE_RATE; // 48kHz采样率
        std::vector<int16_t> test_audio(num_samples);
        const float frequency = 440.0f; // A4音符
        const float amplitude = 0.5f * 32767.0f; // 50%音量
        
        for (int i = 0; i < num_samples; i++) {
            float t = static_cast<float>(i) / AUDIO_SAMPLE_RATE;
            test_audio[i] = static_cast<int16_t>(amplitude * sinf(2.0f * static_cast<float>(M_PI) * frequency * t));
        }
        
        // 分帧处理 
        const int FRAME_SIZE = AUDIO_FRAME_SIZE; // 20ms帧
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
                uint8_t decoded_frame[FRAME_SIZE * AUDIO_CHANNELS * sizeof(int16_t)];
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
        
        // 测试语音重采样功能
        std::cout << "开始语音重采样测试..." << std::endl;
        
        // 初始化重采样：从48kHz降到16kHz
        error = init_audio_resample(&audio_system, 48000, 16000, 1, SRC_SINC_BEST_QUALITY);
        if (error != AUDIO_ERROR_NONE) {
            std::cerr << "重采样初始化失败: 错误代码 " << error << std::endl;
        } else {
            std::cout << "重采样初始化成功 (48kHz -> 16kHz)" << std::endl;
            
            // 开始录音（录制您的语音）
            std::cout << "请开始说话，将录制5秒您的语音..." << std::endl;
            error = start_recording(&audio_system);
            if (error != AUDIO_ERROR_NONE) {
                std::cerr << "开始录音失败: 错误代码 " << error << std::endl;
            } else {
                // 录音5秒钟
                std::cout << "录音中...请说话 (5秒)" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));
                
                // 停止录音
                error = stop_recording(&audio_system);
                if (error != AUDIO_ERROR_NONE) {
                    std::cerr << "停止录音失败: 错误代码 " << error << std::endl;
                } else {
                    std::cout << "录音已停止，开始处理语音数据..." << std::endl;
                    
                    // 收集所有录音数据
                    std::vector<int16_t> all_recorded_audio;
                    std::vector<int16_t> recorded_frame;
                    
                    // 获取所有录音数据
                    while (get_recorded_audio(&audio_system, recorded_frame)) {
                        all_recorded_audio.insert(all_recorded_audio.end(), 
                                                recorded_frame.begin(), 
                                                recorded_frame.end());
                    }
                    
                    if (all_recorded_audio.empty()) {
                        std::cout << "没有录制到音频数据" << std::endl;
                    } else {
                        std::cout << "录制到 " << all_recorded_audio.size() << " 个样本 (48kHz)" << std::endl;
                        
                        // 执行重采样：48kHz -> 16kHz
                        std::vector<int16_t> resampled_audio;
                        error = process_audio_resample(&audio_system, all_recorded_audio, resampled_audio);
                        if (error != AUDIO_ERROR_NONE) {
                            std::cerr << "语音重采样处理失败: 错误代码 " << error << std::endl;
                        } else {
                            std::cout << "语音重采样成功: " << all_recorded_audio.size() << " 样本 -> " 
                                      << resampled_audio.size() << " 样本" << std::endl;
                            
                            // 计算实际采样率
                            double actual_ratio = static_cast<double>(resampled_audio.size()) / all_recorded_audio.size();
                            double actual_output_rate = AUDIO_SAMPLE_RATE * actual_ratio;
                            std::cout << "实际输出采样率: " << actual_output_rate << "Hz" << std::endl;
                            
                            // 保存重采样后的语音到文件
                            std::ofstream resampled_file("resampled_voice_16k.raw", std::ios::binary);
                            if (resampled_file) {
                                resampled_file.write(reinterpret_cast<const char*>(resampled_audio.data()), 
                                                   resampled_audio.size() * sizeof(int16_t));
                                resampled_file.close();
                                std::cout << "重采样后的语音已保存到 resampled_voice_16k.raw" << std::endl;
                            }
                            
                            // 保存原始录音到文件进行对比
                            std::ofstream original_file("original_voice_48k.raw", std::ios::binary);
                            if (original_file) {
                                original_file.write(reinterpret_cast<const char*>(all_recorded_audio.data()), 
                                                  all_recorded_audio.size() * sizeof(int16_t));
                                original_file.close();
                                std::cout << "原始录音已保存到 original_voice_48k.raw" << std::endl;
                            }
                            
                            // 播放对比测试
                            std::cout << "开始播放对比测试..." << std::endl;
                            
                            // 先播放原始录音（48kHz）
                            std::cout << "播放原始录音（48kHz）..." << std::endl;
                            error = start_playback(&audio_system);
                            if (error == AUDIO_ERROR_NONE) {
                                // 将原始录音分帧添加到播放队列
                                const int frame_size = AUDIO_FRAME_SIZE; // 48kHz, 20ms帧
                                for (size_t i = 0; i < all_recorded_audio.size(); i += frame_size) {
                                    size_t remaining = all_recorded_audio.size() - i;
                                    size_t current_frame_size = std::min(static_cast<size_t>(frame_size), remaining);
                                    
                                    std::vector<int16_t> playback_frame(
                                        all_recorded_audio.begin() + i, 
                                        all_recorded_audio.begin() + i + current_frame_size
                                    );
                                    
                                    add_frame_to_playback_queue(&audio_system, playback_frame);
                                    
                                    // 控制播放速度，模拟实时播放
                                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                }
                                
                                // 等待播放完成
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                stop_playback(&audio_system);
                                std::cout << "原始录音播放完成" << std::endl;
                            }
                            
                            // 等待一下再播放重采样后的录音
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            
                            // 播放重采样后的语音（通过上采样到48kHz）
                            std::cout << "播放重采样后的语音（16kHz->48kHz）..." << std::endl;
                            
                            // 将16kHz音频重新上采样到48kHz进行播放
                            std::cout << "将16kHz音频上采样到48kHz进行播放..." << std::endl;
                            
                            // 初始化上采样器：16kHz -> 48kHz
                            audio_error_t upsample_error = init_audio_resample(&audio_system, 16000, 48000, 1, SRC_SINC_BEST_QUALITY);
                            if (upsample_error != AUDIO_ERROR_NONE) {
                                std::cerr << "上采样初始化失败: 错误代码 " << upsample_error << std::endl;
                            } else {
                                // 执行上采样：16kHz -> 48kHz
                                std::vector<int16_t> upsampled_audio;
                                upsample_error = process_audio_resample(&audio_system, resampled_audio, upsampled_audio);
                                if (upsample_error != AUDIO_ERROR_NONE) {
                                    std::cerr << "上采样处理失败: 错误代码 " << upsample_error << std::endl;
                                } else {
                                    std::cout << "上采样成功: " << resampled_audio.size() << " 样本 -> " 
                                              << upsampled_audio.size() << " 样本" << std::endl;
                                    
                                    // 保存上采样后的音频
                                    std::ofstream upsampled_file("upsampled_voice_48k.raw", std::ios::binary);
                                    if (upsampled_file) {
                                        upsampled_file.write(reinterpret_cast<const char*>(upsampled_audio.data()), 
                                                           upsampled_audio.size() * sizeof(int16_t));
                                        upsampled_file.close();
                                        std::cout << "上采样后的语音已保存到 upsampled_voice_48k.raw" << std::endl;
                                    }
                                    
                                    // 播放上采样后的音频（48kHz）
                                    error = start_playback(&audio_system);
                                    if (error == AUDIO_ERROR_NONE) {
                                        // 将上采样后的语音分帧添加到播放队列
                                        const int frame_size = AUDIO_FRAME_SIZE; // 48kHz, 20ms帧
                                        for (size_t i = 0; i < upsampled_audio.size(); i += frame_size) {
                                            size_t remaining = upsampled_audio.size() - i;
                                            size_t current_frame_size = std::min(static_cast<size_t>(frame_size), remaining);
                                            
                                            std::vector<int16_t> playback_frame(
                                                upsampled_audio.begin() + i, 
                                                upsampled_audio.begin() + i + current_frame_size
                                            );
                                            
                                            add_frame_to_playback_queue(&audio_system, playback_frame);
                                            
                                            // 控制播放速度，模拟实时播放
                                            std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                        }
                                        
                                        // 等待播放完成
                                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                        stop_playback(&audio_system);
                                        std::cout << "语音播放完成" << std::endl;
                                    } else {
                                        std::cerr << "开始播放失败: 错误代码 " << error << std::endl;
                                    }
                                }
                                
                                // 释放上采样资源
                                release_audio_resample(&audio_system);
                            }
                        }
                    }
                }
            }
            
            // 释放重采样资源
            release_audio_resample(&audio_system);
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