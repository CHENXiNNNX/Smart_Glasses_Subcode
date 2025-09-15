#ifndef CHATBOT_H
#define CHATBOT_H

#include "../media/audio/audio.h"
#include "../protocol/websocket/websocket.h"
#include "state_machine/state_machine.h"
#include "event/eventqueue.h"
#include "event/app_event.h"
#include "intent/intent_handler.h"
#include "utils/user_log.h"
#include "../../3rdparty/snowboy/include/snowboy-detect-c-wrapper.h"

#include <thread>
#include <atomic>
#include <string>
#include <json/json.h>
#include <memory>

// 应用状态枚举
enum class ChatbotState {
    fault,      // 故障状态
    startup,    // 启动状态
    stopping,   // 停止状态
    idle,       // 空闲状态
    listening,  // 聆听状态
    thinking,   // 思考状态
    speaking,   // 说话状态
};

// WebSocket消息处理器前向声明
class ChatbotMsgHandler;

/**
 * @brief 智能眼镜聊天机器人核心类
 * 
 * 基于官方Application架构设计，集成音频处理、状态机管理、
 * WebSocket通信和意图处理等核心功能
 */
class Chatbot {
public:
    /**
     * @brief 构造函数
     * 
     * @param address WebSocket服务器地址
     * @param port WebSocket服务器端口
     * @param token 认证令牌
     * @param deviceId 设备ID
     * @param aliyun_api_key 阿里云API密钥
     * @param protocolVersion 协议版本
     * @param sample_rate 音频采样率
     * @param channels 音频声道数
     * @param frame_duration 音频帧时长(ms)
     */
    Chatbot(const std::string& address, int port, const std::string& token, 
            const std::string& deviceId, const std::string& aliyun_api_key, 
            int protocolVersion, int sample_rate = 16000, int channels = 1, 
            int frame_duration = 20);

    /**
     * @brief 析构函数
     */
    ~Chatbot();

    /**
     * @brief 运行聊天机器人
     * 
     * 启动状态机线程和事件循环，开始处理用户交互
     */
    void Run();

    /**
     * @brief 停止聊天机器人
     * 
     * 发送停止事件，优雅关闭所有线程和连接
     */
    void Stop();

    // ==================== 核心组件 ====================
    
    /**
     * @brief 音频系统
     * 
     * 负责音频录制、播放、Opus编解码等功能
     */
    audio_system_t audio_system_;

    /**
     * @brief 状态机
     * 
     * 管理应用的各种状态转换
     */
    StateMachine client_state_;

    /**
     * @brief 事件队列
     * 
     * 处理应用事件（如唤醒检测、VAD结束等）
     */
    EventQueue<int> eventQueue_;

    /**
     * @brief 意图队列
     * 
     * 处理AI识别的用户意图
     */
    EventQueue<Json::Value> intentQueue_;

    /**
     * @brief WebSocket客户端
     * 
     * 与AI服务器进行实时通信
     */
    WebSocketClient ws_client_;

    /**
     * @brief 意图处理器
     * 
     * 注册和执行用户意图对应的功能
     */
    IntentHandler intent_handler_;

    // ==================== 状态管理 ====================

    /**
     * @brief 设置首次音频消息接收标志
     */
    void set_first_audio_msg_received(bool flag) {
        first_audio_msg_received_ = flag;
    }

    /**
     * @brief 获取首次音频消息接收标志
     */
    bool get_first_audio_msg_received() const {
        return first_audio_msg_received_;
    }

    /**
     * @brief 设置TTS完成标志
     */
    void set_tts_completed(bool flag) {
        tts_completed_ = flag;
    }

    /**
     * @brief 获取TTS完成标志
     */
    bool get_tts_completed() const {
        return tts_completed_;
    }

    /**
     * @brief 设置对话完成标志
     */
    void set_dialogue_completed(bool flag) {
        dialogue_completed_ = flag;
    }

    /**
     * @brief 获取对话完成标志
     */
    bool get_dialogue_completed() const {
        return dialogue_completed_;
    }

    /**
     * @brief 设置阿里云API密钥
     */
    void set_aliyun_api_key(const std::string& key) {
        aliyun_api_key_ = key;
    }

    /**
     * @brief 获取阿里云API密钥
     */
    std::string get_aliyun_api_key() const {
        return aliyun_api_key_;
    }

    /**
     * @brief 设置线程停止信号
     */
    void set_threads_stop_sig(bool flag) {
        threads_stop_flag_.store(flag);
    }

    /**
     * @brief 获取线程停止信号
     */
    bool get_threads_stop_sig() const {
        return threads_stop_flag_.load();
    }

    /**
     * @brief 设置WebSocket协议版本
     */
    void set_ws_protocolVersion(int version) {
        ws_protocolVersion_ = version;
    }

    /**
     * @brief 获取WebSocket协议版本
     */
    int get_ws_protocolVersion() const {
        return ws_protocolVersion_;
    }

    /**
     * @brief 获取当前状态
     */
    int getState() const {
        return client_state_.GetCurrentState();
    }

    // ==================== 音频控制 ====================

    /**
     * @brief 开始录音
     */
    bool startRecording();

    /**
     * @brief 停止录音
     */
    bool stopRecording();

    /**
     * @brief 开始播放
     */
    bool startPlayback();

    /**
     * @brief 停止播放
     */
    bool stopPlayback();

    /**
     * @brief 清空录音队列
     */
    void clearRecordingQueue();

    /**
     * @brief 清空播放队列
     */
    void clearPlaybackQueue();

    /**
     * @brief 获取录音数据
     */
    bool getRecordedAudio(std::vector<int16_t>& recordedData);

    /**
     * @brief 添加音频帧到播放队列
     */
    void addFrameToPlaybackQueue(const std::vector<int16_t>& pcm_frame);

    // ==================== WebSocket控制 ====================

    /**
     * @brief 连接WebSocket服务器
     */
    void connectWebSocket();

    /**
     * @brief 断开WebSocket连接
     */
    void disconnectWebSocket();

    /**
     * @brief 发送文本消息
     */
    void sendTextMessage(const std::string& message);

    /**
     * @brief 发送二进制音频数据
     */
    void sendAudioData(const uint8_t* data, size_t size);

    /**
     * @brief 检查WebSocket连接状态
     */
    bool isWebSocketConnected() const;

    // ==================== 事件处理 ====================

    /**
     * @brief 入队事件
     */
    void enqueueEvent(AppEvent event);

    /**
     * @brief 入队意图
     */
    void enqueueIntent(const Json::Value& intent);

    // ==================== Opus编解码 ====================

    /**
     * @brief 编码音频数据为Opus格式
     */
    bool encodeOpus(const std::vector<int16_t>& pcm_data, std::vector<uint8_t>& opus_data);

    /**
     * @brief 解码Opus数据为PCM格式
     */
    bool decodeOpus(const std::vector<uint8_t>& opus_data, std::vector<int16_t>& pcm_data);

    /**
     * @brief 打包二进制协议帧
     */
    std::vector<uint8_t> packBinaryFrame(const std::vector<uint8_t>& opus_data);

    /**
     * @brief 解包二进制协议帧
     */
    bool unpackBinaryFrame(const std::vector<uint8_t>& packed_data, 
                          std::vector<uint8_t>& opus_data, 
                          BinProtocolInfo& protocol_info);

private:
    // ==================== 私有成员变量 ====================

    // 状态标志
    bool first_audio_msg_received_ = false;
    bool tts_completed_ = false;
    bool dialogue_completed_ = false;
    
    // 配置参数
    std::string aliyun_api_key_;
    int ws_protocolVersion_;
    
    // 音频参数
    int sample_rate_;
    int channels_;
    int frame_duration_;
    
    // 线程控制
    std::atomic<bool> threads_stop_flag_ = false;
    std::thread state_trans_thread_;
    
    // 消息处理器
    std::unique_ptr<ChatbotMsgHandler> msg_handler_;
    
    // Snowboy唤醒检测
    SnowboyDetect* snowboy_detector_ = nullptr;
    std::atomic<bool> wakeword_detection_running_ = false;
    std::thread wakeword_detection_thread_;

    // ==================== 私有方法 ====================

    /**
     * @brief 初始化音频系统
     */
    bool initAudioSystem();

    /**
     * @brief 释放音频系统
     */
    void deinitAudioSystem();

    /**
     * @brief 配置状态机
     */
    void configureStateMachine();

    /**
     * @brief 设置WebSocket回调
     */
    void setupWebSocketCallbacks();

    /**
     * @brief 状态机事件循环
     */
    void stateEventLoop();

    /**
     * @brief 处理WebSocket消息
     */
    void handleWebSocketMessage(const std::string& message, bool is_binary);

    /**
     * @brief 处理WebSocket关闭
     */
    void handleWebSocketClose();
    
    // ==================== Snowboy唤醒检测 ====================
    
    /**
     * @brief 初始化Snowboy检测器
     */
    bool initSnowboyDetector();
    
    /**
     * @brief 释放Snowboy检测器
     */
    void deinitSnowboyDetector();
    
    /**
     * @brief 启动唤醒词检测
     */
    void startWakewordDetection();
    
    /**
     * @brief 停止唤醒词检测
     */
    void stopWakewordDetection();
    
    /**
     * @brief 唤醒词检测循环
     */
    void wakewordDetectionLoop();
};

/**
 * @brief WebSocket消息处理器
 * 
 * 负责解析和处理来自AI服务器的各种消息类型
 */
class ChatbotMsgHandler {
public:
    /**
     * @brief 构造函数
     */
    explicit ChatbotMsgHandler(Chatbot* chatbot);

    /**
     * @brief 处理WebSocket接收到的消息
     */
    void handleMessage(const std::string& message, bool is_binary);

private:
    Chatbot* chatbot_;

    /**
     * @brief 处理VAD消息
     */
    void handleVadMessage(const Json::Value& root);

    /**
     * @brief 处理ASR消息
     */
    void handleAsrMessage(const Json::Value& root);

    /**
     * @brief 处理聊天消息
     */
    void handleChatMessage(const Json::Value& root);

    /**
     * @brief 处理TTS消息
     */
    void handleTtsMessage(const Json::Value& root);

    /**
     * @brief 处理意图消息
     */
    void handleIntentMessage(const Json::Value& root);

    /**
     * @brief 处理二进制音频消息
     */
    void handleBinaryMessage(const std::string& message);

    /**
     * @brief 处理错误消息
     */
    void handleErrorMessage(const Json::Value& root);
};

#endif // CHATBOT_H
