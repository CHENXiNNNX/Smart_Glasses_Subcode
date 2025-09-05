#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "test_camera.h"
#include "media_config.h"

int main(int argc, char *argv[]) {
    video_system_t *video_sys = NULL;
    
    // 初始化视频系统
    if (init_video_system(&video_sys, CAMERA_WIDTH, CAMERA_HEIGHT) != 0) {
        printf("Failed to initialize video system\n");
        return -1;
    }
    
    printf("Video system initialized successfully\n");
    
    // 主循环
    while (1) {
        // 捕获帧
        if (capture_frame(video_sys) != 0) {
            continue;
        }
        
        // 处理帧
        if (process_frame(video_sys) != 0) {
            release_frame(video_sys);
            continue;
        }
        
        // 编码视频
        if (encode_video(video_sys) != 0) {
            release_frame(video_sys);
            continue;
        }
        
        // RTSP推流
        if (rtsp_stream(video_sys) != 0) {
            release_frame(video_sys);
            continue;
        }
        
        // 释放帧资源
        release_frame(video_sys);
    }
    
    // 清理资源
    release_video_system(&video_sys);
    return 0;
}



