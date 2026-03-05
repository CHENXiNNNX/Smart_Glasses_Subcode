/*
 * video_service_impl.hpp - 视频服务实现
 */

#pragma once

#include "../interfaces/ivideo_service.hpp"
#include <atomic>
#include <mutex>
#include <string>

namespace app::media::camera
{
    class CameraDrv;
}

namespace app
{

    class VideoServiceImpl : public IVideoService
    {
    public:
        explicit VideoServiceImpl(media::camera::CameraDrv* drv);
        ~VideoServiceImpl() override;

        void        setExplainUrl(const std::string& url, const std::string& token = "") override;
        std::string explainImage(const std::string& question) override;

        VideoError takePhoto(const std::string& filename, bool with_explain = false) override;
        bool       isPhotoCapturing() const override;

        VideoError startRecord(const std::string& filename, int duration_sec = 0) override;
        VideoError stopRecord() override;
        bool       isRecording() const override;

        bool           isStreaming() const override;
        VideoError     startStream() override;
        VideoMainState getMainState() const override;
        VideoError     setMainState(VideoMainState state) override;

    private:
        media::camera::CameraDrv*   drv_;
        std::atomic<VideoMainState> main_state_{VideoMainState::NONE};
        std::atomic<bool>           photo_capturing_{false};
        mutable std::mutex          photo_mtx_;
    };

} // namespace app
