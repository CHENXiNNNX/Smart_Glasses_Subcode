/**
 * @file activationv2.cc
 * @brief 设备激活管理V2实现
 */

#include "activationv2.h"
#include "../../tool/log/log.h"
#include "../../../common/common.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <condition_variable>
#include <map>
#include <cstring>

using json = nlohmann::json;

namespace app {
namespace chatbot {
namespace activation {

using namespace tool::log;

// ============================================================================
// RAII包装器（智能指针删除器）
// ============================================================================

/**
 * @brief CURL句柄删除器
 */
struct CurlDeleter {
    void operator()(CURL* p) const {
        if (p) {
            curl_easy_cleanup(p);
        }
    }
};
using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;

/**
 * @brief CURL Slist删除器
 */
struct CurlSlistDeleter {
    void operator()(curl_slist* p) const {
        if (p) {
            curl_slist_free_all(p);
        }
    }
};
using CurlSlistPtr = std::unique_ptr<curl_slist, CurlSlistDeleter>;

// ============================================================================
// HttpClient - HTTP客户端（RAII封装）
// ============================================================================

class HttpClient {
public:
    HttpClient() {
        curl_ = CurlPtr(curl_easy_init());
        if (!curl_) {
            LOG_ERROR("HttpClient", "Failed to initialize CURL");
        }
    }
    
    ~HttpClient() {
        // RAII自动清理
    }
    
    bool isValid() const {
        return curl_ != nullptr;
    }
    
    /**
     * @brief HTTP POST请求
     */
    struct HttpResponse {
        int status_code;
        std::string body;
        std::string error_message;
        bool success;
    };
    
    HttpResponse post(const std::string& url,
                     const std::string& post_data,
                     const std::map<std::string, std::string>& headers,
                     int timeout_ms,
                     bool verify_ssl) {
        HttpResponse response;
        response.success = false;
        response.status_code = 0;
        
        if (!curl_) {
            response.error_message = "CURL not initialized";
            return response;
        }
        
        // 构建headers（RAII管理）
        curl_slist* raw_headers = nullptr;
        for (const auto& [key, value] : headers) {
            std::string header = key + ": " + value;
            raw_headers = curl_slist_append(raw_headers, header.c_str());
        }
        CurlSlistPtr headers_ptr(raw_headers);
        
        // 设置CURL选项
        curl_easy_setopt(curl_.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl_.get(), CURLOPT_HTTPHEADER, raw_headers);
        curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYPEER, verify_ssl ? 1L : 0L);
        curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYHOST, verify_ssl ? 2L : 0L);
        curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
        curl_easy_setopt(curl_.get(), CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeout_ms / 2));
        
        // 执行请求
        CURLcode res = curl_easy_perform(curl_.get());
        
        if (res != CURLE_OK) {
            response.error_message = curl_easy_strerror(res);
            return response;
        }
        
        // 获取HTTP状态码
        long http_code = 0;
        curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<int>(http_code);
        
        // 判断成功
        response.success = (http_code >= 200 && http_code < 300);
        
        return response;
    }

private:
    CurlPtr curl_;
    
    // CURL写回调
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t total_size = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->reserve(str->size() + total_size);  // ✅ 预留空间
        str->append(static_cast<char*>(contents), total_size);
        return total_size;
    }
};

// ============================================================================
// DeviceActivationV2::Impl 内部实现
// ============================================================================

class DeviceActivationV2::Impl {
public:
    // 配置
    ActivationConfig config;
    
    // HTTP客户端（RAII管理）
    std::unique_ptr<HttpClient> http_client;
    
    // 轮询状态
    std::atomic<ActivationStatus> polling_status{ActivationStatus::UNKNOWN};
    std::unique_ptr<std::thread> polling_thread;
    std::atomic<bool> should_stop_polling{false};
    std::condition_variable polling_cv;
    std::mutex polling_mutex;
    
    // 最后的激活结果
    ActivationResult last_result;
    mutable std::mutex result_mutex;
    
    // 回调函数
    ActivationStatusCallback status_callback;
    ActivationProgressCallback progress_callback;
    ActivationErrorCallback error_callback;
    mutable std::mutex callback_mutex;
    
    // 统计信息
    DeviceActivationV2::Stats stats;
    
    explicit Impl(const ActivationConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("ActivationV2", "Impl created");
        
        // 创建HTTP客户端
        http_client = std::make_unique<HttpClient>();
        
        if (!http_client->isValid()) {
            LOG_ERROR("ActivationV2", "Failed to create HTTP client");
        }
    }
    
    ~Impl() {
        LOG_DEBUG("ActivationV2", "Impl destroying...");
        stopPollingThread();
        LOG_DEBUG("ActivationV2", "Impl destroyed");
    }
    
    // ========================================================================
    // 激活检查核心逻辑
    // ========================================================================
    
    ActivationResult checkActivationInternal(const std::string& mac, const std::string& uuid) {
        ActivationResult result;
        result.check_timestamp = get_nowus();
        
        uint64_t start_time = get_nowus();
        
        if (!http_client || !http_client->isValid()) {
            result.status = ActivationStatus::ERROR;
            result.error = ActivationError::CURL_INIT_FAILED;
            result.error_message = "HTTP client not initialized";
            stats.check_failed.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        
        // 构建POST数据
        json post_json;
        post_json["platform"] = config.platform;
        post_json["version"] = config.version;
        post_json["board"]["type"] = config.board_type;
        post_json["board"]["name"] = config.board_name;
        std::string post_data = post_json.dump();
        
        // 构建HTTP Headers
        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        headers["Device-Id"] = mac;
        headers["User-Agent"] = config.user_agent;
        headers["Accept-Language"] = config.accept_language;
        
        LOG_DEBUG("ActivationV2", "Checking activation: Device-Id=%s", mac.c_str());
        
        // 重试机制
        int retry = 0;
        HttpClient::HttpResponse http_resp;
        
        for (retry = 0; retry < config.max_retries; retry++) {
            if (retry > 0) {
                LOG_INFO("ActivationV2", "Retry %d/%d after %dms", 
                        retry, config.max_retries, config.retry_delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(config.retry_delay_ms));
                stats.retry_count.fetch_add(1, std::memory_order_relaxed);
            }
            
            // 执行HTTP请求
            http_resp = http_client->post(
                config.api_url, 
                post_data, 
                headers,
                config.request_timeout_ms,
                config.verify_ssl
            );
            
            if (http_resp.success) {
                break;  // 成功，退出重试
            }
            
            // 网络错误，记录统计
            stats.network_errors.fetch_add(1, std::memory_order_relaxed);
            LOG_WARN("ActivationV2", "HTTP request failed: %s", http_resp.error_message.c_str());
        }
        
        // 更新统计
        stats.check_count.fetch_add(1, std::memory_order_relaxed);
        
        uint64_t check_time = get_nowus() - start_time;
        stats.total_check_time_us.fetch_add(check_time, std::memory_order_relaxed);
        
        uint64_t count = stats.check_count.load(std::memory_order_relaxed);
        if (count > 0) {
            stats.avg_check_time_us.store(
                stats.total_check_time_us.load() / count,
                std::memory_order_relaxed
            );
        }
        
        // 检查HTTP响应
        if (!http_resp.success) {
            result.status = ActivationStatus::ERROR;
            result.error = ActivationError::NETWORK_ERROR;
            result.error_message = http_resp.error_message;
            result.http_status_code = http_resp.status_code;
            stats.check_failed.fetch_add(1, std::memory_order_relaxed);
            
            LOG_ERROR("ActivationV2", "Activation check failed after %d retries", retry);
            return result;
        }
        
        // 解析JSON响应
        try {
            json response = json::parse(http_resp.body);
            
            result.http_status_code = http_resp.status_code;
            
            // 检查是否未激活（返回激活码）
            if (response.contains("activation") && 
                response["activation"].is_object() &&
                response["activation"].contains("code")) {
                
                result.status = ActivationStatus::NOT_ACTIVATED;
                result.activation_code = response["activation"]["code"];
                result.error = ActivationError::NONE;
                
                LOG_INFO("ActivationV2", "Device not activated, activation code: %s",
                        result.activation_code.c_str());
            } else {
                // 已激活
                result.status = ActivationStatus::ACTIVATED;
                result.error = ActivationError::NONE;
                
                LOG_INFO("ActivationV2", "Device activated successfully");
            }
            
            stats.check_success.fetch_add(1, std::memory_order_relaxed);
            
        } catch (const json::parse_error& e) {
            result.status = ActivationStatus::ERROR;
            result.error = ActivationError::PARSE_ERROR;
            result.error_message = std::string("JSON parse error: ") + e.what();
            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
            stats.check_failed.fetch_add(1, std::memory_order_relaxed);
            
            LOG_ERROR("ActivationV2", "Failed to parse response: %s", e.what());
            LOG_DEBUG("ActivationV2", "Response body: %s", http_resp.body.c_str());
        }
        
        return result;
    }
    
    // ========================================================================
    // 异步轮询线程
    // ========================================================================
    
    bool startPollingThread(const std::string& mac, const std::string& uuid, int timeout_sec) {
        // 检查是否已在运行
        if (polling_thread && polling_thread->joinable()) {
            LOG_WARN("ActivationV2", "Polling already running");
            return false;
        }
        
        should_stop_polling.store(false, std::memory_order_release);
        polling_status.store(ActivationStatus::CHECKING, std::memory_order_release);
        
        polling_thread = std::make_unique<std::thread>([this, mac, uuid, timeout_sec]() {
            LOG_INFO("ActivationV2", "========================================");
            LOG_INFO("ActivationV2", "  Device Activation Polling Started");
            LOG_INFO("ActivationV2", "========================================");
            
            // 首次检查
            ActivationResult result = checkActivationInternal(mac, uuid);
            
            if (result.isActivated()) {
                LOG_INFO("ActivationV2", "✓ Device already activated");
                polling_status.store(ActivationStatus::ACTIVATED, std::memory_order_release);
                saveLastResult(result);
                invokeStatusCallback(ActivationStatus::ACTIVATED, result);
                return;
            }
            
            if (result.hasError()) {
                LOG_ERROR("ActivationV2", "Initial check failed: %s", result.error_message.c_str());
                polling_status.store(ActivationStatus::ERROR, std::memory_order_release);
                saveLastResult(result);
                invokeErrorCallback(result.error, result.error_message);
                return;
            }
            
            // 未激活，显示激活信息
            LOG_INFO("ActivationV2", "⚠ Device not activated");
            LOG_INFO("ActivationV2", "  Activation URL: %s", config.activation_url.c_str());
            LOG_INFO("ActivationV2", "  Activation Code: %s", result.activation_code.c_str());
            LOG_INFO("ActivationV2", "  Polling interval: %d seconds", config.poll_interval_sec);
            LOG_INFO("ActivationV2", "========================================");
            
            polling_status.store(ActivationStatus::NOT_ACTIVATED, std::memory_order_release);
            saveLastResult(result);
            invokeStatusCallback(ActivationStatus::NOT_ACTIVATED, result);
            
            // 开始轮询
            int elapsed = 0;
            while (elapsed < timeout_sec && !should_stop_polling.load(std::memory_order_acquire)) {
                // ✅ 可中断的等待
                std::unique_lock<std::mutex> lock(polling_mutex);
                if (polling_cv.wait_for(lock, std::chrono::seconds(config.poll_interval_sec),
                    [this]() { return should_stop_polling.load(std::memory_order_acquire); })) {
                    LOG_INFO("ActivationV2", "Polling cancelled by user");
                    polling_status.store(ActivationStatus::CANCELLED, std::memory_order_release);
                    return;
                }
                
                elapsed += config.poll_interval_sec;
                
                // 触发进度回调
                invokeProgressCallback(elapsed, timeout_sec);
                
                LOG_DEBUG("ActivationV2", "Checking activation... (%d/%d sec)", elapsed, timeout_sec);
                
                // 再次检查
                result = checkActivationInternal(mac, uuid);
                
                if (result.isActivated()) {
                    LOG_INFO("ActivationV2", "========================================");
                    LOG_INFO("ActivationV2", "  ✓ Device Activated Successfully!");
                    LOG_INFO("ActivationV2", "========================================");
                    
                    polling_status.store(ActivationStatus::ACTIVATED, std::memory_order_release);
                    saveLastResult(result);
                    invokeStatusCallback(ActivationStatus::ACTIVATED, result);
                    return;
                }
                
                if (result.hasError()) {
                    LOG_WARN("ActivationV2", "Check failed, continue polling... (error: %s)",
                            result.error_message.c_str());
                }
            }
            
            // 轮询超时
            if (elapsed >= timeout_sec) {
                LOG_WARN("ActivationV2", "========================================");
                LOG_WARN("ActivationV2", "  ✗ Activation Timeout (%d sec)", timeout_sec);
                LOG_WARN("ActivationV2", "========================================");
                
                result.status = ActivationStatus::TIMEOUT;
                result.error = ActivationError::TIMEOUT;
                result.error_message = "Activation polling timeout";
                
                polling_status.store(ActivationStatus::TIMEOUT, std::memory_order_release);
                saveLastResult(result);
                invokeStatusCallback(ActivationStatus::TIMEOUT, result);
            }
        });
        
        return true;
    }
    
    void stopPollingThread() {
        should_stop_polling.store(true, std::memory_order_release);
        polling_cv.notify_all();
        
        if (polling_thread && polling_thread->joinable()) {
            polling_thread->join();
        }
        polling_thread.reset();
        
        LOG_DEBUG("ActivationV2", "Polling thread stopped");
    }
    
    bool isPollingActive() const {
        return polling_thread && polling_thread->joinable();
    }
    
    // ========================================================================
    // 结果管理
    // ========================================================================
    
    void saveLastResult(const ActivationResult& result) {
        std::lock_guard<std::mutex> lock(result_mutex);
        last_result = result;
    }
    
    ActivationResult getLastResult() const {
        std::lock_guard<std::mutex> lock(result_mutex);
        return last_result;
    }
    
    // ========================================================================
    // 回调调用（异常安全）
    // ========================================================================
    
    void invokeStatusCallback(ActivationStatus status, const ActivationResult& result) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (status_callback) {
            try {
                status_callback(status, result);
            } catch (const std::exception& e) {
                LOG_ERROR("ActivationV2", "Status callback exception: %s", e.what());
            }
        }
    }
    
    void invokeProgressCallback(int elapsed_sec, int total_sec) {
        if (!config.enable_progress_callback) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (progress_callback) {
            try {
                progress_callback(elapsed_sec, total_sec);
            } catch (const std::exception& e) {
                LOG_ERROR("ActivationV2", "Progress callback exception: %s", e.what());
            }
        }
    }
    
    void invokeErrorCallback(ActivationError error, const std::string& message) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        
        if (error_callback) {
            try {
                error_callback(error, message);
            } catch (const std::exception& e) {
                LOG_ERROR("ActivationV2", "Error callback exception: %s", e.what());
            }
        }
    }
};

// ============================================================================
// CURL全局初始化（线程安全，只初始化一次）
// ============================================================================

// ✅ 使用局部静态变量确保只初始化一次
static void ensureCurlGlobalInit() {
    static std::once_flag curl_init_flag;
    std::call_once(curl_init_flag, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        LOG_INFO("ActivationV2", "CURL global initialized");
        
        // 注册清理函数（程序退出时调用）
        std::atexit([]() {
            curl_global_cleanup();
            LOG_DEBUG("ActivationV2", "CURL global cleaned up");
        });
    });
}

// ============================================================================
// DeviceActivationV2 公共接口实现
// ============================================================================

DeviceActivationV2::DeviceActivationV2(const ActivationConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    
    // ✅ 线程安全的全局初始化
    ensureCurlGlobalInit();
    
    LOG_INFO("ActivationV2", "Device Activation V2 created");
}

DeviceActivationV2::~DeviceActivationV2() {
    LOG_INFO("ActivationV2", "Device Activation V2 destroying...");
    
    // 停止轮询
    stopPolling();
    
    // 输出统计
    logStats();
    
    // RAII自动清理
    LOG_INFO("ActivationV2", "Device Activation V2 destroyed");
}

// ========================================================================
// 同步检查接口
// ========================================================================

ActivationResult DeviceActivationV2::checkActivation(const std::string& mac, 
                                                     const std::string& uuid) {
    return pImpl_->checkActivationInternal(mac, uuid);
}

bool DeviceActivationV2::isActivated(const std::string& mac, const std::string& uuid) {
    ActivationResult result = checkActivation(mac, uuid);
    return result.isActivated();
}

// ========================================================================
// 异步检查接口
// ========================================================================

std::future<ActivationResult> DeviceActivationV2::checkActivationAsync(
    const std::string& mac, 
    const std::string& uuid) {
    
    // 使用std::async启动异步任务
    return std::async(std::launch::async, [this, mac, uuid]() {
        return pImpl_->checkActivationInternal(mac, uuid);
    });
}

// ========================================================================
// 轮询接口
// ========================================================================

bool DeviceActivationV2::startPolling(const std::string& mac, 
                                      const std::string& uuid,
                                      int timeout_sec) {
    return pImpl_->startPollingThread(mac, uuid, timeout_sec);
}

void DeviceActivationV2::stopPolling() {
    pImpl_->stopPollingThread();
}

bool DeviceActivationV2::isPolling() const {
    return pImpl_->isPollingActive();
}

ActivationResult DeviceActivationV2::waitForPollingComplete(int timeout_ms) {
    if (!pImpl_->polling_thread || !pImpl_->polling_thread->joinable()) {
        LOG_WARN("ActivationV2", "No active polling to wait for");
        return pImpl_->getLastResult();
    }
    
    if (timeout_ms <= 0) {
        // 无限等待
        pImpl_->polling_thread->join();
    } else {
        // 等待指定时间
        std::unique_lock<std::mutex> lock(pImpl_->polling_mutex);
        pImpl_->polling_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms));
    }
    
    return pImpl_->getLastResult();
}

// ========================================================================
// 回调设置
// ========================================================================

void DeviceActivationV2::setStatusCallback(ActivationStatusCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->status_callback = callback;
}

void DeviceActivationV2::setProgressCallback(ActivationProgressCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->progress_callback = callback;
}

void DeviceActivationV2::setErrorCallback(ActivationErrorCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
    pImpl_->error_callback = callback;
}

// ========================================================================
// 配置管理
// ========================================================================

const ActivationConfig& DeviceActivationV2::getConfig() const {
    return pImpl_->config;
}

bool DeviceActivationV2::updateConfig(const ActivationConfig& config) {
    if (isPolling()) {
        LOG_WARN("ActivationV2", "Cannot update config while polling");
        return false;
    }
    
    pImpl_->config = config;
    LOG_INFO("ActivationV2", "Configuration updated");
    return true;
}

// ========================================================================
// 统计信息
// ========================================================================

void DeviceActivationV2::getStats(Stats& out_stats) const {
    out_stats.check_count.store(pImpl_->stats.check_count.load());
    out_stats.check_success.store(pImpl_->stats.check_success.load());
    out_stats.check_failed.store(pImpl_->stats.check_failed.load());
    out_stats.network_errors.store(pImpl_->stats.network_errors.load());
    out_stats.parse_errors.store(pImpl_->stats.parse_errors.load());
    out_stats.retry_count.store(pImpl_->stats.retry_count.load());
    out_stats.total_check_time_us.store(pImpl_->stats.total_check_time_us.load());
    out_stats.avg_check_time_us.store(pImpl_->stats.avg_check_time_us.load());
}

void DeviceActivationV2::resetStats() {
    pImpl_->stats.check_count.store(0);
    pImpl_->stats.check_success.store(0);
    pImpl_->stats.check_failed.store(0);
    pImpl_->stats.network_errors.store(0);
    pImpl_->stats.parse_errors.store(0);
    pImpl_->stats.retry_count.store(0);
    pImpl_->stats.total_check_time_us.store(0);
    pImpl_->stats.avg_check_time_us.store(0);
    
    LOG_INFO("ActivationV2", "Stats reset");
}

void DeviceActivationV2::logStats() const {
    uint64_t total = pImpl_->stats.check_count.load();
    uint64_t success = pImpl_->stats.check_success.load();
    uint64_t failed = pImpl_->stats.check_failed.load();
    uint64_t network_err = pImpl_->stats.network_errors.load();
    uint64_t parse_err = pImpl_->stats.parse_errors.load();
    uint64_t retries = pImpl_->stats.retry_count.load();
    
    LOG_INFO("ActivationV2", "=== Device Activation V2 Statistics ===");
    LOG_INFO("ActivationV2", "  Check count:     %llu", total);
    LOG_INFO("ActivationV2", "  Success:         %llu", success);
    LOG_INFO("ActivationV2", "  Failed:          %llu", failed);
    LOG_INFO("ActivationV2", "  Network errors:  %llu", network_err);
    LOG_INFO("ActivationV2", "  Parse errors:    %llu", parse_err);
    LOG_INFO("ActivationV2", "  Retry count:     %llu", retries);
    
    if (total > 0) {
        uint64_t avg_time = pImpl_->stats.avg_check_time_us.load();
        LOG_INFO("ActivationV2", "  Avg check time:  %llu ms", avg_time / 1000);
        
        double success_rate = (double)success / total * 100.0;
        LOG_INFO("ActivationV2", "  Success rate:    %.2f%%", success_rate);
        
        if (success_rate < 90.0) {
            LOG_WARN("ActivationV2", "Low success rate, check network stability");
        }
    }
}

// ========================================================================
// 工具函数
// ========================================================================

std::string DeviceActivationV2::statusToString(ActivationStatus status) {
    switch (status) {
        case ActivationStatus::UNKNOWN:         return "UNKNOWN";
        case ActivationStatus::CHECKING:        return "CHECKING";
        case ActivationStatus::NOT_ACTIVATED:   return "NOT_ACTIVATED";
        case ActivationStatus::ACTIVATED:       return "ACTIVATED";
        case ActivationStatus::ERROR:           return "ERROR";
        case ActivationStatus::TIMEOUT:         return "TIMEOUT";
        case ActivationStatus::CANCELLED:       return "CANCELLED";
        default:                                return "INVALID";
    }
}

std::string DeviceActivationV2::errorToString(ActivationError error) {
    switch (error) {
        case ActivationError::NONE:             return "NONE";
        case ActivationError::NETWORK_ERROR:    return "NETWORK_ERROR";
        case ActivationError::TIMEOUT:          return "TIMEOUT";
        case ActivationError::SERVER_ERROR:     return "SERVER_ERROR";
        case ActivationError::CLIENT_ERROR:     return "CLIENT_ERROR";
        case ActivationError::PARSE_ERROR:      return "PARSE_ERROR";
        case ActivationError::INVALID_RESPONSE: return "INVALID_RESPONSE";
        case ActivationError::CURL_INIT_FAILED: return "CURL_INIT_FAILED";
        case ActivationError::CANCELLED:        return "CANCELLED";
        case ActivationError::UNKNOWN:          return "UNKNOWN";
        default:                                return "INVALID";
    }
}

} // namespace activation
} // namespace chatbot
} // namespace app

