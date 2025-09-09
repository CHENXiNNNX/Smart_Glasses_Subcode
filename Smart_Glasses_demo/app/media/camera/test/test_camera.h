#ifndef __CAMERA_H
#define __CAMERA_H

// 媒体配置文件
#include "../../media_config.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../../../rkmpi/include/sample_comm.h"

#if USE_RTSP
#include "../../../protocol/rtsp/rtsp.h"
#elif USE_WEBRTC
#include "../../../protocol/webrtc/webrtc.h"
#endif

// 核心结构体
typedef struct {
    // 基本参数
    int width;              
    int height;
    
    // RKMPI资源
    VENC_STREAM_S stFrame; // 编码流
    
    
#if USE_RTSP
    // RTSP资源
    rtsp_demo_handle rtsp_handle;
    rtsp_session_handle rtsp_session;
#elif USE_WEBRTC
    //空实现
#endif
    
    // 状态管理
    bool is_initialized;
    bool is_streaming;
    bool quit_flag; 
    float current_fps;
    
    // 线程资源
    pthread_t stream_thread;
} video_system_t;

// 系统管理接口
int init_video_system(video_system_t **sys, int width, int height);
int start_video_stream(video_system_t *sys);
int stop_video_stream(video_system_t *sys);
int release_video_system(video_system_t **sys);

// 工具接口
float get_current_fps(video_system_t *sys);
bool is_system_ready(video_system_t *sys);
int set_video_quality(video_system_t *sys, int bitrate, int gop);

// 内部工具函数
RK_U64 TEST_COMM_GetNowUs(void);
int vi_dev_init(void);
int vi_chn_init(int channelId, int width, int height);
int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType);

#endif // __CAMERA_H
