#include <stdio.h> 
#include <unistd.h> 
#include <signal.h> 
#include"mpr20100.h"

int main(void) {
    MPR121_Event event;
    int ret;

    // 1. 初始化设备
    if (mpr121_init() != 0) {
        fprintf(stderr, "[MPR121] Init failed\n");
        return -1;
    }

    printf("[MPR121] Start reading events (Ctrl+C to exit)...\n");
    printf("Format: Channel=%d, KeyCode=%d, Type=[Pressed/Released]\n", 0, 0);
    printf("=============================================================\n");

    // 2. 循环读取事件
    while (1) {
        // 非阻塞读取（超时10ms，避免CPU占用过高）
        ret = mpr121_read_event(&event, 10);
        if (ret == 0) {
            // 解析到有效事件，打印
            const char *type_str = (event.type == MPR121_EVENT_PRESSED) ? "Pressed" : "Released";
            printf("[MPR121] Channel=%d, KeyCode=%d (0x%04x), Type=%s\n",
                   event.channel, event.key_code, event.key_code, type_str);
        } else if (ret == -1) {
            // 读取失败或收到退出信号，退出循环
            break;
        }
        // 超时（ret==1）：无事件，继续循环
    }

    // 3. 释放资源
    mpr121_deinit();
    printf("[MPR121] Exit success\n");

    return 0;
}
//#endif