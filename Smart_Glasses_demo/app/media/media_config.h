#ifndef MEDIA__CONFIG_H
#define MEDIA__CONFIG_H

#define USE_RTSP 0
#define RTSP_PORT 554
#define RTSP_PATH "/live/0"

#define USE_WEBRTC 1

#define DISPLAY_FPS 0 // 1: display FPS, 0: not display FPS

#define CAMERA_WIDTH  1280 
#define CAMERA_HEIGHT 720 
#define CAMERA_FPS 30
#define H264_Default_Bitrate 5 * 1024  // 越高→画质更好、带宽更大、卡顿风险更低，但占网更大；越低→更省带宽，但容易糊、方块
#define H264_Default_Gop 30 // 越大→码流更省、画质平均更稳，但丢包恢复慢、切流黑屏更久；越小→恢复快、首屏快，但码率更高

#define ISP_PATH "/etc/iqfiles"
#define PICTURE_PATH "/root/picture/"
#define RECORD_PATH "/root/video/"

// 音频配置常量
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 1
#define AUDIO_FRAME_DURATION_MS 20
#define AUDIO_FRAME_SIZE (AUDIO_SAMPLE_RATE / 1000 * AUDIO_FRAME_DURATION_MS) * AUDIO_CHANNELS


#endif // MEDIA__CONFIG_H
