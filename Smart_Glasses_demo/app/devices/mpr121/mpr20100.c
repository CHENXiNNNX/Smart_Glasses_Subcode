
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <linux/input.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include"mpr20100.h"


/************************ 静态全局变量（内部使用） ************************/
static MPR121_DevHandle g_mpr121_handle = {0}; // 全局设备句柄
// 在mpr20100.c的全局变量区域添加
 //MPR121_DevHandle g_mpr121_handle = {0}; // 全局设备句柄
/************************ 内部工具函数（static，对外不可见） ************************/
/**
 * @brief 信号处理函数：捕获Ctrl+C，设置退出标志
 * @param sig 信号值（仅处理SIGINT）
 */
static void mpr121_sigint_handler(int sig) {
    if (sig == SIGINT) {
        printf("\n[MPR121] Received Ctrl+C, preparing to exit...\n");
        g_mpr121_handle.exit_flag = 1;
    }
}

/**
 * @brief 内部函数：查找MPR121对应的input设备路径
 * @return 成功返回设备路径字符串（需free），失败返回NULL
 */
static char *mpr121_find_dev_path(void) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    char *dev_path = NULL;
    char name[256] = {0};
    int fd = -1, ret = -1;

    // 打开/dev/input目录
    dir = opendir("/dev/input");
    if (!dir) {
        fprintf(stderr, "[MPR121] Failed to open /dev/input: %s\n", strerror(errno));
        return NULL;
    }

    // 遍历目录下的event文件
    while ((entry = readdir(dir)) != NULL) {
        // 只处理event开头的文件
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        // 拼接设备路径
        char path[64] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);

        // 打开设备读取名称
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            continue;
        }

        // 通过ioctl获取设备名称
        ret = ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        close(fd);
        if (ret < 0) {
            continue;
        }
        name[ret] = '\0';

        // 匹配MPR121设备名称
        if (strcmp(name, MPR121_DEV_NAME) == 0) {
            dev_path = strdup(path);
            break;
        }
    }

    closedir(dir);

    // 未找到设备的提示
    if (!dev_path) {
        fprintf(stderr, "[MPR121] Device not found (name: %s)\n", MPR121_DEV_NAME);
    }

    return dev_path;
}

/**
 * @brief 内部函数：解析单个input_event为MPR121_Event
 * @param ev input子系统事件结构体
 * @param event 输出的MPR121事件结构体
 * @param channel 缓存的触摸通道号（由EV_MSC事件解析）
 * @return 成功返回0，无有效事件返回-1
 */
static int mpr121_parse_event(const struct input_event *ev, MPR121_Event *event, int *channel) {
    if (!ev || !event || !channel) {
        return -1;
    }

    // 初始化事件
    memset(event, 0, sizeof(MPR121_Event));
    event->type = MPR121_EVENT_NONE;
    event->timestamp.tv_sec = ev->time.tv_sec;    // 复制秒
    event->timestamp.tv_nsec = ev->time.tv_usec * 1000;  // 微秒转纳秒
    // 解析触摸通道（EV_MSC事件）
    if (ev->type == EV_MSC && ev->code == MSC_SCAN) {
        if(ev->value==0)
        *channel = ev->value+1;
       else if(ev->value==1)
        *channel = ev->value-1;
       else
        *channel = ev->value;
       //  *channel = ev->value;
        return -1; // 仅缓存通道，无有效事件
    }

    // 解析按键事件（EV_KEY）
    if (ev->type == EV_KEY) {
        event->channel = *channel;
        event->key_code = ev->code;
        event->type = (ev->value) ? MPR121_EVENT_PRESSED : MPR121_EVENT_RELEASED;
        return 0; // 解析到有效事件
    }

    // 同步事件（无数据，忽略）
    if (ev->type == EV_SYN) {
        return -1;
    }

    return -1;
}

/************************ 对外公开接口（核心功能） ************************/
/**
 * @brief 初始化MPR121设备
 * @return 成功返回0，失败返回-1
 * @note 其他开发者首先调用此接口初始化设备
 */
int mpr121_init(void) {
    // 重置全局句柄
    memset(&g_mpr121_handle, 0, sizeof(MPR121_DevHandle));

    // 1. 查找设备路径
    g_mpr121_handle.dev_path = mpr121_find_dev_path();
    if (!g_mpr121_handle.dev_path) {
        return -1;
    }

    // 2. 注册信号处理（捕获Ctrl+C）
    signal(SIGINT, mpr121_sigint_handler);

    // 3. 非阻塞模式打开设备
    g_mpr121_handle.fd = open(g_mpr121_handle.dev_path, O_RDONLY | O_NONBLOCK);
    if (g_mpr121_handle.fd < 0) {
        fprintf(stderr, "[MPR121] Failed to open device %s: %s\n", 
                g_mpr121_handle.dev_path, strerror(errno));
        free(g_mpr121_handle.dev_path);
        g_mpr121_handle.dev_path = NULL;
        return -1;
    }

    printf("[MPR121] Init success (device: %s, fd: %d)\n", 
           g_mpr121_handle.dev_path, g_mpr121_handle.fd);
    return 0;
}

/**
 * @brief 读取MPR121触摸事件（阻塞/非阻塞兼容）
 * @param event 输出参数：存储读取到的事件
 * @param timeout_ms 超时时间（ms）：0=非阻塞，-1=永久阻塞，>0=超时阻塞
 * @return 成功返回0，超时返回1，失败返回-1
 * @note 其他开发者调用此接口读取事件
 */
int mpr121_read_event(MPR121_Event *event, int timeout_ms) {
    struct input_event ev = {0};
    ssize_t n = 0;
    int current_channel = -1; // 缓存当前触摸通道
    struct timeval tv = {0};
    fd_set read_fds;

    // 参数校验
    if (!event || g_mpr121_handle.fd < 0) {
        fprintf(stderr, "[MPR121] Invalid param or device not init\n");
        return -1;
    }

    // 超时设置（仅阻塞模式）
    if (timeout_ms > 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
    }

    // 循环读取事件，直到获取有效事件/超时/退出
    while (!g_mpr121_handle.exit_flag) {
        FD_ZERO(&read_fds);
        FD_SET(g_mpr121_handle.fd, &read_fds);

        // 阻塞等待文件描述符可读
        int ret = select(g_mpr121_handle.fd + 1, &read_fds, NULL, NULL, 
                        (timeout_ms < 0) ? NULL : ((timeout_ms == 0) ? &tv : &tv));
        if (ret < 0) {
            if (errno == EINTR) {
                continue; // 被信号中断，重试
            }
            fprintf(stderr, "[MPR121] Select error: %s\n", strerror(errno));
            return -1;
        }
        if (ret == 0) {
            return 1; // 超时
        }

        // 读取事件
        n = read(g_mpr121_handle.fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            fprintf(stderr, "[MPR121] Read error: %s\n", strerror(errno));
            return -1;
        }

        // 校验事件长度
        if (n != sizeof(ev)) {
            fprintf(stderr, "[MPR121] Invalid event size: %zd (expected %zu)\n", 
                    n, sizeof(ev));
            continue;
        }

        // 解析事件
        if (mpr121_parse_event(&ev, event, &current_channel) == 0) {
            // 解析到有效事件，返回
            return 0;
        }
    }

    return -1; // 收到退出信号
}

/**
 * @brief 释放MPR121设备资源
 * @return 成功返回0，失败返回-1
 * @note 其他开发者退出前调用此接口
 */
int mpr121_deinit(void) {
    // 关闭设备文件描述符
    if (g_mpr121_handle.fd >= 0) {
        close(g_mpr121_handle.fd);
        g_mpr121_handle.fd = -1;
    }

    // 释放设备路径内存
    if (g_mpr121_handle.dev_path) {
        free(g_mpr121_handle.dev_path);
        g_mpr121_handle.dev_path = NULL;
    }

    // 重置退出标志
    g_mpr121_handle.exit_flag = 0;

    printf("[MPR121] Deinit success\n");
    return 0;
}

/**
 * @brief 获取MPR121设备状态
 * @return 已初始化返回1，未初始化返回0
 * @note 辅助接口，用于检查设备状态
 */
int mpr121_is_inited(void) {
    return (g_mpr121_handle.fd >= 0) ? 1 : 0;
}


/**
 * @brief 获取MPR121设备文件描述符
 * @return 成功返回文件描述符，失败返回-1
 */
 int mpr121_get_fd(void) {
    return g_mpr121_handle.fd;
}

/**
 * @brief 获取退出标志，在按下Ctrl+C时被设置
 * @return 0表示标志未被设置 ，1表示标志已设置，需要退出程序
 */
 int mpr121_get_exit_flag(void) {
    return g_mpr121_handle.exit_flag;
}