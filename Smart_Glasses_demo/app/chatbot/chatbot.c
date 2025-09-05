#include "chatbot.h"
// #include "../../gui_app/pages/ui_ChatBotPage/app_ChatBotPage.h"   //modify
#include "../../../AIChat_demo/Client/c_interface/AIchat_c_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// ================== 内部状态管理 ==================

static struct {
    void* ai_app_instance;                    // AI应用实例
    pthread_t ai_thread;                      // AI线程
    volatile bool is_running;                 // 运行状态
    volatile bool is_initialized;             // 初始化状态
    chatbot_config_t config;                  // 配置参数
    
    // 回调函数
    chatbot_state_callback_t state_callback;
    chatbot_intent_callback_t intent_callback;
    chatbot_error_callback_t error_callback;
    
    // 状态管理
    chatbot_state_t current_state;
    chatbot_state_t last_state;
    
    // 线程同步
    pthread_mutex_t state_mutex;
    pthread_mutex_t config_mutex;
    
} chatbot_ctx = {
    .ai_app_instance = NULL,
    .is_running = false,
    .is_initialized = false,
    .state_callback = NULL,
    .intent_callback = NULL,
    .error_callback = NULL,
    .current_state = CHATBOT_STATE_FAULT,
    .last_state = CHATBOT_STATE_FAULT,
    .state_mutex = PTHREAD_MUTEX_INITIALIZER,
    .config_mutex = PTHREAD_MUTEX_INITIALIZER
};

// ================== 内部函数声明 ==================

static void* ai_thread_func(void* arg);
static void update_state(chatbot_state_t new_state);
static void process_intent_data(void);
static void handle_error(const char* message);

// ================== AI线程函数 ==================

static void* ai_thread_func(void* arg) {
    printf("[CHATBOT] AI thread started\n");
    
    if (chatbot_ctx.ai_app_instance) {
        // 运行AI应用 (这是阻塞调用)
        run_aichat_app(chatbot_ctx.ai_app_instance);
    }
    
    // 线程结束时清理
    printf("[CHATBOT] AI thread ending, cleaning up...\n");
    
    pthread_mutex_lock(&chatbot_ctx.state_mutex);
    if (chatbot_ctx.ai_app_instance) {
        destroy_aichat_app(chatbot_ctx.ai_app_instance);
        chatbot_ctx.ai_app_instance = NULL;
    }
    chatbot_ctx.is_running = false;
    update_state(CHATBOT_STATE_FAULT);
    pthread_mutex_unlock(&chatbot_ctx.state_mutex);
    
    return NULL;
}

// ================== 状态管理函数 ==================

static void update_state(chatbot_state_t new_state) {
    chatbot_state_t old_state = chatbot_ctx.current_state;
    
    if (old_state != new_state) {
        chatbot_ctx.last_state = old_state;
        chatbot_ctx.current_state = new_state;
        
        printf("[CHATBOT] State changed: %d -> %d\n", old_state, new_state);
        
        // 调用状态变化回调
        if (chatbot_ctx.state_callback) {
            chatbot_ctx.state_callback(old_state, new_state);
        }
    }
}

// ================== 意图处理函数 ==================

static void process_intent_data(void) {
    if (!chatbot_ctx.ai_app_instance || !chatbot_ctx.is_running) {
        return;
    }
    
    IntentData intent_data;
    if (get_aichat_app_intent(chatbot_ctx.ai_app_instance, &intent_data)) {
        // 转换为简化的意图结构
        chatbot_intent_t simplified_intent;
        
        strncpy(simplified_intent.function_name, intent_data.function_name, 
                sizeof(simplified_intent.function_name) - 1);
        simplified_intent.function_name[sizeof(simplified_intent.function_name) - 1] = '\0';
        
        // 将参数转换为JSON字符串格式
        simplified_intent.arguments[0] = '\0';
        int offset = 0;
        offset += snprintf(simplified_intent.arguments + offset, 
                          sizeof(simplified_intent.arguments) - offset, "{");
        
        for (int i = 0; i < intent_data.argument_count && i < 10; i++) {
            if (i > 0) {
                offset += snprintf(simplified_intent.arguments + offset,
                                  sizeof(simplified_intent.arguments) - offset, ",");
            }
            offset += snprintf(simplified_intent.arguments + offset,
                              sizeof(simplified_intent.arguments) - offset,
                              "\"%s\":\"%s\"", 
                              intent_data.argument_keys[i], 
                              intent_data.argument_values[i]);
        }
        offset += snprintf(simplified_intent.arguments + offset,
                          sizeof(simplified_intent.arguments) - offset, "}");
        
        simplified_intent.is_valid = true;
        
        printf("[CHATBOT] Intent received: %s(%s)\n", 
               simplified_intent.function_name, simplified_intent.arguments);
        
        // 调用意图处理回调
        if (chatbot_ctx.intent_callback) {
            chatbot_ctx.intent_callback(&simplified_intent);
        }
    }
}

// ================== 错误处理函数 ==================

static void handle_error(const char* message) {
    printf("[CHATBOT] Error: %s\n", message);
    
    if (chatbot_ctx.error_callback) {
        chatbot_ctx.error_callback(message);
    }
}

// ================== 公共API实现 ==================

int chatbot_init(const chatbot_config_t* config) {
    if (!config) {
        handle_error("Invalid configuration");
        return -1;
    }
    
    pthread_mutex_lock(&chatbot_ctx.config_mutex);
    
    if (chatbot_ctx.is_initialized) {
        pthread_mutex_unlock(&chatbot_ctx.config_mutex);
        handle_error("Chatbot already initialized");
        return -1;
    }
    
    // 复制配置
    memcpy(&chatbot_ctx.config, config, sizeof(chatbot_config_t));
    
    // 初始化状态
    chatbot_ctx.is_initialized = true;
    chatbot_ctx.is_running = false;
    update_state(CHATBOT_STATE_STARTUP);
    
    pthread_mutex_unlock(&chatbot_ctx.config_mutex);
    
    printf("[CHATBOT] Initialized with server: %s:%d\n", 
           config->server_address, config->server_port);
    
    return 0;
}

int chatbot_start(void) {
    pthread_mutex_lock(&chatbot_ctx.state_mutex);
    
    if (!chatbot_ctx.is_initialized) {
        pthread_mutex_unlock(&chatbot_ctx.state_mutex);
        handle_error("Chatbot not initialized");
        return -1;
    }
    
    if (chatbot_ctx.is_running) {
        pthread_mutex_unlock(&chatbot_ctx.state_mutex);
        handle_error("Chatbot already running");
        return -1;
    }
    
    // 创建AI应用实例
    chatbot_ctx.ai_app_instance = create_aichat_app(
        chatbot_ctx.config.server_address,
        chatbot_ctx.config.server_port,
        chatbot_ctx.config.access_token,
        chatbot_ctx.config.device_id,
        chatbot_ctx.config.aliyun_api_key,
        chatbot_ctx.config.protocol_version,
        chatbot_ctx.config.sample_rate,
        chatbot_ctx.config.channels,
        chatbot_ctx.config.frame_duration
    );
    
    if (!chatbot_ctx.ai_app_instance) {
        pthread_mutex_unlock(&chatbot_ctx.state_mutex);
        handle_error("Failed to create AI application instance");
        return -1;
    }
    
    // 启动AI线程
    chatbot_ctx.is_running = true;
    if (pthread_create(&chatbot_ctx.ai_thread, NULL, ai_thread_func, NULL) != 0) {
        destroy_aichat_app(chatbot_ctx.ai_app_instance);
        chatbot_ctx.ai_app_instance = NULL;
        chatbot_ctx.is_running = false;
        pthread_mutex_unlock(&chatbot_ctx.state_mutex);
        handle_error("Failed to create AI thread");
        return -1;
    }
    
    update_state(CHATBOT_STATE_IDLE);
    pthread_mutex_unlock(&chatbot_ctx.state_mutex);
    
    printf("[CHATBOT] Started successfully\n");
    return 0;
}

int chatbot_stop(void) {
    pthread_mutex_lock(&chatbot_ctx.state_mutex);
    
    if (!chatbot_ctx.is_running) {
        pthread_mutex_unlock(&chatbot_ctx.state_mutex);
        return 0; // 已经停止，不是错误
    }
    
    update_state(CHATBOT_STATE_STOPPING);
    
    // 发送停止信号
    if (chatbot_ctx.ai_app_instance) {
        stop_aichat_app(chatbot_ctx.ai_app_instance);
    }
    
    pthread_mutex_unlock(&chatbot_ctx.state_mutex);
    
    // 等待线程结束
    if (chatbot_ctx.is_running) {
        pthread_join(chatbot_ctx.ai_thread, NULL);
    }
    
    printf("[CHATBOT] Stopped successfully\n");
    return 0;
}

void chatbot_cleanup(void) {
    chatbot_stop();
    
    pthread_mutex_lock(&chatbot_ctx.config_mutex);
    chatbot_ctx.is_initialized = false;
    chatbot_ctx.state_callback = NULL;
    chatbot_ctx.intent_callback = NULL;
    chatbot_ctx.error_callback = NULL;
    pthread_mutex_unlock(&chatbot_ctx.config_mutex);
    
    printf("[CHATBOT] Cleaned up\n");
}

chatbot_state_t chatbot_get_state(void) {
    pthread_mutex_lock(&chatbot_ctx.state_mutex);
    chatbot_state_t state = chatbot_ctx.current_state;
    pthread_mutex_unlock(&chatbot_ctx.state_mutex);
    return state;
}

bool chatbot_get_intent(chatbot_intent_t* intent) {
    if (!intent || !chatbot_ctx.is_running) {
        return false;
    }
    
    // 这个函数在process_events中处理，这里只是检查接口
    return false;
}

void chatbot_register_state_callback(chatbot_state_callback_t callback) {
    pthread_mutex_lock(&chatbot_ctx.config_mutex);
    chatbot_ctx.state_callback = callback;
    pthread_mutex_unlock(&chatbot_ctx.config_mutex);
}

void chatbot_register_intent_callback(chatbot_intent_callback_t callback) {
    pthread_mutex_lock(&chatbot_ctx.config_mutex);
    chatbot_ctx.intent_callback = callback;
    pthread_mutex_unlock(&chatbot_ctx.config_mutex);
}

void chatbot_register_error_callback(chatbot_error_callback_t callback) {
    pthread_mutex_lock(&chatbot_ctx.config_mutex);
    chatbot_ctx.error_callback = callback;
    pthread_mutex_unlock(&chatbot_ctx.config_mutex);
}

void chatbot_process_events(void) {
    if (!chatbot_ctx.is_running) {
        return;
    }
    
    // 更新AI状态
    if (chatbot_ctx.ai_app_instance) {
        ChatState ai_state = get_aichat_app_state(chatbot_ctx.ai_app_instance);
        chatbot_state_t new_state = (chatbot_state_t)ai_state;
        
        pthread_mutex_lock(&chatbot_ctx.state_mutex);
        update_state(new_state);
        pthread_mutex_unlock(&chatbot_ctx.state_mutex);
    }
    
    // 处理意图数据
    process_intent_data();
}

bool chatbot_is_running(void) {
    pthread_mutex_lock(&chatbot_ctx.state_mutex);
    bool running = chatbot_ctx.is_running;
    pthread_mutex_unlock(&chatbot_ctx.state_mutex);
    return running;
}
