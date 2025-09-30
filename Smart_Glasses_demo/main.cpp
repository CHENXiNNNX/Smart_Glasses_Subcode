#include <iostream>
#include <memory>
#include <algorithm>
#include <cmath>

#include "app/media/media_config.h"
#include "rtc/rtc.hpp"
#include "app/protocol/webrtc/signaling.h"
#include "app/protocol/webrtc/webrtc.h"  
#include "app/media/camera/camera.h"
#include "app/media/audio/audio.h"
#include "app/media/sync.h"
#include <nlohmann/json.hpp>

using namespace glasses::protocol;
using json = nlohmann::json;

// 设备配置
constexpr const char* DEVICE_ID = "glasses_123456";
constexpr const char* SERVER_URL = "ws://192.168.50.184:8000";

// 全局变量
std::shared_ptr<Signaling> signaling;
std::shared_ptr<WebRTCManage> webrtcManager;  
std::unique_ptr<video_system_t> video_system;
std::unique_ptr<audio_system_t> audio_system;
sync_context_t sync_ctx;

// 创建WebRTC配置
WebRTCConfig createWebRTCConfig() {
    WebRTCConfig config;
    
    // 功能开关配置
    config.enableDataChannel = false;    // 启用数据通道
    config.enableAudioSend = true;      // 启用音频发送
    config.enableAudioReceive = true;   // 启用音频接收
    config.enableVideoSend = true;      // 启用视频发送
    
    // 数据通道配置
    config.dataChannelLabel = "glasses_data_channel";
    
    // ICE服务器配置
    config.ice.stunServers = {
        "stun:stun.l.google.com:19302",
        "stun:stun.miwifi.com:3478",
        "stun:stun.chat.bilibili.com:3478"
    };
    
    // TURN服务器配置
    config.ice.turnServers = {
    };
    
    // ICE传输策略配置
    config.ice.useRelayOnly = false; 
    
    // SCTP传输配置 
    config.sctp.recvBufferSize = 2 * 1024 * 1024;
    config.sctp.sendBufferSize = 2 * 1024 * 1024;
    config.sctp.maxChunksOnQueue = 20 * 1024;
    config.sctp.initialCongestionWindow = 10;
    config.sctp.maxBurst = 10;
    config.sctp.congestionControlModule = 0;
    config.sctp.delayedSackTime = std::chrono::milliseconds{20};
    config.sctp.minRetransmitTimeout = std::chrono::milliseconds{200};
    config.sctp.maxRetransmitTimeout = std::chrono::milliseconds{10000};
    config.sctp.initialRetransmitTimeout = std::chrono::milliseconds{1000};
    config.sctp.maxRetransmitAttempts = 5;
    config.sctp.heartbeatInterval = std::chrono::milliseconds{30000};
    
    // ========== 性能优化配置 ==========
    config.performance.audioThreadCount = 1;     // 音频单线程
    config.performance.videoThreadCount = 2;     // 视频双线程（分离编码和发送）
    config.performance.enableZeroCopy = true;    // 启用零拷贝
    config.performance.maxQueueSize = 100;       // 最大队列长度
    
    return config;
}

// 视频帧回调函数
void onVideoFrameCallback(void *data, int len, uint64_t timestamp) {
    if (webrtcManager && data && len > 0) {
        // 判断是否为关键帧（检查H264 NAL Unit类型）
        bool isKeyFrame = false;
        if (len >= 5) {
            uint8_t* h264Data = static_cast<uint8_t*>(data);
            // 查找SPS (0x67) 或 PPS (0x68) NAL Unit，表示关键帧
            for (int i = 0; i < len - 4; i++) {
                if (h264Data[i] == 0x00 && h264Data[i+1] == 0x00 && 
                    h264Data[i+2] == 0x00 && h264Data[i+3] == 0x01) {
                    uint8_t nalType = h264Data[i+4] & 0x1F;
                    if (nalType == 7 || nalType == 5) {  // SPS or IDR
                        isKeyFrame = true;
                        break;
                    }
                }
            }
        }
        
        // 使用外部缓冲区
        webrtcManager->sendVideoData(static_cast<const uint8_t*>(data), len, timestamp, isKeyFrame);
    }
}

// 音频数据回调函数
void onAudioDataCallback(void *data, int len, uint64_t timestamp) {
    if (webrtcManager && data && len > 0) {
        // 直接使用外部缓冲区，由WebRTC管理器使用缓冲区池管理
        webrtcManager->sendAudioData(static_cast<const uint8_t*>(data), len, timestamp);
    }
}

// 音频数据接收回调函数
void onReceivedAudioDataCallback(const uint8_t* data, size_t size) {
    if (!audio_system || !data || size == 0) {
        return;
    }
    
    // 解码Opus数据为PCM
    uint8_t pcm_buffer[4096];
    size_t pcm_size = sizeof(pcm_buffer);
    
    if (decode_opus(audio_system.get(), const_cast<uint8_t*>(data), size, pcm_buffer, &pcm_size) != AUDIO_ERROR_NONE) {
        return;
    }
    
    // 将解码后的PCM数据转换为vector<int16_t>
    int16_t* pcm_data = reinterpret_cast<int16_t*>(pcm_buffer);
    int num_pcm_samples = pcm_size / sizeof(int16_t);
    
    // 音量增强处理
    const float volume_boost = AUDIO_MASTER_VOLUME;
    for (int i = 0; i < num_pcm_samples; i++) {
        float sample = static_cast<float>(pcm_data[i]);
        sample *= volume_boost;
        
        // 防止溢出
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        
        pcm_data[i] = static_cast<int16_t>(sample);
    }
    
    // 分帧处理
    const int FRAME_SIZE = AUDIO_FRAME_SIZE;
    
    if (num_pcm_samples > FRAME_SIZE) {
        // 分帧添加到播放队列
        for (size_t i = 0; i < num_pcm_samples; i += FRAME_SIZE) {
            size_t remaining = num_pcm_samples - i;
            size_t current_frame_size = std::min(static_cast<size_t>(FRAME_SIZE), remaining);
            
            std::vector<int16_t> playback_frame(pcm_data + i, pcm_data + i + current_frame_size);
            add_frame_to_playback_queue(audio_system.get(), playback_frame);
        }
    } else {
        // 单帧直接添加
        std::vector<int16_t> pcm_frame(pcm_data, pcm_data + num_pcm_samples);
        add_frame_to_playback_queue(audio_system.get(), pcm_frame);
    }
}

// 设置信令回调
void setupSignalingCallbacks() {
    if (!signaling) return;
    
    signaling->onStatusChanged([](ConnectionStatus status) {
        std::cout << "[Main] 信令状态变化: " << static_cast<int>(status) << std::endl;
        
        if (status == ConnectionStatus::CONNECTED) {
            std::cout << "[Main] 自动加入房间..." << std::endl;
            signaling->joinRoom();
        }
    });
    
    signaling->onError([](ErrorCode errorCode, const std::string& errorMessage) {
        std::cout << "[Main] 信令错误 [" << static_cast<int>(errorCode) 
                  << "]: " << errorMessage << std::endl;
    });
}

// 设置WebRTC回调
void setupWebRTCCallbacks() {
    if (!webrtcManager) return;
    
    webrtcManager->onStateChanged([](WebRTCState status) {
        std::cout << "[Main] WebRTC状态变化: " << static_cast<int>(status) << std::endl;
        
        if (status == WebRTCState::CONNECTED) {
            std::cout << "[Main] WebRTC连接建立成功，启动音视频流..." << std::endl;
            
            // 启动视频流
            if (start_video_stream(video_system.get()) == 0) {
                std::cout << "[Main] 基础视频流启动成功" << std::endl;
                
                if (set_video_mode(video_system.get(), VIDEO_MODE_WEBRTC) == 0) {
                    std::cout << "[Main] 视频模式设置为WebRTC" << std::endl;
                    
                    #if USE_WEBRTC
                    if (start_webrtc_video_stream(video_system.get()) == 0) {
                        std::cout << "[Main] WebRTC视频流启动成功" << std::endl;
                    } else {
                        std::cout << "[Main] WebRTC视频流启动失败" << std::endl;
                    }
                    #endif
                } else {
                    std::cout << "[Main] 设置视频模式失败" << std::endl;
                }
            } else {
                std::cout << "[Main] 启动基础视频流失败" << std::endl;
            }
            
            // 启动音频流
            if (audio_system) {
                if (set_audio_mode(audio_system.get(), AUDIO_MODE_WEBRTC) == AUDIO_ERROR_NONE) {
                    std::cout << "[Main] 音频模式设置为WebRTC" << std::endl;
                    
                    #if USE_WEBRTC
                    if (start_webrtc_audio_stream(audio_system.get()) == AUDIO_ERROR_NONE) {
                        std::cout << "[Main] WebRTC音频流启动成功" << std::endl;
                        
                        // 启动音频播放
                        if (start_playback(audio_system.get()) == AUDIO_ERROR_NONE) {
                            std::cout << "[Main] 音频播放已启动" << std::endl;
                        } else {
                            std::cout << "[Main] 音频播放启动失败" << std::endl;
                        }
                    } else {
                        std::cout << "[Main] WebRTC音频流启动失败" << std::endl;
                    }
                    #endif
                } else {
                    std::cout << "[Main] 设置音频模式失败" << std::endl;
                }
            }
            
            // 打印性能统计（10秒后）
            std::thread([]{
                std::this_thread::sleep_for(std::chrono::seconds(10));
                if (webrtcManager) {
                    auto stats = webrtcManager->getStats();
                    std::cout << "\n========== WebRTC性能统计 (10秒) ==========" << std::endl;
                    std::cout << "音频发送包数: " << stats.audioPacketsSent << std::endl;
                    std::cout << "音频接收包数: " << stats.audioPacketsReceived << std::endl;
                    std::cout << "视频发送包数: " << stats.videoPacketsSent << std::endl;
                    std::cout << "音频发送字节: " << stats.audioBytesSent << " bytes" << std::endl;
                    std::cout << "视频发送字节: " << stats.videoBytesSent << " bytes" << std::endl;
                    std::cout << "============================================\n" << std::endl;
                }
            }).detach();
        }
    });
    
    webrtcManager->onDataMessage([](const std::string& message) {
        std::cout << "[Main] 收到数据通道消息: " << message << std::endl;
    });
    
    webrtcManager->onAudioData(onReceivedAudioDataCallback);
}

int main(void) {
    std::cout << "=== 智能眼镜WebRTC客户端启动 ===" << std::endl;
    std::cout << "设备ID: " << DEVICE_ID << std::endl;
    std::cout << "服务器地址: " << SERVER_URL << std::endl;

    // 初始化libdatachannel日志
    rtc::InitLogger(rtc::LogLevel::Info);

    // 初始化时间同步模块
    std::cout << "[Main] 初始化时间同步模块..." << std::endl;
    if (sync_init(&sync_ctx) != 0) {
        std::cout << "[Main] 时间同步模块初始化失败" << std::endl;
        return -1;
    }
    std::cout << "[Main] 时间同步模块初始化成功" << std::endl;

    // 初始化视频系统
    std::cout << "[Main] 初始化视频系统..." << std::endl;
    video_system_t* temp_video_system = nullptr;
    if (init_video_system(&temp_video_system, CAMERA_WIDTH, CAMERA_HEIGHT, VIDEO_MODE_NONE, &sync_ctx) != 0) {
        std::cout << "[Main] 视频系统初始化失败" << std::endl;
        sync_deinit(&sync_ctx);
        return -1;
    }
    video_system.reset(temp_video_system);
    std::cout << "[Main] 视频系统初始化成功" << std::endl;

    // 初始化音频系统
    std::cout << "[Main] 初始化音频系统..." << std::endl;
    audio_system = std::make_unique<audio_system_t>();
    if (audio_system_init(audio_system.get(), &sync_ctx) != AUDIO_ERROR_NONE) {
        std::cout << "[Main] 音频系统初始化失败" << std::endl;
        audio_system.reset();
        return -1;
    }
    std::cout << "[Main] 音频系统初始化成功" << std::endl;

    // 创建WebRTC配置
    WebRTCConfig config = createWebRTCConfig();
    
    // 创建模块实例
    signaling = std::make_shared<Signaling>(DEVICE_ID, SERVER_URL);
    webrtcManager = std::make_shared<WebRTCManage>(config);
    
    // 初始化WebRTC管理器
    if (!webrtcManager->initialize(signaling)) {
        std::cout << "[Main] WebRTC管理器初始化失败" << std::endl;
        return -1;
    }
    
    // 设置回调函数
    setupSignalingCallbacks();
    setupWebRTCCallbacks();
    
    // 设置视频WebRTC回调
    std::cout << "[Main] 设置视频WebRTC回调..." << std::endl;
    #if USE_WEBRTC
    if (set_webrtc_callback(video_system.get(), webrtcManager.get(), onVideoFrameCallback) != 0) {
        std::cout << "[Main] 设置视频WebRTC回调失败" << std::endl;
        return -1;
    }
    #endif
    std::cout << "[Main] 视频WebRTC回调设置成功" << std::endl;

    // 设置音频WebRTC回调
    std::cout << "[Main] 设置音频WebRTC回调..." << std::endl;
    #if USE_WEBRTC
    if (set_webrtc_audio_callback(audio_system.get(), webrtcManager.get(), onAudioDataCallback) != AUDIO_ERROR_NONE) {
        std::cout << "[Main] 设置音频WebRTC回调失败" << std::endl;
        return -1;
    }
    #endif
    std::cout << "[Main] 音频WebRTC回调设置成功" << std::endl;

    // 连接到服务器
    if (!signaling->connect()) {
        std::cout << "[Main] 连接服务器失败" << std::endl;
        return -1;
    }
    
    std::cout << "[Main] 正在连接服务器..." << std::endl;
    std::cout << "[Main] 输入 'q' 退出程序, 'stats' 查看统计信息" << std::endl;
    
    // 主循环
    std::string input;
    while (std::cin >> input) {
        if (input == "q" || input == "Q") {
            break;
        } else if (input == "stats") {
            if (webrtcManager) {
                auto stats = webrtcManager->getStats();
                std::cout << "\n========== WebRTC实时统计 ==========" << std::endl;
                std::cout << "音频发送包数: " << stats.audioPacketsSent << std::endl;
                std::cout << "音频接收包数: " << stats.audioPacketsReceived << std::endl;
                std::cout << "视频发送包数: " << stats.videoPacketsSent << std::endl;
                std::cout << "音频发送字节: " << stats.audioBytesSent / 1024.0 << " KB" << std::endl;
                std::cout << "视频发送字节: " << stats.videoBytesSent / 1024.0 / 1024.0 << " MB" << std::endl;
                std::cout << "====================================\n" << std::endl;
            }
        } else {
            std::cout << "[Main] 未知命令，输入 'q' 退出, 'stats' 查看统计" << std::endl;
        }
    }

    std::cout << "[Main] 正在关闭客户端..." << std::endl;
    
    // 停止音视频流
    if (audio_system) {
        #if USE_WEBRTC
        stop_webrtc_audio_stream(audio_system.get());
        #endif
        audio_system_deinit(audio_system.get());
        audio_system.reset();
        std::cout << "[Main] 音频系统已释放" << std::endl;
    }
    
    if (video_system) {
        #if USE_WEBRTC
        stop_webrtc_video_stream(video_system.get());
        #endif
        stop_video_stream(video_system.get());
        video_system_t* temp_video_system = video_system.release();
        release_video_system(&temp_video_system);
        std::cout << "[Main] 视频系统已释放" << std::endl;
    }
    
    // 释放时间同步模块
    sync_deinit(&sync_ctx);
    std::cout << "[Main] 时间同步模块已释放" << std::endl;
    
    // 按依赖关系清理资源
    if (webrtcManager) {
        webrtcManager->shutdown();
        webrtcManager.reset();
    }
    
    if (signaling) {
        signaling->disconnect();
        signaling.reset();
    }

    std::cout << "[Main] 客户端已退出" << std::endl;
    return 0;
}
