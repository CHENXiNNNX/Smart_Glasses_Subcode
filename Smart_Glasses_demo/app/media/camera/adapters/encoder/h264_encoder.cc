/*
 * h264_encoder.cc - H264 编码器 (VI→VPSS→VENC)
 */

#include "media/camera/platform.hpp"
#include "media/camera/adapters/encoder/h264_encoder.hpp"
#include "media/camera/pool/frame_pool.hpp"
#include "tool/log/log.hpp"
#include "tool/time/time.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace app::media::camera
{

#define TAG "Camera"

    /* H264Encoder Impl */
    class H264Encoder::Impl
    {
    public:
        static constexpr int CHN_ID   = 0;
        static constexpr int VI_CHN   = 0;
        static constexpr int VPSS_GRP = 0;
        static constexpr int VPSS_CHN = 0;

        H264Cfg               cfg_;
        FramePool*            pool_    = nullptr;
        bool                  init_    = false;
        bool                  running_ = false;
        bool                  bound_   = false;
        H264Cb                cb_;
        std::mutex            cb_mtx_;
        std::thread           thread_;
        std::atomic<bool>     stop_{false};
        std::atomic<uint32_t> frames_{0};
        std::atomic<uint32_t> drops_{0};
        std::chrono::steady_clock::time_point last_idr_request_{};

        Error init(const H264Cfg& cfg, FramePool* pool)
        {
            cfg_  = cfg;
            pool_ = pool;
#if CAM_HAS_SDK
            VENC_CHN_ATTR_S attr{};
            attr.stVencAttr.enType =
                (cfg.codec == Codec::H264) ? RK_VIDEO_ID_AVC : RK_VIDEO_ID_HEVC;
            attr.stVencAttr.enPixelFormat   = RK_FMT_YUV420SP;
            attr.stVencAttr.u32PicWidth     = cfg.width;
            attr.stVencAttr.u32PicHeight    = cfg.height;
            attr.stVencAttr.u32VirWidth     = cfg.width;
            attr.stVencAttr.u32VirHeight    = cfg.height;
            attr.stVencAttr.u32StreamBufCnt = 3;

            if (cfg.codec == Codec::H264)
            {
                attr.stRcAttr.enRcMode             = VENC_RC_MODE_H264CBR;
                attr.stRcAttr.stH264Cbr.u32BitRate = cfg.bitrate;
                attr.stRcAttr.stH264Cbr.u32Gop     = cfg.gop;
                attr.stVencAttr.u32Profile         = H264E_PROFILE_BASELINE;
                attr.stVencAttr.u32BufSize         = cfg.bitrate * 2 * 1024;
            }
            else
            {
                attr.stRcAttr.enRcMode             = VENC_RC_MODE_H265CBR;
                attr.stRcAttr.stH265Cbr.u32BitRate = cfg.bitrate;
                attr.stRcAttr.stH265Cbr.u32Gop     = cfg.gop;
                attr.stVencAttr.u32BufSize         = cfg.width * cfg.height * 3 / 2;
            }

            if (RK_MPI_VENC_CreateChn(CHN_ID, &attr) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "H264: 创建失败");
                return Error::ENCODER_ERROR;
            }

            VENC_RECV_PIC_PARAM_S recv{};
            recv.s32RecvPicNum = -1;
            if (RK_MPI_VENC_StartRecvFrame(CHN_ID, &recv) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "H264: 启动接收失败");
                RK_MPI_VENC_DestroyChn(CHN_ID);
                return Error::ENCODER_ERROR;
            }

            LOG_INFO(TAG, "H264: %dx%d %dfps %dkbps GOP=%d", cfg.width, cfg.height, cfg.fps,
                     cfg.bitrate, cfg.gop);
#else
            LOG_WARN(TAG, "H264: 跳过(无SDK)");
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
            /* VI[0] -> VPSS[0] */
            MPP_CHN_S vi_src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S vpss_dst{RK_ID_VPSS, 0, VPSS_CHN};
            if (RK_MPI_SYS_Bind(&vi_src, &vpss_dst) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "H264: VI->VPSS 绑定失败");
                return Error::DEVICE_ERROR;
            }
            /* VPSS[0] -> VENC[0] */
            MPP_CHN_S vpss_src{RK_ID_VPSS, 0, VPSS_CHN};
            MPP_CHN_S venc_dst{RK_ID_VENC, 0, CHN_ID};
            if (RK_MPI_SYS_Bind(&vpss_src, &venc_dst) != RK_SUCCESS)
            {
                RK_MPI_SYS_UnBind(&vi_src, &vpss_dst);
                LOG_ERROR(TAG, "H264: VPSS->VENC 绑定失败");
                return Error::DEVICE_ERROR;
            }
            LOG_INFO(TAG, "H264: VI[%d]->VPSS[%d]->VENC[%d]", VI_CHN, VPSS_GRP, CHN_ID);
#endif
            bound_ = true;
            return Error::OK;
        }

        void unbind()
        {
            if (!bound_)
                return;
#if CAM_HAS_SDK
            MPP_CHN_S vpss_src{RK_ID_VPSS, 0, VPSS_CHN};
            MPP_CHN_S venc_dst{RK_ID_VENC, 0, CHN_ID};
            RK_MPI_SYS_UnBind(&vpss_src, &venc_dst);

            MPP_CHN_S vi_src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S vpss_dst{RK_ID_VPSS, 0, VPSS_CHN};
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
            LOG_INFO(TAG, "H264: 启动");
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
            LOG_INFO(TAG, "H264: 停止");
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
                frame->keyframe  = (stream.pstPack[0].DataType.enH264EType == H264E_NALU_IDRSLICE ||
                                   stream.pstPack[0].DataType.enH265EType == H265E_NALU_IDRSLICE);

                RK_MPI_VENC_ReleaseStream(CHN_ID, &stream);
                frames_++;

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

    H264Encoder::H264Encoder() : impl_(std::make_unique<Impl>()) {}
    H264Encoder::~H264Encoder()
    {
        deinit();
    }
    Error H264Encoder::init(const H264Cfg& cfg, FramePool* pool)
    {
        return impl_->init(cfg, pool);
    }
    void H264Encoder::deinit()
    {
        impl_->deinit();
    }
    bool H264Encoder::is_init() const
    {
        return impl_->init_;
    }
    Error H264Encoder::start()
    {
        return impl_->start();
    }
    Error H264Encoder::stop()
    {
        impl_->stop();
        return Error::OK;
    }
    bool H264Encoder::is_running() const
    {
        return impl_->running_;
    }
    void H264Encoder::set_cb(H264Cb cb)
    {
        std::lock_guard<std::mutex> lk(impl_->cb_mtx_);
        impl_->cb_ = std::move(cb);
    }
    Error H264Encoder::set_bitrate(uint16_t kbps)
    {
        const uint16_t clamped = static_cast<uint16_t>(
            std::max(2u, std::min(static_cast<unsigned>(kbps), 200000u)));
        impl_->cfg_.bitrate = clamped;

#if CAM_HAS_SDK
        if (!impl_->init_)
            return Error::NOT_INIT;

        VENC_CHN_ATTR_S attr{};
        if (RK_MPI_VENC_GetChnAttr(Impl::CHN_ID, &attr) != RK_SUCCESS)
        {
            LOG_WARN(TAG, "VENC GetChnAttr失败 %ukbps", clamped);
            return Error::OK;
        }

        if (impl_->cfg_.codec == Codec::H264)
        {
            if (attr.stRcAttr.enRcMode != VENC_RC_MODE_H264CBR)
            {
                LOG_WARN(TAG, "VENC RC非H264CBR，跳过设码率");
                return Error::OK;
            }
            attr.stRcAttr.stH264Cbr.u32BitRate = clamped;
            attr.stVencAttr.u32BufSize         = static_cast<RK_U32>(clamped) * 2u * 1024u;
        }
        else
        {
            if (attr.stRcAttr.enRcMode != VENC_RC_MODE_H265CBR)
            {
                LOG_WARN(TAG, "VENC RC非H265CBR，跳过设码率");
                return Error::OK;
            }
            attr.stRcAttr.stH265Cbr.u32BitRate = clamped;
            attr.stVencAttr.u32BufSize = impl_->cfg_.width * impl_->cfg_.height * 3u / 2u;
        }

        if (RK_MPI_VENC_SetChnAttr(Impl::CHN_ID, &attr) != RK_SUCCESS)
        {
            LOG_WARN(TAG, "VENC SetChnAttr失败 %ukbps", clamped);
            return Error::DEVICE_ERROR;
        }
#endif
        return Error::OK;
    }
    Error H264Encoder::set_gop(uint8_t gop)
    {
        impl_->cfg_.gop = gop;
        return Error::OK;
    }

    Error H264Encoder::request_idr()
    {
        if (!impl_->init_ || !impl_->running_)
            return Error::NOT_INIT;
#if CAM_HAS_SDK
        using clock = std::chrono::steady_clock;
        auto now = clock::now();
        if (now - impl_->last_idr_request_ < std::chrono::milliseconds(450))
            return Error::OK;
        impl_->last_idr_request_ = now;

        RK_S32 ret = RK_MPI_VENC_RequestIDR(Impl::CHN_ID, RK_FALSE);
        if (ret != RK_SUCCESS)
            ret = RK_MPI_VENC_RequestIDR(Impl::CHN_ID, RK_TRUE);
        if (ret != RK_SUCCESS)
        {
            LOG_WARN(TAG, "VENC RequestIDR失败 %#x", ret);
            return Error::DEVICE_ERROR;
        }
#endif
        return Error::OK;
    }

    Error H264Encoder::set_resolution(uint16_t w, uint16_t h)
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
        vi_attr.u32Depth              = 0;

        if (RK_MPI_VI_SetChnAttr(0, Impl::VI_CHN, &vi_attr) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "H264: VI 通道配置失败");
            return Error::DEVICE_ERROR;
        }
        if (RK_MPI_VI_EnableChn(0, Impl::VI_CHN) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "H264: VI 通道启用失败");
            return Error::DEVICE_ERROR;
        }

        impl_->cfg_.width  = w;
        impl_->cfg_.height = h;

        VENC_CHN_ATTR_S attr{};
        attr.stVencAttr.enType =
            (impl_->cfg_.codec == Codec::H264) ? RK_VIDEO_ID_AVC : RK_VIDEO_ID_HEVC;
        attr.stVencAttr.enPixelFormat   = RK_FMT_YUV420SP;
        attr.stVencAttr.u32PicWidth     = w;
        attr.stVencAttr.u32PicHeight    = h;
        attr.stVencAttr.u32VirWidth     = w;
        attr.stVencAttr.u32VirHeight    = h;
        attr.stVencAttr.u32StreamBufCnt = 3;

        if (impl_->cfg_.codec == Codec::H264)
        {
            attr.stRcAttr.enRcMode             = VENC_RC_MODE_H264CBR;
            attr.stRcAttr.stH264Cbr.u32BitRate = impl_->cfg_.bitrate;
            attr.stRcAttr.stH264Cbr.u32Gop     = impl_->cfg_.gop;
            attr.stVencAttr.u32Profile         = H264E_PROFILE_BASELINE;
            attr.stVencAttr.u32BufSize         = impl_->cfg_.bitrate * 2 * 1024;
        }
        else
        {
            attr.stRcAttr.enRcMode             = VENC_RC_MODE_H265CBR;
            attr.stRcAttr.stH265Cbr.u32BitRate = impl_->cfg_.bitrate;
            attr.stRcAttr.stH265Cbr.u32Gop     = impl_->cfg_.gop;
            attr.stVencAttr.u32BufSize         = w * h * 3 / 2;
        }

        if (RK_MPI_VENC_CreateChn(Impl::CHN_ID, &attr) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "H264: VENC 通道创建失败");
            return Error::ENCODER_ERROR;
        }

        VENC_RECV_PIC_PARAM_S recv{};
        recv.s32RecvPicNum = -1;
        RK_MPI_VENC_StartRecvFrame(Impl::CHN_ID, &recv);
        impl_->bind();

        LOG_INFO(TAG, "H264: 分辨率切换为 %dx%d", w, h);
        if (was_running)
            impl_->start();
        return Error::OK;
#else
        (void)w;
        (void)h;
        return Error::NOT_SUPPORTED;
#endif
    }

    const H264Cfg& H264Encoder::cfg() const
    {
        return impl_->cfg_;
    }

} // namespace app::media::camera
