/*
 * explain_service.cc - AI 识图服务
 */

#include "explain_service.hpp"
#include "media/camera/adapters/encoder/jpeg_encoder.hpp"
#include "protocol/http/http.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace app::media::camera
{

    class ExplainService::Impl
    {
    public:
        std::string             url_;
        std::string             token_;
        std::mutex              mtx_;
        std::atomic<bool>       pending_{false};
        std::vector<uint8_t>    frame_data_;
        std::condition_variable cv_;
    };

    ExplainService::ExplainService() : impl_(std::make_unique<Impl>()) {}
    ExplainService::~ExplainService() = default;

    void ExplainService::set_url(const std::string& url, const std::string& token)
    {
        std::lock_guard<std::mutex> lk(impl_->mtx_);
        impl_->url_   = url;
        impl_->token_ = token;
    }

    std::string ExplainService::explain(const std::string& question, JpegEncoder* jpeg)
    {
        if (impl_->url_.empty())
            return R"({"success":false,"message":"AI URL未设置"})";
        if (!jpeg || !jpeg->is_running())
            return R"({"success":false,"message":"JPEG编码器未运行"})";

        impl_->pending_ = true;
        {
            std::unique_lock<std::mutex> lk(impl_->mtx_);
            impl_->frame_data_.clear();
            bool ok = impl_->cv_.wait_for(lk, std::chrono::seconds(5),
                                          [this] { return !impl_->frame_data_.empty(); });
            if (!ok)
            {
                impl_->pending_ = false;
                return R"({"success":false,"message":"获取JPEG超时"})";
            }
        }

        protocol::http::HttpClient         http_client;
        std::map<std::string, std::string> form_fields{{"question", question}};
        std::map<std::string, std::string> headers;
        if (!impl_->token_.empty())
            headers["Authorization"] = "Bearer " + impl_->token_;

        auto response = http_client.postMultipart(
            impl_->url_, form_fields, "file", impl_->frame_data_.data(), impl_->frame_data_.size(),
            "camera.jpg", "image/jpeg", headers, 30000, true);

        if (!response.success)
            return R"({"success":false,"message":")" + response.error_message + R"("})";

        return response.body;
    }

    void ExplainService::feed_frame(const uint8_t* data, size_t size)
    {
        if (!impl_->pending_.exchange(false))
            return;
        std::lock_guard<std::mutex> lk(impl_->mtx_);
        impl_->frame_data_.assign(data, data + size);
        impl_->cv_.notify_one();
    }

} // namespace app::media::camera
