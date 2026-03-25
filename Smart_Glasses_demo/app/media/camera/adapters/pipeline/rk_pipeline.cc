/*
 * rk_pipeline.cc - RK VI+VPSS 管道
 */

#include "media/camera/adapters/pipeline/pipeline_interface.hpp"
#include "media/camera/platform.hpp"
#include "media/camera/adapters/isp/isp_ctrl.hpp"
#include "tool/log/log.hpp"

#include <cstring>

namespace app::media::camera
{

    using namespace tool::log;

#define TAG "Camera"

    class RkPipeline : public IRawPipeline
    {
    public:
        Error init(const PipelineConfig& cfg, IspCtrl* isp) override;
        void  deinit() override;

    private:
        Error init_vi(const PipelineConfig& cfg);
        void  deinit_vi();
        Error init_vpss(const PipelineConfig& cfg, IspCtrl* isp);
        void  deinit_vpss();
    };

#if CAM_HAS_SDK
    static RK_S32 aiisp_ainr_callback(RK_VOID* pAinrParam, RK_VOID* pPrivateData)
    {
        if (!pAinrParam || !pPrivateData)
            return -1;
        auto* ctx = static_cast<rk_aiq_sys_ctx_t*>(pPrivateData);
        memset(pAinrParam, 0, sizeof(rk_ainr_param));
        int ret = rk_aiq_uapi2_sysctl_getAinrParams(ctx, static_cast<rk_ainr_param*>(pAinrParam));
        return (ret == 0) ? RK_SUCCESS : -1;
    }
#endif

    Error RkPipeline::init(const PipelineConfig& cfg, IspCtrl* isp)
    {
#if CAM_HAS_SDK
        if (RK_MPI_SYS_Init() != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "MPI 初始化失败");
            return Error::DEVICE_ERROR;
        }
#endif

        Error err = init_vi(cfg);
        if (err != Error::OK)
            return err;

        err = init_vpss(cfg, isp);
        if (err != Error::OK)
        {
            deinit_vi();
            return err;
        }

        return Error::OK;
    }

    void RkPipeline::deinit()
    {
        deinit_vpss();
        deinit_vi();
#if CAM_HAS_SDK
        RK_MPI_SYS_Exit();
#endif
    }

    Error RkPipeline::init_vi(const PipelineConfig& cfg)
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
        chn0.stSize.u32Width       = cfg.h264_width;
        chn0.stSize.u32Height      = cfg.h264_height;
        chn0.enPixelFormat         = RK_FMT_YUV420SP;
        chn0.enCompressMode        = COMPRESS_MODE_NONE;
        chn0.u32Depth              = 0;

        if (RK_MPI_VI_SetChnAttr(0, 0, &chn0) != RK_SUCCESS)
            return Error::DEVICE_ERROR;
        if (RK_MPI_VI_EnableChn(0, 0) != RK_SUCCESS)
            return Error::DEVICE_ERROR;

        LOG_INFO(TAG, "VI[0]: %dx%d", cfg.h264_width, cfg.h264_height);

        VI_CHN_ATTR_S chn1{};
        chn1.stIspOpt.u32BufCount  = 3;
        chn1.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
        chn1.stSize.u32Width       = cfg.jpeg_width;
        chn1.stSize.u32Height      = cfg.jpeg_height;
        chn1.enPixelFormat         = RK_FMT_YUV420SP;
        chn1.enCompressMode        = COMPRESS_MODE_NONE;
        chn1.u32Depth              = 2;

        if (RK_MPI_VI_SetChnAttr(0, 1, &chn1) != RK_SUCCESS)
            return Error::DEVICE_ERROR;
        if (RK_MPI_VI_EnableChn(0, 1) != RK_SUCCESS)
            return Error::DEVICE_ERROR;

        LOG_INFO(TAG, "VI[1]: %dx%d", cfg.jpeg_width, cfg.jpeg_height);
#else
        (void)cfg;
        LOG_WARN(TAG, "VI: 跳过(无SDK)");
#endif
        return Error::OK;
    }

    void RkPipeline::deinit_vi()
    {
#if CAM_HAS_SDK
        RK_MPI_VI_DisableChn(0, 1);
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
#endif
    }

    Error RkPipeline::init_vpss(const PipelineConfig& cfg, IspCtrl* isp)
    {
#if CAM_HAS_SDK
        VPSS_GRP_ATTR_S grp0{};
        grp0.u32MaxW                     = 4096;
        grp0.u32MaxH                     = 4096;
        grp0.enPixelFormat               = RK_FMT_YUV420SP;
        grp0.stFrameRate.s32SrcFrameRate = -1;
        grp0.stFrameRate.s32DstFrameRate = -1;
        grp0.enCompressMode              = COMPRESS_MODE_NONE;

        if (RK_MPI_VPSS_CreateGrp(0, &grp0) != RK_SUCCESS)
        {
            LOG_ERROR(TAG, "VPSS[0]: 创建失败");
            return Error::DEVICE_ERROR;
        }

        if (cfg.enable_aiisp && isp)
        {
            void* ctx = isp->aiq_ctx();
            if (ctx)
            {
                AIISP_ATTR_S aiisp{};
                aiisp.bEnable                          = RK_TRUE;
                aiisp.stAiIspCallback.pfUpdateCallback = (AIISP_CALLBACK)aiisp_ainr_callback;
                aiisp.stAiIspCallback.pPrivateData     = ctx;
                aiisp.pModelFilePath                   = cfg.aiisp_model_path.c_str();
                aiisp.u32FrameBufCnt                   = cfg.aiisp_frame_buf_cnt;

                if (RK_MPI_VPSS_SetGrpAIISPAttr(0, &aiisp) != RK_SUCCESS)
                    LOG_WARN(TAG, "VPSS[0]: AIISP 配置失败，将无降噪");
                else
                    LOG_INFO(TAG, "VPSS[0]: AIISP 降噪已启用");
            }
        }

        VPSS_CHN_ATTR_S chn0{};
        chn0.enChnMode                   = VPSS_CHN_MODE_USER;
        chn0.enDynamicRange              = DYNAMIC_RANGE_SDR8;
        chn0.enPixelFormat               = RK_FMT_YUV420SP;
        chn0.stFrameRate.s32SrcFrameRate = -1;
        chn0.stFrameRate.s32DstFrameRate = -1;
        chn0.u32Width                    = cfg.h264_width;
        chn0.u32Height                   = cfg.h264_height;
        chn0.enCompressMode              = COMPRESS_MODE_NONE;

        if (RK_MPI_VPSS_SetChnAttr(0, 0, &chn0) != RK_SUCCESS ||
            RK_MPI_VPSS_EnableChn(0, 0) != RK_SUCCESS || RK_MPI_VPSS_StartGrp(0) != RK_SUCCESS)
        {
            RK_MPI_VPSS_DestroyGrp(0);
            LOG_ERROR(TAG, "VPSS[0]: 配置失败");
            return Error::DEVICE_ERROR;
        }

        LOG_INFO(TAG, "VPSS[0]: %dx%d", cfg.h264_width, cfg.h264_height);

        VPSS_GRP_ATTR_S grp1{};
        grp1.u32MaxW                     = 4096;
        grp1.u32MaxH                     = 4096;
        grp1.enPixelFormat               = RK_FMT_YUV420SP;
        grp1.stFrameRate.s32SrcFrameRate = -1;
        grp1.stFrameRate.s32DstFrameRate = -1;
        grp1.enCompressMode              = COMPRESS_MODE_NONE;

        if (RK_MPI_VPSS_CreateGrp(1, &grp1) != RK_SUCCESS)
        {
            RK_MPI_VPSS_StopGrp(0);
            RK_MPI_VPSS_DisableChn(0, 0);
            RK_MPI_VPSS_DestroyGrp(0);
            LOG_ERROR(TAG, "VPSS[1]: 创建失败");
            return Error::DEVICE_ERROR;
        }

        if (cfg.enable_aiisp && isp && isp->aiq_ctx())
        {
            AIISP_ATTR_S aiisp{};
            aiisp.bEnable                          = RK_TRUE;
            aiisp.stAiIspCallback.pfUpdateCallback = (AIISP_CALLBACK)aiisp_ainr_callback;
            aiisp.stAiIspCallback.pPrivateData     = isp->aiq_ctx();
            aiisp.pModelFilePath                   = cfg.aiisp_model_path.c_str();
            aiisp.u32FrameBufCnt                   = cfg.aiisp_frame_buf_cnt;
            if (RK_MPI_VPSS_SetGrpAIISPAttr(1, &aiisp) != RK_SUCCESS)
                LOG_WARN(TAG, "VPSS[1]: AIISP 配置失败");
        }

        VPSS_CHN_ATTR_S chn1{};
        chn1.enChnMode                   = VPSS_CHN_MODE_USER;
        chn1.enDynamicRange              = DYNAMIC_RANGE_SDR8;
        chn1.enPixelFormat               = RK_FMT_YUV420SP;
        chn1.stFrameRate.s32SrcFrameRate = -1;
        chn1.stFrameRate.s32DstFrameRate = -1;
        chn1.u32Width                    = cfg.jpeg_width;
        chn1.u32Height                   = cfg.jpeg_height;
        chn1.enCompressMode              = COMPRESS_MODE_NONE;

        if (RK_MPI_VPSS_SetChnAttr(1, 0, &chn1) != RK_SUCCESS ||
            RK_MPI_VPSS_EnableChn(1, 0) != RK_SUCCESS || RK_MPI_VPSS_StartGrp(1) != RK_SUCCESS)
        {
            RK_MPI_VPSS_StopGrp(1);
            RK_MPI_VPSS_DisableChn(1, 0);
            RK_MPI_VPSS_DestroyGrp(1);
            RK_MPI_VPSS_StopGrp(0);
            RK_MPI_VPSS_DisableChn(0, 0);
            RK_MPI_VPSS_DestroyGrp(0);
            LOG_ERROR(TAG, "VPSS[1]: 配置失败");
            return Error::DEVICE_ERROR;
        }

        LOG_INFO(TAG, "VPSS[1]: %dx%d", cfg.jpeg_width, cfg.jpeg_height);
#else
        (void)cfg;
        (void)isp;
        LOG_WARN(TAG, "VPSS: 跳过(无SDK)");
#endif
        return Error::OK;
    }

    void RkPipeline::deinit_vpss()
    {
#if CAM_HAS_SDK
        RK_MPI_VPSS_StopGrp(1);
        RK_MPI_VPSS_DisableChn(1, 0);
        RK_MPI_VPSS_DestroyGrp(1);

        RK_MPI_VPSS_StopGrp(0);
        RK_MPI_VPSS_DisableChn(0, 0);
        RK_MPI_VPSS_DestroyGrp(0);
#endif
    }

    std::unique_ptr<IRawPipeline> create_rk_pipeline()
    {
        return std::make_unique<RkPipeline>();
    }

} // namespace app::media::camera
