#include "tg28_powerkey_demo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>

/* ========================== 内部工具函数（对外不可见） ========================== */
// 获取毫秒级时间戳（内部调试用）
static unsigned long get_timestamp_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime failed");
        return 0;
    }
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

// 查找TG28电源键设备（内部使用）
static int find_tg28_key_device(char *dev_path, size_t len)
{
    DIR *dir;
    struct dirent *entry;
    char path[256], name[256];
    int fd;

    dir = opendir("/dev/input");
    if (!dir) {
        perror("opendir /dev/input failed");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;

        if (ioctl(fd, EVIOCGNAME(sizeof(name)-1), name) > 0) {
            if (strcmp(name, "tg28-powerkey") == 0) {
                strncpy(dev_path, path, len-1);
                close(fd);
                closedir(dir);
                return 0;
            }
        }
        close(fd);
    }

    closedir(dir);
    fprintf(stderr, "TG28 power key device not found!\n");
    return -1;
}

// 事件处理核心逻辑（内部线程函数）
static void *tg28_event_handler(void *arg)
{
    Tg28KeyHandle *handle = (Tg28KeyHandle *)arg;
    struct input_event ev;
    ssize_t n;

    while (handle->thread_running) {
        n = read(handle->fd, &ev, sizeof(struct input_event));

        if (n < 0) {
            if (errno == EAGAIN) {
                usleep(10000); // 10ms休眠，降低CPU占用
                continue;
            }
            perror("read event failed");
            break;
        }

        // 确保读取到完整的事件
        if (n != sizeof(struct input_event)) {
            fprintf(stderr, "Invalid event size: %zd\n", n);
            continue;
        }

        // 只处理按键事件
        if (ev.type != EV_KEY)
            continue;

        // 触发短按回调
        if (ev.code == KEY_POWER && ev.value == 1) {
            if (handle->short_cb) {
                handle->short_cb(); // 执行调用方的短按处理函数
            }
        }
        // 触发双击回调
        else if (ev.code == KEY_RESTART && ev.value == 1) {
            if (handle->double_cb) {
                handle->double_cb(); // 执行调用方的双击处理函数
            }
        }
    }

    pthread_exit(NULL);
    return NULL;
}

/* ========================== 对外接口的具体实现 ========================== */
int tg28_key_init(Tg28KeyHandle *handle, Tg28ShortPressCallback short_cb, Tg28DoubleClickCallback double_cb)
{
    if (!handle) {
        fprintf(stderr, "Invalid handle pointer\n");
        return -1;
    }

    // 初始化句柄
    memset(handle, 0, sizeof(Tg28KeyHandle));
    handle->short_cb = short_cb;
    handle->double_cb = double_cb;

    // 查找设备
    if (find_tg28_key_device(handle->dev_path, sizeof(handle->dev_path)) != 0) {
        return -1;
    }

    // 打开设备（非阻塞模式）
    handle->fd = open(handle->dev_path, O_RDONLY | O_NONBLOCK);
    if (handle->fd < 0) {
        perror("open device failed");
        return -1;
    }

    printf("TG28 key device init success: %s\n", handle->dev_path);
    return 0;
}

int tg28_key_start(Tg28KeyHandle *handle)
{
    if (!handle || handle->fd < 0) {
        fprintf(stderr, "Device not initialized\n");
        return -1;
    }

    if (handle->thread_running) {
        fprintf(stderr, "Event thread already running\n");
        return -1;
    }

    // 标记线程运行，创建后台线程
    handle->thread_running = 1;
    int ret = pthread_create(&handle->event_thread, NULL, tg28_event_handler, handle);
    if (ret != 0) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(ret));
        handle->thread_running = 0;
        return -1;
    }

    // 分离线程，避免资源泄漏
    pthread_detach(handle->event_thread);
    printf("TG28 event thread started\n");
    return 0;
}

int tg28_key_stop(Tg28KeyHandle *handle)
{
    if (!handle || !handle->thread_running) {
        return 0;
    }

    // 停止线程
    handle->thread_running = 0;
    // 等待线程退出（最多500ms）
    pthread_join(handle->event_thread, NULL);
    printf("TG28 event thread stopped\n");
    return 0;
}

int tg28_key_deinit(Tg28KeyHandle *handle)
{
    if (!handle) {
        return -1;
    }

    // 停止事件监听
    tg28_key_stop(handle);

    // 关闭设备
    if (handle->fd >= 0) {
        close(handle->fd);
        handle->fd = -1;
    }

    memset(handle, 0, sizeof(Tg28KeyHandle));
    printf("TG28 key device deinitialized\n");
    return 0;
}