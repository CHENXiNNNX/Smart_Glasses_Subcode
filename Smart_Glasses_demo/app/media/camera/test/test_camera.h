#ifndef __CAMERA_H
#define __CAMERA_H

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "sample_comm.h"
#include "rtsp_demo.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

// 核心结构体
typedef struct {
    // 基本参数
    int width;
    int height;
    
    // RKMPI资源
    MB_POOL mem_pool;
    MB_BLK mem_block;
    VIDEO_FRAME_INFO_S h264_frame;
    VIDEO_FRAME_INFO_S vi_frame;
    VENC_STREAM_S stFrame; // h264 fream
    
    // OpenCV资源
    cv::Mat *opencv_frame;
    unsigned char *frame_data;
    
    // RTSP资源
    rtsp_demo_handle rtsp_handle = NULL;
    rtsp_session_handle rtsp_session;
    
    // 状态管理
    bool is_initialized;
    bool is_streaming;
    float current_fps;
    RK_U32 time_ref;
    
    // 性能统计
    char fps_text[16];
} video_system_t;

// 前向声明结构体video_system_t
// typedef struct video_system_s video_system_t;

// 系统管理接口
int init_video_system(video_system_t **sys, int width, int height);
int start_video_stream(video_system_t *sys);
int stop_video_stream(video_system_t *sys);
int release_video_system(video_system_t **sys);

// 帧处理接口
int capture_frame(video_system_t *sys);
int process_frame(video_system_t *sys);
int encode_video(video_system_t *sys);
int rtsp_stream(video_system_t *sys);
int release_frame(video_system_t *sys);

// 工具接口
float get_current_fps(video_system_t *sys);
bool is_system_ready(video_system_t *sys);
int set_video_quality(video_system_t *sys, int bitrate, int gop);

RK_U64 TEST_COMM_GetNowUs();
int vi_dev_init();
int vi_chn_init(int channelId, int width, int height);
int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType);

#endif // __CAMERA_H