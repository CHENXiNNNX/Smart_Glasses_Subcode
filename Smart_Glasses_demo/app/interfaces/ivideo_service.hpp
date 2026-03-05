/*
 * ivideo_service.hpp - 视频服务接口
 */

#pragma once

#include <string>

namespace app
{

    enum class VideoMainState
    {
        NONE,
        PHOTO,
        RECORD
    };

    struct VideoError
    {
        int         code; // 0=成功
        std::string message;
    };

    class IVideoService
    {
    public:
        virtual ~IVideoService() = default;

        // AI 识图
        virtual void setExplainUrl(const std::string& url, const std::string& token = "") = 0;
        virtual std::string explainImage(const std::string& question)                     = 0;

        // 拍照
        virtual VideoError takePhoto(const std::string& filename, bool with_explain = false) = 0;
        virtual bool       isPhotoCapturing() const                                          = 0;

        // 录像
        virtual VideoError startRecord(const std::string& filename, int duration_sec = 0) = 0;
        virtual VideoError stopRecord()                                                   = 0;
        virtual bool       isRecording() const                                            = 0;

        // 流控制
        virtual bool           isStreaming() const                = 0;
        virtual VideoError     startStream()                      = 0;
        virtual VideoMainState getMainState() const               = 0;
        virtual VideoError     setMainState(VideoMainState state) = 0;
    };

} // namespace app
