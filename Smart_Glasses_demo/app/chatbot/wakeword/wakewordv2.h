/**
 * @file wakewordv2.h
 * @brief 唤醒词检测器V2 - 现代C++重写版本
 * @details 特性：
 *          - RAII资源管理（Snowboy检测器）
 *          - Pimpl隐藏实现细节
 *          - 智能指针（无裸指针）
 *          - 线程安全的回调管理
 *          - 异常安全的回调执行
 *          - 配置化设计
 *          - 统一日志系统
 *          - 音频缓冲管理
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-11
 */

#ifndef WAKEWORDV2_H
#define WAKEWORDV2_H

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <vector>
#include <cstdint>

namespace app {
namespace chatbot {
namespace wakeword {

// ============================================================================
// 唤醒词检测结果枚举
// ============================================================================

/**
 * @brief 唤醒词检测结果
 */
enum class WakewordResult {
    NONE = 0,           // 无检测
    SILENCE = -1,       // 静音
    ERROR = -2,         // 错误
    HOTWORD_1 = 1,      // 检测到唤醒词1
    HOTWORD_2 = 2,      // 检测到唤醒词2
    HOTWORD_3 = 3       // 检测到唤醒词3
};

/**
 * @brief 唤醒词错误类型
 */
enum class WakewordError {
    NONE = 0,
    NOT_INITIALIZED,        // 未初始化
    ALREADY_INITIALIZED,    // 已初始化
    INVALID_RESOURCE,       // 无效的资源文件
    INVALID_MODEL,          // 无效的模型文件
    INVALID_PARAMS,         // 无效参数
    DETECTOR_ERROR,         // 检测器错误
    CALLBACK_EXCEPTION,     // 回调异常
    UNKNOWN                 // 未知错误
};

// ============================================================================
// 唤醒词配置
// ============================================================================

/**
 * @brief 唤醒词检测器配置
 */
struct WakewordConfig {
    // 文件路径
    std::string resource_file;          // Snowboy资源文件路径（common.res）
    std::string model_file;             // 唤醒词模型文件路径（*.umdl或*.pmdl）
    
    // 检测参数
    float sensitivity = 0.5f;           // 灵敏度（0.0-1.0）
    float audio_gain = 1.0f;            // 音频增益
    bool apply_frontend = false;        // 是否应用前端处理（通常设为false，因为已有3A）
    
    // 音频缓冲
    size_t max_buffer_size = 4096;      // 最大缓冲区大小（样本数）
    bool enable_buffering = false;      // 是否启用音频缓冲
    
    // 日志
    std::string log_prefix = "WakewordV2";  // 日志前缀
};

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * @brief 唤醒词检测回调函数类型
 * @param result 检测结果
 * @param hotword_index 唤醒词索引（从1开始）
 */
using WakewordCallback = std::function<void(WakewordResult result, int hotword_index)>;

/**
 * @brief 错误回调函数类型
 * @param error 错误类型
 * @param message 错误消息
 */
using WakewordErrorCallback = std::function<void(WakewordError error, const std::string& message)>;

// ============================================================================
// WakewordDetectorV2 类定义
// ============================================================================

/**
 * @brief 唤醒词检测器V2
 * @details 基于Snowboy的唤醒词检测，使用现代C++特性重写
 */
class WakewordDetectorV2 {
public:
    /**
     * @brief 构造函数
     * @param config 唤醒词配置
     */
    explicit WakewordDetectorV2(const WakewordConfig& config = WakewordConfig());
    
    /**
     * @brief 析构函数（RAII自动清理）
     */
    ~WakewordDetectorV2();
    
    // ========================================================================
    // 初始化和控制
    // ========================================================================
    
    /**
     * @brief 初始化唤醒词检测器
     * @return WakewordError::NONE 成功
     */
    WakewordError initialize();
    
    /**
     * @brief 启用/禁用检测
     * @param enabled true-启用, false-禁用
     */
    void setEnabled(bool enabled);
    
    /**
     * @brief 是否已启用
     */
    bool isEnabled() const;
    
    /**
     * @brief 是否已初始化
     */
    bool isInitialized() const;
    
    /**
     * @brief 重置检测器（清空内部状态）
     */
    void reset();
    
    // ========================================================================
    // 音频处理
    // ========================================================================
    
    /**
     * @brief 处理音频帧
     * @param data 音频数据（int16_t格式）
     * @param length 数据长度（样本数）
     * @return WakewordResult 检测结果
     */
    WakewordResult processAudioFrame(const int16_t* data, int length);
    
    // ========================================================================
    // 参数设置
    // ========================================================================
    
    /**
     * @brief 设置灵敏度
     * @param sensitivity 灵敏度（0.0-1.0）
     */
    void setSensitivity(float sensitivity);
    
    /**
     * @brief 设置音频增益
     * @param gain 音频增益
     */
    void setAudioGain(float gain);
    
    // ========================================================================
    // 回调设置
    // ========================================================================
    
    /**
     * @brief 设置唤醒词检测回调（线程安全）
     * @param callback 检测回调函数
     */
    void setWakewordCallback(WakewordCallback callback);
    
    /**
     * @brief 设置错误回调（线程安全）
     * @param callback 错误回调函数
     */
    void setErrorCallback(WakewordErrorCallback callback);
    
    // ========================================================================
    // 信息查询
    // ========================================================================
    
    /**
     * @brief 获取采样率
     */
    int getSampleRate() const;
    
    /**
     * @brief 获取声道数
     */
    int getNumChannels() const;
    
    /**
     * @brief 获取每样本位数
     */
    int getBitsPerSample() const;
    
    /**
     * @brief 获取唤醒词数量
     */
    int getNumHotwords() const;
    
    // 禁止拷贝和赋值
    WakewordDetectorV2(const WakewordDetectorV2&) = delete;
    WakewordDetectorV2& operator=(const WakewordDetectorV2&) = delete;

private:
    class Impl;  // Pimpl惯用法
    std::unique_ptr<Impl> pImpl_;
};

} // namespace wakeword
} // namespace chatbot
} // namespace app

#endif // WAKEWORDV2_H

