/*
 * video_service_impl.cc - 视频服务
 */

#include "video_service_impl.hpp"
#include "../media/camera/camera.hpp"

#include <chrono>
#include <thread>

namespace app
{

    VideoServiceImpl::VideoServiceImpl(media::camera::CameraDrv* drv) : drv_(drv) {}

    VideoServiceImpl::~VideoServiceImpl() = default;

    void VideoServiceImpl::setExplainUrl(const std::string& url, const std::string& token)
    {
        if (drv_)
            drv_->set_explain_url(url, token);
    }

    std::string VideoServiceImpl::explainImage(const std::string& question)
    {
        return drv_ ? drv_->explain_image(question) : "";
    }

    VideoError VideoServiceImpl::takePhoto(const std::string& filename, bool with_explain)
    {
        (void)with_explain;
        if (!drv_)
            return {1, "相机未初始化"};

        if (photo_capturing_.exchange(true))
            return {1, "正在拍照"};

        std::string path = filename.empty() ? "/root/picture/photo.jpg" : filename;

        /* 拍照前暂停 H264 流，减轻 ISP 负载，避免 sofInfo 池耗尽 */
        drv_->pause_h264();

        auto err = drv_->jpeg().save(path,
                                     [this](const std::string& p, media::camera::Error e)
                                     {
                                         (void)p;
                                         (void)e;
                                         photo_capturing_ = false;
                                         /* 拍照完成后恢复 H264 流 */
                                         if (drv_)
                                             drv_->resume_h264();
                                     });

        if (err != media::camera::Error::OK)
        {
            photo_capturing_ = false;
            drv_->resume_h264();
            return {static_cast<int>(err), "保存失败"};
        }

        /* 等待完成（最多5秒） */
        for (int i = 0; i < 50 && photo_capturing_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (photo_capturing_)
        {
            photo_capturing_ = false;
            drv_->resume_h264();
            return {1, "超时"};
        }
        return {0, ""};
    }

    bool VideoServiceImpl::isPhotoCapturing() const
    {
        return photo_capturing_;
    }

    VideoError VideoServiceImpl::startRecord(const std::string& filename, int duration_sec)
    {
        if (!drv_)
            return {1, "相机未初始化"};

        std::string path = filename.empty() ? "/root/video/record.h264" : filename;
        auto        err  = drv_->recorder().start(path, duration_sec);
        return {err == media::camera::Error::OK ? 0 : static_cast<int>(err),
                err == media::camera::Error::OK ? "" : "开始录像失败"};
    }

    VideoError VideoServiceImpl::stopRecord()
    {
        if (!drv_)
            return {1, "相机未初始化"};

        auto err = drv_->recorder().stop();
        if (err == media::camera::Error::OK)
            main_state_ = VideoMainState::NONE;
        return {err == media::camera::Error::OK ? 0 : static_cast<int>(err),
                err == media::camera::Error::OK ? "" : "停止录像失败"};
    }

    bool VideoServiceImpl::isRecording() const
    {
        return drv_ && drv_->recorder().is_recording();
    }

    bool VideoServiceImpl::isStreaming() const
    {
        return drv_ && drv_->is_running();
    }

    VideoError VideoServiceImpl::startStream()
    {
        if (!drv_)
            return {1, "相机未初始化"};

        auto err = drv_->start();
        return {err == media::camera::Error::OK ? 0 : static_cast<int>(err),
                err == media::camera::Error::OK ? "" : "启动失败"};
    }

    VideoMainState VideoServiceImpl::getMainState() const
    {
        return main_state_;
    }

    VideoError VideoServiceImpl::setMainState(VideoMainState state)
    {
        main_state_ = state;
        return {0, ""};
    }

} // namespace app
