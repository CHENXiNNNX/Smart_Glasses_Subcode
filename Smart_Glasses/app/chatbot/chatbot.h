#ifndef CHATBOT_H
#define CHATBOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// AI聊天状态枚举
typedef enum {
    CHATBOT_STATE_FAULT = 0,      // 故障状态
    CHATBOT_STATE_STARTUP,        // 启动中
    CHATBOT_STATE_STOPPING,       // 停止中  
    CHATBOT_STATE_IDLE,           // 空闲
    CHATBOT_STATE_LISTENING,      // 监听中
    CHATBOT_STATE_THINKING,       // 思考中
    CHATBOT_STATE_SPEAKING        // 说话中
} chatbot_state_t;

// 意图数据结构
typedef struct {
    char function_name[128];           // 函数名称
    char arguments[512];               // JSON格式的参数
    bool is_valid;                     // 数据是否有效
} chatbot_intent_t;

// 聊天机器人配置结构
typedef struct {
    char server_address[64];           // 服务器地址
    int server_port;                   // 服务器端口
    char access_token[64];             // 访问令牌
    char device_id[32];                // 设备ID
    char aliyun_api_key[64];          // 阿里云API密钥
    int protocol_version;              // 协议版本
    int sample_rate;                   // 采样率
    int channels;                      // 声道数
    int frame_duration;                // 帧时长
} chatbot_config_t;

// 状态变化回调函数类型
typedef void (*chatbot_state_callback_t)(chatbot_state_t old_state, chatbot_state_t new_state);

// 意图处理回调函数类型  
typedef void (*chatbot_intent_callback_t)(const chatbot_intent_t* intent);

// 错误处理回调函数类型
typedef void (*chatbot_error_callback_t)(const char* error_message);

// ================== 公共API ==================

/**
 * @brief 初始化聊天机器人
 * @param config 配置参数
 * @return 0成功，-1失败
 */
int chatbot_init(const chatbot_config_t* config);

/**
 * @brief 启动聊天机器人
 * @return 0成功，-1失败
 */
int chatbot_start(void);

/**
 * @brief 停止聊天机器人
 * @return 0成功，-1失败
 */
int chatbot_stop(void);

/**
 * @brief 清理聊天机器人资源
 */
void chatbot_cleanup(void);

/**
 * @brief 获取当前状态
 * @return 当前状态
 */
chatbot_state_t chatbot_get_state(void);

/**
 * @brief 检查是否有新的意图数据
 * @param intent 输出参数，存储意图数据
 * @return true有新意图，false无意图
 */
bool chatbot_get_intent(chatbot_intent_t* intent);

/**
 * @brief 注册状态变化回调
 * @param callback 回调函数
 */
void chatbot_register_state_callback(chatbot_state_callback_t callback);

/**
 * @brief 注册意图处理回调
 * @param callback 回调函数
 */
void chatbot_register_intent_callback(chatbot_intent_callback_t callback);

/**
 * @brief 注册错误处理回调
 * @param callback 回调函数
 */
void chatbot_register_error_callback(chatbot_error_callback_t callback);

/**
 * @brief 处理聊天机器人事件 (需要在主循环中定期调用)
 */
void chatbot_process_events(void);

/**
 * @brief 检查聊天机器人是否正在运行
 * @return true运行中，false未运行
 */
bool chatbot_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // CHATBOT_H
