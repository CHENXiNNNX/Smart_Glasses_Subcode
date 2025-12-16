#ifndef __TOUCH_HANDLE_H
#define __TOUCH_HANDLE_H

#include <time.h>
#include <signal.h>
#include "mpr20100.h"

// 状态机状态定义
typedef enum {
    STATE_IDLE,                // 空闲状态
    STATE_WAIT_RELEASE,        // 等待释放（已按下未释放）
    STATE_WAIT_DBL_CLICK,      // 等待双击（第一次单击已完成）
    STATE_WAIT_SLIDE_RELEASE   // 等待滑动释放（多通道按下）
} TouchState;

// 同时按下的通道信息
typedef struct {
    int channels[MPR121_MAX_CHANNEL];  // 记录同时按下的通道
    int count;                          // 同时按下的通道数量
    struct timespec first_press_ts;     // 首次按下时间戳（用于判断同时性）
} CoPressInfo;

// 全局状态结构体（对外声明为不透明类型，隐藏内部实现）
typedef struct {
    TouchState state;          // 当前状态
    int current_ch;            // 当前操作的主通道
    int prev_ch;               // 上一个操作的主通道
    long press_time;           // 按下时间(ms)
    long release_time;         // 释放时间(ms)
    int click_count;           // 单击计数（用于双击检测）
    int slide_path[MPR121_MAX_CHANNEL]; // 滑动路径记录
    int slide_len;             // 滑动路径长度
    CoPressInfo co_press;      // 同时按下的通道信息
    volatile sig_atomic_t exit_flag;    // 退出标记
} TouchStateStruct;

// 配置参数
#define DBL_CLICK_INTERVAL 600   // 双击最大间隔(ms)
#define SIMULTANEOUS_THRESHOLD 50 // 同时按下判断阈值(ms)
#define JITTER_FILTER 5          // 抖动过滤时间(ms)
#define CLICK_MAX_DURATION 500    // 单击最大按下时长(ms)
#define READ_TIMEOUT 100         // 事件读取超时(ms)

/**
 * @brief 初始化触摸处理模块
 * @return 成功返回0，失败返回-1
 */
int touch_handler_init(void);

/**
 * @brief 处理触摸事件
 * @param evt 待处理的触摸事件
 */
void process_touch_event(MPR121_Event evt);

/**
 * @brief 触摸事件处理主循环
 * @return 正常退出返回0，异常返回-1
 */
int touch_handler_run(void);

/**
 * @brief 释放触摸处理模块资源
 */
void touch_handler_deinit(void);

/**
 * @brief 获取退出标志状态
 * @return 1表示需要退出，0表示正常运行
 */
int touch_handler_get_exit_flag(void);

#endif