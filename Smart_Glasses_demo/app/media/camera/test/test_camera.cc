#include "test_camera.h"

RK_U64 get_nowus(void) {
	struct timespec time = {0, 0};      // define a timespec structure to store time
	clock_gettime(CLOCK_MONOTONIC, &time);  // Start timing from the moment the system boots up
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}

int vi_dev_init(void) {
	printf("%s\n", __func__);
	int ret = 0;        // use for error checking 
	int devId = 0;      // device id 
	int pipeId = devId; // same as the device ID and one-to-one bound

	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));

	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_SetDevAttr already\n");
	}
    
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(devId);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(devId);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = 1;
		stBindPipe.PipeId[0] = pipeId;
		ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
		if (ret != RK_SUCCESS) {
			printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	} else {
		printf("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

int vi_chn_init(int channelId, int width, int height) {
	int ret;
	int buf_cnt = 2;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 2; //0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
	ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(0, channelId);
	if (ret) {
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	return ret;
}

int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
	printf("%s\n",__func__);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_ATTR_S stAttr;
	memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

	if (enType == RK_VIDEO_ID_AVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
		stAttr.stRcAttr.stH264Cbr.u32Gop = 1;
	} else if (enType == RK_VIDEO_ID_HEVC) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		stAttr.stRcAttr.stH265Cbr.u32BitRate = 10 * 1024;
		stAttr.stRcAttr.stH265Cbr.u32Gop = 60;
	} else if (enType == RK_VIDEO_ID_MJPEG) {
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
		stAttr.stRcAttr.stMjpegCbr.u32BitRate = 10 * 1024;
	}

	stAttr.stVencAttr.enType = enType;
	stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	if (enType == RK_VIDEO_ID_AVC)
		stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
	stAttr.stVencAttr.u32PicWidth = width;
	stAttr.stVencAttr.u32PicHeight = height;
	stAttr.stVencAttr.u32VirWidth = width;
	stAttr.stVencAttr.u32VirHeight = height;
	stAttr.stVencAttr.u32StreamBufCnt = 2;
	stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
	stAttr.stVencAttr.enMirror = MIRROR_NONE;

	RK_MPI_VENC_CreateChn(chnId, &stAttr);

	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);

	return 0;
}

// 视频流处理线程函数
static void *stream_process_thread(void *arg)
{
    video_system_t *sys = (video_system_t *)arg;
    RK_U64 last_stat_time = get_nowus();
    int frame_count = 0;
    
    printf("[CAMERA] stream_process_thread started\n");
    
    while (!sys->quit_flag) {
        // 获取编码后的H264流
        RK_S32 ret = RK_MPI_VENC_GetStream(0, &sys->stFrame, 100);
        if (ret == RK_SUCCESS && sys->stFrame.pstPack != NULL && sys->stFrame.u32PackCount > 0) {
            frame_count++;
            
            // 通过RTSP传输视频流
#if USE_RTSP
            if (sys->rtsp_handle && sys->rtsp_session) {
                void *pData = RK_MPI_MB_Handle2VirAddr(sys->stFrame.pstPack->pMbBlk);
                rtsp_tx_video(sys->rtsp_session, (uint8_t*)pData,
                             sys->stFrame.pstPack->u32Len, sys->stFrame.pstPack->u64PTS);
                rtsp_do_event(sys->rtsp_handle);
            }
#endif
            
            // 释放编码流
            RK_MPI_VENC_ReleaseStream(0, &sys->stFrame);
        }
        
        // 计算FPS
        RK_U64 current_time = get_nowus();
        if (current_time - last_stat_time >= 1000000) {
            sys->current_fps = (float)frame_count * 1000000.0f / (float)(current_time - last_stat_time);
            last_stat_time = current_time;
            frame_count = 0;
            
#if DISPLAY_FPS
            printf("[CAMERA] Current FPS: %.2f\n", sys->current_fps);
#endif
        }
    }
    
    printf("[CAMERA] stream_process_thread stopped\n");
    return NULL;
}

int init_video_system(video_system_t **sys, int width, int height) {
    // Stop RkLunch service
    system("RkLunch-stop.sh");
    
    // Allocate memory for the video structure
    *sys = (video_system_t*)malloc(sizeof(video_system_t));
    memset(*sys, 0, sizeof(video_system_t));

    // Set the resolution
    (*sys)->width = width;
    (*sys)->height = height;
    (*sys)->is_initialized = false;
    (*sys)->is_streaming = false;
    (*sys)->current_fps = 0;
    (*sys)->quit_flag = false;

    // Allocate memory for stream packet
    (*sys)->stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));

    // Whether to use multiple sensors
    RK_BOOL multi_sensor = RK_FALSE;        

    // ISP parameter path
    const char *iq_dir = "/etc/iqfiles";    

    // HDR
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL; // ISP working mode, default is normal mode

    // rkaiq init
    SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir); // 0 indicates the default first camera
    SAMPLE_COMM_ISP_Run(0); // Run ISP system

    // rkmpi init
	if (RK_MPI_SYS_Init() != RK_SUCCESS) {
		RK_LOGE("rk mpi sys init fail!");
		free(*sys);
		*sys = NULL;
		return -1;
	}

    
    #if USE_RTSP
        // rtsp init
        printf("rtsp init\n");
        (*sys)->rtsp_handle = create_rtsp_demo(554);    // RTSP port
        if ((*sys)->rtsp_handle == NULL) {
            printf("rtsp_create_demo failed\n");
            RK_MPI_SYS_Exit();
            free((*sys)->stFrame.pstPack);
            free(*sys);
            *sys = NULL;
            return -1;
        }
        
        (*sys)->rtsp_session = rtsp_new_session((*sys)->rtsp_handle, "/live/0");    // RTSP stream path
        if ((*sys)->rtsp_session == NULL) {
            printf("rtsp_new_session failed\n");
            rtsp_del_demo((*sys)->rtsp_handle);
            RK_MPI_SYS_Exit();
            free((*sys)->stFrame.pstPack);
            free(*sys);
            *sys = NULL;
            return -1;
        }
        
        rtsp_set_video((*sys)->rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);    // Set RTSP video codec 
        rtsp_sync_video_ts((*sys)->rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());   // Sync RTSP time
    #elif USE_WEBRTC
        // webrtc init
        printf("webrtc init\n");
    #endif
    
    // vi init
    if (vi_dev_init() != 0) {
        printf("vi_dev_init failed\n");
#if USE_RTSP
        rtsp_del_demo((*sys)->rtsp_handle);
#endif
        RK_MPI_SYS_Exit();
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    if (vi_chn_init(0, width, height) != 0) {
        printf("vi_chn_init failed\n");
#if USE_RTSP
        rtsp_del_demo((*sys)->rtsp_handle);
#endif
        RK_MPI_VI_DisableDev(0);
        RK_MPI_SYS_Exit();
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }

    // venc init
    RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC; // RK_VIDEO_ID_AVC: H264, RK_VIDEO_ID_HEVC: H265
    if (venc_init(0, width, height, enCodecType) != 0) {
        printf("venc_init failed\n");
#if USE_RTSP
        rtsp_del_demo((*sys)->rtsp_handle);
#endif
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
        RK_MPI_SYS_Exit();
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }

    // 绑定VI通道到VENC通道
    MPP_CHN_S stSrcChn;
    MPP_CHN_S stDestChn;
    
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
    
    if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn) != RK_SUCCESS) {
        printf("RK_MPI_SYS_Bind failed\n");
#if USE_RTSP
        rtsp_del_demo((*sys)->rtsp_handle);
#endif
        RK_MPI_VENC_DestroyChn(0);
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
        RK_MPI_SYS_Exit();
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }

    // Initialization successful
    (*sys)->is_initialized = true;
    printf("Init video system success!\n");	

    return 0;
}

int start_video_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    
    if (sys->is_streaming) {
        printf("Video stream already started\n");
        return 0;
    }
    
    printf("start_video_stream\n");
    
    // 创建流处理线程
    if (pthread_create(&sys->stream_thread, NULL, stream_process_thread, sys) != 0) {
        printf("Failed to create stream_process_thread\n");
        return -1;
    }
    
    sys->is_streaming = true;
    
    return 0;
}

int stop_video_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    
    if (!sys->is_streaming) {
        printf("Video stream already stopped\n");
        return 0;
    }
    
    printf("stop_video_stream\n");
    
    sys->quit_flag = true;
    
    // 等待线程结束
    pthread_join(sys->stream_thread, NULL);
    
    sys->is_streaming = false;
    sys->quit_flag = false;
    
    return 0;
}

int release_video_system(video_system_t **sys) {
    if (!(*sys) || !(*sys)->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    else {
        // 停止视频流
        stop_video_stream(*sys);

        // 释放绑定关系
        MPP_CHN_S stSrcChn;
        MPP_CHN_S stDestChn;
        
        stSrcChn.enModId = RK_ID_VI;
        stSrcChn.s32DevId = 0;
        stSrcChn.s32ChnId = 0;
        
        stDestChn.enModId = RK_ID_VENC;
        stDestChn.s32DevId = 0;
        stDestChn.s32ChnId = 0;
        
        RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);

        // Disable venc channel
        RK_MPI_VENC_StopRecvFrame(0);
        RK_MPI_VENC_DestroyChn(0);

        // Disable vi channel
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);

        // Stop ISP
        SAMPLE_COMM_ISP_Stop(0);

        // Free dynamic memory
        free((*sys)->stFrame.pstPack);

        // Delete rtsp
        #if USE_RTSP
        if ((*sys)->rtsp_handle)
            rtsp_del_demo((*sys)->rtsp_handle);
        #elif USE_WEBRTC

        #endif
        
        // Exit rkmpi
        RK_MPI_SYS_Exit();
        
        // 释放系统结构体
        free(*sys);
        *sys = NULL;

        printf("Video system released successfully\n");
        
        return 0;
    }
}

// 获取当前FPS
float get_current_fps(video_system_t *sys)
{
    if (sys == NULL || !sys->is_initialized) {
        return 0.0;
    }
    
    return sys->current_fps;
}

// 检查系统是否就绪
bool is_system_ready(video_system_t *sys)
{
    if (sys == NULL || !sys->is_initialized) {
        return false;
    }
    
    return true;
}

// 设置视频质量
int set_video_quality(video_system_t *sys, int bitrate, int gop)
{
    if (sys == NULL || !sys->is_initialized) {
        printf("[CAMERA] video_system not initialized\n");
        return -1;
    }
    
    VENC_CHN_ATTR_S stVencChnAttr;
    int ret = RK_MPI_VENC_GetChnAttr(0, &stVencChnAttr);
    if (ret != RK_SUCCESS) {
        printf("[CAMERA] RK_MPI_VENC_GetChnAttr failed ret %d\n", ret);
        return -1;
    }
    
    if (stVencChnAttr.stVencAttr.enType == RK_VIDEO_ID_AVC) {
        stVencChnAttr.stRcAttr.stH264Cbr.u32BitRate = bitrate;
        stVencChnAttr.stRcAttr.stH264Cbr.u32Gop = gop;
    } else if (stVencChnAttr.stVencAttr.enType == RK_VIDEO_ID_HEVC) {
        stVencChnAttr.stRcAttr.stH265Cbr.u32BitRate = bitrate;
        stVencChnAttr.stRcAttr.stH265Cbr.u32Gop = gop;
    }
    
    ret = RK_MPI_VENC_SetChnAttr(0, &stVencChnAttr);
    if (ret != RK_SUCCESS) {
        printf("[CAMERA] RK_MPI_VENC_SetChnAttr failed ret %d\n", ret);
        return -1;
    }
    
    return 0;
}

