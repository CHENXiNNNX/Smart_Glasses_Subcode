#ifndef AUDIO_H
#define AUDIO_H

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opus/opus.h>
#include <cstdint>
#include <portaudio.h>
#include <samplerate.h>
#include <speex/speex_preprocess.h>
#include "../media_config.h"
#include "../../../common/common.h"
#include "../../protocol/websocket/websocket.h"
#include "../sync.h"

// 音频模式枚举
typedef enum {
    AUDIO_MODE_NONE = 0,
    AUDIO_MODE_AI,
    AUDIO_MODE_WEBRTC,
    AUDIO_MODE_WAKEWORD,
} audio_mode_t;

// 音频错误类型枚举
typedef enum {
    AUDIO_ERROR_NONE = 0,
    AUDIO_ERROR_INITIALIZE_FAILED,
    AUDIO_ERROR_DEVICE_NOT_FOUND,
    AUDIO_ERROR_STREAM_OPEN_FAILED,
    AUDIO_ERROR_STREAM_START_FAILED,
    AUDIO_ERROR_ENCODE_FAILED,
    AUDIO_ERROR_DECODE_FAILED,
    AUDIO_ERROR_MODE_CONFLICT,
    AUDIO_ERROR_INVALID_PARAM,
    AUDIO_ERROR_MEMORY_ALLOC_FAILED
} audio_error_t;

// 重采样参数结构体
typedef struct {
    int input_sample_rate;      // 输入采样率
    int output_sample_rate;     // 输出采样率
    int channels;               // 通道数
    int converter_type;         // 重采样算法类型 (SRC_SINC_*)
    SRC_STATE* src_state;       // libsamplerate状态
    bool is_initialized;        // 初始化状态
} audio_resample_t;

// 3A算法参数结构体
typedef struct {
    bool denoise_enabled;       // 降噪功能开关
    bool agc_enabled;           // 自动增益控制开关
    bool vad_enabled;           // 语音活动检测开关
    bool dereverb_enabled;      // 去混响功能开关
    float agc_level;            // AGC目标电平 (dB)
    int noise_suppress_level;   // 噪声抑制级别 (dB)
    int echo_suppress_level;    // 回声抑制级别 (dB)
    int agc_increment;          // AGC增益增加速度 (dB/秒)
    int agc_decrement;          // AGC增益减少速度 (dB/秒)
    int agc_max_gain;           // AGC最大增益 (dB)
} audio_3a_config_t;

// 统一结构体管理audio_system_t
typedef struct {
    // 音频参数
    int sample_rate;
    int channels;
    int frame_duration_ms;

    // 编解码器相关
    OpusEncoder* encoder;        // 主编码器（48kHz，用于WebRTC）
    OpusDecoder* decoder;        // 主解码器（48kHz，用于TTS）
    OpusEncoder* ai_encoder;     // AI专用编码器（16kHz）

    // 录音相关
    std::queue<std::vector<int16_t>> recordedAudioQueue;  // 录音队列
    std::mutex recordedAudioMutex;   // 录音队列互斥锁
    std::condition_variable recordedAudioCV; // 录音条件变量
    PaStream* recordStream;          // 录音流
    bool isRecording;                // 录音状态标志
    
    // 播放相关
    std::queue<std::vector<int16_t>> playbackQueue;       // 播放队列
    std::mutex playbackMutex;        // 播放队列互斥锁
    PaStream* playbackStream;        // 播放流
    bool isPlaying;                  // 播放状态标志

    // 当前音频模式
    audio_mode_t current_mode;
    
    // 重采样配置
    audio_resample_t resample_config;
    
    // 3A算法配置和状态
    audio_3a_config_t a3_config;     // 3A算法配置参数
    SpeexPreprocessState* a3_state;  // Speex预处理状态
    
    #if USE_WEBRTC
    // WebRTC相关资源
    void *webrtc_manager;           // WebRTC管理器指针
    bool is_webrtc_streaming;       // WebRTC音频推流状态
    void (*webrtc_audio_callback)(void *data, int len, uint64_t timestamp); // WebRTC音频回调函数
    #endif
    
    // xiaozhi AI相关资源
    void *ai_manager;               // AI管理器指针
    bool is_ai_streaming;           // AI音频推流状态
    void (*ai_audio_callback)(void *data, int len, uint64_t timestamp); // AI音频回调函数
    
    // 时间同步上下文
    sync_context_t *sync_ctx;        // 时间同步上下文指针
} audio_system_t;

// 初始化音频系统
audio_error_t audio_system_init(audio_system_t *audio_system, sync_context_t *sync_ctx);

// 释放音频系统
audio_error_t audio_system_deinit(audio_system_t *audio_system);

// 设置音频模式
audio_error_t set_audio_mode(audio_system_t *audio_system, audio_mode_t mode);

// 获取当前模式
audio_mode_t get_current_mode(audio_system_t *audio_system);

// 开始录音
audio_error_t start_recording(audio_system_t *audio_system);

// 停止录音
audio_error_t stop_recording(audio_system_t *audio_system);

// 清空录音队列
audio_error_t clear_recording_queue(audio_system_t *audio_system);

// 开始播放
audio_error_t start_playback(audio_system_t *audio_system);

// 停止播放函数
audio_error_t stop_playback(audio_system_t *audio_system);

// 清空播放队列
audio_error_t clear_playback_queue(audio_system_t *audio_system);

// 初始化编码器、解码器
audio_error_t init_opus_codec(audio_system_t *audio_system);

// 编码opus
audio_error_t encode_opus(audio_system_t *audio_system, uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size);

// 解码opus
audio_error_t decode_opus(audio_system_t *audio_system, uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size);

// 释放 Opus 相关资源 编码器、解码器
audio_error_t release_opus_codec(audio_system_t *audio_system);

// 保存录音后的音频文件
audio_error_t save_audio(audio_system_t *audio_system, const char *file_path);

// 获取录音数据
bool get_recorded_audio(audio_system_t *audio_system, std::vector<int16_t>& recordedData);

// 添加音频帧到播放队列
void add_frame_to_playback_queue(audio_system_t *audio_system, const std::vector<int16_t>& pcm_frame);

// 从文件加载音频
std::queue<std::vector<int16_t>> load_audio_from_file(audio_system_t *audio_system, const std::string& filename, int frame_duration_ms);

// 重采样相关函数
audio_error_t init_audio_resample(audio_system_t *audio_system, int input_rate, int output_rate, int channels, int converter_type);
audio_error_t process_audio_resample(audio_system_t *audio_system, const std::vector<int16_t>& input_data, std::vector<int16_t>& output_data);
audio_error_t release_audio_resample(audio_system_t *audio_system);
bool is_resample_initialized(audio_system_t *audio_system);

// 3A算法相关函数
audio_error_t init_audio_3a(audio_system_t *audio_system);
audio_error_t release_audio_3a(audio_system_t *audio_system);
audio_error_t process_audio_3a(audio_system_t *audio_system, std::vector<int16_t>& audio_frame);
audio_error_t configure_audio_3a(audio_system_t *audio_system, const audio_3a_config_t* config);

#if USE_RTSP
// 暂留空实现
#endif
#if USE_WEBRTC
audio_error_t start_webrtc_audio_stream(audio_system_t *audio_system);
audio_error_t stop_webrtc_audio_stream(audio_system_t *audio_system);
audio_error_t set_webrtc_audio_callback(audio_system_t *audio_system, void *webrtc_manager, void (*audio_callback)(void *data, int len, uint64_t timestamp));
#endif

// xiaozhi AI音频流管理
audio_error_t start_ai_audio_stream(audio_system_t *audio_system);
audio_error_t stop_ai_audio_stream(audio_system_t *audio_system);
audio_error_t set_ai_audio_callback(audio_system_t *audio_system, void *ai_manager, void (*audio_callback)(void *data, int len, uint64_t timestamp));

#endif // AUDIO_H
