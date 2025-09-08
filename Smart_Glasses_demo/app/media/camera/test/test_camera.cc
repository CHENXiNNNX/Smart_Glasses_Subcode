#include "test_camera.h"

RK_U64 TEST_COMM_GetNowUs() {
	struct timespec time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000; /* microseconds */
}

int vi_dev_init() {
	printf("%s\n", __func__);
	int ret = 0;
	int devId = 0;
	int pipeId = devId;

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
	vi_chn_attr.stIspOpt.enMemoryType =
	    VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
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
	stAttr.stVencAttr.enPixelFormat = RK_FMT_RGB888;
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

int init_video_system(video_system_t **sys, int width, int height) {
    // Stop RkLunch service
    system("RkLunch-stop.sh");
    
    // Allocate memory for the video structure
    *sys = (video_system_t*)malloc(sizeof(video_system_t));
    memset(*sys, 0, sizeof(video_system_t));

    // Set the resolution
    (*sys)->width = width;
    (*sys)->height = height;

    (*sys)->stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));

    // Create pool for the video system
    MB_POOL_CONFIG_S pool_cfg;
    memset(&pool_cfg, 0, sizeof(MB_POOL_CONFIG_S));
    pool_cfg.u64MBSize = width * height * 3;
    pool_cfg.u32MBCnt = 1;
    pool_cfg.enAllocType = MB_ALLOC_TYPE_DMA;
    (*sys)->mem_pool = RK_MPI_MB_CreatePool(&pool_cfg);
    printf("Create Pool success !\n");	

    // Get MB from Pool 
    (*sys)->mem_block = RK_MPI_MB_GetMB((*sys)->mem_pool, width * height * 3, RK_TRUE);

    // Build h264_frame
    (*sys)->h264_frame.stVFrame.u32Width = width;
    (*sys)->h264_frame.stVFrame.u32Height = height;
    (*sys)->h264_frame.stVFrame.u32VirWidth = width;
    (*sys)->h264_frame.stVFrame.u32VirHeight = height;
    (*sys)->h264_frame.stVFrame.enPixelFormat = RK_FMT_RGB888;
    (*sys)->h264_frame.stVFrame.u32FrameFlag = 160;
    (*sys)->h264_frame.stVFrame.pMbBlk = (*sys)->mem_block;
    (*sys)->frame_data = (unsigned char*)RK_MPI_MB_Handle2VirAddr((*sys)->mem_block);
    (*sys)->opencv_frame = new cv::Mat(cv::Size(width, height), CV_8UC3, (*sys)->frame_data);
    
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
		return -1;
	}

    
    #if USE_RTSP
        // rtsp init
        printf("rtsp init\n");
        (*sys)->rtsp_handle = create_rtsp_demo(554);    // RTSP port
        (*sys)->rtsp_session = rtsp_new_session((*sys)->rtsp_handle, "/live/0");    // RTSP stream path
        rtsp_set_video((*sys)->rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);    // Set RTSP video codec 
        rtsp_sync_video_ts((*sys)->rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());   // Sync RTSP time
    #elif USE_WEBRTC
        // webrtc init
        printf("webrtc init\n");
    #endif
    
    // vi init
    vi_dev_init();
    vi_chn_init(0, width, height);

    // venc init
    RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC; // RK_VIDEO_ID_AVC: H264, RK_VIDEO_ID_HEVC: H265
    venc_init(0, width, height, enCodecType); 

    // Initialization successful
    (*sys)->is_initialized = true;
    printf("Init video system success!\n");	

    return 0;
}

int start_video_stream(video_system_t *sys) {
    return 0;
}

int stop_video_stream(video_system_t *sys) {
    return 0;
}

int release_video_system(video_system_t **sys) {
    if (!(*sys) || !(*sys)->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    else {
        // Destory MB
        RK_MPI_MB_ReleaseMB((*sys)->mem_block);

        // Destory Pool
        RK_MPI_MB_DestroyPool((*sys)->mem_pool);

        // Disable vi channel
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);

        // Stop ISP
        SAMPLE_COMM_ISP_Stop(0);

        // Disable venc channel
        RK_MPI_VENC_StopRecvFrame(0);
        RK_MPI_VENC_DestroyChn(0);

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

        return 0;
    }
}

int capture_frame(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    else {
        sys->h264_frame.stVFrame.u32TimeRef = sys->time_ref++; // Frame sequence identifier
        sys->h264_frame.stVFrame.u64PTS = TEST_COMM_GetNowUs(); // Frame timestamp
        
        // Get vi frame success return 0, else return -1
        RK_S32 ret = RK_MPI_VI_GetChnFrame(0, 0, &sys->vi_frame, -1);
        return (ret == RK_SUCCESS) ? 0 : -1; 
    }
}

int process_frame(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    else {
        // Get vi original frame YUV420P data and convert it to BGR
        void *vi_data = RK_MPI_MB_Handle2VirAddr(sys->vi_frame.stVFrame.pMbBlk); 

        // Create a Mat object in YUV420P format
        cv::Mat yuv420sp(sys->height + sys->height/2, sys->width, CV_8UC1, vi_data);

        // Create a Mat object in BGR format
        cv::Mat bgr(sys->height, sys->width, CV_8UC3, sys->frame_data);

        // Convert YUV420P to BGR
        cv::cvtColor(yuv420sp, bgr, cv::COLOR_YUV420sp2BGR);

        // Resize the frame to the same size as the input frame
        cv::resize(bgr, *sys->opencv_frame, cv::Size(sys->width, sys->height), 0, 0, cv::INTER_LINEAR);

        // FPS display on the frame
        if (DISPLAY_FPS) {
            sprintf(sys->fps_text, "fps = %.2f", sys->current_fps);
            cv::putText(*sys->opencv_frame, 
                        sys->fps_text, 
                        cv::Point(40, 40),        // display position
                        cv::FONT_HERSHEY_SIMPLEX, // display font
                        1,                        // display font scale
                        cv::Scalar(0,255,0),      // display color
                        2);                       // display thickness
        }
        // Copy the frame data to the input frame_data
        memcpy(sys->frame_data, sys->opencv_frame->data, sys->width * sys->height * 3);

        return 0;
    }
}

int encode_to_h264(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    
    return RK_MPI_VENC_SendFrame(0, &sys->h264_frame, -1);
}

#if USE_RTSP
int rtsp_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    else {
        RK_S32 ret = RK_MPI_VENC_GetStream(0, &sys->stFrame, -1);
        if (ret == RK_SUCCESS) {
            if (sys->rtsp_handle && sys->rtsp_session) {
                void *pData = RK_MPI_MB_Handle2VirAddr(sys->stFrame.pstPack->pMbBlk);
                rtsp_tx_video(sys->rtsp_session, (uint8_t*)pData,
                             sys->stFrame.pstPack->u32Len, sys->stFrame.pstPack->u64PTS);
                rtsp_do_event(sys->rtsp_handle);
            }
            // 计算FPS
            RK_U64 nowUs = TEST_COMM_GetNowUs();
            sys->current_fps = (float)1000000 / (float)(nowUs - sys->h264_frame.stVFrame.u64PTS);
            printf("Current FPS: %.2f\n", sys->current_fps);
            
            return 0;
        }
        return -1;
    }
}
#endif

#if USE_WEBRTC
int webrtc_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("Video system not initialized or not valid!\n");
        return -1;
    }
    else {
        
        return 0;
    }
}
#endif

int release_frame(video_system_t *sys) {
    if (!sys || !sys->is_initialized) return -1;
    
    RK_S32 ret1 = RK_MPI_VI_ReleaseChnFrame(0, 0, &sys->vi_frame);
    if (ret1 != RK_SUCCESS) {
        printf("RK_MPI_VI_ReleaseChnFrame failed! ret = %d\n", ret1);
    }
    RK_S32 ret2 = RK_MPI_VENC_ReleaseStream(0, &sys->stFrame);
    if (ret2 != RK_SUCCESS) {
        printf("RK_MPI_VENC_ReleaseStream failed! ret = %d\n", ret2);
    }
    
    return (ret1 == RK_SUCCESS && ret2 == RK_SUCCESS) ? 0 : -1;
}

