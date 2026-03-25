/*
 * camera.cc - 摄像头驱动
 */

#include "camera.hpp"
#include "domain/frame_dispatcher.hpp"
#include "adapters/pipeline/pipeline_interface.hpp"
#include "tool/log/log.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>

namespace app::media::camera
{

    using namespace tool::log;

#define TAG "Camera"

    class CameraDrv::Impl
    {
    public:
        CameraCfg cfg_;
        bool      init_    = false;
        bool      running_ = false;

        std::shared_ptr<sync_context_t> sync_ctx_;

        std::unique_ptr<IRawPipeline> pipeline_;
        FramePool                     pool_;
        IspCtrl                       isp_;
        H264Encoder                   h264_;
        JpegEncoder                   jpeg_;
        Recorder                      recorder_;
        RtspServer                    rtsp_;
        ExplainService                explain_;

        domain::FrameDispatcher dispatcher_;

        H264Cb     h264_cb_;
        JpegCb     jpeg_cb_;
        std::mutex h264_cb_mtx_;
        std::mutex jpeg_cb_mtx_;

        using H264SinkRaw = std::function<void(const uint8_t*, size_t, uint64_t, bool)>;
        H264SinkRaw webrtc_sink_;
        std::mutex  webrtc_sink_mtx_;

        std::atomic<uint32_t>                 h264_frames_{0};
        std::atomic<uint32_t>                 jpeg_frames_{0};
        std::chrono::steady_clock::time_point stats_time_;

        Error init(const CameraCfg& cfg, std::shared_ptr<sync_context_t> sync_ctx)
        {
            if (init_)
                return Error::ALREADY_INIT;

            cfg_      = cfg;
            sync_ctx_ = sync_ctx;

            if (isp_.init(cfg.iq_file_dir) != Error::OK)
                return Error::DEVICE_ERROR;

            pipeline_ = create_rk_pipeline();
            PipelineConfig pipe_cfg;
            pipe_cfg.h264_width          = cfg.h264.width;
            pipe_cfg.h264_height         = cfg.h264.height;
            pipe_cfg.jpeg_width          = cfg.jpeg.width;
            pipe_cfg.jpeg_height         = cfg.jpeg.height;
            pipe_cfg.enable_h264         = cfg.enable_h264;
            pipe_cfg.jpeg_dst_fps        = cfg.jpeg_dst_fps;
            pipe_cfg.enable_aiisp        = cfg.vpss_aiisp.enable_aiisp;
            pipe_cfg.aiisp_model_path    = cfg.vpss_aiisp.aiisp_model_path;
            pipe_cfg.aiisp_frame_buf_cnt = cfg.vpss_aiisp.aiisp_frame_buf_cnt;

            if (pipeline_->init(pipe_cfg, &isp_) != Error::OK)
            {
                isp_.deinit();
                return Error::DEVICE_ERROR;
            }

            if (pool_.init(cfg.memory) != Error::OK)
            {
                pipeline_->deinit();
                isp_.deinit();
                return Error::MEMORY_ERROR;
            }

            if (cfg.enable_h264)
            {
                if (h264_.init(cfg.h264, &pool_) != Error::OK)
                {
                    pool_.deinit();
                    pipeline_->deinit();
                    isp_.deinit();
                    return Error::ENCODER_ERROR;
                }
            }

            if (cfg.enable_jpeg)
            {
                if (jpeg_.init(cfg.jpeg, &pool_) != Error::OK)
                {
                    h264_.deinit();
                    pool_.deinit();
                    pipeline_->deinit();
                    isp_.deinit();
                    return Error::ENCODER_ERROR;
                }
            }

            stats_time_ = std::chrono::steady_clock::now();
            init_       = true;

            LOG_INFO(TAG, "初始化完成 (VPSS+AIISP降噪)");
            return Error::OK;
        }

        void deinit()
        {
            if (!init_)
                return;

            stop();
            dispatcher_.remove_all();

            jpeg_.deinit();
            h264_.deinit();
            pool_.deinit();
            pipeline_->deinit();
            pipeline_.reset();
            isp_.deinit();

            init_ = false;
            LOG_INFO(TAG, "已释放");
        }

        Error start()
        {
            if (running_)
                return Error::OK;

            dispatcher_.remove_all();

            if (cfg_.enable_h264)
            {
                dispatcher_.add_h264_sink(
                    [this](const uint8_t* data, size_t size, uint64_t pts, bool keyframe)
                    {
                        h264_frames_++;
                        if (recorder_.is_recording())
                            recorder_.write_frame(data, size);
                        if (rtsp_.is_running())
                            rtsp_.send_frame(data, size, pts, keyframe);
                        {
                            std::lock_guard<std::mutex> lk(webrtc_sink_mtx_);
                            if (webrtc_sink_)
                                webrtc_sink_(data, size, pts, keyframe);
                        }
                        /* h264_cb 需 FramePtr，从 pool 分配拷贝 */
                        std::function<void(const FramePtr&)> h264_cb;
                        {
                            std::lock_guard<std::mutex> lk(h264_cb_mtx_);
                            h264_cb = h264_cb_;
                        }
                        if (h264_cb)
                        {
                            FramePtr f = pool_.alloc(size);
                            if (f && f->data)
                            {
                                std::memcpy(f->data, data, size);
                                f->size     = size;
                                f->pts      = pts;
                                f->keyframe = keyframe;
                                if (sync_ctx_)
                                    f->timestamp = sync_get_timestamp(sync_ctx_.get(), pts, false);
                                h264_cb(f);
                            }
                        }
                    });
                h264_.set_cb([this](const FramePtr& f) { dispatcher_.dispatch_h264(f); });

                if (h264_.start() != Error::OK)
                    return Error::ENCODER_ERROR;
            }

            if (cfg_.enable_jpeg)
            {
                dispatcher_.add_jpeg_sink(
                    [this](const FramePtr& f)
                    {
                        jpeg_frames_++;
                        if (sync_ctx_)
                            f->timestamp = sync_get_timestamp(sync_ctx_.get(), f->pts, false);
                        explain_.feed_frame(f->data, f->size);
                        {
                            std::lock_guard<std::mutex> lk(jpeg_cb_mtx_);
                            if (jpeg_cb_)
                                jpeg_cb_(f);
                        }
                    });
                jpeg_.set_cb([this](const FramePtr& f) { dispatcher_.dispatch_jpeg(f); });

                if (jpeg_.start() != Error::OK)
                {
                    h264_.stop();
                    return Error::ENCODER_ERROR;
                }
            }

            running_ = true;
            LOG_INFO(TAG, "启动");
            return Error::OK;
        }

        void stop()
        {
            if (!running_)
                return;
            recorder_.stop();
            jpeg_.stop();
            h264_.stop();
            running_ = false;
            LOG_INFO(TAG, "停止");
        }

        void pause_h264()
        {
            if (!running_ || !cfg_.enable_h264 || !h264_.is_running())
                return;
            h264_.stop();
        }

        void resume_h264()
        {
            if (!running_ || !cfg_.enable_h264 || h264_.is_running())
                return;
            if (h264_.start() != Error::OK)
                LOG_ERROR(TAG, "H264: 恢复失败");
        }

        Stats stats() const
        {
            Stats s{};
            auto  now = std::chrono::steady_clock::now();
            auto  elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - stats_time_).count();
            if (elapsed > 0)
            {
                s.h264_fps = h264_frames_.load() * 1000.0f / elapsed;
                s.jpeg_fps = jpeg_frames_.load() * 1000.0f / elapsed;
            }
            s.h264_frames   = h264_frames_.load();
            s.jpeg_frames   = jpeg_frames_.load();
            s.record_frames = recorder_.frames();
            s.record_sec    = recorder_.duration_sec();
            s.mem_used      = pool_.used();
            s.mem_total     = pool_.total();
            return s;
        }

        void reset_stats()
        {
            h264_frames_ = 0;
            jpeg_frames_ = 0;
            stats_time_  = std::chrono::steady_clock::now();
        }

        std::string explain_image_impl(const std::string& question)
        {
            return explain_.explain(question, &jpeg_);
        }
    };

    CameraDrv::CameraDrv() : impl_(std::make_unique<Impl>()) {}
    CameraDrv::~CameraDrv()
    {
        deinit();
    }

    Error CameraDrv::init(const CameraCfg& cfg, std::shared_ptr<sync_context_t> sync_ctx)
    {
        return impl_->init(cfg, sync_ctx);
    }
    void CameraDrv::deinit()
    {
        impl_->deinit();
    }
    bool CameraDrv::is_init() const
    {
        return impl_->init_;
    }
    Error CameraDrv::start()
    {
        return impl_->start();
    }
    Error CameraDrv::stop()
    {
        impl_->stop();
        return Error::OK;
    }
    bool CameraDrv::is_running() const
    {
        return impl_->running_;
    }
    void CameraDrv::pause_h264()
    {
        impl_->pause_h264();
    }
    void CameraDrv::resume_h264()
    {
        impl_->resume_h264();
    }

    H264Encoder& CameraDrv::h264()
    {
        return impl_->h264_;
    }
    JpegEncoder& CameraDrv::jpeg()
    {
        return impl_->jpeg_;
    }
    Recorder& CameraDrv::recorder()
    {
        return impl_->recorder_;
    }
    IspCtrl& CameraDrv::isp()
    {
        return impl_->isp_;
    }
    RtspServer& CameraDrv::rtsp()
    {
        return impl_->rtsp_;
    }

    void CameraDrv::set_h264_cb(H264Cb cb)
    {
        std::lock_guard<std::mutex> lk(impl_->h264_cb_mtx_);
        impl_->h264_cb_ = std::move(cb);
    }
    void CameraDrv::set_jpeg_cb(JpegCb cb)
    {
        std::lock_guard<std::mutex> lk(impl_->jpeg_cb_mtx_);
        impl_->jpeg_cb_ = std::move(cb);
    }
    void
    CameraDrv::set_webrtc_sink(std::function<void(const uint8_t*, size_t, uint64_t, bool)> sink)
    {
        std::lock_guard<std::mutex> lk(impl_->webrtc_sink_mtx_);
        impl_->webrtc_sink_ = std::move(sink);
    }
    void CameraDrv::set_explain_url(const std::string& url, const std::string& token)
    {
        impl_->explain_.set_url(url, token);
    }
    std::string CameraDrv::explain_image(const std::string& question)
    {
        return impl_->explain_image_impl(question);
    }
    Stats CameraDrv::stats() const
    {
        return impl_->stats();
    }
    void CameraDrv::reset_stats()
    {
        impl_->reset_stats();
    }
    const CameraCfg& CameraDrv::cfg() const
    {
        return impl_->cfg_;
    }

} // namespace app::media::camera
