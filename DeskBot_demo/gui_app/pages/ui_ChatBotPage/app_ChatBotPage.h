#ifndef __APP_CHATBOTPAGE_H
#define __APP_CHATBOTPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t chat_bot_move_dir;

// 创建并初始化Application对象, 会开启一个线程
int start_ai_chat(const char* address, int port, const char* token, const char* deviceId, const char* aliyun_api_key, 
                    int protocolVersion, int sample_rate, int channels, int frame_duration);

int stop_ai_chat();

// 获取 AI Chat 状态
int get_ai_chat_state(void);

// 专门处理Intent，目前只有运动
void chat_bot_get_intent_process(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif