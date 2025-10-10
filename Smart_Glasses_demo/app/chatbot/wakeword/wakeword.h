#ifndef WAKEWORD_H
#define WAKEWORD_H

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <vector>

// 前向声明Snowboy C接口
extern "C" {
    typedef struct SnowboyDetect SnowboyDetect;
}

namespace glasses {
namespace chatbot {
namespace wakeword {

/**
 * @brief 唤醒词检测器
 * @details 使用Snowboy库进行唤醒词检测
 */
class WakewordDetector {
public:
    /**
     * @brief 构造函数
     */
    WakewordDetector();
    
    /**
     * @brief 析构函数
     */
    ~WakewordDetector();
    
    /**
     * @brief 初始化唤醒词检测器
     * @param resource_file 资源文件路径（common.res）
     * @param model_file 模型文件路径（*.umdl或*.pmdl）
     * @param sensitivity 灵敏度（0.0-1.0），默认0.5
     * @param audio_gain 音频增益，默认1.0
     * @return true-成功, false-失败
     */
    bool initialize(const std::string& resource_file,
                   const std::string& model_file,
                   float sensitivity = 0.5f,
                   float audio_gain = 1.0f);
    
    /**
     * @brief 处理音频帧
     * @param data 音频数据（int16格式）
     * @param length 数据长度（样本数）
     * @return 检测结果：0-无检测，>0-检测到第N个关键词
     */
    int processAudioFrame(const int16_t* data, int length);
    
    /**
     * @brief 设置唤醒词检测回调
     * @param callback 检测到唤醒词时的回调函数
     */
    void setCallback(std::function<void(int hotword_index)> callback);
    
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
    
    /**
     * @brief 启用/禁用
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
     * @brief 重置检测器
     */
    void reset();

private:
    SnowboyDetect* detector_;                           // Snowboy检测器
    std::function<void(int)> callback_;                 // 唤醒词回调
    std::atomic<bool> enabled_;                         // 是否启用
    std::atomic<bool> initialized_;                     // 是否已初始化
    
    // 禁止拷贝
    WakewordDetector(const WakewordDetector&) = delete;
    WakewordDetector& operator=(const WakewordDetector&) = delete;
};

} // namespace wakeword
} // namespace chatbot
} // namespace glasses

#endif // WAKEWORD_H


