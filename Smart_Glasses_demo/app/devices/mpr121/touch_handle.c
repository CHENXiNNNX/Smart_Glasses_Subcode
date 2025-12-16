#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "touch_handle.h"
#include "mpr20100.h"

// 全局状态实例
static TouchStateStruct g_touch = {
    .state = STATE_IDLE,
    .current_ch = -1,
    .prev_ch = -1,
    .click_count = 0,
    .slide_len = 0,
    .co_press.count = 0,
    .exit_flag = 0
};

// 动作处理函数声明
static void handle_click(int ch);
static void handle_double_click(int ch);
static void handle_slide(int *path, int len);

// 工具函数：获取当前时间(ms)
static long get_current_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 工具函数：计算两个时间戳的差值(ms)
static long time_diff_ms(struct timespec *t1, struct timespec *t2) {
    long sec_diff = t1->tv_sec - t2->tv_sec;
    long nsec_diff = t1->tv_nsec - t2->tv_nsec;
    return sec_diff * 1000 + nsec_diff / 1000000;
}

// 重置状态机
static void reset_state() {
    g_touch.state = STATE_IDLE;
    g_touch.current_ch = -1;
    g_touch.prev_ch = -1;
    g_touch.click_count = 0;
    g_touch.slide_len = 0;
    g_touch.co_press.count = 0;
    memset(g_touch.slide_path, 0, sizeof(g_touch.slide_path));
    memset(g_touch.co_press.channels, 0, sizeof(g_touch.co_press.channels));
}

// 设置非阻塞IO
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 单击处理
static void handle_click(int ch) {
    printf("[ACTION] 单击 通道 %d\n", ch + 1);
}

// 双击处理
static void handle_double_click(int ch) {
    printf("[ACTION] 双击 通道 %d\n", ch + 1);
}

// 滑动处理
static void handle_slide(int *path, int len) {
    printf("[ACTION] 滑动（路径：");
    for (int i = 0; i < len; i++) {
        printf("%d", path[i] + 1);
        if (i < len - 1) printf("→");
    }
    printf("）\n");
}

// 检查通道是否已在同时按下列表中
static int is_in_copress(int ch) {
    for (int i = 0; i < g_touch.co_press.count; i++) {
        if (g_touch.co_press.channels[i] == ch) {
            return 1;
        }
    }
    return 0;
}

// 状态机核心处理逻辑
void process_touch_event(MPR121_Event evt) {
    if (evt.channel < 0 || evt.channel >= MPR121_MAX_CHANNEL) {
        return;
    }

    long now = get_current_ms();
    const char *event_type = (evt.type == MPR121_EVENT_PRESSED) ? "按下" : "释放";
    printf("[DEBUG] 通道=%d, 事件=%s, 状态=%d, 滑动长度=%d, 单击序列=%d\n",
           evt.channel + 1, event_type, g_touch.state, g_touch.slide_len, g_touch.click_count);

    switch (g_touch.state) {
        case STATE_IDLE:
            if (evt.type == MPR121_EVENT_PRESSED) {
                // 记录首次按下信息
                g_touch.current_ch = evt.channel;
                g_touch.co_press.channels[0] = evt.channel;
                g_touch.co_press.first_press_ts = evt.timestamp;
                g_touch.co_press.count = 1;
                g_touch.press_time = now;
                g_touch.state = STATE_WAIT_RELEASE;
            }
            break;

        case STATE_WAIT_RELEASE:
            if (evt.type == MPR121_EVENT_PRESSED) {
                // 计算与首次按下的时间差
                long press_diff = time_diff_ms(&evt.timestamp, &g_touch.co_press.first_press_ts);
                
                if (abs(press_diff) <= SIMULTANEOUS_THRESHOLD) {
                    // 时间差在阈值内，视为同时按下（误触处理）
                    if (!is_in_copress(evt.channel) && 
                        g_touch.co_press.count < MPR121_MAX_CHANNEL) {
                        g_touch.co_press.channels[g_touch.co_press.count++] = evt.channel;
                    }
                } else {
                    // 时间差超过阈值，视为滑动（依次按下）
                    g_touch.slide_path[0] = g_touch.current_ch;
                    g_touch.slide_path[1] = evt.channel;
                    g_touch.slide_len = 2;
                    g_touch.prev_ch = g_touch.current_ch;
                    g_touch.current_ch = evt.channel;
                    // 添加到同时按下列表
                    if (!is_in_copress(evt.channel) && 
                        g_touch.co_press.count < MPR121_MAX_CHANNEL) {
                        g_touch.co_press.channels[g_touch.co_press.count++] = evt.channel;
                    }
                    g_touch.state = STATE_WAIT_SLIDE_RELEASE;
                }
            }
            else if (evt.type == MPR121_EVENT_RELEASED) {
                // 检查是否是当前按下的通道
                if (!is_in_copress(evt.channel)) {
                    break;
                }

                // 移除释放的通道
                int remaining = 0;
                for (int i = 0; i < g_touch.co_press.count; i++) {
                    if (g_touch.co_press.channels[i] != evt.channel) {
                        g_touch.co_press.channels[remaining++] = g_touch.co_press.channels[i];
                    }
                }
                g_touch.co_press.count = remaining;

                // 所有通道都已释放
                if (g_touch.co_press.count == 0) {
                    long press_duration = now - g_touch.press_time;
                    if (press_duration > CLICK_MAX_DURATION) {
                        printf("[DEBUG] 按下时间过长，不视为单击\n");
                        reset_state();
                        break;
                    }

                    // 处理单击（支持多通道同时按下后释放）
                    g_touch.release_time = now;
                    g_touch.click_count++;
                    g_touch.prev_ch = g_touch.current_ch; // 用第一个按下的通道作为主通道
                    g_touch.state = STATE_WAIT_DBL_CLICK;
                }
            }
            break;

        case STATE_WAIT_DBL_CLICK: {
            long interval = now - g_touch.release_time;
            
            // 双击超时检查
            if (interval > DBL_CLICK_INTERVAL) {
                handle_click(g_touch.prev_ch);
                reset_state();
                break;
            }

            if (evt.type == MPR121_EVENT_PRESSED) {
                // 记录第二次按下信息（允许同时误触多通道）
                g_touch.current_ch = evt.channel;
                g_touch.co_press.channels[0] = evt.channel;
                g_touch.co_press.first_press_ts = evt.timestamp;
                g_touch.co_press.count = 1;
                g_touch.press_time = now;
                g_touch.state = STATE_WAIT_RELEASE;
            }
            break;
        }

        case STATE_WAIT_SLIDE_RELEASE:
            if (evt.type == MPR121_EVENT_PRESSED) {
                // 继续检测滑动路径（后续按下的通道）
                long press_diff = time_diff_ms(&evt.timestamp, &g_touch.co_press.first_press_ts);
                if (abs(press_diff) > SIMULTANEOUS_THRESHOLD && 
                    !is_in_copress(evt.channel)) {
                    // 新增滑动路径点
                    if (g_touch.slide_len < MPR121_MAX_CHANNEL) {
                        g_touch.slide_path[g_touch.slide_len++] = evt.channel;
                        g_touch.current_ch = evt.channel;
                    }
                    // 添加到同时按下列表
                    if (g_touch.co_press.count < MPR121_MAX_CHANNEL) {
                        g_touch.co_press.channels[g_touch.co_press.count++] = evt.channel;
                    }
                }
            }
            else if (evt.type == MPR121_EVENT_RELEASED) {
                // 检查是否是当前按下的通道
                if (!is_in_copress(evt.channel)) {
                    break;
                }

                // 移除释放的通道
                int remaining = 0;
                for (int i = 0; i < g_touch.co_press.count; i++) {
                    if (g_touch.co_press.channels[i] != evt.channel) {
                        g_touch.co_press.channels[remaining++] = g_touch.co_press.channels[i];
                    }
                }
                g_touch.co_press.count = remaining;

                // 所有通道释放后处理滑动
                if (g_touch.co_press.count == 0) {
                    if (g_touch.slide_len >= 2) {
                        handle_slide(g_touch.slide_path, g_touch.slide_len);
                    }
                    reset_state();
                }
            }
            break;

        default:
            reset_state();
            break;
    }

    // 双击判定：完成两次单击序列
    if (g_touch.click_count == 2) {
        long total_interval = now - g_touch.release_time;
        if (total_interval <= DBL_CLICK_INTERVAL) {
            handle_double_click(g_touch.prev_ch);
            reset_state();
        }
    }
}

int touch_handler_init(void) {
    // 初始化MPR121设备
    if (mpr121_init() != 0) {
        fprintf(stderr, "[错误] 设备初始化失败\n");
        return -1;
    }

    // 设置非阻塞模式
    int mpr121_fd = mpr121_get_fd();
    if (mpr121_fd >= 0) {
        if (set_nonblock(mpr121_fd) == 0) {
            printf("[信息] 已设置非阻塞模式\n");
        } else {
            fprintf(stderr, "[警告] 非阻塞模式设置失败: %s\n", strerror(errno));
        }
    } else {
        printf("[警告] 未获取设备FD，无法设置非阻塞模式\n");
    }

    // 打印触发规则
    printf("触摸事件监听已启动，按Ctrl+C退出...\n");
    printf("触发规则：\n");
    printf("  ✅ 单击：同一通道（允许短暂误触多通道）按下→释放\n");
    printf("  ✅ 双击：同一通道（允许短暂误触多通道）按下→释放→按下→释放（间隔≤600ms）\n");
    printf("  ✅ 滑动：多通道依次触发（路径长度≥2）\n");

    return 0;
}

int touch_handler_run(void) {
    MPR121_Event evt;
    int ret;
    long last_jitter_ms = 0;
    int last_ch = -1;
    int last_type = -1;
    
    while (!mpr121_get_exit_flag()) {
        // 非阻塞读取事件
        ret = mpr121_read_event(&evt, READ_TIMEOUT); 
    
        // 优先检查退出标记
        if (mpr121_get_exit_flag()) break;

        if (ret == 0) {
            // 抖动过滤
            long now = get_current_ms();
            if (now - last_jitter_ms > JITTER_FILTER || 
                evt.channel != last_ch || evt.type != last_type) {
                process_touch_event(evt);
                last_jitter_ms = now;
                last_ch = evt.channel;
                last_type = evt.type;
            }
        } 
        // 处理超时情况：检查是否有等待双击的状态需要处理
        else if (ret > 0 && g_touch.state == STATE_WAIT_DBL_CLICK) {
            long now = get_current_ms();
            long interval = now - g_touch.release_time;
            if (interval > DBL_CLICK_INTERVAL) {
                handle_click(g_touch.prev_ch);
                reset_state();
            }
        }
        // 处理错误（忽略EINTR）
        else if (ret == -1 && errno != EINTR) {
            fprintf(stderr, "[错误] 事件读取失败: %s\n", strerror(errno));
            usleep(10000);
        }
    }

    return 0;
}

void touch_handler_deinit(void) {
    mpr121_deinit();
    printf("\n程序已退出\n");
}

int touch_handler_get_exit_flag(void) {
    return mpr121_get_exit_flag();
}