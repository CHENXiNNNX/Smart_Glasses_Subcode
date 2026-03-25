/*
 * rtsp_server.cc - RTSP 推流
 */

#include "rtsp_server.hpp"
#include "tool/log/log.hpp"
#include "protocol/rtsp/rtsp.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace app::media::camera
{

    using namespace tool::log;

#define TAG "Camera"

    class RtspServer::Impl
    {
    public:
        bool                running_ = false;
        uint16_t            port_    = 554;
        std::string         path_;
        rtsp_demo_handle    demo_    = nullptr;
        rtsp_session_handle session_ = nullptr;
        std::thread         thread_;
        std::atomic<bool>   stop_{false};
        std::mutex          session_mtx_;

        Error start(uint16_t port, const std::string& path);
        Error stop();
        void  send_frame(const uint8_t* data, size_t size, uint64_t pts, bool keyframe);
        void  event_loop();
    };

    Error RtspServer::Impl::start(uint16_t port, const std::string& path)
    {
        if (running_)
            return Error::BUSY;
        demo_ = rtsp_new_demo(port);
        if (!demo_)
        {
            LOG_ERROR(TAG, "RTSP: 创建服务失败");
            return Error::DEVICE_ERROR;
        }
        session_ = rtsp_new_session(demo_, path.c_str());
        if (!session_)
        {
            LOG_ERROR(TAG, "RTSP: 创建会话失败");
            rtsp_del_demo(demo_);
            demo_ = nullptr;
            return Error::DEVICE_ERROR;
        }
        rtsp_set_video(session_, RTSP_CODEC_ID_VIDEO_H264, nullptr, 0);
        port_ = port;
        path_ = path;
        stop_ = false;

        thread_  = std::thread(&Impl::event_loop, this);
        running_ = true;
        LOG_INFO(TAG, "RTSP: port=%d path=%s", port, path.c_str());
        return Error::OK;
    }

    Error RtspServer::Impl::stop()
    {
        if (!running_)
            return Error::OK;
        stop_ = true;
        if (thread_.joinable())
            thread_.join();
        {
            std::lock_guard<std::mutex> lk(session_mtx_);
            if (session_)
            {
                rtsp_del_session(session_);
                session_ = nullptr;
            }
            if (demo_)
            {
                rtsp_del_demo(demo_);
                demo_ = nullptr;
            }
        }
        LOG_INFO(TAG, "RTSP: 停止");
        running_ = false;
        return Error::OK;
    }

    void RtspServer::Impl::send_frame(const uint8_t* data, size_t size, uint64_t pts, bool keyframe)
    {
        (void)keyframe;
        if (!running_ || !data || size == 0)
            return;
        std::lock_guard<std::mutex> lk(session_mtx_);
        if (session_)
            rtsp_tx_video(session_, data, static_cast<int>(size), pts);
    }

    void RtspServer::Impl::event_loop()
    {
        while (!stop_)
        {
            rtsp_demo_handle d = nullptr;
            {
                std::lock_guard<std::mutex> lk(session_mtx_);
                d = demo_;
            }
            if (d)
                rtsp_do_event(d);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    RtspServer::RtspServer() : impl_(std::make_unique<Impl>()) {}
    RtspServer::~RtspServer()
    {
        stop();
    }
    Error RtspServer::start(uint16_t port, const std::string& path)
    {
        return impl_->start(port, path);
    }
    Error RtspServer::stop()
    {
        return impl_->stop();
    }
    bool RtspServer::is_running() const
    {
        return impl_->running_;
    }
    void RtspServer::send_frame(const uint8_t* data, size_t size, uint64_t pts, bool keyframe)
    {
        impl_->send_frame(data, size, pts, keyframe);
    }

} // namespace app::media::camera
