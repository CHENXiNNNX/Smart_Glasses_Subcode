#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yolov5.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

#include <unistd.h>   
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <time.h>
#include "AIcamera_c_interface.h"


// 计算framebuffer大小
size_t screensize = 320 * 240 * 2; // BGR565格式，每个像素占2字节

int main(int argc, char **argv) 
{
    if (argc != 2)
    {
        printf("%s <yolov5 model_path>\n", argv[0]);
        return -1;
    }
    system("RkLunch-stop.sh");
    const char *model_path = argv[1];

    // 打开framebuffer设备
    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) {
        printf("Error: cannot open framebuffer device.");
        return -1;
    }

    // 映射framebuffer到内存
    uint8_t* framebuffer = (uint8_t*)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (framebuffer == MAP_FAILED) {
        printf("Error: failed to map framebuffer to memory.");
        close(fb);
        return -1;
    }

    // 启动AI相机
    if (start_ai_camera(model_path) != 0) {
        printf("Failed to start AI camera.\n");
        munmap(framebuffer, screensize);
        close(fb);
        return -1;
    }

    // 持续从buf中读取数据并刷新到framebuffer
    while (true) {
        usleep(33000);
        get_buf_data(framebuffer);
    }

    // 正常情况下不会到达这里，但在程序结束时应该清理资源
    stop_ai_camera();
    munmap(framebuffer, screensize);
    close(fb);

    return 0;
}