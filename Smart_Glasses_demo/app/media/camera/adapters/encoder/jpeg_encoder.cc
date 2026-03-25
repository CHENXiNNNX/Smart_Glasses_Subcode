/*
 * jpeg_encoder.cc - JPEG 编码器 (VI→VPSS→VENC)
 */

#include "media/camera/platform.hpp"
#include "media/camera/adapters/encoder/jpeg_encoder.hpp"
#include "media/camera/pool/frame_pool.hpp"
#include "tool/log/log.hpp"
#include "tool/time/time.hpp"
#include "tool/file/file.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace app::media::camera
{

#define TAG "Camera"

    /* JpegEncoder Impl */
    class JpegEncoder::Impl
    {
    public:
        static constexpr int CHN_ID   = 1;
        static constexpr int VI_CHN   = 1;
        static constexpr int VPSS_GRP = 1;
        static constexpr int VPSS_CHN = 0;

        JpegCfg               cfg_;
        FramePool*            pool_    = nullptr;
        bool                  init_    = false;
        bool                  running_ = false;
        bool                  bound_   = false;
        JpegCb                cb_;
        std::mutex            cb_mtx_;
        std::thread           thread_;
        std::atomic<bool>     stop_{false};
        std::atomic<uint32_t> frames_{0};
        std::atomic<uint32_t> drops_{0};
        std::atomic<bool>     save_req_{false};
        std::string           save_path_;
        PhotoCb               save_cb_;
        std::mutex            save_mtx_;

        Error init(const JpegCfg& cfg, FramePool* pool)
        {
            cfg_  = cfg;
            pool_ = pool;
#if CAM_HAS_SDK
            VENC_CHN_ATTR_S attr{};
            attr.stVencAttr.enType                    = RK_VIDEO_ID_MJPEG;
            attr.stVencAttr.enPixelFormat             = RK_FMT_YUV420SP;
            attr.stVencAttr.u32PicWidth               = cfg.width;
            attr.stVencAttr.u32PicHeight              = cfg.height;
            attr.stVencAttr.u32VirWidth               = cfg.width;
            attr.stVencAttr.u32VirHeight              = cfg.height;
            attr.stVencAttr.u32StreamBufCnt           = 2;
            attr.stVencAttr.stAttrJpege.bSupportDCF   = RK_FALSE;
            attr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
            attr.stRcAttr.enRcMode                    = VENC_RC_MODE_MJPEGFIXQP;
            attr.stRcAttr.stMjpegFixQp.u32Qfactor     = cfg.quality;

            if (RK_MPI_VENC_CreateChn(CHN_ID, &attr) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "JPEG: 创建失败");
                return Error::ENCODER_ERROR;
            }

            VENC_RECV_PIC_PARAM_S recv{};
            recv.s32RecvPicNum = -1;
            if (RK_MPI_VENC_StartRecvFrame(CHN_ID, &recv) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "JPEG: 启动接收失败");
                RK_MPI_VENC_DestroyChn(CHN_ID);
                return Error::ENCODER_ERROR;
            }

            LOG_INFO(TAG, "JPEG: %dx%d Q=%d", cfg.width, cfg.height, cfg.quality);
#else
            LOG_WARN(TAG, "JPEG: 跳过(无SDK)");
#endif
            init_ = true;
            return Error::OK;
        }

        void deinit()
        {
            stop();
            unbind();
#if CAM_HAS_SDK
            RK_MPI_VENC_StopRecvFrame(CHN_ID);
            RK_MPI_VENC_DestroyChn(CHN_ID);
#endif
            init_ = false;
        }

        Error bind()
        {
            if (bound_)
                return Error::OK;
#if CAM_HAS_SDK
            /* VI[1] -> VPSS[1] */
            MPP_CHN_S vi_src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S vpss_dst{RK_ID_VPSS, 1, VPSS_CHN};
            if (RK_MPI_SYS_Bind(&vi_src, &vpss_dst) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "JPEG: VI->VPSS 绑定失败");
                return Error::DEVICE_ERROR;
            }
            /* VPSS[1] -> VENC[1] */
            MPP_CHN_S vpss_src{RK_ID_VPSS, 1, VPSS_CHN};
            MPP_CHN_S venc_dst{RK_ID_VENC, 0, CHN_ID};
            if (RK_MPI_SYS_Bind(&vpss_src, &venc_dst) != RK_SUCCESS)
            {
                RK_MPI_SYS_UnBind(&vi_src, &vpss_dst);
                LOG_ERROR(TAG, "JPEG: VPSS->VENC 绑定失败");
                return Error::DEVICE_ERROR;
            }
            LOG_INFO(TAG, "JPEG: VI[%d]->VPSS[%d]->VENC[%d]", VI_CHN, VPSS_GRP, CHN_ID);
#endif
            bound_ = true;
            return Error::OK;
        }

        void unbind()
        {
            if (!bound_)
                return;
#if CAM_HAS_SDK
            MPP_CHN_S vpss_src{RK_ID_VPSS, 1, VPSS_CHN};
            MPP_CHN_S venc_dst{RK_ID_VENC, 0, CHN_ID};
            RK_MPI_SYS_UnBind(&vpss_src, &venc_dst);

            MPP_CHN_S vi_src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S vpss_dst{RK_ID_VPSS, 1, VPSS_CHN};
            RK_MPI_SYS_UnBind(&vi_src, &vpss_dst);
#endif
            bound_ = false;
        }

        Error start()
        {
            if (running_)
                return Error::OK;
            if (bind() != Error::OK)
                return Error::DEVICE_ERROR;
            stop_    = false;
            thread_  = std::thread(&Impl::loop, this);
            running_ = true;
            LOG_INFO(TAG, "JPEG: 启动");
            return Error::OK;
        }

        void stop()
        {
            if (!running_)
                return;
            stop_ = true;
            if (thread_.joinable())
                thread_.join();
            running_ = false;
            LOG_INFO(TAG, "JPEG: 停止");
        }

        Error save(const std::string& path, PhotoCb cb)
        {
            std::lock_guard<std::mutex> lk(save_mtx_);
            if (save_req_)
                return Error::BUSY;
            save_path_ = path;
            save_cb_   = cb;
            save_req_  = true;
            return Error::OK;
        }

        void loop()
        {
#if CAM_HAS_SDK
            VENC_STREAM_S stream{};
            stream.pstPack = static_cast<VENC_PACK_S*>(malloc(sizeof(VENC_PACK_S)));
            if (!stream.pstPack)
                return;

            while (!stop_)
            {
                RK_S32 ret = RK_MPI_VENC_GetStream(CHN_ID, &stream, 100);
                if (ret != RK_SUCCESS)
                    continue;

                if (stream.u32PackCount == 0 || !stream.pstPack)
                {
                    RK_MPI_VENC_ReleaseStream(CHN_ID, &stream);
                    continue;
                }

                size_t total = 0;
                for (RK_U32 i = 0; i < stream.u32PackCount; ++i)
                    total += stream.pstPack[i].u32Len;

                FramePtr frame = pool_ ? pool_->alloc(total) : nullptr;
                if (!frame)
                {
                    drops_++;
                    RK_MPI_VENC_ReleaseStream(CHN_ID, &stream);
                    continue;
                }

                size_t offset = 0;
                for (RK_U32 i = 0; i < stream.u32PackCount; ++i)
                {
                    void* ptr = RK_MPI_MB_Handle2VirAddr(stream.pstPack[i].pMbBlk);
                    if (ptr)
                    {
                        memcpy(frame->data + offset, ptr, stream.pstPack[i].u32Len);
                        offset += stream.pstPack[i].u32Len;
                    }
                }

                frame->size      = total;
                frame->width     = cfg_.width;
                frame->height    = cfg_.height;
                frame->pts       = stream.pstPack[0].u64PTS;
                frame->timestamp = app::tool::time::uptime_us();
                frame->keyframe  = true;

                RK_MPI_VENC_ReleaseStream(CHN_ID, &stream);
                frames_++;

                if (save_req_.exchange(false))
                {
                    std::string path;
                    PhotoCb     cb;
                    {
                        std::lock_guard<std::mutex> lk(save_mtx_);
                        path = save_path_;
                        cb   = save_cb_;
                    }
                    Error                        err = Error::OK;
                    app::tool::file::FileWrapper file(path, app::tool::file::FileMode::WRITE);
                    if (file.valid() && file.write(frame->data, frame->size))
                    {
                        file.flush();
                        LOG_INFO(TAG, "保存: %s (%u字节)", path.c_str(),
                                 static_cast<unsigned>(frame->size));
                    }
                    else
                    {
                        LOG_ERROR(TAG, "保存失败: %s", path.c_str());
                        err = Error::DEVICE_ERROR;
                    }
                    if (cb)
                        cb(path, err);
                }

                {
                    std::lock_guard<std::mutex> lk(cb_mtx_);
                    if (cb_)
                        cb_(frame);
                }
            }

            free(stream.pstPack);
#else
            while (!stop_)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
        }
    };

    JpegEncoder::JpegEncoder() : impl_(std::make_unique<Impl>()) {}
    JpegEncoder::~JpegEncoder()
    {
        deinit();
    }
    Error JpegEncoder::init(const JpegCfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void JpegEncoder::deinit()
    {
        impl_->deinit();
    }
    bool JpegEncoder::is_init() const
    {
        return impl_->init_;
    }
    Error JpegEncoder::start()
    {
        return impl_->start();
    }
    Error JpegEncoder::stop()
    {
        impl_->stop();
        return Error::OK;
    }
    bool JpegEncoder::is_running() const
    {
        return impl_->running_;
    }
    void JpegEncoder::set_cb(JpegCb cb)
    {
        std::lock_guard<std::mutex> lk(impl_->cb_mtx_);
        impl_->cb_ = std::move(cb);
    }
    Error JpegEncoder::set_quality(uint8_t quality)
    {
        impl_->cfg_.quality = quality;
        return Error::OK;
    }

    Error JpegEncoder::set_resolution(uint16_t w, uint16_t h)
    {
        if (!impl_->init_)
            return Error::NOT_INIT;
        if (w == impl_->cfg_.width && h == impl_->cfg_.height)
            return Error::OK;
#if CAM_HAS_SDK
        bool was_running = impl_->running_;
        if (was_running)
            impl_->stop();
        impl_->unbind();

        RK_MPI_VENC_StopRecvFrame(Impl::CHN_ID);
        RK_MPI_VENC_DestroyChn(Impl::CHN_ID);
        RK_MPI_VI_DisableChn(0, Impl::VI_CHN);

        VI_CHN_ATTR_S vi_attr{};
        vi_attr.stIspOpt.u32BufCount  = 3;
        vi_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
        vi_attr.stSize.u32Width       = w;
        vi_attr.stSize.u32Height      = h;
        vi_attr.enPixelFormat         = RK_FMT_YUV420SP;
        vi_attr.enCompressMode        = COMPRESS_MODE_NONE;
        vi_attr.u32Depth              = 2;

        if (RK_MPI_VI_SetChnAttr(0, Impl::VI_CHN, &vi_attr) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "JPEG: VI 通道配置失败");
            return Error::DEVICE_ERROR;
        }
        if (RK_MPI_VI_EnableChn(0, Impl::VI_CHN) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "JPEG: VI 通道启用失败");
            return Error::DEVICE_ERROR;
        }

        impl_->cfg_.width  = w;
        impl_->cfg_.height = h;

        VENC_CHN_ATTR_S attr{};
        attr.stVencAttr.enType                    = RK_VIDEO_ID_MJPEG;
        attr.stVencAttr.enPixelFormat             = RK_FMT_YUV420SP;
        attr.stVencAttr.u32PicWidth               = w;
        attr.stVencAttr.u32PicHeight              = h;
        attr.stVencAttr.u32VirWidth               = w;
        attr.stVencAttr.u32VirHeight              = h;
        attr.stVencAttr.u32StreamBufCnt           = 2;
        attr.stVencAttr.stAttrJpege.bSupportDCF   = RK_FALSE;
        attr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
        attr.stRcAttr.enRcMode                    = VENC_RC_MODE_MJPEGFIXQP;
        attr.stRcAttr.stMjpegFixQp.u32Qfactor     = impl_->cfg_.quality;

        if (RK_MPI_VENC_CreateChn(Impl::CHN_ID, &attr) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "JPEG: VENC 通道创建失败");
            return Error::ENCODER_ERROR;
        }

        VENC_RECV_PIC_PARAM_S recv{};
        recv.s32RecvPicNum = -1;
        RK_MPI_VENC_StartRecvFrame(Impl::CHN_ID, &recv);
        impl_->bind();

        LOG_INFO(TAG, "JPEG: 分辨率切换为 %dx%d", w, h);
        if (was_running)
            impl_->start();
        return Error::OK;
#else
        (void)w;
        (void)h;
        return Error::NOT_SUPPORTED;
#endif
    }

    Error JpegEncoder::save(const std::string& path, PhotoCb cb)
    {
        return impl_->save(path, cb);
    }
    const JpegCfg& JpegEncoder::cfg() const
    {
        return impl_->cfg_;
    }

} // namespace app::media::camera
