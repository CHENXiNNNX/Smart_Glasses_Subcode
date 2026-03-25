#pragma once

#define RTSP_PORT 554
#define RTSP_PATH "/live/0"

#define CAMERA_WIDTH 1280
#define CAMERA_HEIGHT 720
#define CAMERA_FPS 60
#define H264_Default_Bitrate                                                                       \
    10 * 1024 
#define H264_Default_Gop                                                                           \
    30 

#define ISP_PATH "/etc/iqfiles"

// AIISP 降噪
#define CAMERA_AIISP_ENABLED 1

// 音频配置常量
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 1
#define AUDIO_FRAME_DURATION_MS 20
#define AUDIO_BIT_RATE 32000

// 音频3A算法配置参数
#define AUDIO_DENOISE_ENABLED true  // 降噪功能开关
#define AUDIO_AGC_ENABLED true      // 自动增益控制开关
#define AUDIO_VAD_ENABLED true      // 语音活动检测开关
#define AUDIO_DEREVERB_ENABLED true // 去混响功能开关
#define AUDIO_AGC_LEVEL 8000.0f     // AGC目标电平 (dB): 范围1000.0-32768.0
#define AUDIO_NOISE_SUPPRESS_LEVEL -45 // 噪声抑制级别 (dB): 范围-30至0, 值越小抑制越强,
#define AUDIO_ECHO_SUPPRESS_LEVEL -90 // 回声抑制级别 (dB): 范围-90至0, 值越小抑制越强
#define AUDIO_AGC_INCREMENT 12        // AGC增益增加速度 (dB/秒): 范围0-30
#define AUDIO_AGC_DECREMENT -40       // AGC增益减少速度 (dB/秒): 范围-90至0
#define AUDIO_AGC_MAX_GAIN 10         // AGC最大增益 (dB): 范围0-60
