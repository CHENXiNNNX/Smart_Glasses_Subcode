/**
 * @file handle.h
 * @brief xiaozhi AI协议处理模块
 * @details 封装xiaozhi云端AI服务的消息协议，包括STT/LLM/TTS/IoT等
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-09
 */

#ifndef PROTOCOL_HANDLE_H
#define PROTOCOL_HANDLE_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace glasses {
namespace chatbot {
namespace protocol {

// ============================================================================
// 前向声明
// ============================================================================
class ProtocolHandler;

// ============================================================================
// 消息类型枚举
// ============================================================================

/**
 * @brief 消息类型
 */
enum class MessageType {
    HELLO,          // 握手消息
    LISTEN,         // 监听控制
    STT,            // 语音识别结果
    LLM,            // 大语言模型回复
    TTS,            // 文本转语音
    IOT,            // IoT设备控制
    ERROR,          // 错误消息
    UNKNOWN         // 未知类型
};

/**
 * @brief 监听模式
 */
enum class ListenMode {
    AUTO,           // 自动停止（检测到静音后停止）
    MANUAL,         // 手动停止（需要显式调用stop）
    REALTIME        // 实时模式（需要AEC支持）
};

/**
 * @brief 监听状态
 */
enum class ListenState {
    START,          // 开始监听
    STOP            // 停止监听
};

/**
 * @brief TTS状态
 */
enum class TTSState {
    START,          // TTS开始
    SENTENCE_START, // 句子开始
    STOP            // TTS停止
};

/**
 * @brief 设备状态
 */
enum class DeviceState {
    UNKNOWN,            // 未知
    STARTING,           // 启动中
    WIFI_CONFIGURING,   // WiFi配置中
    IDLE,               // 空闲
    CONNECTING,         // 连接中
    LISTENING,          // 监听中
    SPEAKING,           // 说话中
    UPGRADING,          // 升级中
    ACTIVATING,         // 激活中
    FATAL_ERROR         // 严重错误
};

/**
 * @brief LLM情感类型
 */
enum class EmotionType {
    NEUTRAL,        // 中性
    HAPPY,          // 开心
    LAUGHING,       // 大笑
    FUNNY,          // 有趣
    SAD,            // 悲伤
    ANGRY,          // 生气
    CRYING,         // 哭泣
    LOVING,         // 爱意
    EMBARRASSED,    // 尴尬
    SURPRISED,      // 惊讶
    SHOCKED,        // 震惊
    THINKING,       // 思考
    WINKING,        // 眨眼
    COOL,           // 酷
    RELAXED,        // 放松
    DELICIOUS,      // 美味
    KISSY,          // 亲吻
    CONFIDENT,      // 自信
    SLEEPY,         // 困倦
    SILLY,          // 傻笑
    CONFUSED        // 困惑
};

// ============================================================================
// 消息结构体
// ============================================================================

/**
 * @brief Hello消息（握手）
 */
struct HelloMessage {
    std::string session_id;         // 会话ID
    int version;                     // 协议版本
    std::string transport;           // 传输方式
    struct {
        std::string format;          // 音频格式（opus）
        int sample_rate;             // 采样率（16000）
        int channels;                // 声道数（1）
        int frame_duration;          // 帧时长（60ms）
    } audio_params;
};

/**
 * @brief Listen消息（监听控制）
 */
struct ListenMessage {
    std::string session_id;         // 会话ID
    ListenState state;              // 监听状态
    ListenMode mode;                // 监听模式
};

/**
 * @brief STT消息（语音识别）
 */
struct STTMessage {
    std::string session_id;         // 会话ID
    std::string text;               // 识别文本
    bool is_final;                  // 是否最终结果
};

/**
 * @brief LLM消息（大语言模型）
 */
struct LLMMessage {
    std::string session_id;         // 会话ID
    std::string text;               // 回复文本
    EmotionType emotion;            // 情感类型
    bool is_final;                  // 是否最终结果
};

/**
 * @brief TTS消息（文本转语音）
 */
struct TTSMessage {
    std::string session_id;         // 会话ID
    TTSState state;                 // TTS状态
    std::string text;               // TTS文本（sentence_start时有效）
};

/**
 * @brief IoT设备属性
 */
struct IoTProperty {
    std::string name;               // 属性名称
    std::string description;        // 属性描述
    std::string type;               // 类型（number/string/boolean）
};

/**
 * @brief IoT设备方法参数
 */
struct IoTMethodParameter {
    std::string name;               // 参数名称
    std::string description;        // 参数描述
    std::string type;               // 类型（number/string/boolean）
};

/**
 * @brief IoT设备方法
 */
struct IoTMethod {
    std::string name;                                   // 方法名称
    std::string description;                            // 方法描述
    std::map<std::string, IoTMethodParameter> parameters;  // 参数列表
};

/**
 * @brief IoT设备描述符
 */
struct IoTDescriptor {
    std::string name;                               // 设备名称
    std::string description;                        // 设备描述
    std::map<std::string, IoTProperty> properties;  // 属性列表
    std::map<std::string, IoTMethod> methods;       // 方法列表
};

/**
 * @brief IoT设备状态
 */
struct IoTDeviceState {
    std::string name;                               // 设备名称
    std::map<std::string, std::string> state;       // 状态值（键值对）
};

/**
 * @brief IoT消息
 */
struct IoTMessage {
    std::string session_id;                         // 会话ID
    bool update;                                    // 是否更新
    std::vector<IoTDescriptor> descriptors;         // 设备描述符列表
    std::vector<IoTDeviceState> states;             // 设备状态列表
    
    // 用于invoke操作
    std::string device_name;                        // 设备名称
    std::string method_name;                        // 方法名称
    std::map<std::string, std::string> parameters;  // 方法参数
};

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * @brief Hello消息回调
 */
using HelloCallback = std::function<void(const HelloMessage& msg)>;

/**
 * @brief STT消息回调
 */
using STTCallback = std::function<void(const STTMessage& msg)>;

/**
 * @brief LLM消息回调
 */
using LLMCallback = std::function<void(const LLMMessage& msg)>;

/**
 * @brief TTS消息回调
 */
using TTSCallback = std::function<void(const TTSMessage& msg)>;

/**
 * @brief IoT消息回调
 */
using IoTCallback = std::function<void(const IoTMessage& msg)>;

/**
 * @brief 错误消息回调
 */
using ErrorCallback = std::function<void(const std::string& error)>;

// ============================================================================
// 协议处理器类
// ============================================================================

/**
 * @brief xiaozhi协议处理器
 * @details 负责xiaozhi协议的编码/解码和消息分发
 */
class ProtocolHandler {
public:
    /**
     * @brief 构造函数
     */
    ProtocolHandler();
    
    /**
     * @brief 析构函数
     */
    ~ProtocolHandler();

    // ========================================================================
    // 回调设置
    // ========================================================================

    /**
     * @brief 设置Hello消息回调
     */
    void setHelloCallback(HelloCallback callback);

    /**
     * @brief 设置STT消息回调
     */
    void setSTTCallback(STTCallback callback);

    /**
     * @brief 设置LLM消息回调
     */
    void setLLMCallback(LLMCallback callback);

    /**
     * @brief 设置TTS消息回调
     */
    void setTTSCallback(TTSCallback callback);

    /**
     * @brief 设置IoT消息回调
     */
    void setIoTCallback(IoTCallback callback);

    /**
     * @brief 设置错误消息回调
     */
    void setErrorCallback(ErrorCallback callback);

    // ========================================================================
    // 消息解析（接收方向）
    // ========================================================================

    /**
     * @brief 解析JSON消息
     * @param json_str JSON字符串
     * @return MessageType 消息类型
     */
    MessageType parseMessage(const std::string& json_str);

    /**
     * @brief 解析JSON消息（从缓冲区）
     * @param buffer 缓冲区
     * @param size 数据大小
     * @return MessageType 消息类型
     */
    MessageType parseMessage(const char* buffer, size_t size);

    // ========================================================================
    // 消息生成（发送方向）
    // ========================================================================

    /**
     * @brief 生成Hello消息
     * @param sample_rate 采样率
     * @param channels 声道数
     * @param frame_duration 帧时长（ms）
     * @return std::string JSON字符串
     */
    std::string generateHelloMessage(int sample_rate = 16000, 
                                     int channels = 1, 
                                     int frame_duration = 60);

    /**
     * @brief 生成Listen消息
     * @param state 监听状态
     * @param mode 监听模式
     * @return std::string JSON字符串
     */
    std::string generateListenMessage(ListenState state, ListenMode mode = ListenMode::AUTO);

    /**
     * @brief 生成IoT描述符消息
     * @param descriptors 设备描述符列表
     * @return std::string JSON字符串
     */
    std::string generateIoTDescriptorMessage(const std::vector<IoTDescriptor>& descriptors);

    /**
     * @brief 生成IoT状态消息
     * @param states 设备状态列表
     * @return std::string JSON字符串
     */
    std::string generateIoTStateMessage(const std::vector<IoTDeviceState>& states);

    /**
     * @brief 生成IoT调用结果消息
     * @param device_name 设备名称
     * @param method_name 方法名称
     * @param success 是否成功
     * @param result 结果描述
     * @return std::string JSON字符串
     */
    std::string generateIoTInvokeResultMessage(const std::string& device_name,
                                               const std::string& method_name,
                                               bool success,
                                               const std::string& result = "");

    // ========================================================================
    // 会话管理
    // ========================================================================

    /**
     * @brief 设置会话ID
     */
    void setSessionId(const std::string& session_id);

    /**
     * @brief 获取会话ID
     */
    std::string getSessionId() const;

    // ========================================================================
    // 工具函数
    // ========================================================================

    /**
     * @brief 将字符串转换为MessageType
     */
    static MessageType stringToMessageType(const std::string& type_str);

    /**
     * @brief 将MessageType转换为字符串
     */
    static std::string messageTypeToString(MessageType type);

    /**
     * @brief 将字符串转换为EmotionType
     */
    static EmotionType stringToEmotionType(const std::string& emotion_str);

    /**
     * @brief 将EmotionType转换为字符串
     */
    static std::string emotionTypeToString(EmotionType emotion);

    // 禁止拷贝和赋值
    ProtocolHandler(const ProtocolHandler&) = delete;
    ProtocolHandler& operator=(const ProtocolHandler&) = delete;

private:
    class Impl;  // 前向声明，使用Pimpl惯用法
    Impl* pimpl_;
};

} // namespace protocol
} // namespace chatbot
} // namespace glasses

#endif // PROTOCOL_HANDLE_H
