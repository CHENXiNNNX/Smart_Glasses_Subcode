/*
 * explain_service.hpp - AI 识图服务
 */

#pragma once

#include "../types.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace app::media::camera
{

    class JpegEncoder;

    class ExplainService
    {
    public:
        ExplainService();
        ~ExplainService();

        void set_url(const std::string& url, const std::string& token = "");
        std::string explain(const std::string& question, JpegEncoder* jpeg);

        /* JPEG 回调喂帧，explain 等待时调用 */
        void feed_frame(const uint8_t* data, size_t size);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
