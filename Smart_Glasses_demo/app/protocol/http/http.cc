/**
 * @file http.cc
 * @brief HTTP客户端实现
 */

 #include "http.hpp"
 #include "../../tool/log/log.hpp"
 #include <curl/curl.h>
 #include <cstring>
 #include <mutex>
 
 namespace app {
 namespace protocol {
 namespace http {
 
 using namespace tool::log;
 
 // ============================================================================
 // RAII删除器实现
 // ============================================================================
 
 void CurlDeleter::operator()(CURL* p) const {
     if (p) {
         curl_easy_cleanup(p);
     }
 }
 
 void CurlSlistDeleter::operator()(curl_slist* p) const {
     if (p) {
         curl_slist_free_all(p);
     }
 }
 
 // ============================================================================
 // CURL全局初始化（线程安全，只初始化一次）
 // ============================================================================
 
 void ensureCurlGlobalInit() {
     static std::once_flag curl_init_flag;
     std::call_once(curl_init_flag, []() {
         curl_global_init(CURL_GLOBAL_DEFAULT);
         LOG_INFO("HttpClient", "CURL全局初始化完成");
         
         // 注册清理函数（程序退出时调用）
         std::atexit([]() {
             curl_global_cleanup();
             LOG_DEBUG("HttpClient", "CURL全局清理完成");
         });
     });
 }
 
 // ============================================================================
 // HttpClient实现
 // ============================================================================
 
 HttpClient::HttpClient() {
     // 确保CURL全局初始化
     ensureCurlGlobalInit();
     
     // 创建CURL句柄
     curl_ = CurlPtr(curl_easy_init());
     
     if (!curl_) {
         LOG_ERROR("HttpClient", "CURL初始化失败");
     }
 }
 
 HttpClient::~HttpClient() {
     // RAII自动清理
 }
 
 bool HttpClient::isValid() const {
     return curl_ != nullptr;
 }
 
 HttpResponse HttpClient::post(const std::string& url,
                                const std::string& post_data,
                                const std::map<std::string, std::string>& headers,
                                int timeout_ms,
                                bool verify_ssl) {
     HttpResponse response;
     response.success = false;
     response.status_code = 0;
     
     if (!curl_) {
         response.error_message = "CURL未初始化";
         return response;
     }
     
     // 构建HTTP头部（RAII管理）
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
     curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, writeCallback);
     curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &response.body);
     curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYPEER, verify_ssl ? 1L : 0L);
     curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYHOST, verify_ssl ? 2L : 0L);
     curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
     curl_easy_setopt(curl_.get(), CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeout_ms / 2));
     
     // 设置User-Agent（避免某些服务器拒绝请求）
     curl_easy_setopt(curl_.get(), CURLOPT_USERAGENT, "SmartGlasses/1.0");
     
     // 执行请求
     CURLcode res = curl_easy_perform(curl_.get());
     
     if (res != CURLE_OK) {
         response.error_message = curl_easy_strerror(res);
         LOG_ERROR("HttpClient", "POST请求失败: %s (URL: %s)", 
                  response.error_message.c_str(), url.c_str());
         return response;
     }
     
     // 获取HTTP状态码
     long http_code = 0;
     curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code);
     response.status_code = static_cast<int>(http_code);
     
     // 判断成功（2xx状态码）
     response.success = (http_code >= 200 && http_code < 300);
     
     if (!response.success) {
         LOG_WARN("HttpClient", "HTTP POST失败: status=%d, url=%s", 
                 response.status_code, url.c_str());
     }
     
     return response;
 }
 
 HttpResponse HttpClient::get(const std::string& url,
                               const std::map<std::string, std::string>& headers,
                               int timeout_ms,
                               bool verify_ssl) {
     HttpResponse response;
     response.success = false;
     response.status_code = 0;
     
     if (!curl_) {
         response.error_message = "CURL未初始化";
         return response;
     }
     
     // 构建HTTP头部（RAII管理）
     curl_slist* raw_headers = nullptr;
     for (const auto& [key, value] : headers) {
         std::string header = key + ": " + value;
         raw_headers = curl_slist_append(raw_headers, header.c_str());
     }
     CurlSlistPtr headers_ptr(raw_headers);
     
     // 设置CURL选项
     curl_easy_setopt(curl_.get(), CURLOPT_URL, url.c_str());
     curl_easy_setopt(curl_.get(), CURLOPT_HTTPGET, 1L);  // GET请求
     curl_easy_setopt(curl_.get(), CURLOPT_HTTPHEADER, raw_headers);
     curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, writeCallback);
     curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &response.body);
     curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYPEER, verify_ssl ? 1L : 0L);
     curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYHOST, verify_ssl ? 2L : 0L);
     curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
     curl_easy_setopt(curl_.get(), CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeout_ms / 2));
     
     // 设置User-Agent
     curl_easy_setopt(curl_.get(), CURLOPT_USERAGENT, "SmartGlasses/1.0");
     
     // 执行请求
     CURLcode res = curl_easy_perform(curl_.get());
     
     if (res != CURLE_OK) {
         response.error_message = curl_easy_strerror(res);
         LOG_ERROR("HttpClient", "GET请求失败: %s (URL: %s)", 
                  response.error_message.c_str(), url.c_str());
         return response;
     }
     
     // 获取HTTP状态码
     long http_code = 0;
     curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code);
     response.status_code = static_cast<int>(http_code);
     
     // 判断成功（2xx状态码）
     response.success = (http_code >= 200 && http_code < 300);
     
     if (!response.success) {
         LOG_WARN("HttpClient", "HTTP GET失败: status=%d, url=%s", 
                 response.status_code, url.c_str());
     }
     
     return response;
 }
 
 size_t HttpClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
     size_t total_size = size * nmemb;
     std::string* str = static_cast<std::string*>(userp);
     
     // 预留空间，避免多次内存分配
     str->reserve(str->size() + total_size);
     str->append(static_cast<char*>(contents), total_size);
     
     return total_size;
 }
 
 } // namespace http
 } // namespace protocol
 } // namespace app
 