/**
 * @file detection_queue.hpp
 * @brief 检测结果队列：线程安全的检测结果传递机制
 */

#ifndef DETECTION_QUEUE_HPP
#define DETECTION_QUEUE_HPP

#include "face_detection.hpp"
#include <mutex>
#include <deque>
#include <chrono>
#include <optional>

namespace app
{
    namespace rknn
    {
        namespace face_detection
        {
            /**
             * @brief 带时间戳的检测结果
             */
            struct TimedDetectionResult
            {
                DetectionResult                       result;
                std::chrono::steady_clock::time_point timestamp;

                TimedDetectionResult() : timestamp(std::chrono::steady_clock::now()) {}

                TimedDetectionResult(const DetectionResult& res)
                    : result(res), timestamp(std::chrono::steady_clock::now())
                {
                }
            };

            /**
             * @brief 线程安全的检测结果队列
             */
            class DetectionResultQueue
            {
            public:
                /**
                 * @brief 构造函数
                 * @param max_size 队列最大容量
                 * @param result_ttl_ms 检测结果有效期（毫秒）
                 */
                explicit DetectionResultQueue(size_t max_size = 10, int result_ttl_ms = 2000)
                    : max_size_(max_size), result_ttl_ms_(result_ttl_ms)
                {
                }

                /**
                 * @brief 推送检测结果到队列
                 * @param result 检测结果
                 * @return 成功返回true
                 */
                bool push(const DetectionResult& result)
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (queue_.size() >= max_size_)
                    {
                        queue_.pop_front();
                    }

                    queue_.emplace_back(result);
                    return true;
                }

                /**
                 * @brief 获取最新的检测结果
                 * @return 检测结果或std::nullopt
                 */
                std::optional<DetectionResult> tryPop()
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (queue_.empty())
                    {
                        return std::nullopt;
                    }

                    auto timed_result = queue_.front();
                    queue_.pop_front();

                    auto now     = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       now - timed_result.timestamp)
                                       .count();

                    if (elapsed > result_ttl_ms_)
                    {
                        return std::nullopt;
                    }

                    return timed_result.result;
                }

                /**
                 * @brief 获取最新的检测结果
                 * @return 检测结果或std::nullopt
                 */
                std::optional<DetectionResult> tryPeek() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (queue_.empty())
                    {
                        return std::nullopt;
                    }

                    const auto& timed_result = queue_.back();

                    auto now     = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       now - timed_result.timestamp)
                                       .count();

                    if (elapsed > result_ttl_ms_)
                    {
                        return std::nullopt;
                    }

                    return timed_result.result;
                }

                /**
                 * @brief 清空队列
                 */
                void clear()
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    queue_.clear();
                }

                /**
                 * @brief 获取队列大小
                 */
                size_t size() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return queue_.size();
                }

                /**
                 * @brief 检查队列是否为空
                 */
                bool empty() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return queue_.empty();
                }

                /**
                 * @brief 设置结果有效期
                 */
                void setResultTTL(int ttl_ms)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    result_ttl_ms_ = ttl_ms;
                }

            private:
                mutable std::mutex               mutex_;
                std::deque<TimedDetectionResult> queue_;
                size_t                           max_size_;
                int                              result_ttl_ms_;
            };

        } // namespace face_detection
    }     // namespace rknn
} // namespace app

#endif // DETECTION_QUEUE_HPP
