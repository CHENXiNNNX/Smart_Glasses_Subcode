/*
 * http_client_impl.hpp - HTTP 客户端实现
 */

#pragma once

#include "../interfaces/ihttp_client.hpp"
#include <memory>

namespace app::protocol::http
{
    class HttpClient;
}

namespace app
{

    class HttpClientImpl : public IHttpClient
    {
    public:
        HttpClientImpl();
        ~HttpClientImpl() override;

        HttpResponse post(const std::string& url, const std::string& body,
                          const std::map<std::string, std::string>& headers, int timeout_ms,
                          bool verify_ssl) override;

        bool valid() const override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app
