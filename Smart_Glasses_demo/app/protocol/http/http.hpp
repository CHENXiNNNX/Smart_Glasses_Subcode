/**
 * @file http.hpp
 * @brief HTTP客户端
 */

#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>
#include <map>
#include <memory>
#include <cstddef>

// 前向声明CURL类型，避免头文件依赖
using CURL // NOLINT(readability-identifier-naming)
    = void;
struct curl_slist;

namespace app
{
    namespace protocol
    {
        namespace http
        {

            // ============================================================================
            // 包装器
            // ============================================================================

            /**
             * @brief CURL句柄删除器
             */
            struct CurlDeleter
            {
                void operator()(CURL* p) const;
            };
            using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;

            /**
             * @brief CURL Slist删除器
             */
            struct CurlSlistDeleter
            {
                void operator()(curl_slist* p) const;
            };
            using CurlSlistPtr = std::unique_ptr<curl_slist, CurlSlistDeleter>;

            // ============================================================================
            // HTTP响应结构
            // ============================================================================

            /**
             * @brief HTTP响应
             */
            struct HttpResponse
            {
                int         status_code;   // HTTP状态码
                std::string body;          // 响应体
                std::string error_message; // 错误消息
                bool        success;       // 请求是否成功

                HttpResponse() : status_code(0), success(false) {}
            };

            // ============================================================================
            // HTTP客户端类
            // ============================================================================

            /**
             * @brief HTTP客户端
             */
            class HttpClient
            {
            public:
                /**
                 * @brief 构造函数
                 */
                HttpClient();

                /**
                 * @brief 析构函数
                 */
                ~HttpClient() = default;

                /**
                 * @brief 检查客户端是否有效
                 * @return true-有效, false-无效
                 */
                bool isValid() const;

                /**
                 * @brief HTTP POST请求
                 * @param url 请求URL
                 * @param post_data POST数据
                 * @param headers HTTP请求头
                 * @param timeout_ms 超时时间
                 * @param verify_ssl 是否验证SSL证书
                 * @return HTTP响应
                 */
                HttpResponse post(const std::string& url, const std::string& post_data,
                                  const std::map<std::string, std::string>& headers, int timeout_ms,
                                  bool verify_ssl);

                /**
                 * @brief HTTP GET请求
                 * @param url 请求URL
                 * @param headers HTTP请求头
                 * @param timeout_ms 超时时间
                 * @param verify_ssl 是否验证SSL证书
                 * @return HTTP响应
                 */
                HttpResponse get(const std::string&                        url,
                                 const std::map<std::string, std::string>& headers, int timeout_ms,
                                 bool verify_ssl);

                /**
                 * @brief HTTP POST multipart/form-data 请求
                 * @param url 请求URL
                 * @param form_fields 表单字段
                 * @param file_field_name 文件字段名称
                 * @param file_data 文件数据指针
                 * @param file_size 文件数据大小
                 * @param file_name 文件名
                 * @param file_content_type 文件Content-Type
                 * @param headers 额外的HTTP请求头
                 * @param timeout_ms 超时时间
                 * @param verify_ssl 是否验证SSL证书
                 * @return HTTP响应
                 */
                HttpResponse
                postMultipart(const std::string&                        url,
                              const std::map<std::string, std::string>& form_fields,
                              const std::string& file_field_name, const void* file_data,
                              size_t file_size, const std::string& file_name = "upload.bin",
                              const std::string& file_content_type = "application/octet-stream",
                              const std::map<std::string, std::string>& headers = {},
                              int timeout_ms = 10000, bool verify_ssl = true);

                // 禁止拷贝和赋值
                HttpClient(const HttpClient&)            = delete;
                HttpClient& operator=(const HttpClient&) = delete;

            private:
                CurlPtr curl_;

                /**
                 * @brief CURL写回调函数
                 */
                static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
            };

            // ============================================================================
            // CURL全局初始化管理
            // ============================================================================

            /**
             * @brief 确保CURL全局初始化
             */
            void ensureCurlGlobalInit();

        } // namespace http
    }     // namespace protocol
} // namespace app

#endif // HTTP_HPP
