#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "chatbot/chatbot.h"

/**
 * @brief 应用主入口函数
 * @return 0成功，-1失败
 */
int app_main(void);

/**
 * @brief 初始化AI聊天机器人
 * @return 0成功，-1失败
 */
int app_init_chatbot(void);

/**
 * @brief 启动AI聊天机器人
 * @return 0成功，-1失败
 */
int app_start_chatbot(void);

/**
 * @brief 运行主事件循环
 */
void app_run_main_loop(void);

/**
 * @brief 关闭应用
 */
void app_shutdown(void);

/**
 * @brief 从配置文件加载聊天机器人配置
 * @param filepath 配置文件路径
 * @param config 配置结构体指针
 * @return 0成功，-1失败
 */
int app_load_config_from_file(const char* filepath, chatbot_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // APP_H
