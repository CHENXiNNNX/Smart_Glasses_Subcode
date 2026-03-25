/*
 * platform.hpp - RK 平台检测与桩
 */

#pragma once

#if __has_include("sample_comm.h")
#define CAM_HAS_SDK 1
#include "sample_comm.h"
#include "rk_mpi_vpss.h"
#include <rk_aiq_user_api2_sysctl.h>
#include <rk_aiq_user_api2_imgproc.h>
#elif __has_include("rkmedia/sample_comm.h")
#define CAM_HAS_SDK 1
#include "rkmedia/sample_comm.h"
#include "rk_mpi_vpss.h"
#include <rk_aiq_user_api2_sysctl.h>
#include <rk_aiq_user_api2_imgproc.h>
#else
#define CAM_HAS_SDK 0

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
        RK_ID_VENC = 1,
        RK_ID_VPSS = 2
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

    RK_S32 RK_MPI_SYS_Init();
    RK_S32 RK_MPI_SYS_Exit();
    RK_S32 RK_MPI_SYS_Bind(const MPP_CHN_S*, const MPP_CHN_S*);
    RK_S32 RK_MPI_SYS_UnBind(const MPP_CHN_S*, const MPP_CHN_S*);

    RK_S32 RK_MPI_VI_SetDevAttr(RK_S32, const VI_DEV_ATTR_S*);
    RK_S32 RK_MPI_VI_EnableDev(RK_S32);
    RK_S32 RK_MPI_VI_DisableDev(RK_S32);
    RK_S32 RK_MPI_VI_SetDevBindPipe(RK_S32, const VI_DEV_BIND_PIPE_S*);
    RK_S32 RK_MPI_VI_SetChnAttr(RK_S32, RK_S32, const VI_CHN_ATTR_S*);
    RK_S32 RK_MPI_VI_EnableChn(RK_S32, RK_S32);
    RK_S32 RK_MPI_VI_DisableChn(RK_S32, RK_S32);

    RK_S32 RK_MPI_VPSS_CreateGrp(RK_S32, const void*);
    RK_S32 RK_MPI_VPSS_DestroyGrp(RK_S32);
    RK_S32 RK_MPI_VPSS_SetChnAttr(RK_S32, RK_S32, const void*);
    RK_S32 RK_MPI_VPSS_EnableChn(RK_S32, RK_S32);
    RK_S32 RK_MPI_VPSS_DisableChn(RK_S32, RK_S32);
    RK_S32 RK_MPI_VPSS_SetGrpAIISPAttr(RK_S32, const void*);
    RK_S32 RK_MPI_VPSS_StartGrp(RK_S32);
    RK_S32 RK_MPI_VPSS_StopGrp(RK_S32);

    RK_S32 RK_MPI_VENC_CreateChn(RK_S32, const VENC_CHN_ATTR_S*);
    RK_S32 RK_MPI_VENC_GetChnAttr(RK_S32, VENC_CHN_ATTR_S*);
    RK_S32 RK_MPI_VENC_SetChnAttr(RK_S32, const VENC_CHN_ATTR_S*);
    RK_S32 RK_MPI_VENC_DestroyChn(RK_S32);
    RK_S32 RK_MPI_VENC_GetStream(RK_S32, VENC_STREAM_S*, RK_S32);
    RK_S32 RK_MPI_VENC_ReleaseStream(RK_S32, VENC_STREAM_S*);
    RK_S32 RK_MPI_VENC_StartRecvFrame(RK_S32, void*);
    RK_S32 RK_MPI_VENC_StopRecvFrame(RK_S32);
    RK_S32 RK_MPI_VENC_RequestIDR(RK_S32 venc_chn, RK_BOOL bInstant);

    RK_S32 RK_MPI_MB_ReleaseBuffer(MB_BLK);
    void*  RK_MPI_MB_Handle2VirAddr(MB_BLK);
}
#endif
