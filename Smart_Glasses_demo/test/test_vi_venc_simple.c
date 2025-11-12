#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
 #include <unistd.h>
 
#include "rk_debug.h"
#include "rk_defines.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vi.h"

static FILE *venc_file = NULL;
static RK_S32 g_frame_cnt = 100;  // 保存100帧
static bool quit = false;

static void sigterm_handler(int sig) {
    fprintf(stderr, "signal %d received, exiting...\n", sig);
    quit = true;
}

RK_U64 get_now_us() {
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000;
}

static void *get_stream_thread(void *arg) {
    (void)arg;
    printf("=== Stream capture thread started ===\n");
    void *pData = NULL;
    int loop_count = 0;
    int s32Ret;

    VENC_STREAM_S stFrame;
    stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));

    while (!quit && loop_count < g_frame_cnt) {
        s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, 1000);  // 1秒超时
        if (s32Ret == RK_SUCCESS) {
            if (venc_file) {
                pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                fwrite(pData, 1, stFrame.pstPack->u32Len, venc_file);
                fflush(venc_file);
            }

            RK_U64 nowUs = get_now_us();
            printf("[Frame %d] seq:%d size:%d pts=%lld delay=%lldus\n",
                   loop_count, stFrame.u32Seq, stFrame.pstPack->u32Len,
                   stFrame.pstPack->u64PTS, nowUs - stFrame.pstPack->u64PTS);

            s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                printf("ERROR: RK_MPI_VENC_ReleaseStream failed: 0x%x\n", s32Ret);
            }
            loop_count++;
        } else if (s32Ret == 0xa004800e) {
            // RK_ERR_VENC_NOBUF - 编码器没有输出缓冲区
            printf("WARN: VENC_NOBUF (0x%x) - VI may not be sending frames yet\n", s32Ret);
            usleep(100000);  // 等待100ms
        } else {
            printf("ERROR: RK_MPI_VENC_GetStream failed: 0x%x\n", s32Ret);
        }
    }

    if (venc_file)
        fclose(venc_file);

    free(stFrame.pstPack);
    printf("=== Captured %d frames, exiting ===\n", loop_count);
    quit = true;
    return NULL;
}

static int vi_dev_init() {
    printf("=== Initializing VI device ===\n");
    int ret = 0;
    int devId = 0;
    int pipeId = devId;

    VI_DEV_ATTR_S stDevAttr;
    VI_DEV_BIND_PIPE_S stBindPipe;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    memset(&stBindPipe, 0, sizeof(stBindPipe));

    // 检查设备配置状态
    ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
    if (ret == RK_ERR_VI_NOT_CONFIG) {
        ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
        if (ret != RK_SUCCESS) {
            printf("ERROR: RK_MPI_VI_SetDevAttr failed: 0x%x\n", ret);
         return -1;
     }
        printf("  VI device configured\n");
    } else {
        printf("  VI device already configured\n");
    }

    // 检查设备使能状态
    ret = RK_MPI_VI_GetDevIsEnable(devId);
    if (ret != RK_SUCCESS) {
        // 使能设备
        ret = RK_MPI_VI_EnableDev(devId);
        if (ret != RK_SUCCESS) {
            printf("ERROR: RK_MPI_VI_EnableDev failed: 0x%x\n", ret);
         return -1;
     }
     
        // 绑定设备与管道
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = pipeId;
        ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
        if (ret != RK_SUCCESS) {
            printf("ERROR: RK_MPI_VI_SetDevBindPipe failed: 0x%x\n", ret);
         return -1;
     }
        printf("  VI device enabled and bound to pipe %d\n", pipeId);
         } else {
        printf("  VI device already enabled\n");
    }

    printf("✓ VI device initialized\n");
    return 0;
}

static int vi_chn_init(int channelId, int width, int height) {
    printf("=== Initializing VI channel %d (%dx%d) ===\n", channelId, width, height);
    int ret;
    int buf_cnt = 2;

    VI_CHN_ATTR_S vi_chn_attr;
    memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
    vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
    vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
    vi_chn_attr.stSize.u32Width = width;
    vi_chn_attr.stSize.u32Height = height;
    vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
    vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
    
    // ⚠️ 关键：绑定到其他设备时必须设置为0
    vi_chn_attr.u32Depth = 0;  // 0: 不能直接get frame（绑定模式）
    
    ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
    if (ret != RK_SUCCESS) {
        printf("ERROR: RK_MPI_VI_SetChnAttr failed: 0x%x\n", ret);
        return ret;
    }

    ret = RK_MPI_VI_EnableChn(0, channelId);
    if (ret != RK_SUCCESS) {
        printf("ERROR: RK_MPI_VI_EnableChn failed: 0x%x\n", ret);
        return ret;
    }

    printf("✓ VI channel initialized (u32Depth=0 for binding mode)\n");
    return 0;
}

static int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
    printf("=== Initializing VENC channel %d ===\n", chnId);
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_ATTR_S stAttr;
    memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

    if (enType == RK_VIDEO_ID_AVC) {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        stAttr.stRcAttr.stH264Cbr.u32BitRate = 6 * 1024;  // 6Mbps
        stAttr.stRcAttr.stH264Cbr.u32Gop = 30;
    }

    stAttr.stVencAttr.enType = enType;
    stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
    stAttr.stVencAttr.u32PicWidth = width;
    stAttr.stVencAttr.u32PicHeight = height;
    stAttr.stVencAttr.u32VirWidth = width;
    stAttr.stVencAttr.u32VirHeight = height;
    stAttr.stVencAttr.u32StreamBufCnt = 3;  // 3个缓冲区
    stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
    stAttr.stVencAttr.enMirror = MIRROR_NONE;

    int ret = RK_MPI_VENC_CreateChn(chnId, &stAttr);
    if (ret != RK_SUCCESS) {
        printf("ERROR: RK_MPI_VENC_CreateChn failed: 0x%x\n", ret);
        return -1;
    }

    memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    stRecvParam.s32RecvPicNum = -1;  // 连续接收
    ret = RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);
    if (ret != RK_SUCCESS) {
        printf("ERROR: RK_MPI_VENC_StartRecvFrame failed: 0x%x\n", ret);
        RK_MPI_VENC_DestroyChn(chnId);
        return -1;
    }

    printf("✓ VENC initialized (H264, 6Mbps, GOP=30)\n");
    return 0;
}

int main(int argc, char *argv[]) {
    RK_S32 s32Ret = RK_FAILURE;
    RK_U32 u32Width = 1920;
    RK_U32 u32Height = 1080;
    const RK_CHAR *pOutPath = "/tmp/test_vi_venc.h264";
    RK_S32 s32ChnId = 0;
    pthread_t stream_thread;

    printf("\n");
    printf("========================================\n");
    printf("  VI-VENC Simple Test (RV1106)\n");
    printf("========================================\n");
    printf("Resolution: %dx%d\n", u32Width, u32Height);
    printf("Output: %s\n", pOutPath);
    printf("Frame count: %d\n", g_frame_cnt);
    printf("========================================\n\n");

    // 打开输出文件
    venc_file = fopen(pOutPath, "w");
    if (!venc_file) {
        printf("ERROR: Cannot open file: %s\n", pOutPath);
        return -1;
    }

    // 注册信号处理
    signal(SIGINT, sigterm_handler);

    // 1. 初始化 RKMPI 系统
    printf("=== Step 1: Initializing RKMPI system ===\n");
    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        printf("ERROR: RK_MPI_SYS_Init failed\n");
        goto __FAILED;
    }
    printf("✓ RKMPI system initialized\n\n");

    // 2. 初始化 VI 设备
    if (vi_dev_init() != 0) {
        goto __FAILED;
    }
    printf("\n");

    // 3. 初始化 VI 通道
    if (vi_chn_init(s32ChnId, u32Width, u32Height) != 0) {
        goto __FAILED;
    }
    printf("\n");

    // 4. 初始化 VENC
    if (venc_init(0, u32Width, u32Height, RK_VIDEO_ID_AVC) != 0) {
        goto __FAILED;
    }
    printf("\n");

    // 5. 绑定 VI 到 VENC
    printf("=== Step 5: Binding VI to VENC ===\n");
    MPP_CHN_S stSrcChn, stDestChn;
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = s32ChnId;

    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;

    s32Ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
    if (s32Ret != RK_SUCCESS) {
        printf("ERROR: RK_MPI_SYS_Bind failed: 0x%x\n", s32Ret);
        goto __FAILED;
    }
    printf("✓ VI chn[%d] bound to VENC chn[0]\n\n", s32ChnId);

    // 6. 创建获取流线程
    printf("=== Step 6: Starting stream capture ===\n");
    pthread_create(&stream_thread, NULL, get_stream_thread, NULL);

    // 7. 等待完成
    pthread_join(stream_thread, NULL);

    // 8. 清理
    printf("\n=== Cleanup ===\n");
    s32Ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    if (s32Ret != RK_SUCCESS) {
        printf("WARN: RK_MPI_SYS_UnBind failed: 0x%x\n", s32Ret);
    }

    RK_MPI_VI_DisableChn(0, s32ChnId);
    RK_MPI_VENC_StopRecvFrame(0);
    RK_MPI_VENC_DestroyChn(0);
    RK_MPI_VI_DisableDev(0);

    printf("✓ Cleanup complete\n");
    printf("\n========================================\n");
    printf("  Test completed successfully!\n");
    printf("  Output saved to: %s\n", pOutPath);
    printf("========================================\n\n");

    RK_MPI_SYS_Exit();
    return 0;

__FAILED:
    printf("\n========================================\n");
    printf("  Test FAILED\n");
    printf("========================================\n\n");
    if (venc_file)
        fclose(venc_file);
    RK_MPI_SYS_Exit();
    return -1;
 }
 
 