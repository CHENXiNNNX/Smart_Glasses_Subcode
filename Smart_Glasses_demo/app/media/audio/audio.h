#ifndef AUDIO_H
#define AUDIO_H

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opus/opus.h>
#include <cstdint>
#include <portaudio.h>
#include "../../protocol/websocket/websocket.h"

// 音频模式枚举
typedef enum {
    AUDIO_MODE_NONE = 0,
    AUDIO_MODE_AI,
    AUDIO_MODE_WEBRTC,
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

// 统一结构体管理audio_system_t
typedef struct {
    // 音频参数
    int sample_rate;
    int channels;
    int frame_duration_ms;

    // 编解码器相关
    OpusEncoder* encoder;
    OpusDecoder* decoder;

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

} audio_system_t;

// 初始化音频系统
audio_error_t audio_system_init(audio_system_t *audio_system);

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

// 打包二进制协议帧
BinProtocol* pack_bin_frame(audio_system_t *audio_system, const uint8_t* payload, size_t payload_size, int ws_protocol_version);

// 解包二进制协议帧
bool unpack_bin_frame(audio_system_t *audio_system, const uint8_t* packed_data, size_t packed_data_size, BinProtocolInfo& protocol_info, std::vector<uint8_t>& opus_data);

#endif // AUDIO_H