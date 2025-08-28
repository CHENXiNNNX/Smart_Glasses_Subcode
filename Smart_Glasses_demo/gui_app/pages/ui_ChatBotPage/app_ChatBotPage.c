#include <stdio.h>
#include <pthread.h>
#include "ui_ChatBotPage.h"
#include "app_ChatBotPage.h"
#include "../../../../AIChat_demo/Client/c_interface/AIchat_c_interface.h"

static void* app_instance = NULL; // AI Chat 应用实例
static pthread_t ai_chat_thread;  // 线程 ID
static volatile int is_running = 0; // 标志位，表示应用是否正在运行


void* ai_chat_thread_func(void* arg) {
    if (app_instance) {
        run_aichat_app(app_instance); // 运行 AI Chat 应用
    }
    is_running = 0; // 线程结束时将标志位重置为 0
    destroy_aichat_app(app_instance);
    app_instance = NULL;
    is_running = 0;
    return NULL;
}

int start_ai_chat(const char* address, int port, const char* token, const char* deviceId, 
                  const char* aliyun_api_key, int protocolVersion, int sample_rate, 
                  int channels, int frame_duration) {

    // 如果应用已经在运行，返回错误
    if (is_running) {
        LV_LOG_ERROR("Error: AI Chat application is already running.\n");
        return -1;
    }

    // 创建 Application 实例
    app_instance = create_aichat_app(address, port, token, deviceId, aliyun_api_key,
                                     protocolVersion, sample_rate, channels, frame_duration);
    if (!app_instance) {
        LV_LOG_ERROR("Error: Failed to create AI Chat application instance.\n");
        return -1;
    }

    // 启动线程运行 AI Chat 应用
    is_running = 1; // 设置运行标志位
    if (pthread_create(&ai_chat_thread, NULL, ai_chat_thread_func, NULL) != 0) {
        LV_LOG_ERROR("Error: Failed to create AI Chat thread.\n");
        destroy_aichat_app(app_instance); // 清理实例
        app_instance = NULL;
        is_running = 0;
        return -1;
    }

    return 0; // 成功启动
}

int stop_ai_chat(void) {
    // 如果应用没有运行，返回错误
    if (!is_running) {
        LV_LOG_ERROR("Error: AI Chat application is not running.\n");
        return -1;
    }
    // 发送停止信号给应用
    stop_aichat_app(app_instance);
}

// 获取 AI Chat 状态
int get_ai_chat_state(void) {
    // 如果应用没有运行，返回错误状态
    if (!is_running || !app_instance) {
        return -1; // 返回 -1 表示无效状态
    }
    // 获取当前状态
    return get_aichat_app_state(app_instance);
}

// 专门处理Intent，目前只有运动
void chat_bot_get_intent_process()
{
    IntentData intent_data;
    uint8_t chat_bot_move_dir = 0;
    if (get_aichat_app_intent(app_instance, &intent_data)) {
        // 打印 function_name 和 arguments
        printf("Function Name: %s\n", intent_data.function_name);
        for (int i = 0; i < intent_data.argument_count; i++) {
            printf("Argument %s: %s\n", intent_data.argument_keys[i], intent_data.argument_values[i]);
        }
        // 根据 function_name 和 arguments 执行相应逻辑
        if (strcmp(intent_data.function_name, "robot_move") == 0) {
            for (int i = 0; i < intent_data.argument_count; i++) {
                if (strcmp(intent_data.argument_keys[i], "direction") == 0) {
                    if (strcmp(intent_data.argument_values[i], "forward") == 0) {
                        chat_bot_move_dir = 1;
                    } else if (strcmp(intent_data.argument_values[i], "backward") == 0) {
                        chat_bot_move_dir = 2;
                    } else if (strcmp(intent_data.argument_values[i], "left") == 0) {
                        chat_bot_move_dir = 3;
                    } else if (strcmp(intent_data.argument_values[i], "right") == 0) {
                        chat_bot_move_dir = 4;
                    } else {
                        chat_bot_move_dir = 0; // 停止
                    }
                    chat_bot_move(chat_bot_move_dir);
                }
            }
        }
    }
    else {
        chat_bot_move(0);
    }
}

// 0 none, 1 forward, 2 back, 3 left, 4 right.
void chat_bot_move(int dir)
{
    // forward
    if(dir == 1)
    {
        LV_LOG_INFO("Move forward.");
        gpio_set_value(MOTOR1_INA, 0);
        gpio_set_value(MOTOR1_INB, 1); // left side move forward
        gpio_set_value(MOTOR2_INA, 0);
        gpio_set_value(MOTOR2_INB, 1); // right side move forward
    }
    // backward
    else if(dir == 2)
    {
        LV_LOG_INFO("Move backward.");
        gpio_set_value(MOTOR1_INA, 1); // left side move back
        gpio_set_value(MOTOR1_INB, 0); 
        gpio_set_value(MOTOR2_INA, 1); // right side move back
        gpio_set_value(MOTOR2_INB, 0); 
    }
    // left
    else if(dir == 3)
    {
        LV_LOG_INFO("Move turn left.");
        gpio_set_value(MOTOR1_INA, 1); // left side move back
        gpio_set_value(MOTOR1_INB, 0); 
        gpio_set_value(MOTOR2_INA, 0); 
        gpio_set_value(MOTOR2_INB, 1); // right side move forward
    }
    // right
    else if(dir == 4)
    {
        LV_LOG_INFO("Move turn right.");
        gpio_set_value(MOTOR1_INA, 0); 
        gpio_set_value(MOTOR1_INB, 1); // left side move forward
        gpio_set_value(MOTOR2_INA, 1); // right side move back
        gpio_set_value(MOTOR2_INB, 0); 
    }
    // reset(stop)
    else 
    {
        gpio_set_value(MOTOR1_INA, 0);
        gpio_set_value(MOTOR1_INB, 0);
        gpio_set_value(MOTOR2_INA, 0);
        gpio_set_value(MOTOR2_INB, 0);
    }
    
}
