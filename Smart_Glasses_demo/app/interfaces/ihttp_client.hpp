/*
 * ihttp_client.hpp - HTTP 客户端接口
 */

#pragma once

#include <string>
#include <map>

namespace app
{

    struct HttpResponse
    {
        bool        success;
        int         status_code;
        std::string body;
        std::string error_message;
    };

    class IHttpClient
    {
    public:
        virtual ~IHttpClient() = default;

        virtual HttpResponse post(const std::string& url, const std::string& body,
                                  const std::map<std::string, std::string>& headers, int timeout_ms,
                                  bool verify_ssl) = 0;

        virtual bool valid() const = 0;
    };

} // namespace app
