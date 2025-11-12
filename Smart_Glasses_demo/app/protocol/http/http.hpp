/**
 * @file http.hpp
 * @brief HTTP客户端 - 基于CURL的RAII封装
 * @details 特性：
 *          - RAII资源管理（智能指针封装CURL）
 *          - 异常安全的HTTP请求
 *          - 自动内存管理
 *          - 超时控制
 *          - SSL验证配置
 *          - 重试机制支持
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-29
 */

 #ifndef HTTP_HPP
 #define HTTP_HPP
 
 #include <string>
 #include <map>
 #include <memory>
 #include <cstddef>
 
 // 前向声明CURL类型，避免头文件依赖
 typedef void CURL;
 struct curl_slist;
 
 namespace app {
 namespace protocol {
 namespace http {
 
 // ============================================================================
 // RAII包装器（智能指针删除器）
 // ============================================================================
 
 /**
  * @brief CURL句柄删除器
  */
 struct CurlDeleter {
     void operator()(CURL* p) const;
 };
 using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;
 
 /**
  * @brief CURL Slist删除器
  */
 struct CurlSlistDeleter {
     void operator()(curl_slist* p) const;
 };
 using CurlSlistPtr = std::unique_ptr<curl_slist, CurlSlistDeleter>;
 
 // ============================================================================
 // HTTP响应结构
 // ============================================================================
 
 /**
  * @brief HTTP响应
  */
 struct HttpResponse {
     int status_code;            // HTTP状态码
     std::string body;           // 响应体
     std::string error_message;  // 错误消息
     bool success;               // 请求是否成功
     
     HttpResponse()
         : status_code(0)
         , success(false) {}
 };
 
 // ============================================================================
 // HTTP客户端类
 // ============================================================================
 
 /**
  * @brief HTTP客户端
  * @details 基于CURL的HTTP客户端封装，使用RAII管理资源
  */
 class HttpClient {
 public:
     /**
      * @brief 构造函数
      * @details 自动初始化CURL
      */
     HttpClient();
     
     /**
      * @brief 析构函数
      * @details RAII自动清理CURL资源
      */
     ~HttpClient();
     
     /**
      * @brief 检查客户端是否有效
      * @return true-有效, false-无效
      */
     bool isValid() const;
     
     /**
      * @brief HTTP POST请求
      * @param url 请求URL
      * @param post_data POST数据（通常是JSON字符串）
      * @param headers HTTP请求头（键值对）
      * @param timeout_ms 超时时间（毫秒）
      * @param verify_ssl 是否验证SSL证书
      * @return HTTP响应
      */
     HttpResponse post(const std::string& url,
                       const std::string& post_data,
                       const std::map<std::string, std::string>& headers,
                       int timeout_ms,
                       bool verify_ssl);
     
     /**
      * @brief HTTP GET请求
      * @param url 请求URL
      * @param headers HTTP请求头（键值对）
      * @param timeout_ms 超时时间（毫秒）
      * @param verify_ssl 是否验证SSL证书
      * @return HTTP响应
      */
     HttpResponse get(const std::string& url,
                      const std::map<std::string, std::string>& headers,
                      int timeout_ms,
                      bool verify_ssl);
     
     // 禁止拷贝和赋值
     HttpClient(const HttpClient&) = delete;
     HttpClient& operator=(const HttpClient&) = delete;
 
 private:
     CurlPtr curl_;  // CURL句柄（RAII管理）
     
     /**
      * @brief CURL写回调函数
      * @details 用于接收HTTP响应数据
      */
     static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
 };
 
 // ============================================================================
 // CURL全局初始化管理
 // ============================================================================
 
 /**
  * @brief 确保CURL全局初始化（线程安全）
  * @details 使用std::call_once确保只初始化一次
  */
 void ensureCurlGlobalInit();
 
 } // namespace http
 } // namespace protocol
 } // namespace app
 
 #endif // HTTP_HPP
 