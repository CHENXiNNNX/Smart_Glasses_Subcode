/* camera.cc - 摄像头驱动 */

#include "camera.hpp"
#include "../sync.hpp"
#include "../media_config.hpp"
#include "../../tool/file/file.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/memory/memory.hpp"
#include "../../tool/time/time.hpp"
#include "../../protocol/http/http.hpp"
#include "../../protocol/rtsp/rtsp.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

/* RK MPI */
#if __has_include("sample_comm.h")
#define CAM_HAS_SDK 1
#include "sample_comm.h"
#elif __has_include("rkmedia/sample_comm.h")
#define CAM_HAS_SDK 1
#include "rkmedia/sample_comm.h"
#else
#define CAM_HAS_SDK 0
#endif

#if !CAM_HAS_SDK
extern "C"
{
    using RK_S32  = int;
    using RK_U32  = unsigned int;
    using RK_U64  = unsigned long long;
    using RK_BOOL = int;
    using MB_BLK  = void*;

    constexpr RK_S32  RK_SUCCESS = 0;
    constexpr RK_BOOL RK_TRUE    = 1;
    constexpr RK_BOOL RK_FALSE   = 0;

    struct VI_DEV_ATTR_S;
    struct VI_DEV_BIND_PIPE_S;
    struct VI_CHN_ATTR_S;
    struct VENC_CHN_ATTR_S;
    struct VIDEO_FRAME_INFO_S;
    struct VENC_STREAM_S;
    struct rk_aiq_sys_ctx_t;

    enum opMode_t
    {
        OP_AUTO   = 0,
        OP_MANUAL = 1
    };
    enum MOD_ID_E
    {
        RK_ID_VI   = 0,
        RK_ID_VENC = 1
    };

    struct MPP_CHN_S
    {
        MOD_ID_E enModId;
        RK_S32   s32DevId;
        RK_S32   s32ChnId;
    };

    enum RK_CODEC_ID_E
    {
        RK_VIDEO_ID_AVC   = 0,
        RK_VIDEO_ID_HEVC  = 1,
        RK_VIDEO_ID_MJPEG = 2
    };

    struct VENC_RECV_PIC_PARAM_S
    {
        RK_S32 s32RecvPicNum;
    };

    RK_S32 RK_MPI_SYS_Init()
    {
        return 0;
    }
    RK_S32 RK_MPI_SYS_Exit()
    {
        return 0;
    }
    RK_S32 RK_MPI_SYS_Bind(const MPP_CHN_S*, const MPP_CHN_S*)
    {
        return 0;
    }
    RK_S32 RK_MPI_SYS_UnBind(const MPP_CHN_S*, const MPP_CHN_S*)
    {
        return 0;
    }

    RK_S32 RK_MPI_VI_SetDevAttr(RK_S32, const VI_DEV_ATTR_S*)
    {
        return 0;
    }
    RK_S32 RK_MPI_VI_EnableDev(RK_S32)
    {
        return 0;
    }
    RK_S32 RK_MPI_VI_DisableDev(RK_S32)
    {
        return 0;
    }
    RK_S32 RK_MPI_VI_SetDevBindPipe(RK_S32, const VI_DEV_BIND_PIPE_S*)
    {
        return 0;
    }
    RK_S32 RK_MPI_VI_SetChnAttr(RK_S32, RK_S32, const VI_CHN_ATTR_S*)
    {
        return 0;
    }
    RK_S32 RK_MPI_VI_EnableChn(RK_S32, RK_S32)
    {
        return 0;
    }
    RK_S32 RK_MPI_VI_DisableChn(RK_S32, RK_S32)
    {
        return 0;
    }

    RK_S32 RK_MPI_VENC_CreateChn(RK_S32, const VENC_CHN_ATTR_S*)
    {
        return 0;
    }
    RK_S32 RK_MPI_VENC_DestroyChn(RK_S32)
    {
        return 0;
    }
    RK_S32 RK_MPI_VENC_GetStream(RK_S32, VENC_STREAM_S*, RK_S32)
    {
        return -1;
    }
    RK_S32 RK_MPI_VENC_ReleaseStream(RK_S32, VENC_STREAM_S*)
    {
        return 0;
    }
    RK_S32 RK_MPI_VENC_StartRecvFrame(RK_S32, void*)
    {
        return 0;
    }
    RK_S32 RK_MPI_VENC_StopRecvFrame(RK_S32)
    {
        return 0;
    }

    RK_S32 RK_MPI_MB_ReleaseBuffer(MB_BLK)
    {
        return 0;
    }
    void* RK_MPI_MB_Handle2VirAddr(MB_BLK)
    {
        return nullptr;
    }
}
#endif

namespace app::media::camera
{

    using namespace tool::log;
    using namespace tool::file;
    using namespace tool::time;

#define TAG "Camera"

    /*============================================================================
     * FramePool 实现
     *============================================================================*/

    class FramePool::Impl
    {
    public:
        std::unique_ptr<tool::memory::MemoryPool> pool_;
        std::mutex                                mtx_;
        size_t                                    total_ = 0;

        Error init(const MemoryCfg& cfg)
        {
            size_t pool_size = cfg.fixed_block_size * cfg.fixed_block_count + cfg.dynamic_max_size;
            pool_            = std::make_unique<tool::memory::MemoryPool>(pool_size);
            total_           = pool_size;
            LOG_INFO(TAG, "帧池: %uKB", static_cast<unsigned>(pool_size / 1024));
            return Error::OK;
        }

        void deinit()
        {
            pool_.reset();
        }

        FramePtr alloc(size_t size)
        {
            if (!pool_)
                return nullptr;

            std::lock_guard<std::mutex> lk(mtx_);
            void*                       mem = pool_->allocate(size);
            if (!mem)
                return nullptr;

            auto frame     = std::make_shared<Frame>();
            frame->data    = static_cast<uint8_t*>(mem);
            frame->size    = size;
            frame->priv    = mem;
            frame->release = [this, mem]()
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (pool_)
                    pool_->deallocate(mem);
            };

            return frame;
        }

        size_t used() const
        {
            return pool_ ? pool_->get_used_memory_fast() : 0;
        }

        size_t total() const
        {
            return total_;
        }
    };

    FramePool::FramePool() : impl_(std::make_unique<Impl>()) {}
    FramePool::~FramePool()
    {
        deinit();
    }

    Error FramePool::init(const MemoryCfg& cfg)
    {
        return impl_->init(cfg);
    }
    void FramePool::deinit()
    {
        impl_->deinit();
    }

    FramePtr FramePool::alloc(size_t size)
    {
        return impl_->alloc(size);
    }
    size_t FramePool::used() const
    {
        return impl_->used();
    }
    size_t FramePool::total() const
    {
        return impl_->total();
    }

    /*============================================================================
     * IspCtrl 实现
     *============================================================================*/

    class IspCtrl::Impl
    {
    public:
        bool              init_   = false;
        rk_aiq_sys_ctx_t* aiq_ctx = nullptr;

        Error init(const std::string& iq_dir)
        {
#if CAM_HAS_SDK
            system("RkLunch-stop.sh 2>/dev/null");

            rk_aiq_static_info_t info{};
            if (rk_aiq_uapi2_sysctl_enumStaticMetas(0, &info) != 0)
            {
                LOG_ERROR(TAG, "ISP: 枚举传感器失败");
                return Error::DEVICE_ERROR;
            }

            const char* sns = info.sensor_info.sensor_name;
            LOG_INFO(TAG, "ISP: 传感器=%s", sns);

            aiq_ctx = rk_aiq_uapi2_sysctl_init(sns, iq_dir.c_str(), nullptr, nullptr);
            if (!aiq_ctx)
            {
                LOG_ERROR(TAG, "ISP: 初始化失败");
                return Error::DEVICE_ERROR;
            }

            if (rk_aiq_uapi2_sysctl_prepare(aiq_ctx, 0, 0, RK_AIQ_WORKING_MODE_NORMAL) != 0)
            {
                LOG_ERROR(TAG, "ISP: prepare失败");
                rk_aiq_uapi2_sysctl_deinit(aiq_ctx);
                aiq_ctx = nullptr;
                return Error::DEVICE_ERROR;
            }

            if (rk_aiq_uapi2_sysctl_start(aiq_ctx) != 0)
            {
                LOG_ERROR(TAG, "ISP: start失败");
                rk_aiq_uapi2_sysctl_deinit(aiq_ctx);
                aiq_ctx = nullptr;
                return Error::DEVICE_ERROR;
            }

            init_ = true;
            LOG_INFO(TAG, "ISP: 就绪");
#else
            (void)iq_dir;
            init_ = true;
            LOG_WARN(TAG, "ISP: 跳过(无SDK)");
#endif
            return Error::OK;
        }

        void deinit()
        {
#if CAM_HAS_SDK
            if (aiq_ctx)
            {
                rk_aiq_uapi2_sysctl_stop(aiq_ctx, false);
                rk_aiq_uapi2_sysctl_deinit(aiq_ctx);
                aiq_ctx = nullptr;
            }
#endif
            init_ = false;
        }
    };

    IspCtrl::IspCtrl() : impl_(std::make_unique<Impl>()) {}
    IspCtrl::~IspCtrl()
    {
        deinit();
    }

    Error IspCtrl::init(const std::string& iq_dir)
    {
        return impl_->init(iq_dir);
    }
    void IspCtrl::deinit()
    {
        impl_->deinit();
    }
    bool IspCtrl::is_init() const
    {
        return impl_->init_;
    }

    Error IspCtrl::set_ae_mode(AeMode mode)
    {
        (void)mode;
        return Error::OK;
    }

    Error IspCtrl::set_exposure(float time_ms, float gain)
    {
        (void)time_ms;
        (void)gain;
        return Error::OK;
    }

    Error IspCtrl::lock_ae(bool lock)
    {
        (void)lock;
        return Error::OK;
    }

    Error IspCtrl::set_awb_mode(AwbMode mode)
    {
        (void)mode;
        return Error::OK;
    }

    Error IspCtrl::set_wb_gain(float r_gain, float b_gain)
    {
        (void)r_gain;
        (void)b_gain;
        return Error::OK;
    }

    Error IspCtrl::lock_awb(bool lock)
    {
        (void)lock;
        return Error::OK;
    }

    Error IspCtrl::set_brightness(uint8_t val)
    {
        (void)val;
        return Error::OK;
    }

    Error IspCtrl::set_contrast(uint8_t val)
    {
        (void)val;
        return Error::OK;
    }

    Error IspCtrl::set_saturation(uint8_t val)
    {
        (void)val;
        return Error::OK;
    }

    Error IspCtrl::set_sharpness(uint8_t val)
    {
        (void)val;
        return Error::OK;
    }

    /*============================================================================
     * H264Encoder 实现
     *============================================================================*/

    class H264Encoder::Impl
    {
    public:
        static constexpr int CHN_ID = 0;
        static constexpr int VI_CHN = 0;

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
            MPP_CHN_S src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S dst{RK_ID_VENC, 0, CHN_ID};
            if (RK_MPI_SYS_Bind(&src, &dst) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "H264: 绑定失败");
                return Error::DEVICE_ERROR;
            }
            LOG_INFO(TAG, "H264: VI[%d]->VENC[%d]", VI_CHN, CHN_ID);
#endif
            bound_ = true;
            return Error::OK;
        }

        void unbind()
        {
            if (!bound_)
                return;

#if CAM_HAS_SDK
            MPP_CHN_S src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S dst{RK_ID_VENC, 0, CHN_ID};
            RK_MPI_SYS_UnBind(&src, &dst);
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
                frame->timestamp = uptime_us();
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
        impl_->cfg_.bitrate = kbps;
        return Error::OK;
    }

    Error H264Encoder::set_gop(uint8_t gop)
    {
        impl_->cfg_.gop = gop;
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
            LOG_ERROR(TAG, "H264: VI通道配置失败");
            return Error::DEVICE_ERROR;
        }

        if (RK_MPI_VI_EnableChn(0, Impl::VI_CHN) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "H264: VI通道启用失败");
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
            LOG_ERROR(TAG, "H264: VENC通道创建失败");
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

    /*============================================================================
     * JpegEncoder 实现
     *============================================================================*/

    class JpegEncoder::Impl
    {
    public:
        static constexpr int CHN_ID = 1;
        static constexpr int VI_CHN = 1;

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

        std::atomic<bool> save_req_{false};
        std::string       save_path_;
        PhotoCb           save_cb_;
        std::mutex        save_mtx_;

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
            MPP_CHN_S src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S dst{RK_ID_VENC, 0, CHN_ID};
            if (RK_MPI_SYS_Bind(&src, &dst) != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "JPEG: 绑定失败");
                return Error::DEVICE_ERROR;
            }
            LOG_INFO(TAG, "JPEG: VI[%d]->VENC[%d]", VI_CHN, CHN_ID);
#endif
            bound_ = true;
            return Error::OK;
        }

        void unbind()
        {
            if (!bound_)
                return;

#if CAM_HAS_SDK
            MPP_CHN_S src{RK_ID_VI, 0, VI_CHN};
            MPP_CHN_S dst{RK_ID_VENC, 0, CHN_ID};
            RK_MPI_SYS_UnBind(&src, &dst);
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
                frame->timestamp = uptime_us();
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

                    Error       err = Error::OK;
                    FileWrapper file(path, FileMode::WRITE);
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
            LOG_ERROR(TAG, "JPEG: VI通道配置失败");
            return Error::DEVICE_ERROR;
        }

        if (RK_MPI_VI_EnableChn(0, Impl::VI_CHN) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "JPEG: VI通道启用失败");
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
            LOG_ERROR(TAG, "JPEG: VENC通道创建失败");
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

    /*============================================================================
     * Recorder 实现
     *============================================================================*/

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

        uint64_t file_size() const
        {
            return bytes_.load();
        }
    };

    Recorder::Recorder() : impl_(std::make_unique<Impl>()) {}
    Recorder::~Recorder()
    {
        stop();
    }

    Error Recorder::start(const std::string& path, int duration_sec)
    {
        return impl_->start(path, duration_sec);
    }
    Error Recorder::stop()
    {
        return impl_->stop();
    }
    bool Recorder::is_recording() const
    {
        return impl_->recording_;
    }
    uint32_t Recorder::duration_sec() const
    {
        return impl_->duration_sec();
    }
    uint64_t Recorder::file_size() const
    {
        return impl_->file_size();
    }
    uint32_t Recorder::frames() const
    {
        return impl_->frames_.load();
    }
    void Recorder::write_frame(const uint8_t* data, size_t size)
    {
        impl_->write_frame(data, size);
    }

    /*============================================================================
     * RtspServer 实现
     *============================================================================*/

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

        Error start(uint16_t port, const std::string& path)
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

            stop_   = false;
            thread_ = std::thread(&Impl::event_loop, this);

            running_ = true;
            LOG_INFO(TAG, "RTSP: rtsp://IP:%d%s", port, path.c_str());
            return Error::OK;
        }

        Error stop()
        {
            if (!running_)
                return Error::OK;

            stop_ = true;
            if (thread_.joinable())
                thread_.join();

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
            LOG_INFO(TAG, "RTSP: 停止");
            running_ = false;
            return Error::OK;
        }

        void send_frame(const uint8_t* data, size_t size, uint64_t pts)
        {
            if (running_ && session_)
                rtsp_tx_video(session_, data, static_cast<int>(size), pts);
        }

        void event_loop()
        {
            while (!stop_ && demo_)
            {
                rtsp_do_event(demo_);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    };

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
    void RtspServer::send_frame(const uint8_t* data, size_t size, uint64_t pts)
    {
        impl_->send_frame(data, size, pts);
    }

    /*============================================================================
     * CameraDrv 实现
     *============================================================================*/

    class CameraDrv::Impl
    {
    public:
        CameraCfg  cfg_;
        bool       init_    = false;
        bool       running_ = false;
        ErrorCb    error_cb_;
        std::mutex error_mtx_;

        std::shared_ptr<sync_context_t> sync_ctx_;

        FramePool   pool_;
        IspCtrl     isp_;
        H264Encoder h264_;
        JpegEncoder jpeg_;
        Recorder    recorder_;
        RtspServer  rtsp_;

        H264Cb     h264_cb_;
        JpegCb     jpeg_cb_;
        std::mutex h264_cb_mtx_;
        std::mutex jpeg_cb_mtx_;

        std::function<void(const FramePtr&)> webrtc_cb_;
        std::mutex                           webrtc_cb_mtx_;

        std::string explain_url_;
        std::string explain_token_;
        std::mutex  explain_mtx_;

        std::atomic<bool>       explain_pending_{false};
        std::vector<uint8_t>    explain_frame_data_;
        std::condition_variable explain_cv_;

        std::atomic<uint32_t>                 h264_frames_{0};
        std::atomic<uint32_t>                 jpeg_frames_{0};
        std::chrono::steady_clock::time_point stats_time_;

        Error init(const CameraCfg& cfg, std::shared_ptr<sync_context_t> sync_ctx)
        {
            if (init_)
                return Error::ALREADY_INIT;

            cfg_      = cfg;
            sync_ctx_ = sync_ctx;

#if CAM_HAS_SDK
            if (RK_MPI_SYS_Init() != RK_SUCCESS)
            {
                LOG_ERROR(TAG, "MPI初始化失败");
                return Error::DEVICE_ERROR;
            }
#endif

            if (isp_.init(cfg.iq_file_dir) != Error::OK)
                return Error::DEVICE_ERROR;

            if (init_vi() != Error::OK)
            {
                isp_.deinit();
                return Error::DEVICE_ERROR;
            }

            if (pool_.init(cfg.memory) != Error::OK)
            {
                deinit_vi();
                isp_.deinit();
                return Error::MEMORY_ERROR;
            }

            if (cfg.enable_h264)
            {
                if (h264_.init(cfg.h264, &pool_) != Error::OK)
                {
                    pool_.deinit();
                    deinit_vi();
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
                    deinit_vi();
                    isp_.deinit();
                    return Error::ENCODER_ERROR;
                }
            }

            stats_time_ = std::chrono::steady_clock::now();
            init_       = true;

            LOG_INFO(TAG, "初始化完成");
            return Error::OK;
        }

        void deinit()
        {
            if (!init_)
                return;

            stop();

            jpeg_.deinit();
            h264_.deinit();
            pool_.deinit();
            deinit_vi();
            isp_.deinit();

#if CAM_HAS_SDK
            RK_MPI_SYS_Exit();
#endif

            init_ = false;
            LOG_INFO(TAG, "已释放");
        }

        Error init_vi()
        {
#if CAM_HAS_SDK
            VI_DEV_ATTR_S dev_attr{};
            if (RK_MPI_VI_SetDevAttr(0, &dev_attr) != RK_SUCCESS)
                return Error::DEVICE_ERROR;

            if (RK_MPI_VI_EnableDev(0) != RK_SUCCESS)
                return Error::DEVICE_ERROR;

            VI_DEV_BIND_PIPE_S bind{};
            bind.u32Num    = 1;
            bind.PipeId[0] = 0;
            if (RK_MPI_VI_SetDevBindPipe(0, &bind) != RK_SUCCESS)
                return Error::DEVICE_ERROR;

            VI_CHN_ATTR_S chn0{};
            chn0.stIspOpt.u32BufCount  = 3;
            chn0.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
            chn0.stSize.u32Width       = cfg_.h264.width;
            chn0.stSize.u32Height      = cfg_.h264.height;
            chn0.enPixelFormat         = RK_FMT_YUV420SP;
            chn0.enCompressMode        = COMPRESS_MODE_NONE;
            chn0.u32Depth              = 0;

            if (RK_MPI_VI_SetChnAttr(0, 0, &chn0) != RK_SUCCESS)
                return Error::DEVICE_ERROR;
            if (RK_MPI_VI_EnableChn(0, 0) != RK_SUCCESS)
                return Error::DEVICE_ERROR;

            LOG_INFO(TAG, "VI[0]: %dx%d", cfg_.h264.width, cfg_.h264.height);

            VI_CHN_ATTR_S chn1{};
            chn1.stIspOpt.u32BufCount  = 3;
            chn1.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
            chn1.stSize.u32Width       = cfg_.jpeg.width;
            chn1.stSize.u32Height      = cfg_.jpeg.height;
            chn1.enPixelFormat         = RK_FMT_YUV420SP;
            chn1.enCompressMode        = COMPRESS_MODE_NONE;
            chn1.u32Depth              = 2;

            if (RK_MPI_VI_SetChnAttr(0, 1, &chn1) != RK_SUCCESS)
                return Error::DEVICE_ERROR;
            if (RK_MPI_VI_EnableChn(0, 1) != RK_SUCCESS)
                return Error::DEVICE_ERROR;

            LOG_INFO(TAG, "VI[1]: %dx%d", cfg_.jpeg.width, cfg_.jpeg.height);
#else
            LOG_WARN(TAG, "VI: 跳过(无SDK)");
#endif
            return Error::OK;
        }

        void deinit_vi()
        {
#if CAM_HAS_SDK
            RK_MPI_VI_DisableChn(0, 1);
            RK_MPI_VI_DisableChn(0, 0);
            RK_MPI_VI_DisableDev(0);
#endif
        }

        Error start()
        {
            if (running_)
                return Error::OK;

            if (cfg_.enable_h264)
            {
                h264_.set_cb(
                    [this](const FramePtr& f)
                    {
                        h264_frames_++;

                        if (sync_ctx_)
                            f->timestamp = sync_get_timestamp(sync_ctx_.get(), f->pts, false);

                        if (recorder_.is_recording())
                            recorder_.write_frame(f->data, f->size);

                        if (rtsp_.is_running())
                            rtsp_.send_frame(f->data, f->size, f->pts);

                        {
                            std::lock_guard<std::mutex> lk(webrtc_cb_mtx_);
                            if (webrtc_cb_)
                                webrtc_cb_(f);
                        }

                        {
                            std::lock_guard<std::mutex> lk(h264_cb_mtx_);
                            if (h264_cb_)
                                h264_cb_(f);
                        }
                    });

                if (h264_.start() != Error::OK)
                    return Error::ENCODER_ERROR;
            }

            if (cfg_.enable_jpeg)
            {
                jpeg_.set_cb(
                    [this](const FramePtr& f)
                    {
                        jpeg_frames_++;

                        if (sync_ctx_)
                            f->timestamp = sync_get_timestamp(sync_ctx_.get(), f->pts, false);

                        if (explain_pending_.exchange(false))
                        {
                            std::lock_guard<std::mutex> lk(explain_mtx_);
                            explain_frame_data_.assign(f->data, f->data + f->size);
                            explain_cv_.notify_one();
                        }

                        {
                            std::lock_guard<std::mutex> lk(jpeg_cb_mtx_);
                            if (jpeg_cb_)
                                jpeg_cb_(f);
                        }
                    });

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
            LOG_DEBUG(TAG, "H264: 暂停");
        }

        void resume_h264()
        {
            if (!running_ || !cfg_.enable_h264 || h264_.is_running())
                return;
            if (h264_.start() != Error::OK)
                LOG_ERROR(TAG, "H264: 恢复失败");
            else
                LOG_DEBUG(TAG, "H264: 恢复");
        }

        Stats stats() const
        {
            Stats s{};

            auto now = std::chrono::steady_clock::now();
            auto elapsed =
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
            if (explain_url_.empty())
                return R"({"success":false,"message":"AI URL未设置"})";

            if (!jpeg_.is_running())
                return R"({"success":false,"message":"JPEG编码器未运行"})";

            explain_pending_ = true;
            {
                std::unique_lock<std::mutex> lk(explain_mtx_);
                explain_frame_data_.clear();
                bool ok = explain_cv_.wait_for(lk, std::chrono::seconds(5),
                                               [this] { return !explain_frame_data_.empty(); });

                if (!ok)
                {
                    explain_pending_ = false;
                    return R"({"success":false,"message":"获取JPEG超时"})";
                }
            }

            protocol::http::HttpClient         http_client;
            std::map<std::string, std::string> form_fields{{"question", question}};
            std::map<std::string, std::string> headers;
            if (!explain_token_.empty())
                headers["Authorization"] = "Bearer " + explain_token_;

            auto response = http_client.postMultipart(
                explain_url_, form_fields, "file", explain_frame_data_.data(),
                explain_frame_data_.size(), "camera.jpg", "image/jpeg", headers, 30000, true);

            if (!response.success)
                return R"({"success":false,"message":")" + response.error_message + R"("})";

            return response.body;
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

    void CameraDrv::set_error_cb(ErrorCb cb)
    {
        std::lock_guard<std::mutex> lk(impl_->error_mtx_);
        impl_->error_cb_ = std::move(cb);
    }

    void CameraDrv::set_webrtc_cb(std::function<void(const FramePtr&)> cb)
    {
        std::lock_guard<std::mutex> lk(impl_->webrtc_cb_mtx_);
        impl_->webrtc_cb_ = std::move(cb);
    }

    void CameraDrv::set_explain_url(const std::string& url, const std::string& token)
    {
        std::lock_guard<std::mutex> lk(impl_->explain_mtx_);
        impl_->explain_url_   = url;
        impl_->explain_token_ = token;
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
