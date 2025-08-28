#ifndef AICHAT_C_INTERFACE_H
#define AICHAT_C_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

// 定义 IntentData 结构体
typedef struct {
    char function_name[128]; // 存储 function_call 的 name
    char argument_keys[10][128]; // 存储最多 10 个 argument 的键
    char argument_values[10][256]; // 存储最多 10 个 argument 的值
    int argument_count; // 存储 argument 的数量
} IntentData;

// 定义状态类型为C语言兼容
typedef enum {
    fault,
    startup,
    stopping,
    idle,
    listening,
    thinking,
    speaking,
} ChatState;

// 创建并初始化Application对象
void* create_aichat_app(const char* address, int port, const char* token, const char* deviceId, const char* aliyun_api_key, int protocolVersion, int sample_rate, int channels, int frame_duration);

// 运行Application对象
void run_aichat_app(void* app_ptr);

// 强制停止Application对象
void stop_aichat_app(void* app_ptr);

// 销毁Application对象
void destroy_aichat_app(void* app_ptr);

// 获取当前状态
ChatState get_aichat_app_state(void* app_ptr);

// 获取 Intent 数据
bool get_aichat_app_intent(void* app_ptr, IntentData* intent_data);

#ifdef __cplusplus
}
#endif 

#endif // APP_C_INTERFACE_H