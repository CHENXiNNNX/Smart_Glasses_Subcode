/**
 * @file activation.hpp
 * @brief 设备激活管理实现
 */

 #ifndef ACTIVATION_HPP
 #define ACTIVATION_HPP
 
 #include <string>
 #include <memory>
 #include <functional>
 #include <atomic>
 #include <mutex>
 #include <future>
 
 namespace app {
 namespace chatbot {
 namespace activation {
 
 // ============================================================================
 // 前向声明
 // ============================================================================
 class DeviceActivation;
 class HttpClient;
 
 // ============================================================================
 // 激活状态枚举
 // ============================================================================
 
 /**
  * @brief 激活状态
  */
 enum class ActivationStatus {
     UNKNOWN = 0,        // 未知
     CHECKING,           // 检查中
     NOT_ACTIVATED,      // 未激活
     ACTIVATED,          // 已激活
     ERROR,              // 错误
     TIMEOUT,            // 超时
     CANCELLED           // 已取消
 };
 
 /**
  * @brief 激活错误类型
  */
 enum class ActivationError {
     NONE = 0,
     NETWORK_ERROR,          // 网络错误
     TIMEOUT,                // 超时
     SERVER_ERROR,           // 服务器错误（5xx）
     CLIENT_ERROR,           // 客户端错误（4xx）
     PARSE_ERROR,            // JSON解析错误
     INVALID_RESPONSE,       // 响应格式错误
     CURL_INIT_FAILED,       // CURL初始化失败
     CANCELLED,              // 用户取消
     UNKNOWN                 // 未知错误
 };
 
 // ============================================================================
 // 激活结果
 // ============================================================================
 
 /**
  * @brief 激活检查结果
  */
 struct ActivationResult {
     ActivationStatus status;        // 激活状态
     ActivationError error;          // 错误类型
     std::string activation_code;    // 激活码（如果未激活）
     std::string error_message;      // 详细错误信息
     int http_status_code;           // HTTP状态码
     uint64_t check_timestamp;       // 检查时间戳
     
     ActivationResult()
         : status(ActivationStatus::UNKNOWN)
         , error(ActivationError::NONE)
         , http_status_code(0)
         , check_timestamp(0) {}
     
     bool isActivated() const {
         return status == ActivationStatus::ACTIVATED;
     }
     
     bool hasError() const {
         return error != ActivationError::NONE;
     }
 };
 
 // ============================================================================
 // 激活配置
 // ============================================================================
 
 /**
  * @brief 激活配置
  */
 struct ActivationConfig {
     // 服务器配置
     std::string api_url = "https://api.tenclass.net/xiaozhi/ota/";
     std::string activation_url = "https://xiaozhi.me";
     
     // 设备信息
     std::string platform = "linux";
     std::string version = "1.0.0";
     std::string board_type = "smart_glasses";
     std::string board_name = "smart_glasses_board";
     std::string user_agent = "SmartGlasses/1.0";
     std::string accept_language = "zh-CN";
     
     // HTTP配置
     int connect_timeout_ms = 5000;      // 连接超时
     int request_timeout_ms = 10000;     // 请求超时
     int max_retries = 3;                // 最大重试次数
     int retry_delay_ms = 1000;          // 重试间隔
     
     // 轮询配置
     int poll_interval_sec = 5;          // 轮询间隔
     int poll_timeout_sec = 300;         // 轮询超时（5分钟）
     bool enable_progress_callback = true;  // 启用进度回调
     
     // SSL配置
     bool verify_ssl = false;            // 是否验证SSL证书（默认关闭，与V1一致）
     
     // 功能开关
     bool enable_auto_retry = true;      // 启用自动重试
     bool enable_detailed_logging = true; // 启用详细日志
 };
 
 // ============================================================================
 // 回调函数类型
 // ============================================================================
 
 /**
  * @brief 激活状态变化回调
  */
 using ActivationStatusCallback = std::function<void(ActivationStatus status, const ActivationResult& result)>;
 
 /**
  * @brief 激活进度回调（轮询时）
  */
 using ActivationProgressCallback = std::function<void(int elapsed_sec, int total_sec)>;
 
 /**
  * @brief 激活错误回调
  */
 using ActivationErrorCallback = std::function<void(ActivationError error, const std::string& message)>;
 
 // ============================================================================
 // 设备激活管理类
 // ============================================================================
 
/**
 * @brief 设备激活管理器
 */
 class DeviceActivation {
 public:
     /**
      * @brief 构造函数
      * @param config 激活配置
      */
     explicit DeviceActivation(const ActivationConfig& config = ActivationConfig());
     
     /**
      * @brief 析构函数（RAII自动清理所有资源）
      */
     ~DeviceActivation();
     
     // ========================================================================
     // 同步检查接口
     // ========================================================================
     
     /**
      * @brief 同步检查激活状态（阻塞）
      * @param mac MAC地址
      * @param uuid UUID
      * @return 激活结果
      */
     ActivationResult checkActivation(const std::string& mac, const std::string& uuid);
     
     /**
      * @brief 快速检查是否已激活（阻塞）
      * @param mac MAC地址
      * @param uuid UUID
      * @return true-已激活, false-未激活或失败
      */
     bool isActivated(const std::string& mac, const std::string& uuid);
     
     // ========================================================================
     // 异步检查接口
     // ========================================================================
     
     /**
      * @brief 异步检查激活状态（立即返回）
      * @param mac MAC地址
      * @param uuid UUID
      * @return future对象，可获取激活结果
      */
     std::future<ActivationResult> checkActivationAsync(const std::string& mac, 
                                                        const std::string& uuid);
     
     // ========================================================================
     // 轮询接口（异步非阻塞）
     // ========================================================================
     
     /**
      * @brief 启动异步轮询（立即返回，后台线程轮询）
      * @param mac MAC地址
      * @param uuid UUID
      * @param timeout_sec 超时时间（秒）
      * @return true-启动成功, false-已在运行
      */
     bool startPolling(const std::string& mac, 
                       const std::string& uuid,
                       int timeout_sec = 300);
     
     /**
      * @brief 停止轮询
      */
     void stopPolling();
     
     /**
      * @brief 检查是否正在轮询
      * @return true-正在轮询
      */
     bool isPolling() const;
     
     /**
      * @brief 等待轮询完成（阻塞）
      * @param timeout_ms 超时时间（毫秒），0表示无限等待
      * @return 最终激活结果
      */
     ActivationResult waitForPollingComplete(int timeout_ms = 0);
     
     // ========================================================================
     // 回调设置
     // ========================================================================
     
     /**
      * @brief 设置激活状态回调
      * @param callback 状态变化回调
      */
     void setStatusCallback(ActivationStatusCallback callback);
     
     /**
      * @brief 设置激活进度回调
      * @param callback 进度回调
      */
     void setProgressCallback(ActivationProgressCallback callback);
     
     /**
      * @brief 设置错误回调
      * @param callback 错误回调
      */
     void setErrorCallback(ActivationErrorCallback callback);
     
     // ========================================================================
     // 配置管理
     // ========================================================================
     
     /**
      * @brief 获取当前配置
      * @return 配置引用
      */
     const ActivationConfig& getConfig() const;
     
     /**
      * @brief 更新配置（仅在非轮询状态下）
      * @param config 新配置
      * @return true-成功, false-正在轮询中无法更新
      */
     bool updateConfig(const ActivationConfig& config);
     
     // ========================================================================
     // 统计信息
     // ========================================================================
     
     /**
      * @brief 激活统计信息
      */
     struct Stats {
         std::atomic<uint64_t> check_count{0};           // 检查次数
         std::atomic<uint64_t> check_success{0};         // 检查成功次数
         std::atomic<uint64_t> check_failed{0};          // 检查失败次数
         std::atomic<uint64_t> network_errors{0};        // 网络错误次数
         std::atomic<uint64_t> parse_errors{0};          // 解析错误次数
         std::atomic<uint64_t> retry_count{0};           // 重试次数
         std::atomic<uint64_t> total_check_time_us{0};   // 总检查耗时
         std::atomic<uint64_t> avg_check_time_us{0};     // 平均检查耗时
     };
     
     /**
      * @brief 获取统计信息
      * @param out_stats 输出统计
      */
     void getStats(Stats& out_stats) const;
     
     /**
      * @brief 重置统计信息
      */
     void resetStats();
     
     /**
      * @brief 输出统计日志
      */
     void logStats() const;
     
     // ========================================================================
     // 工具函数
     // ========================================================================
     
     /**
      * @brief 激活状态转字符串
      * @param status 激活状态
      * @return 状态字符串
      */
     static std::string statusToString(ActivationStatus status);
     
     /**
      * @brief 激活错误转字符串
      * @param error 错误类型
      * @return 错误字符串
      */
     static std::string errorToString(ActivationError error);
     
     // 禁止拷贝和赋值
     DeviceActivation(const DeviceActivation&) = delete;
     DeviceActivation& operator=(const DeviceActivation&) = delete;

 private:
     class Impl;
     std::unique_ptr<Impl> pImpl_;
 };
 
 } // namespace activation
 } // namespace chatbot
 } // namespace app
 
 #endif // ACTIVATION_HPP
 