/*
 * platform_stub.cc - RK MPI 桩实现 (无 SDK 时)
 */

#include "platform.hpp"

#if !CAM_HAS_SDK

extern "C"
{

    RK_S32 RK_MPI_SYS_Init() { return 0; }
    RK_S32 RK_MPI_SYS_Exit() { return 0; }
    RK_S32 RK_MPI_SYS_Bind(const MPP_CHN_S*, const MPP_CHN_S*) { return 0; }
    RK_S32 RK_MPI_SYS_UnBind(const MPP_CHN_S*, const MPP_CHN_S*) { return 0; }

    RK_S32 RK_MPI_VI_SetDevAttr(RK_S32, const VI_DEV_ATTR_S*) { return 0; }
    RK_S32 RK_MPI_VI_EnableDev(RK_S32) { return 0; }
    RK_S32 RK_MPI_VI_DisableDev(RK_S32) { return 0; }
    RK_S32 RK_MPI_VI_SetDevBindPipe(RK_S32, const VI_DEV_BIND_PIPE_S*) { return 0; }
    RK_S32 RK_MPI_VI_SetChnAttr(RK_S32, RK_S32, const VI_CHN_ATTR_S*) { return 0; }
    RK_S32 RK_MPI_VI_EnableChn(RK_S32, RK_S32) { return 0; }
    RK_S32 RK_MPI_VI_DisableChn(RK_S32, RK_S32) { return 0; }

    RK_S32 RK_MPI_VPSS_CreateGrp(RK_S32, const void*) { return 0; }
    RK_S32 RK_MPI_VPSS_DestroyGrp(RK_S32) { return 0; }
    RK_S32 RK_MPI_VPSS_SetChnAttr(RK_S32, RK_S32, const void*) { return 0; }
    RK_S32 RK_MPI_VPSS_EnableChn(RK_S32, RK_S32) { return 0; }
    RK_S32 RK_MPI_VPSS_DisableChn(RK_S32, RK_S32) { return 0; }
    RK_S32 RK_MPI_VPSS_SetGrpAIISPAttr(RK_S32, const void*) { return 0; }
    RK_S32 RK_MPI_VPSS_StartGrp(RK_S32) { return 0; }
    RK_S32 RK_MPI_VPSS_StopGrp(RK_S32) { return 0; }

    RK_S32 RK_MPI_VENC_CreateChn(RK_S32, const VENC_CHN_ATTR_S*) { return 0; }
    RK_S32 RK_MPI_VENC_DestroyChn(RK_S32) { return 0; }
    RK_S32 RK_MPI_VENC_GetStream(RK_S32, VENC_STREAM_S*, RK_S32) { return -1; }
    RK_S32 RK_MPI_VENC_ReleaseStream(RK_S32, VENC_STREAM_S*) { return 0; }
    RK_S32 RK_MPI_VENC_StartRecvFrame(RK_S32, void*) { return 0; }
    RK_S32 RK_MPI_VENC_StopRecvFrame(RK_S32) { return 0; }

    RK_S32 RK_MPI_MB_ReleaseBuffer(MB_BLK) { return 0; }
    void*  RK_MPI_MB_Handle2VirAddr(MB_BLK) { return nullptr; }
}

#endif
