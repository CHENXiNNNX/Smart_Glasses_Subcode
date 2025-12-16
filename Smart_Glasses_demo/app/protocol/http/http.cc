/**
 * @file http.cc
 * @brief HTTP客户端实现
 */

#include "http.hpp"
#include "../../tool/log/log.hpp"
#include <curl/curl.h>
#include <cstring>
#include <ctime>
#include <mutex>

namespace app
{
    namespace protocol
    {
        namespace http
        {

            using namespace tool::log;

            namespace
            {
                constexpr const char* LOG_TAG                     = "HTTP";
                constexpr long        HTTP_SUCCESS_LOWER_BOUND    = 200;
                constexpr long        HTTP_SUCCESS_UPPER_BOUND    = 300;
                constexpr long        HTTP_CONNECT_TIMEOUT_FACTOR = 2;
            } // namespace

            // ============================================================================
            // 删除器实现
            // ============================================================================

            void CurlDeleter::operator()(CURL* p) const
            {
                if (p)
                {
                    curl_easy_cleanup(p);
                }
            }

            void CurlSlistDeleter::operator()(curl_slist* p) const
            {
                if (p)
                {
                    curl_slist_free_all(p);
                }
            }

            // ============================================================================
            // CURL全局初始化
            // ============================================================================

            void ensureCurlGlobalInit()
            {
                static std::once_flag curl_init_flag;
                std::call_once(curl_init_flag,
                               []()
                               {
                                   curl_global_init(CURL_GLOBAL_DEFAULT);
                                   LOG_INFO(LOG_TAG, "CURL全局初始化完成");

                                   std::atexit(
                                       []()
                                       {
                                           curl_global_cleanup();
                                           LOG_DEBUG(LOG_TAG, "CURL全局清理完成");
                                       });
                               });
            }

            // ============================================================================
            // HttpClient实现
            // ============================================================================

            HttpClient::HttpClient()
            {
                // 确保CURL全局初始化
                ensureCurlGlobalInit();

                // 创建CURL句柄
                curl_ = CurlPtr(curl_easy_init());

                if (!curl_)
                {
                    LOG_ERROR(LOG_TAG, "CURL初始化失败");
                }
            }

            bool HttpClient::isValid() const
            {
                return curl_ != nullptr;
            }

            HttpResponse HttpClient::post(const std::string& url, const std::string& post_data,
                                          const std::map<std::string, std::string>& headers,
                                          int timeout_ms, bool verify_ssl)
            {
                HttpResponse response;
                response.success     = false;
                response.status_code = 0;

                if (!curl_)
                {
                    response.error_message = "CURL未初始化";
                    return response;
                }

                // 构建HTTP头部
                curl_slist* raw_headers = nullptr;
                for (const auto& [key, value] : headers)
                {
                    std::string header = key;
                    header.append(": ");
                    header.append(value);
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
                curl_easy_setopt(curl_.get(), CURLOPT_CONNECTTIMEOUT_MS,
                                 static_cast<long>(timeout_ms / HTTP_CONNECT_TIMEOUT_FACTOR));

                // 设置User-Agent
                curl_easy_setopt(curl_.get(), CURLOPT_USERAGENT, "SmartGlasses/1.0");

                // 执行请求
                CURLcode result_code = curl_easy_perform(curl_.get());

                if (result_code != CURLE_OK)
                {
                    response.error_message = curl_easy_strerror(result_code);
                    LOG_ERROR(LOG_TAG, "POST请求失败: %s (URL: %s)", response.error_message.c_str(),
                              url.c_str());
                    return response;
                }

                // 获取HTTP状态码
                long http_code = 0;
                curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code);
                response.status_code = static_cast<int>(http_code);

                // 判断成功
                response.success =
                    (http_code >= HTTP_SUCCESS_LOWER_BOUND && http_code < HTTP_SUCCESS_UPPER_BOUND);

                if (!response.success)
                {
                    LOG_WARN(LOG_TAG, "HTTP POST失败: status=%ld, url=%s", http_code, url.c_str());
                }

                return response;
            }

            HttpResponse HttpClient::get(const std::string&                        url,
                                         const std::map<std::string, std::string>& headers,
                                         int timeout_ms, bool verify_ssl)
            {
                HttpResponse response;
                response.success     = false;
                response.status_code = 0;

                if (!curl_)
                {
                    response.error_message = "CURL未初始化";
                    return response;
                }

                // 构建HTTP头部
                curl_slist* raw_headers = nullptr;
                for (const auto& [key, value] : headers)
                {
                    std::string header = key;
                    header.append(": ");
                    header.append(value);
                    raw_headers = curl_slist_append(raw_headers, header.c_str());
                }
                CurlSlistPtr headers_ptr(raw_headers);

                // 设置CURL选项
                curl_easy_setopt(curl_.get(), CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl_.get(), CURLOPT_HTTPGET, 1L); // GET请求
                curl_easy_setopt(curl_.get(), CURLOPT_HTTPHEADER, raw_headers);
                curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, writeCallback);
                curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &response.body);
                curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYPEER, verify_ssl ? 1L : 0L);
                curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYHOST, verify_ssl ? 2L : 0L);
                curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
                curl_easy_setopt(curl_.get(), CURLOPT_CONNECTTIMEOUT_MS,
                                 static_cast<long>(timeout_ms / HTTP_CONNECT_TIMEOUT_FACTOR));

                // 设置User-Agent
                curl_easy_setopt(curl_.get(), CURLOPT_USERAGENT, "SmartGlasses/1.0");

                // 执行请求
                CURLcode result_code = curl_easy_perform(curl_.get());

                if (result_code != CURLE_OK)
                {
                    response.error_message = curl_easy_strerror(result_code);
                    LOG_ERROR(LOG_TAG, "GET请求失败: %s (URL: %s)", response.error_message.c_str(),
                              url.c_str());
                    return response;
                }

                // 获取HTTP状态码
                long http_code = 0;
                curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code);
                response.status_code = static_cast<int>(http_code);

                // 判断成功
                response.success =
                    (http_code >= HTTP_SUCCESS_LOWER_BOUND && http_code < HTTP_SUCCESS_UPPER_BOUND);

                if (!response.success)
                {
                    LOG_WARN(LOG_TAG, "HTTP GET失败: status=%ld, url=%s", http_code, url.c_str());
                }

                return response;
            }

            size_t HttpClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
            {
                size_t total_size = size * nmemb;
                auto*  str        = static_cast<std::string*>(userp);

                str->reserve(str->size() + total_size);
                str->append(static_cast<char*>(contents), total_size);

                return total_size;
            }

            HttpResponse HttpClient::postMultipart(
                const std::string& url, const std::map<std::string, std::string>& form_fields,
                const std::string& file_field_name, const void* file_data, size_t file_size,
                const std::string& file_name, const std::string& file_content_type,
                const std::map<std::string, std::string>& headers, int timeout_ms, bool verify_ssl)
            {
                HttpResponse response;
                response.success     = false;
                response.status_code = 0;

                if (!curl_)
                {
                    response.error_message = "CURL未初始化";
                    return response;
                }

                // 生成multipart边界
                std::string boundary = "----SmartGlassesBoundary";
                boundary += std::to_string(time(nullptr));

                // 构建multipart/form-data请求体
                std::string multipart_data;

                // 添加文本字段
                for (const auto& [key, value] : form_fields)
                {
                    multipart_data += "--" + boundary + "\r\n";
                    multipart_data += "Content-Disposition: form-data; name=\"" + key + "\"\r\n";
                    multipart_data += "\r\n";
                    multipart_data += value + "\r\n";
                }

                // 添加文件字段
                multipart_data += "--" + boundary + "\r\n";
                multipart_data += "Content-Disposition: form-data; name=\"" + file_field_name +
                                  "\"; filename=\"" + file_name + "\"\r\n";
                multipart_data += "Content-Type: " + file_content_type + "\r\n";
                multipart_data += "\r\n";

                // 添加文件数据
                multipart_data.append(static_cast<const char*>(file_data), file_size);
                multipart_data += "\r\n";

                // 添加结束边界
                multipart_data += "--" + boundary + "--\r\n";

                // 构建HTTP头部
                curl_slist* raw_headers = nullptr;

                // 添加Content-Type头部
                std::string content_type_header = "Content-Type: multipart/form-data; boundary=" + boundary;
                raw_headers                     = curl_slist_append(raw_headers, content_type_header.c_str());

                // 添加额外的头部
                for (const auto& [key, value] : headers)
                {
                    std::string header = key + ": " + value;
                    raw_headers       = curl_slist_append(raw_headers, header.c_str());
                }

                CurlSlistPtr headers_ptr(raw_headers);

                // 设置CURL选项
                curl_easy_setopt(curl_.get(), CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDS, multipart_data.c_str());
                curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(multipart_data.size()));
                curl_easy_setopt(curl_.get(), CURLOPT_HTTPHEADER, raw_headers);
                curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, writeCallback);
                curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &response.body);
                curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYPEER, verify_ssl ? 1L : 0L);
                curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYHOST, verify_ssl ? 2L : 0L);
                curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
                curl_easy_setopt(curl_.get(), CURLOPT_CONNECTTIMEOUT_MS,
                                 static_cast<long>(timeout_ms / HTTP_CONNECT_TIMEOUT_FACTOR));

                // 设置User-Agent
                curl_easy_setopt(curl_.get(), CURLOPT_USERAGENT, "SmartGlasses/1.0");

                // 执行请求
                CURLcode result_code = curl_easy_perform(curl_.get());

                if (result_code != CURLE_OK)
                {
                    response.error_message = curl_easy_strerror(result_code);
                    LOG_ERROR(LOG_TAG, "Multipart POST请求失败: %s (URL: %s)", response.error_message.c_str(),
                              url.c_str());
                    return response;
                }

                // 获取HTTP状态码
                long http_code = 0;
                curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &http_code);
                response.status_code = static_cast<int>(http_code);

                // 判断成功
                response.success =
                    (http_code >= HTTP_SUCCESS_LOWER_BOUND && http_code < HTTP_SUCCESS_UPPER_BOUND);

                if (!response.success)
                {
                    LOG_WARN(LOG_TAG, "HTTP Multipart POST失败: status=%ld, url=%s", http_code, url.c_str());
                }

                return response;
            }

        } // namespace http
    }     // namespace protocol
} // namespace app
