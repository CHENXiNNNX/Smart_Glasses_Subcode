/*
 * activation.cc - 设备激活
 */

#include "activation.hpp"
#include "../../interfaces/ihttp_client.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/time/time.hpp"
#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#else
#include "../../../third_party/libdatachannel/deps/json/single_include/nlohmann/json.hpp"
#endif
#include <thread>
#include <condition_variable>
#include <map>
#include <utility>

using json = nlohmann::json;

namespace app
{
    namespace chatbot
    {
        namespace activation
        {

            using namespace tool::log;

            namespace
            {
                constexpr const char* LOG_TAG                        = "ACTIVATION";
                constexpr double      SUCCESS_RATE_WARNING_THRESHOLD = 90.0;
            } // namespace

            class DeviceActivation::Impl
            {
            public:
                // 配置
                ActivationConfig config;

                // HTTP客户端
                app::IHttpClient* http_client = nullptr;

                // 轮询状态
                std::atomic<ActivationStatus> polling_status{ActivationStatus::UNKNOWN};
                std::unique_ptr<std::thread>  polling_thread;
                std::atomic<bool>             should_stop_polling{false};
                std::condition_variable       polling_cv;
                std::mutex                    polling_mutex;

                // 最后的激活结果
                ActivationResult   last_result;
                mutable std::mutex result_mutex;

                // 回调函数
                ActivationStatusCallback   status_callback;
                ActivationProgressCallback progress_callback;
                ActivationErrorCallback    error_callback;
                mutable std::mutex         callback_mutex;

                // 统计信息
                DeviceActivation::Stats stats;

                Impl(ActivationConfig cfg, app::IHttpClient* hc)
                    : config(std::move(cfg)), http_client(hc)
                {
                    if (!http_client || !http_client->valid())
                        LOG_ERROR(LOG_TAG, "HTTP未设置或无效");
                }

                ~Impl()
                {
                    stopPollingThread();
                }

                ActivationResult checkActivationInternal(const std::string& mac,
                                                         const std::string& uuid)
                {
                    ActivationResult result;
                    result.check_timestamp = static_cast<uint64_t>(app::tool::time::uptime_us());

                    static_cast<void>(uuid);

                    uint64_t start_time = static_cast<uint64_t>(app::tool::time::uptime_us());

                    if (!http_client || !http_client->valid())
                    {
                        result.status        = ActivationStatus::ERROR;
                        result.error         = ActivationError::CURL_INIT_FAILED;
                        result.error_message = "HTTP客户端未初始化";
                        stats.check_failed.fetch_add(1, std::memory_order_relaxed);
                        return result;
                    }

                    // 构建POST数据
                    json post_json             = json::object();
                    post_json["platform"]      = config.platform;
                    post_json["version"]       = config.version;
                    post_json["board"]["type"] = config.board_type;
                    post_json["board"]["name"] = config.board_name;
                    std::string post_data      = post_json.dump();

                    // 构建HTTP Headers
                    std::map<std::string, std::string> headers{};
                    headers["Content-Type"]    = "application/json";
                    headers["Device-Id"]       = mac;
                    headers["User-Agent"]      = config.user_agent;
                    headers["Accept-Language"] = config.accept_language;

                    LOG_DEBUG(LOG_TAG, "检查激活状态: Device-Id=%s", mac.c_str());

                    // 重试机制
                    int               retry = 0;
                    app::HttpResponse http_resp{};

                    for (retry = 0; retry < config.max_retries; retry++)
                    {
                        if (retry > 0)
                        {
                            LOG_INFO(LOG_TAG, "重试 %d/%d，等待%dms", retry, config.max_retries,
                                     config.retry_delay_ms);
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(config.retry_delay_ms));
                            stats.retry_count.fetch_add(1, std::memory_order_relaxed);
                        }

                        // 执行HTTP请求
                        http_resp = http_client->post(config.api_url, post_data, headers,
                                                      config.request_timeout_ms, config.verify_ssl);

                        if (http_resp.success)
                        {
                            break;
                        }

                        stats.network_errors.fetch_add(1, std::memory_order_relaxed);
                        LOG_WARN(LOG_TAG, "HTTP请求失败: %s", http_resp.error_message.c_str());
                    }

                    stats.check_count.fetch_add(1, std::memory_order_relaxed);

                    uint64_t check_time =
                        static_cast<uint64_t>(app::tool::time::uptime_us()) - start_time;
                    stats.total_check_time_us.fetch_add(check_time, std::memory_order_relaxed);

                    uint64_t count = stats.check_count.load(std::memory_order_relaxed);
                    if (count > 0)
                    {
                        stats.avg_check_time_us.store(stats.total_check_time_us.load() / count,
                                                      std::memory_order_relaxed);
                    }

                    // 检查HTTP响应
                    if (!http_resp.success)
                    {
                        result.status           = ActivationStatus::ERROR;
                        result.error            = ActivationError::NETWORK_ERROR;
                        result.error_message    = http_resp.error_message;
                        result.http_status_code = http_resp.status_code;
                        stats.check_failed.fetch_add(1, std::memory_order_relaxed);

                        LOG_ERROR(LOG_TAG, "激活检查失败，已重试%d次", retry);
                        return result;
                    }

                    // 解析JSON响应
                    try
                    {
                        json response = json::parse(http_resp.body);

                        result.http_status_code = http_resp.status_code;

                        // 检查是否未激活（返回激活码）
                        if (response.contains("activation") && response["activation"].is_object() &&
                            response["activation"].contains("code"))
                        {

                            result.status          = ActivationStatus::NOT_ACTIVATED;
                            result.activation_code = response["activation"]["code"];
                            result.error           = ActivationError::NONE;

                            LOG_INFO(LOG_TAG, "设备未激活，激活码: %s",
                                     result.activation_code.c_str());
                        }
                        else
                        {
                            // 已激活
                            result.status = ActivationStatus::ACTIVATED;
                            result.error  = ActivationError::NONE;

                            LOG_INFO(LOG_TAG, "设备已激活");
                        }

                        stats.check_success.fetch_add(1, std::memory_order_relaxed);
                    }
                    catch (const json::parse_error& e)
                    {
                        result.status        = ActivationStatus::ERROR;
                        result.error         = ActivationError::PARSE_ERROR;
                        result.error_message = std::string("JSON解析错误: ") + e.what();
                        stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                        stats.check_failed.fetch_add(1, std::memory_order_relaxed);

                        LOG_ERROR(LOG_TAG, "响应解析失败: %s", e.what());
                        LOG_DEBUG(LOG_TAG, "响应内容: %s", http_resp.body.c_str());
                    }

                    return result;
                }

                bool startPollingThread(const std::string& mac, const std::string& uuid,
                                        int timeout_sec)
                {
                    // 检查是否已在运行
                    if (polling_thread && polling_thread->joinable())
                    {
                        LOG_WARN(LOG_TAG, "轮询已在运行");
                        return false;
                    }

                    should_stop_polling.store(false, std::memory_order_release);
                    polling_status.store(ActivationStatus::CHECKING, std::memory_order_release);

                    polling_thread = std::make_unique<std::thread>(
                        [this, mac, uuid, timeout_sec]()
                        {
                            LOG_INFO(LOG_TAG, "激活轮询启动");

                            ActivationResult result = checkActivationInternal(mac, uuid);

                            if (result.isActivated())
                            {
                                LOG_INFO(LOG_TAG, "已激活");
                                polling_status.store(ActivationStatus::ACTIVATED,
                                                     std::memory_order_release);
                                saveLastResult(result);
                                invokeStatusCallback(ActivationStatus::ACTIVATED, result);
                                return;
                            }

                            if (result.hasError())
                            {
                                LOG_ERROR(LOG_TAG, "初次检查失败: %s",
                                          result.error_message.c_str());
                                polling_status.store(ActivationStatus::ERROR,
                                                     std::memory_order_release);
                                saveLastResult(result);
                                invokeErrorCallback(result.error, result.error_message);
                                return;
                            }

                            LOG_INFO(LOG_TAG, "未激活 URL=%s 码=%s 间隔=%ds",
                                     config.activation_url.c_str(), result.activation_code.c_str(),
                                     config.poll_interval_sec);

                            polling_status.store(ActivationStatus::NOT_ACTIVATED,
                                                 std::memory_order_release);
                            saveLastResult(result);
                            invokeStatusCallback(ActivationStatus::NOT_ACTIVATED, result);

                            // 开始轮询
                            int elapsed = 0;
                            while (elapsed < timeout_sec &&
                                   !should_stop_polling.load(std::memory_order_acquire))
                            {
                                // 可中断的等待
                                std::unique_lock<std::mutex> lock(polling_mutex);
                                if (polling_cv.wait_for(
                                        lock, std::chrono::seconds(config.poll_interval_sec),
                                        [this]() {
                                            return should_stop_polling.load(
                                                std::memory_order_acquire);
                                        }))
                                {
                                    LOG_INFO(LOG_TAG, "轮询取消");
                                    polling_status.store(ActivationStatus::CANCELLED,
                                                         std::memory_order_release);
                                    return;
                                }

                                elapsed += config.poll_interval_sec;

                                // 触发进度回调
                                invokeProgressCallback(elapsed, timeout_sec);

                                LOG_DEBUG(LOG_TAG, "检查激活中... (%d/%d 秒)", elapsed,
                                          timeout_sec);

                                // 再次检查
                                result = checkActivationInternal(mac, uuid);

                                if (result.isActivated())
                                {
                                    LOG_INFO(LOG_TAG, "激活成功");

                                    polling_status.store(ActivationStatus::ACTIVATED,
                                                         std::memory_order_release);
                                    saveLastResult(result);
                                    invokeStatusCallback(ActivationStatus::ACTIVATED, result);
                                    return;
                                }

                                if (result.hasError())
                                {
                                    LOG_WARN(LOG_TAG, "检查失败: %s", result.error_message.c_str());
                                }
                            }

                            // 轮询超时
                            if (elapsed >= timeout_sec)
                            {
                                LOG_WARN(LOG_TAG, "激活超时 %ds", timeout_sec);

                                result.status        = ActivationStatus::TIMEOUT;
                                result.error         = ActivationError::TIMEOUT;
                                result.error_message = "激活轮询超时";

                                polling_status.store(ActivationStatus::TIMEOUT,
                                                     std::memory_order_release);
                                saveLastResult(result);
                                invokeStatusCallback(ActivationStatus::TIMEOUT, result);
                            }
                        });

                    return true;
                }

                void stopPollingThread()
                {
                    should_stop_polling.store(true, std::memory_order_release);
                    polling_cv.notify_all();

                    if (polling_thread && polling_thread->joinable())
                    {
                        polling_thread->join();
                    }
                    polling_thread.reset();

                    LOG_DEBUG(LOG_TAG, "轮询线程已停止");
                }

                bool isPollingActive() const
                {
                    return polling_thread && polling_thread->joinable();
                }

                void saveLastResult(const ActivationResult& result)
                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    last_result = result;
                }

                ActivationResult getLastResult() const
                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    return last_result;
                }

                void invokeStatusCallback(ActivationStatus status, const ActivationResult& result)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (status_callback)
                    {
                        try
                        {
                            status_callback(status, result);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "状态回调异常: %s", e.what());
                        }
                    }
                }

                void invokeProgressCallback(int elapsed_sec, int total_sec)
                {
                    if (!config.enable_progress_callback)
                    {
                        return;
                    }

                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (progress_callback)
                    {
                        try
                        {
                            progress_callback(elapsed_sec, total_sec);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "进度回调异常: %s", e.what());
                        }
                    }
                }

                void invokeErrorCallback(ActivationError error, const std::string& message)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (error_callback)
                    {
                        try
                        {
                            error_callback(error, message);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "错误回调异常: %s", e.what());
                        }
                    }
                }
            };

            DeviceActivation::DeviceActivation(ActivationConfig  config,
                                               app::IHttpClient* http_client)
                : pImpl_(std::make_unique<Impl>(std::move(config), http_client))
            {
            }

            DeviceActivation::~DeviceActivation()
            {
                stopPolling();
                logStats();
            }

            ActivationResult DeviceActivation::checkActivation(const std::string& mac,
                                                               const std::string& uuid)
            {
                return pImpl_->checkActivationInternal(mac, uuid);
            }

            bool DeviceActivation::isActivated(const std::string& mac, const std::string& uuid)
            {
                ActivationResult result = checkActivation(mac, uuid);
                return result.isActivated();
            }

            std::future<ActivationResult>
            DeviceActivation::checkActivationAsync(const std::string& mac, const std::string& uuid)
            {

                // 使用std::async启动异步任务
                return std::async(std::launch::async, [this, mac, uuid]()
                                  { return pImpl_->checkActivationInternal(mac, uuid); });
            }

            bool DeviceActivation::startPolling(const std::string& mac, const std::string& uuid,
                                                int timeout_sec)
            {
                return pImpl_->startPollingThread(mac, uuid, timeout_sec);
            }

            void DeviceActivation::stopPolling()
            {
                pImpl_->stopPollingThread();
            }

            bool DeviceActivation::isPolling() const
            {
                return pImpl_->isPollingActive();
            }

            ActivationResult DeviceActivation::waitForPollingComplete(int timeout_ms)
            {
                if (!pImpl_->polling_thread || !pImpl_->polling_thread->joinable())
                {
                    LOG_WARN(LOG_TAG, "没有活动的轮询可等待");
                    return pImpl_->getLastResult();
                }

                if (timeout_ms <= 0)
                {
                    // 无限等待
                    pImpl_->polling_thread->join();
                }
                else
                {
                    // 等待指定时间
                    std::unique_lock<std::mutex> lock(pImpl_->polling_mutex);
                    pImpl_->polling_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms));
                }

                return pImpl_->getLastResult();
            }

            void DeviceActivation::setStatusCallback(ActivationStatusCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->status_callback = std::move(callback);
            }

            void DeviceActivation::setProgressCallback(ActivationProgressCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->progress_callback = std::move(callback);
            }

            void DeviceActivation::setErrorCallback(ActivationErrorCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->error_callback = std::move(callback);
            }

            const ActivationConfig& DeviceActivation::getConfig() const
            {
                return pImpl_->config;
            }

            bool DeviceActivation::updateConfig(const ActivationConfig& config)
            {
                if (isPolling())
                {
                    LOG_WARN(LOG_TAG, "轮询运行中，无法更新配置");
                    return false;
                }

                pImpl_->config = config;
                return true;
            }

            void DeviceActivation::getStats(Stats& out_stats) const
            {
                out_stats.check_count.store(pImpl_->stats.check_count.load());
                out_stats.check_success.store(pImpl_->stats.check_success.load());
                out_stats.check_failed.store(pImpl_->stats.check_failed.load());
                out_stats.network_errors.store(pImpl_->stats.network_errors.load());
                out_stats.parse_errors.store(pImpl_->stats.parse_errors.load());
                out_stats.retry_count.store(pImpl_->stats.retry_count.load());
                out_stats.total_check_time_us.store(pImpl_->stats.total_check_time_us.load());
                out_stats.avg_check_time_us.store(pImpl_->stats.avg_check_time_us.load());
            }

            void DeviceActivation::resetStats()
            {
                pImpl_->stats.check_count.store(0);
                pImpl_->stats.check_success.store(0);
                pImpl_->stats.check_failed.store(0);
                pImpl_->stats.network_errors.store(0);
                pImpl_->stats.parse_errors.store(0);
                pImpl_->stats.retry_count.store(0);
                pImpl_->stats.total_check_time_us.store(0);
                pImpl_->stats.avg_check_time_us.store(0);
            }

            void DeviceActivation::logStats() const
            {
                uint64_t total       = pImpl_->stats.check_count.load();
                uint64_t success     = pImpl_->stats.check_success.load();
                uint64_t failed      = pImpl_->stats.check_failed.load();
                uint64_t network_err = pImpl_->stats.network_errors.load();
                uint64_t parse_err   = pImpl_->stats.parse_errors.load();
                uint64_t retries     = pImpl_->stats.retry_count.load();

                LOG_INFO(LOG_TAG, "统计 检查=%llu 成功=%llu 失败=%llu 网络=%llu 解析=%llu 重试=%llu",
                         total, success, failed, network_err, parse_err, retries);

                if (total > 0)
                {
                    double sr = (double)success / total * 100.0;
                    if (sr < SUCCESS_RATE_WARNING_THRESHOLD)
                        LOG_WARN(LOG_TAG, "成功率 %.1f%%", sr);
                }
            }

            std::string DeviceActivation::statusToString(ActivationStatus status)
            {
                switch (status)
                {
                case ActivationStatus::UNKNOWN:
                    return "UNKNOWN";
                case ActivationStatus::CHECKING:
                    return "CHECKING";
                case ActivationStatus::NOT_ACTIVATED:
                    return "NOT_ACTIVATED";
                case ActivationStatus::ACTIVATED:
                    return "ACTIVATED";
                case ActivationStatus::ERROR:
                    return "ERROR";
                case ActivationStatus::TIMEOUT:
                    return "TIMEOUT";
                case ActivationStatus::CANCELLED:
                    return "CANCELLED";
                default:
                    return "INVALID";
                }
            }

            std::string DeviceActivation::errorToString(ActivationError error)
            {
                switch (error)
                {
                case ActivationError::NONE:
                    return "NONE";
                case ActivationError::NETWORK_ERROR:
                    return "NETWORK_ERROR";
                case ActivationError::TIMEOUT:
                    return "TIMEOUT";
                case ActivationError::SERVER_ERROR:
                    return "SERVER_ERROR";
                case ActivationError::CLIENT_ERROR:
                    return "CLIENT_ERROR";
                case ActivationError::PARSE_ERROR:
                    return "PARSE_ERROR";
                case ActivationError::INVALID_RESPONSE:
                    return "INVALID_RESPONSE";
                case ActivationError::CURL_INIT_FAILED:
                    return "CURL_INIT_FAILED";
                case ActivationError::CANCELLED:
                    return "CANCELLED";
                case ActivationError::UNKNOWN:
                    return "UNKNOWN";
                default:
                    return "INVALID";
                }
            }

        } // namespace activation
    }     // namespace chatbot
} // namespace app
