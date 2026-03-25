/*
 * camera.hpp - 摄像头驱动
 */

#pragma once

#include "types.hpp"
#include "pool/frame_pool.hpp"
#include "adapters/isp/isp_ctrl.hpp"
#include "adapters/encoder/h264_encoder.hpp"
#include "adapters/encoder/jpeg_encoder.hpp"
#include "recorder.hpp"
#include "rtsp_server.hpp"
#include "explain/explain_service.hpp"

#include "../sync.hpp"

#include <functional>
#include <memory>
#include <string>

namespace app::media::camera
{

    class CameraDrv
    {
    public:
        CameraDrv();
        ~CameraDrv();

        CameraDrv(const CameraDrv&)            = delete;
        CameraDrv& operator=(const CameraDrv&) = delete;

        Error init(const CameraCfg& cfg, std::shared_ptr<sync_context_t> sync_ctx = nullptr);
        void  deinit();
        bool  is_init() const;

        Error start();
        Error stop();
        bool  is_running() const;

        void pause_h264();
        void resume_h264();

        H264Encoder& h264();
        JpegEncoder& jpeg();
        Recorder&    recorder();
        IspCtrl&     isp();
        RtspServer&  rtsp();

        void set_h264_cb(H264Cb cb);
        void set_jpeg_cb(JpegCb cb);
        void set_webrtc_sink(std::function<void(const uint8_t*, size_t, uint64_t, bool)> sink);

        void        set_explain_url(const std::string& url, const std::string& token = "");
        std::string explain_image(const std::string& question);

        Stats stats() const;
        void  reset_stats();

        const CameraCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
