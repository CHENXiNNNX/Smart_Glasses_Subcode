#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <pthread.h>
#include "global_events.h"

// 定义事件消息结构体
typedef struct AppMessage {
    AppEventType type;
    void *data;
} AppMessage;

// 定义事件队列
typedef struct EventQueue {
    AppMessage *events;  // 动态数组存储事件
    size_t capacity;
    size_t head;
    size_t tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} EventQueue;

// 定义事件处理函数指针类型
typedef void (*EventHandler)(void *);

// 定义事件管理器
typedef struct EventManager {
    EventHandler handlers[GLOBAL_EVENT_MAX];
    EventQueue event_queue;
} EventManager;

// 初始化事件管理器和事件队列
void event_manager_init(EventManager *manager, size_t queue_capacity);

// 清理事件管理器资源
void event_manager_deinit(EventManager *manager);

// 注册事件处理函数
bool event_manager_register_handler(EventManager *manager, AppEventType type, EventHandler handler);

// 发送事件到队列
bool event_manager_send_event(EventManager *manager, AppEventType type, void *data);

// 分发事件给相应的处理函数
void event_manager_dispatch_events(EventManager *manager);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif // EVENT_MANAGER_H