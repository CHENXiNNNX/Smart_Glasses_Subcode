/*
 * recorder.cc - 录像
 */

#include "recorder.hpp"
#include "tool/file/file.hpp"
#include "tool/log/log.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace app::media::camera
{

    using namespace tool::log;
    using namespace tool::file;

#define TAG "Camera"

    class Recorder::Impl
    {
    public:
        bool                                  recording_ = false;
        std::unique_ptr<FileWrapper>          file_;
        std::string                           path_;
        std::atomic<uint32_t>                 frames_{0};
        std::atomic<uint64_t>                 bytes_{0};
        std::chrono::steady_clock::time_point start_time_;
        int                                   max_duration_ = 0;

        Error start(const std::string& path, int duration_sec)
        {
            if (recording_)
                return Error::BUSY;
            file_ = std::make_unique<FileWrapper>(path, FileMode::WRITE);
            if (!file_->valid())
            {
                LOG_ERROR(TAG, "录像: 打开失败 %s", path.c_str());
                file_.reset();
                return Error::DEVICE_ERROR;
            }
            path_         = path;
            frames_       = 0;
            bytes_        = 0;
            max_duration_ = duration_sec;
            start_time_   = std::chrono::steady_clock::now();
            recording_    = true;
            if (duration_sec > 0)
                LOG_INFO(TAG, "录像: 开始 %s (%d秒)", path.c_str(), duration_sec);
            else
                LOG_INFO(TAG, "录像: 开始 %s", path.c_str());
            return Error::OK;
        }

        Error stop()
        {
            if (!recording_)
                return Error::OK;
            file_->flush();
            file_.reset();
            recording_ = false;
            LOG_INFO(TAG, "录像: 停止 %s (%u帧 %uKB)", path_.c_str(), frames_.load(),
                     static_cast<unsigned>(bytes_.load() / 1024));
            return Error::OK;
        }

        void write_frame(const uint8_t* data, size_t size)
        {
            if (!recording_ || !file_)
                return;
            if (max_duration_ > 0 && duration_sec() >= static_cast<uint32_t>(max_duration_))
            {
                stop();
                return;
            }
            if (file_->write(data, size))
            {
                frames_++;
                bytes_ += size;
            }
        }

        uint32_t duration_sec() const
        {
            if (!recording_)
                return 0;
            auto now = std::chrono::steady_clock::now();
            return static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count());
        }

        uint64_t file_size() const { return bytes_.load(); }
    };

    Recorder::Recorder() : impl_(std::make_unique<Impl>()) {}
    Recorder::~Recorder() { impl_->stop(); }
    Error Recorder::start(const std::string& path, int duration_sec) { return impl_->start(path, duration_sec); }
    Error Recorder::stop() { return impl_->stop(); }
    bool  Recorder::is_recording() const { return impl_->recording_; }
    uint32_t Recorder::duration_sec() const { return impl_->duration_sec(); }
    uint64_t Recorder::file_size() const { return impl_->file_size(); }
    uint32_t Recorder::frames() const { return impl_->frames_.load(); }
    void Recorder::write_frame(const uint8_t* data, size_t size) { impl_->write_frame(data, size); }

} // namespace app::media::camera
