#include "app.h"
#include "chatbot/chatbot.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

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

// ================== 主要功能函数 ==================

int app_init_chatbot(void) {
    printf("[APP] 🚀 初始化AI聊天机器人...\n");
    
    // 配置AI聊天机器人参数
    chatbot_config_t config = {
        .server_address = "192.168.2.8",          // 服务器地址
        .server_port = 8000,                       // 服务器端口
        .access_token = "123456",                  // 访问令牌
        .device_id = "00:11:22:33:44:55",          // 设备ID
        .aliyun_api_key = "sk-81130796d2924bcc8f00a0e79ed5417d",               // 阿里云API密钥
        .protocol_version = 2,                     // 协议版本
        .sample_rate = 16000,                      // 采样率
        .channels = 1,                             // 单声道
        .frame_duration = 40                       // 帧时长
    };
    
    // 从配置文件读取实际配置 (TODO: 实现配置文件读取)
    // load_config_from_file(&config);
    
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
