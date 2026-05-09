/* wav_recorder.cc - WAV 文件录制工具实现 */

#include "wav_recorder.hpp"
#include "../../../tool/file/file.hpp"
#include "../../../tool/log/log.hpp"

#include <atomic>
#include <chrono>
#include <mutex>

namespace app::media::audio
{

    using namespace tool::log;
    using namespace tool::file;

#define TAG "WavRec"

    /*====================================================================
     * WAV 文件头
     *====================================================================*/

#pragma pack(push, 1)
    struct WavHeader
    {
        char     riff[4]         = {'R', 'I', 'F', 'F'};
        uint32_t file_size       = 0;
        char     wave[4]         = {'W', 'A', 'V', 'E'};
        char     fmt[4]          = {'f', 'm', 't', ' '};
        uint32_t fmt_size        = 16;
        uint16_t audio_fmt       = 1;
        uint16_t channels        = 1;
        uint32_t sample_rate     = 48000;
        uint32_t byte_rate       = 96000;
        uint16_t block_align     = 2;
        uint16_t bits_per_sample = 16;
        char     data[4]         = {'d', 'a', 't', 'a'};
        uint32_t data_size       = 0;
    };
#pragma pack(pop)

    class WavRecorder::Impl
    {
    public:
        std::unique_ptr<FileWrapper>          file_;
        std::mutex                            mtx_;
        bool                                  recording_ = false;
        uint32_t                              rate_      = 48000;
        uint8_t                               channels_  = 1;
        int                                   duration_  = 0;
        std::atomic<uint32_t>                 frames_{0};
        std::atomic<size_t>                   bytes_{0};
        std::chrono::steady_clock::time_point start_;

        Error start(const std::string& path, uint32_t rate, uint8_t channels, int duration_sec)
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (recording_)
                return Error::BUSY;

            file_ = std::make_unique<FileWrapper>(path, FileMode::WRITE);
            if (!file_->valid())
            {
                LOG_ERROR(TAG, "创建失败 %s", path.c_str());
                file_.reset();
                return Error::DEVICE_ERROR;
            }

            rate_     = rate;
            channels_ = channels;
            duration_ = duration_sec;

            WavHeader h{};
            h.channels        = channels;
            h.sample_rate     = rate;
            h.bits_per_sample = 16;
            h.block_align     = static_cast<uint16_t>(channels * 2);
            h.byte_rate       = rate * h.block_align;

            if (!file_->write(&h, sizeof(h)))
            {
                file_.reset();
                return Error::DEVICE_ERROR;
            }

            frames_    = 0;
            bytes_     = 0;
            start_     = std::chrono::steady_clock::now();
            recording_ = true;
            LOG_INFO(TAG, "开始 %s %uHz %uch", path.c_str(), rate, channels);
            return Error::OK;
        }

        Error stop()
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!recording_)
                return Error::OK;

            recording_ = false;
            if (file_)
            {
                size_t data_sz = bytes_.load();

                file_->seek(4, SEEK_SET);
                uint32_t fsz = static_cast<uint32_t>(sizeof(WavHeader) - 8 + data_sz);
                file_->write(&fsz, sizeof(fsz));

                file_->seek(40, SEEK_SET);
                uint32_t dsz = static_cast<uint32_t>(data_sz);
                file_->write(&dsz, sizeof(dsz));

                file_->flush();
                file_.reset();
                LOG_INFO(TAG, "停止 %u帧 %uKB", frames_.load(), (unsigned)(data_sz / 1024));
            }
            return Error::OK;
        }

        void write(const int16_t* data, size_t samples)
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!recording_ || !file_)
                return;

            size_t bytes = samples * channels_ * sizeof(int16_t);
            if (file_->write(data, bytes))
            {
                frames_++;
                bytes_ += bytes;
            }

            if (duration_ > 0)
            {
                auto sec = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start_)
                               .count();
                if (sec >= duration_)
                    recording_ = false;
            }
        }

        void write(const FramePtr& f)
        {
            if (f)
                write(f->get<int16_t>(), f->samples);
        }

        uint32_t duration_sec() const
        {
            if (!recording_)
                return 0;
            return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::steady_clock::now() - start_)
                                             .count());
        }

        uint64_t file_size() const
        {
            return bytes_.load();
        }
        uint32_t frames() const
        {
            return frames_.load();
        }
    };

    WavRecorder::WavRecorder() : impl_(std::make_unique<Impl>()) {}
    WavRecorder::~WavRecorder()
    {
        stop();
    }

    Error WavRecorder::start(const std::string& path, uint32_t rate, uint8_t channels, int dur)
    {
        return impl_->start(path, rate, channels, dur);
    }
    Error WavRecorder::stop()
    {
        return impl_->stop();
    }
    bool WavRecorder::is_recording() const
    {
        return impl_->recording_;
    }

    void WavRecorder::write(const int16_t* d, size_t n)
    {
        impl_->write(d, n);
    }
    void WavRecorder::write(const FramePtr& f)
    {
        impl_->write(f);
    }

    uint32_t WavRecorder::duration_sec() const
    {
        return impl_->duration_sec();
    }
    uint64_t WavRecorder::file_size() const
    {
        return impl_->file_size();
    }
    uint32_t WavRecorder::frames() const
    {
        return impl_->frames();
    }

} // namespace app::media::audio
