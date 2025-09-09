#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "../media_config.h"
#include "../../../rkmpi/include/sample_comm.h"

#if USE_RTSP
#include "../../protocol/rtsp/rtsp.h"
#elif USE_WEBRTC
// WebRTC相关头文件，暂留空实现
#endif

// 视频模式枚举
typedef enum {
    VIDEO_MODE_NONE = 0,      // 空模式
    VIDEO_MODE_PHOTO,         // 拍照模式
    VIDEO_MODE_RECORD,        // 录像模式
    VIDEO_MODE_RTSP,          // RTSP推流模式
    VIDEO_MODE_WEBRTC         // WebRTC推流模式(预留空实现)
} video_mode_t;

// 错误类型枚举
typedef enum {
    VIDEO_ERROR_NONE = 0,      // 无错误
    VIDEO_ERROR_INVALID_PARAM, // 无效参数
    VIDEO_ERROR_ALLOC_MEM,     // 内存分配失败
    VIDEO_ERROR_INIT,          // 初始化失败
    VIDEO_ERROR_RUNNING,       // 运行中错误
    VIDEO_ERROR_UNKNOWN        // 未知错误
} video_error_t;

// 编码格式枚举
typedef enum {
    ENCODE_FORMAT_JPEG = 0,   // JPEG格式
    ENCODE_FORMAT_H264,       // H264格式
    ENCODE_FORMAT_H265        // H265格式(预留)
} encode_format_t;

// 视频系统结构体
typedef struct {
    // 基本参数
    int width;                    // 图像宽度
    int height;                   // 图像高度
    video_mode_t current_mode;    // 当前模式
    encode_format_t encode_format; // 编码格式
    
    // RKMPI资源
    VENC_STREAM_S stFrame;        // 编码流结构体
    
    // 协议资源 
    #if USE_RTSP
    rtsp_demo_handle rtsp_handle; // RTSP演示句柄
    rtsp_session_handle rtsp_session; // RTSP会话句柄
    bool is_rtsp_streaming;         // 是否正在RTSP推流
    #elif USE_WEBRTC
    // WebRTC相关资源，暂留空实现
    #endif
    
    // 状态管理
    bool is_initialized;          // 初始化状态
    bool is_streaming;            // 流状态
    bool quit_flag;               // 退出标志
    float current_fps;            // 当前帧率
    
    // 线程资源
    pthread_t stream_thread;      // 流处理线程
    
    // photo config
    int current_picture_id;       // 当前照片ID
    int save_picture_count;       // 保存照片数量
    int photo_capture_count;      // 拍照采集计数
    int photo_capture_total;      // 采集帧数，只取最后一帧
    bool photo_capturing;         // 是否正在拍照采集

    // record config
    int current_record_id;        // 当前录像ID
    int record_seconds;           // 录像秒数
    bool is_recording;          // 是否正在录像
    FILE *record_file;           // 录像文件句柄
    char record_filename[256];   // 录像文件名
    
    // 编码参数
    int bitrate;                  // 码率
    int gop;                      // GOP大小
    int quality;                  // 质量参数
} video_system_t;

// 统一时间管理
RK_U64 get_nowus(void);

// 视频系统管理
int isp_init(void);
int vi_dev_init(void);
int vi_chn_init(int channelId, int width, int height);
int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType);
int init_video_system(video_system_t **sys, int width, int height, video_mode_t mode);
int release_video_system(video_system_t **sys);

// 视频流管理
int start_video_stream(video_system_t *sys);
int stop_video_stream(video_system_t *sys);

// 编码流处理
int get_encoded_stream(video_system_t *sys, VENC_STREAM_S *stFrame, int timeout_ms);
int release_encoded_stream(video_system_t *sys, VENC_STREAM_S *stFrame);

// 编码器管理
int set_encoding_params(video_system_t *sys, encode_format_t format, int bitrate, int gop, int quality);

// 功能管理
int take_picture(video_system_t *sys);
int start_record(video_system_t *sys);
int stop_record(video_system_t *sys);


// 推流管理
int start_rtsp_stream(video_system_t *sys);
int stop_rtsp_stream(video_system_t *sys);
int webrtc_stream(video_system_t *sys);

// 工具函数管理
video_mode_t get_video_mode(video_system_t *sys);
int set_video_mode(video_system_t *sys, video_mode_t mode);
float get_current_fps(video_system_t *sys);
bool is_system_ready(video_system_t *sys);
int set_video_quality(video_system_t *sys, int bitrate, int gop);

#endif // __CAMERA_H__
