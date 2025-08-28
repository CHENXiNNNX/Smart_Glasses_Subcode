#ifndef GLOBAL_EVENTS_H
#define GLOBAL_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// 定义全局事件类型
typedef enum {
    GLOBAL_EVENT_NONE = 0,              // 无事件
    APP_EVENT_ERROR_OCCURRED,           // 发生错误
    APP_EVENT_WIFI_CONNECTED,           // Wi-Fi 连接成功
    GLOBAL_EVENT_MAX                    // 最大事件类型（用于边界检查）
} AppEventType;


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // GLOBAL_EVENTS_H