/**
 * @file wakeword.hpp
 * @brief 唤醒词检测器实现
 */

#ifndef WAKEWORD_HPP
#define WAKEWORD_HPP

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <vector>
#include <cstdint>

namespace app
{
    namespace chatbot
    {
        namespace wakeword
        {

            // ============================================================================
            // 唤醒词检测结果枚举
            // ============================================================================

            /**
             * @brief 唤醒词检测结果
             */
            enum class WakewordResult
            {
                NONE      = 0,  // 无检测
                SILENCE   = -1, // 静音
                ERROR     = -2, // 错误
                HOTWORD_1 = 1,  // 检测到唤醒词1
                HOTWORD_2 = 2,  // 检测到唤醒词2
                HOTWORD_3 = 3   // 检测到唤醒词3
            };

            /**
             * @brief 唤醒词错误类型
             */
            enum class WakewordError
            {
                NONE = 0,
                NOT_INITIALIZED,     // 未初始化
                ALREADY_INITIALIZED, // 已初始化
                INVALID_RESOURCE,    // 无效的资源文件
                INVALID_MODEL,       // 无效的模型文件
                INVALID_PARAMS,      // 无效参数
                DETECTOR_ERROR,      // 检测器错误
                CALLBACK_EXCEPTION,  // 回调异常
                UNKNOWN              // 未知错误
            };

            // ============================================================================
            // 唤醒词配置
            // ============================================================================

            /**
             * @brief 唤醒词检测器配置
             */
            struct WakewordConfig
            {
                // 文件路径
                std::string resource_file; // 资源文件路径
                std::string model_file;    // 模型文件路径

                // 检测参数
                float sensitivity = 0.5f; // 灵敏度
                float audio_gain  = 1.0f; // 音频增益
                bool apply_frontend = false; // 是否应用前端处理

                // 音频缓冲
                size_t max_buffer_size  = 4096;  // 最大缓冲区大小
                bool   enable_buffering = false; // 是否启用音频缓冲

                // 日志
                std::string log_prefix = "Wakeword"; // 日志前缀
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
            using WakewordErrorCallback =
                std::function<void(WakewordError error, const std::string& message)>;

            // ============================================================================
            // WakewordDetector 类定义
            // ============================================================================

            /**
             * @brief 唤醒词检测器
             */
            class WakewordDetector
            {
            public:
                /**
                 * @brief 构造函数
                 * @param config 唤醒词配置
                 */
                explicit WakewordDetector(const WakewordConfig& config = WakewordConfig());

                /**
                 * @brief 析构函数
                 */
                ~WakewordDetector();

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
                 * @param data 音频数据
                 * @param length 数据长度
                 * @return WakewordResult 检测结果
                 */
                WakewordResult processAudioFrame(const int16_t* data, int length);

                // ========================================================================
                // 参数设置
                // ========================================================================

                /**
                 * @brief 设置灵敏度
                 * @param sensitivity 灵敏度
                 */
                void setSensitivity(float sensitivity);

                /**
                 * @brief 设置音频增益
                 * @param gain 音频增益（推荐1.0）
                 */
                void setAudioGain(float gain);

                // ========================================================================
                // 回调设置
                // ========================================================================

                /**
                 * @brief 设置唤醒词检测回调
                 * @param callback 检测回调函数
                 */
                void setWakewordCallback(WakewordCallback callback);

                /**
                 * @brief 设置错误回调
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
                WakewordDetector(const WakewordDetector&)            = delete;
                WakewordDetector& operator=(const WakewordDetector&) = delete;

            private:
                class Impl;
                std::unique_ptr<Impl> pImpl_;
            };

        } // namespace wakeword
    }     // namespace chatbot
} // namespace app

#endif // WAKEWORD_HPP
