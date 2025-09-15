#include "app/media/camera/camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// 全局变量
static video_system_t *g_sys = NULL;
static bool g_running = true;

// 信号处理函数
void signal_handler(int sig) {
    printf("\n[CAMERA] Received signal %d, shutting down...\n", sig);
    g_running = false;
}

// 打印帮助信息
void print_help() {
    printf("\n=== 智能眼镜相机测试程序 ===\n");
    printf("命令列表:\n");
    printf("  help, h     - 显示帮助信息\n");
    printf("  init        - 初始化相机系统\n");
    printf("  start       - 启动视频流\n");
    printf("  stop        - 停止视频流\n");
    printf("  mode <n>    - 切换模式 (1:拍照, 2:录像, 3:RTSP)\n");
    printf("  photo       - 拍照\n");
    printf("  record      - 开始录像\n");
    printf("  stop_record - 停止录像\n");
    printf("  rtsp        - 开始RTSP推流\n");
    printf("  stop_rtsp   - 停止RTSP推流\n");
    printf("  fps         - 显示当前FPS\n");
    printf("  status      - 显示系统状态\n");
    printf("  quit, q     - 退出程序\n");
    printf("==============================\n\n");
}

// 初始化相机系统
int init_camera() {
    if (g_sys) {
        printf("[CAMERA] System already initialized\n");
        return 0;
    }
    
    printf("[CAMERA] Initializing camera system...\n");
    
    // 初始化视频系统 (1920x1080, RTSP模式)
    if (init_video_system(&g_sys, 1920, 1080, VIDEO_MODE_RTSP) != 0) {
        printf("[CAMERA] Failed to initialize video system\n");
        return -1;
    }
    
    printf("[CAMERA] Camera system initialized successfully\n");
    return 0;
}

// 启动视频流
int start_stream() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    if (start_video_stream(g_sys) != 0) {
        printf("[CAMERA] Failed to start video stream\n");
        return -1;
    }
    
    printf("[CAMERA] Video stream started\n");
    return 0;
}

// 停止视频流
int stop_stream() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    if (stop_video_stream(g_sys) != 0) {
        printf("[CAMERA] Failed to stop video stream\n");
        return -1;
    }
    
    printf("[CAMERA] Video stream stopped\n");
    return 0;
}

// 切换模式
int switch_mode(int mode) {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    video_mode_t video_mode;
    switch (mode) {
        case 1:
            video_mode = VIDEO_MODE_PHOTO;
            printf("[CAMERA] Switching to PHOTO mode\n");
            break;
        case 2:
            video_mode = VIDEO_MODE_RECORD;
            printf("[CAMERA] Switching to RECORD mode\n");
            break;
        case 3:
            video_mode = VIDEO_MODE_RTSP;
            printf("[CAMERA] Switching to RTSP mode\n");
            break;
        default:
            printf("[CAMERA] Invalid mode: %d\n", mode);
            return -1;
    }
    
    if (set_video_mode(g_sys, video_mode) != 0) {
        printf("[CAMERA] Failed to switch mode\n");
        return -1;
    }
    
    return 0;
}

// 拍照
int take_photo() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    if (get_video_mode(g_sys) != VIDEO_MODE_PHOTO) {
        printf("[CAMERA] Not in photo mode, switching...\n");
        if (switch_mode(1) != 0) {
            return -1;
        }
    }
    
    if (take_picture(g_sys) != 0) {
        printf("[CAMERA] Failed to take picture\n");
        return -1;
    }
    
    printf("[CAMERA] Photo capture started\n");
    return 0;
}

// 开始录像
int start_recording() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    if (get_video_mode(g_sys) != VIDEO_MODE_RECORD) {
        printf("[CAMERA] Not in record mode, switching...\n");
        if (switch_mode(2) != 0) {
            return -1;
        }
    }
    
    if (start_record(g_sys) != 0) {
        printf("[CAMERA] Failed to start recording\n");
        return -1;
    }
    
    printf("[CAMERA] Recording started\n");
    return 0;
}

// 停止录像
int stop_recording() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    if (stop_record(g_sys) != 0) {
        printf("[CAMERA] Failed to stop recording\n");
        return -1;
    }
    
    printf("[CAMERA] Recording stopped\n");
    return 0;
}

// 开始RTSP推流
int start_rtsp() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    if (get_video_mode(g_sys) != VIDEO_MODE_RTSP) {
        printf("[CAMERA] Not in RTSP mode, switching...\n");
        if (switch_mode(3) != 0) {
            return -1;
        }
    }
    
    if (start_rtsp_stream(g_sys) != 0) {
        printf("[CAMERA] Failed to start RTSP stream\n");
        return -1;
    }
    
    printf("[CAMERA] RTSP stream started\n");
    return 0;
}

// 停止RTSP推流
int stop_rtsp() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return -1;
    }
    
    if (stop_rtsp_stream(g_sys) != 0) {
        printf("[CAMERA] Failed to stop RTSP stream\n");
        return -1;
    }
    
    printf("[CAMERA] RTSP stream stopped\n");
    return 0;
}

// 显示FPS
void show_fps() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return;
    }
    
    float fps = get_current_fps(g_sys);
    printf("[CAMERA] Current FPS: %.2f\n", fps);
}

// 显示系统状态
void show_status() {
    if (!g_sys) {
        printf("[CAMERA] System not initialized\n");
        return;
    }
    
    printf("\n=== 系统状态 ===\n");
    printf("初始化状态: %s\n", g_sys->is_initialized ? "已初始化" : "未初始化");
    printf("流状态: %s\n", g_sys->is_streaming ? "运行中" : "已停止");
    printf("当前模式: %d\n", g_sys->current_mode);
    printf("当前FPS: %.2f\n", g_sys->current_fps);
    printf("图像尺寸: %dx%d\n", g_sys->width, g_sys->height);
    printf("编码格式: %d\n", g_sys->encode_format);
    printf("===============\n\n");
}

// 清理资源
void cleanup() {
    if (g_sys) {
        printf("[CAMERA] Cleaning up resources...\n");
        stop_stream();
        release_video_system(&g_sys);
        g_sys = NULL;
    }
}

// 主函数
int main(int argc, char *argv[]) {
    char command[256];
    char *token;
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("智能眼镜相机测试程序启动\n");
    print_help();
    
    while (g_running) {
        printf("camera> ");
        fflush(stdout);
        
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        // 移除换行符
        command[strcspn(command, "\n")] = 0;
        
        // 解析命令
        token = strtok(command, " ");
        if (token == NULL) {
            continue;
        }
        
        if (strcmp(token, "help") == 0 || strcmp(token, "h") == 0) {
            print_help();
        }
        else if (strcmp(token, "init") == 0) {
            init_camera();
        }
        else if (strcmp(token, "start") == 0) {
            start_stream();
        }
        else if (strcmp(token, "stop") == 0) {
            stop_stream();
        }
        else if (strcmp(token, "mode") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                int mode = atoi(token);
                switch_mode(mode);
            } else {
                printf("[CAMERA] Usage: mode <1|2|3>\n");
            }
        }
        else if (strcmp(token, "photo") == 0) {
            take_photo();
        }
        else if (strcmp(token, "record") == 0) {
            start_recording();
        }
        else if (strcmp(token, "stop_record") == 0) {
            stop_recording();
        }
        else if (strcmp(token, "rtsp") == 0) {
            start_rtsp();
        }
        else if (strcmp(token, "stop_rtsp") == 0) {
            stop_rtsp();
        }
        else if (strcmp(token, "fps") == 0) {
            show_fps();
        }
        else if (strcmp(token, "status") == 0) {
            show_status();
        }
        else if (strcmp(token, "quit") == 0 || strcmp(token, "q") == 0) {
            break;
        }
        else {
            printf("[CAMERA] Unknown command: %s\n", token);
            printf("Type 'help' for available commands\n");
        }
    }
    
    cleanup();
    printf("[CAMERA] Program terminated\n");
    return 0;
}