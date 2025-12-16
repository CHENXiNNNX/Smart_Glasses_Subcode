
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "tg28_powerkey_demo.h"
// 全局句柄（方便信号处理函数访问）
static Tg28KeyHandle g_tg28_handle;

// 自定义短按处理函数
void my_short_press_handler(void)
{
    printf("[业务逻辑] 检测到电源键短按 → 执行关机操作...\n");
    // 这里添加实际业务逻辑：比如调用system("poweroff")等
}

// 自定义双击处理函数
void my_double_click_handler(void)
{
    printf("[业务逻辑] 检测到电源键双击 → 执行重启操作...\n");
    // 这里添加实际业务逻辑：比如调用system("reboot")等
}

// 信号处理（捕获Ctrl+C，优雅退出）
void sigint_handler(int sig)
{
    printf("\nReceived Ctrl+C, exit gracefully...\n");
    tg28_key_deinit(&g_tg28_handle);
    exit(0);
}

int main(int argc, char *argv[])
{
    int ret;

    // 注册Ctrl+C信号处理
    signal(SIGINT, sigint_handler);

    // 1. 初始化TG28设备，注册自定义回调
    ret = tg28_key_init(&g_tg28_handle, my_short_press_handler, my_double_click_handler);
    if (ret != 0) {
        fprintf(stderr, "TG28 init failed\n");
        return -1;
    }

    // 2. 启动后台事件监听线程
    ret = tg28_key_start(&g_tg28_handle);
    if (ret != 0) {
        tg28_key_deinit(&g_tg28_handle);
        fprintf(stderr, "TG28 start failed\n");
        return -1;
    }

    // 3. 主线程处理其他业务（示例：循环打印）
    printf("TG28 key listener running... (Ctrl+C to exit)\n");
    while (1) {
        sleep(2);
        //printf("主线程：处理其他业务中...\n");
    }

    // 4. 释放资源（实际由信号处理函数触发）
    tg28_key_deinit(&g_tg28_handle);
    return 0;
}