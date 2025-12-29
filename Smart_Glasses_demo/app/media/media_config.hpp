#ifndef MEDIA_CONFIG_HPP
#define MEDIA_CONFIG_HPP

#define USE_RTSP 0
#define RTSP_PORT 554
#define RTSP_PATH "/live/0"

#define USE_WEBRTC 1

#define DISPLAY_FPS 0 // 1: display FPS, 0: not display FPS

#define CAMERA_WIDTH 1280
#define CAMERA_HEIGHT 720
#define CAMERA_FPS 30
#define H264_Default_Bitrate                                                                       \
    5 * 1024 // 越高->画质更好、带宽更大、卡顿风险更低，但占网更大；越低->更省带宽，但容易糊、方块
#define H264_Default_Gop                                                                           \
    10 // 越大->码流更省、画质平均更稳，但丢包恢复慢、切流黑屏更久；越小->恢复快、首屏快，但码率更高

#define ISP_PATH "/etc/iqfiles"
#define PICTURE_PATH "/root/picture/"
#define RECORD_PATH "/root/video/"

// 音频配置常量
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 1
#define AUDIO_FRAME_DURATION_MS 20
#define AUDIO_BIT_RATE 32000
#define AUDIO_FRAME_SIZE (AUDIO_SAMPLE_RATE / 1000 * AUDIO_FRAME_DURATION_MS) * AUDIO_CHANNELS

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
#define AUDIO_MASTER_VOLUME 0.3f      // 主音量 (0.0-1.0)

#endif // MEDIA_CONFIG_HPP
