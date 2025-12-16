#ifndef TG28_POWERKEY_H_
#define TG28_POWERKEY_H_

#include <stdint.h>
#include <pthread.h>

/* 按键定义（复用系统标准值） */
#define KEY_POWER     116
#define KEY_RESTART   114  // 内核标准KEY_RESTART数值
#define KEY_SLEEP     142

/* ========================== 对外暴露的类型定义 ========================== */
// 回调函数类型：短按/双击触发时执行
typedef void (*Tg28ShortPressCallback)(void);
typedef void (*Tg28DoubleClickCallback)(void);

// TG28设备句柄（封装内部状态，调用方只需声明，无需关心内部实现细节）
typedef struct {
    int fd;                     // 设备文件描述符
    char dev_path[64];          // 设备路径
    pthread_t event_thread;     // 事件处理线程ID
    int thread_running;         // 线程运行标记
    // 回调函数指针
    Tg28ShortPressCallback short_cb;
    Tg28DoubleClickCallback double_cb;
} Tg28KeyHandle;

/* ========================== 对外暴露的核心接口 ========================== */
/**
 * tg28_key_init - 初始化TG28按键设备
 * @handle: 输出参数，设备句柄（调用方需传入空的Tg28KeyHandle指针）
 * @short_cb: 短按回调函数（可为NULL）
 * @double_cb: 双击回调函数（可为NULL）
 * 返回值：0成功，-1失败
 */
int tg28_key_init(Tg28KeyHandle *handle, Tg28ShortPressCallback short_cb, Tg28DoubleClickCallback double_cb);

/**
 * tg28_key_start - 启动事件监听（后台线程）
 * @handle: 设备句柄
 * 返回值：0成功，-1失败
 */
int tg28_key_start(Tg28KeyHandle *handle);

/**
 * tg28_key_stop - 停止事件监听
 * @handle: 设备句柄
 * 返回值：0成功，-1失败
 */
int tg28_key_stop(Tg28KeyHandle *handle);

/**
 * tg28_key_deinit - 释放TG28设备资源
 * @handle: 设备句柄
 * 返回值：0成功，-1失败
 */
int tg28_key_deinit(Tg28KeyHandle *handle);

#endif // TG28_KEY_H_