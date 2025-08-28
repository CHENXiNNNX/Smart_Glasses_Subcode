#include "app.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

// ================== 全局变量 ==================

static volatile bool app_running = true;

// ================== 回调函数实现 ==================

// 状态变化回调
void on_chatbot_state_changed(chatbot_state_t old_state, chatbot_state_t new_state) {
    printf("[APP] Chatbot state changed: %d -> %d\n", old_state, new_state);
    
    switch (new_state) {
        case CHATBOT_STATE_IDLE:
            printf("[APP] 🤖 AI助手已就绪，等待语音唤醒...\n");
            break;
        case CHATBOT_STATE_LISTENING:
            printf("[APP] 👂 正在监听您的指令...\n");
            break;
        case CHATBOT_STATE_THINKING:
            printf("[APP] 🤔 正在思考中...\n");
            break;
        case CHATBOT_STATE_SPEAKING:
            printf("[APP] 🗣️ AI正在回复...\n");
            break;
        case CHATBOT_STATE_FAULT:
            printf("[APP] ❌ AI助手出现故障\n");
            break;
        default:
            break;
    }
}

// 意图处理回调
void on_chatbot_intent_received(const chatbot_intent_t* intent) {
    printf("[APP] 🎯 收到意图: %s\n", intent->function_name);
    printf("[APP] 📋 参数: %s\n", intent->arguments);
    
    // 根据意图执行相应动作
    if (strcmp(intent->function_name, "take_photo") == 0) {
        printf("[APP] 📸 执行拍照指令\n");
        // TODO: 调用相机拍照功能
        
    } else if (strcmp(intent->function_name, "start_recording") == 0) {
        printf("[APP] 🎥 开始录像\n");
        // TODO: 调用相机录像功能
        
    } else if (strcmp(intent->function_name, "ai_vision_analyze") == 0) {
        printf("[APP] 👁️ 启动AI视觉分析\n");
        // TODO: 调用AI视觉识别功能
        
    } else if (strcmp(intent->function_name, "make_call") == 0) {
        printf("[APP] 📞 发起通话\n");
        // TODO: 调用通话功能
        
    } else if (strcmp(intent->function_name, "robot_move") == 0) {
        printf("[APP] 🤖 机器人移动指令\n");
        // 这是原有的机器人移动功能，可以保留或移除
        
    } else {
        printf("[APP] ❓ 未知意图: %s\n", intent->function_name);
    }
}

// 错误处理回调
void on_chatbot_error(const char* error_message) {
    printf("[APP] ❌ AI助手错误: %s\n", error_message);
}

// ================== 信号处理 ==================

void signal_handler(int sig) {
    printf("\n[APP] 收到退出信号，正在关闭...\n");
    app_running = false;
}

// ================== 配置文件读取函数 ==================

int app_load_config_from_file(const char* filepath, chatbot_config_t* config) {
    if (!filepath || !config) {
        printf("[APP] ❌ 配置文件读取：参数无效\n");
        return -1;
    }
    
    FILE *file = fopen(filepath, "r");
    if (!file) {
        printf("[APP] ⚠️ 无法打开配置文件: %s，将使用默认配置\n", filepath);
        return -1;
    }
    
    printf("[APP] 📖 正在读取配置文件: %s\n", filepath);
    
    char line[256];
    int items_read = 0;
    
    while (fgets(line, sizeof(line), file)) {
        char key[128], value[128];
        
        // 跳过注释行和空行
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        // 解析键值对
        if (sscanf(line, "%[^=]=%s", key, value) != 2) {
            continue;
        }
        
        // 匹配AI聊天相关配置
        if (strcmp(key, "AIChat_server_url") == 0) {
            strncpy(config->server_address, value, sizeof(config->server_address) - 1);
            config->server_address[sizeof(config->server_address) - 1] = '\0';
            items_read++;
            printf("[APP] 🌐 服务器地址: %s\n", config->server_address);
            
        } else if (strcmp(key, "AIChat_server_port") == 0) {
            config->server_port = atoi(value);
            items_read++;
            printf("[APP] 🔌 服务器端口: %d\n", config->server_port);
            
        } else if (strcmp(key, "AIChat_server_token") == 0) {
            strncpy(config->access_token, value, sizeof(config->access_token) - 1);
            config->access_token[sizeof(config->access_token) - 1] = '\0';
            items_read++;
            printf("[APP] 🔑 访问令牌: %s\n", config->access_token);
            
        } else if (strcmp(key, "AIChat_Client_ID") == 0) {
            strncpy(config->device_id, value, sizeof(config->device_id) - 1);
            config->device_id[sizeof(config->device_id) - 1] = '\0';
            items_read++;
            printf("[APP] 📱 设备ID: %s\n", config->device_id);
            
        } else if (strcmp(key, "aliyun_api_key") == 0) {
            strncpy(config->aliyun_api_key, value, sizeof(config->aliyun_api_key) - 1);
            config->aliyun_api_key[sizeof(config->aliyun_api_key) - 1] = '\0';
            items_read++;
            if (strlen(config->aliyun_api_key) > 10) {
                char preview[12];
                strncpy(preview, config->aliyun_api_key, 10);
                preview[10] = '\0';
                printf("[APP] 🔐 阿里云API密钥: %s...\n", preview);
            } else {
                printf("[APP] 🔐 阿里云API密钥: %s\n", config->aliyun_api_key);
            }
            
        } else if (strcmp(key, "AIChat_protocol_version") == 0) {
            config->protocol_version = atoi(value);
            items_read++;
            printf("[APP] 📋 协议版本: %d\n", config->protocol_version);
            
        } else if (strcmp(key, "AIChat_sample_rate") == 0) {
            config->sample_rate = atoi(value);
            items_read++;
            printf("[APP] 🎵 采样率: %d\n", config->sample_rate);
            
        } else if (strcmp(key, "AIChat_channels") == 0) {
            config->channels = atoi(value);
            items_read++;
            printf("[APP] 🔊 声道数: %d\n", config->channels);
            
        } else if (strcmp(key, "AIChat_frame_duration") == 0) {
            config->frame_duration = atoi(value);
            items_read++;
            printf("[APP] ⏱️ 帧时长: %d\n", config->frame_duration);
        }
    }
    
    fclose(file);
    
    printf("[APP] ✅ 配置文件读取完成，共读取 %d 个配置项\n", items_read);
    
    // 检查必要的配置项是否都已读取
    if (items_read >= 5) { // 至少要有服务器地址、端口、令牌、设备ID、API密钥
        return 0;
    } else {
        printf("[APP] ⚠️ 配置文件中缺少必要的配置项\n");
        return -1;
    }
}

// ================== 主要功能函数 ==================

int app_init_chatbot(void) {
    printf("[APP] 🚀 初始化AI聊天机器人...\n");
    
    // 初始化默认配置
    chatbot_config_t config = {
        .server_address = "192.168.50.214",          // 默认服务器地址
        .server_port = 8000,                       // 默认服务器端口
        .access_token = "123456",                  // 默认访问令牌
        .device_id = "00:11:22:33:44:55",          // 默认设备ID
        .aliyun_api_key = "sk-81130796d2924bcc8f00a0e79ed5417d", // 默认阿里云API密钥
        .protocol_version = 2,                     // 默认协议版本
        .sample_rate = 16000,                      // 默认采样率
        .channels = 1,                             // 默认单声道
        .frame_duration = 40                       // 默认帧时长
    };
    
    // 从配置文件读取实际配置
    const char* config_file = "./system_para.conf";
    if (app_load_config_from_file(config_file, &config) != 0) {
        printf("[APP] ⚠️ 配置文件读取失败，使用默认配置\n");
    } else {
        printf("[APP] ✅ 配置文件读取成功\n");
    }
    
    // 初始化聊天机器人
    if (chatbot_init(&config) != 0) {
        printf("[APP] ❌ 聊天机器人初始化失败\n");
        return -1;
    }
    
    // 注册回调函数
    chatbot_register_state_callback(on_chatbot_state_changed);
    chatbot_register_intent_callback(on_chatbot_intent_received);
    chatbot_register_error_callback(on_chatbot_error);
    
    printf("[APP] ✅ 聊天机器人初始化成功\n");
    return 0;
}

int app_start_chatbot(void) {
    printf("[APP] 🎬 启动AI聊天机器人...\n");
    
    if (chatbot_start() != 0) {
        printf("[APP] ❌ 聊天机器人启动失败\n");
        return -1;
    }
    
    printf("[APP] ✅ 聊天机器人启动成功\n");
    return 0;
}

void app_run_main_loop(void) {
    printf("[APP] 🔄 进入主事件循环...\n");
    
    while (app_running) {
        // 处理AI聊天机器人事件
        chatbot_process_events();
        
        // 检查聊天机器人状态
        if (!chatbot_is_running() && app_running) {
            printf("[APP] ⚠️ 聊天机器人意外停止，尝试重启...\n");
            usleep(1000000); // 等待1秒
            app_start_chatbot();
        }
        
        // 100ms循环间隔
        usleep(100000);
    }
}

void app_shutdown(void) {
    printf("[APP] 🛑 关闭应用...\n");
    
    // 停止聊天机器人
    chatbot_stop();
    
    // 清理资源
    chatbot_cleanup();
    
    printf("[APP] ✅ 应用已关闭\n");
}

// ================== 公共API实现 ==================

int app_main(void) {
    printf("[APP] 🌟 无屏幕智能眼镜应用启动\n");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化聊天机器人
    if (app_init_chatbot() != 0) {
        return -1;
    }
    
    // 启动聊天机器人
    if (app_start_chatbot() != 0) {
        app_shutdown();
        return -1;
    }
    
    // 运行主循环
    app_run_main_loop();
    
    // 清理并退出
    app_shutdown();
    
    printf("[APP] 👋 再见！\n");
    return 0;
}
