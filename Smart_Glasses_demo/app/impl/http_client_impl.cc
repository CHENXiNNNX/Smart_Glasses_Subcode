/*
 * http_client_impl.cc - HTTP 客户端
 */

#include "http_client_impl.hpp"
#include "../protocol/http/http.hpp"

namespace app
{

    struct HttpClientImpl::Impl
    {
        std::unique_ptr<protocol::http::HttpClient> client =
            std::make_unique<protocol::http::HttpClient>();
    };

    HttpClientImpl::HttpClientImpl() : impl_(std::make_unique<Impl>()) {}

    HttpClientImpl::~HttpClientImpl() = default;

    HttpResponse HttpClientImpl::post(const std::string& url, const std::string& body,
                                      const std::map<std::string, std::string>& headers,
                                      int timeout_ms, bool verify_ssl)
    {
        HttpResponse out;
        if (!impl_->client || !impl_->client->valid())
        {
            out.success       = false;
            out.error_message = "HTTP 未初始化";
            return out;
        }

        auto resp         = impl_->client->post(url, body, headers, timeout_ms, verify_ssl);
        out.success       = resp.success;
        out.status_code   = resp.status_code;
        out.body          = resp.body;
        out.error_message = resp.error_message;
        return out;
    }

    bool HttpClientImpl::valid() const
    {
        return impl_->client && impl_->client->valid();
    }

} // namespace app
