#include "camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

// 统一时间管理
RK_U64 get_nowus(void) {
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (RK_U64)time.tv_sec * 1000000 + (RK_U64)time.tv_nsec / 1000;
}

// ISP初始化
int isp_init(void) {
    printf("[CAMERA] ISP init\n");
    
    // 是否使用多传感器
    RK_BOOL multi_sensor = RK_FALSE;
    
    // ISP参数路径
    const char *iq_dir = "/etc/iqfiles";
    
    // HDR模式
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
    
    // rkaiq初始化
    SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
    SAMPLE_COMM_ISP_Run(0);
    
    return 0;
}

// VI设备初始化
int vi_dev_init(void) {
    printf("[CAMERA] VI device init\n");
    
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
            printf("[CAMERA] RK_MPI_VI_SetDevAttr failed: %x\n", ret);
            return -1;
        }
    }
    
    // 检查设备使能状态
    ret = RK_MPI_VI_GetDevIsEnable(devId);
    if (ret != RK_SUCCESS) {
        // 使能设备
        ret = RK_MPI_VI_EnableDev(devId);
        if (ret != RK_SUCCESS) {
            printf("[CAMERA] RK_MPI_VI_EnableDev failed: %x\n", ret);
            return -1;
        }
        
        // 绑定设备与管道
        stBindPipe.u32Num = 1;
        stBindPipe.PipeId[0] = pipeId;
        ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
        if (ret != RK_SUCCESS) {
            printf("[CAMERA] RK_MPI_VI_SetDevBindPipe failed: %x\n", ret);
            return -1;
        }
    }
    
    return 0;
}

// VI通道初始化
int vi_chn_init(int channelId, int width, int height) {
    printf("[CAMERA] VI channel init\n");
    
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
    vi_chn_attr.u32Depth = 2;
    
    ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
    ret |= RK_MPI_VI_EnableChn(0, channelId);
    
    if (ret) {
        printf("[CAMERA] VI channel init failed: %d\n", ret);
        return ret;
    }
    
    return 0;
}

// 编码器初始化
int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType) {
    printf("[CAMERA] VENC init\n");
    
    VENC_RECV_PIC_PARAM_S stRecvParam;
    VENC_CHN_ATTR_S stAttr;
    
    memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));
    
    // 根据编码类型设置参数
    if (enType == RK_VIDEO_ID_AVC) {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
        stAttr.stRcAttr.stH264Cbr.u32Gop = 60;
    } else if (enType == RK_VIDEO_ID_HEVC) {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
        stAttr.stRcAttr.stH265Cbr.u32BitRate = 10 * 1024;
        stAttr.stRcAttr.stH265Cbr.u32Gop = 60;
    } else if (enType == RK_VIDEO_ID_MJPEG) {
        stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGFIXQP;
        stAttr.stRcAttr.stH264Cbr.u32BitRate = 10 * 1024;
        stAttr.stRcAttr.stH264Cbr.u32Gop = 1;
    }
    
    stAttr.stVencAttr.enType = enType;
    stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
    if (enType == RK_VIDEO_ID_AVC) {
        stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
    }
    stAttr.stVencAttr.u32PicWidth = width;
    stAttr.stVencAttr.u32PicHeight = height;
    stAttr.stVencAttr.u32VirWidth = width;
    stAttr.stVencAttr.u32VirHeight = height;
    stAttr.stVencAttr.u32StreamBufCnt = 2;
    stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
    stAttr.stVencAttr.enMirror = MIRROR_NONE;
    
    // JPEG特殊配置
    if (enType == RK_VIDEO_ID_MJPEG) {
        stAttr.stVencAttr.stAttrJpege.bSupportDCF = RK_FALSE;
        stAttr.stVencAttr.stAttrJpege.stMPFCfg.u8LargeThumbNailNum = 0;
        stAttr.stVencAttr.stAttrJpege.enReceiveMode = VENC_PIC_RECEIVE_SINGLE;
    }
    
    RK_MPI_VENC_CreateChn(chnId, &stAttr);
    
    memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    stRecvParam.s32RecvPicNum = -1;
    RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);
    
    return 0;
}

// 视频流处理线程
static void *stream_process_thread(void *arg) {
    video_system_t *sys = (video_system_t *)arg;
    RK_U64 last_stat_time = get_nowus();
    int frame_count = 0;
    
    printf("[CAMERA] Stream process thread started\n");
    
    while (!sys->quit_flag) {
        VENC_STREAM_S stFrame;
        stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
        
        RK_S32 ret = RK_MPI_VENC_GetStream(0, &stFrame, 100);
        if (ret == RK_SUCCESS && stFrame.pstPack != NULL && stFrame.u32PackCount > 0) {
            frame_count++;
            
            // 根据模式处理数据
            switch (sys->current_mode) {
                case VIDEO_MODE_RTSP:
                {
                    #if USE_RTSP
                    if (sys->is_rtsp_streaming && sys->rtsp_handle && sys->rtsp_session) {
                        void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                        rtsp_tx_video(sys->rtsp_session, (uint8_t*)pData,
                                    stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
                        rtsp_do_event(sys->rtsp_handle);
                    }
                    #endif
                }
                break;
                    
                case VIDEO_MODE_PHOTO:
                {
                    if (sys->photo_capturing) {
                        sys->photo_capture_count++;
                        
                        // 如果是最后一帧，保存文件
                        if (sys->photo_capture_count >= sys->photo_capture_total) {
                            char filename[128];
                            sprintf(filename, "photo_%d.jpg", sys->current_picture_id);
                            
                            char full_path[256];
                            snprintf(full_path, sizeof(full_path), "%s/%s", PICTURE_PATH, filename);
                            
                            FILE *file = fopen(full_path, "w");
                            if (file) {
                                void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                                fwrite(pData, 1, stFrame.pstPack->u32Len, file);
                                fclose(file);
                                printf("[CAMERA] Photo saved: %s (%d bytes)\n", full_path, stFrame.pstPack->u32Len);
                            }
                            
                            // 重置拍照状态
                            sys->photo_capture_count = 0;
                            sys->photo_capturing = false;
                            sys->current_picture_id++;
                        }
                    }
                }
                break;
                    
                case VIDEO_MODE_RECORD:
                {
                    if (sys->is_recording && sys->record_file) {
                        void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                        fwrite(pData, 1, stFrame.pstPack->u32Len, sys->record_file);
                        fflush(sys->record_file);  // 确保数据写入磁盘
                        printf("[CAMERA] Video frame written: %d bytes\n", stFrame.pstPack->u32Len);
                    }
                }
                break;
                
                case VIDEO_MODE_WEBRTC:
                {
                    // WebRTC推流模式，暂留空实现
                }
                break;
                default:
                    break;
            }
            
            // 释放编码流
            RK_MPI_VENC_ReleaseStream(0, &stFrame);
        }
        
        free(stFrame.pstPack);
        
        // 计算FPS
        RK_U64 current_time = get_nowus();
        if (current_time - last_stat_time >= 1000000) {
            sys->current_fps = (float)frame_count * 1000000.0f / (current_time - last_stat_time);
            last_stat_time = current_time;
            frame_count = 0;
            
            #if DISPLAY_FPS
            printf("[CAMERA] Current FPS: %.2f\n", sys->current_fps);
            #endif
        }
    }
    
    printf("[CAMERA] Stream process thread stopped\n");
    return NULL;
}

// 初始化视频系统
int init_video_system(video_system_t **sys, int width, int height, video_mode_t mode) {
    printf("[CAMERA] Init video system\n");
    
    // 停止RkLunch服务
    system("RkLunch-stop.sh");
    
    // 分配内存
    *sys = (video_system_t*)malloc(sizeof(video_system_t));
    if (!*sys) {
        printf("[CAMERA] Memory allocation failed\n");
        return -1;
    }
    
    memset(*sys, 0, sizeof(video_system_t));
    
    // 设置基本参数
    (*sys)->width = width;
    (*sys)->height = height;
    (*sys)->current_mode = mode;
    (*sys)->is_initialized = false;
    (*sys)->is_streaming = false;
    (*sys)->current_fps = 0;
    (*sys)->quit_flag = false;

    // rtsp config
    (*sys)->is_rtsp_streaming = false;

    // photo config
    (*sys)->current_picture_id = 0;
    (*sys)->save_picture_count = 10;
    (*sys)->photo_capture_count = 0;    // 拍照采集帧数初始化值
    (*sys)->photo_capture_total = 5;    // 采集帧数，只取最后一帧
    (*sys)->photo_capturing = false;

    // record config
    (*sys)->current_record_id = 0; 
    (*sys)->record_seconds = 15;
    (*sys)->is_recording = false;

    // encode config
    (*sys)->bitrate = 10 * 1024;
    (*sys)->gop = 60;
    (*sys)->quality = 77;
    
    // 分配编码流内存
    (*sys)->stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
    if (!(*sys)->stFrame.pstPack) {
        printf("[CAMERA] Stream memory allocation failed\n");
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    // ISP初始化
    if (isp_init() != 0) {
        printf("[CAMERA] ISP init failed\n");
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    // RKMPI系统初始化
    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        printf("[CAMERA] RKMPI sys init failed\n");
        SAMPLE_COMM_ISP_Stop(0);
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    // 协议初始化
    #if USE_RTSP
    if (mode == VIDEO_MODE_RTSP) {
        (*sys)->rtsp_handle = create_rtsp_demo(554);
        if ((*sys)->rtsp_handle == NULL) {
            printf("[CAMERA] RTSP init failed\n");
            RK_MPI_SYS_Exit();
            SAMPLE_COMM_ISP_Stop(0);
            free((*sys)->stFrame.pstPack);
            free(*sys);
            *sys = NULL;
            return -1;
        }
        
        (*sys)->rtsp_session = rtsp_new_session((*sys)->rtsp_handle, "/live/0");
        if ((*sys)->rtsp_session == NULL) {
            printf("[CAMERA] RTSP session init failed\n");
            rtsp_del_demo((*sys)->rtsp_handle);
            RK_MPI_SYS_Exit();
            SAMPLE_COMM_ISP_Stop(0);
            free((*sys)->stFrame.pstPack);
            free(*sys);
            *sys = NULL;
            return -1;
        }
        
        rtsp_set_video((*sys)->rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
        rtsp_sync_video_ts((*sys)->rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
    }
    #elif USE_WEBRTC
    if (mode == VIDEO_MODE_WEBRTC) {
        // WebRTC初始化，暂留空实现
        printf("[CAMERA] WebRTC mode not implemented yet\n");
    }
    #endif
    
    // VI设备初始化
    if (vi_dev_init() != 0) {
        printf("[CAMERA] VI device init failed\n");
        #if USE_RTSP
        if ((*sys)->rtsp_handle) {
            rtsp_del_demo((*sys)->rtsp_handle);
        }
        #endif
        RK_MPI_SYS_Exit();
        SAMPLE_COMM_ISP_Stop(0);
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    // VI通道初始化
    if (vi_chn_init(0, width, height) != 0) {
        printf("[CAMERA] VI channel init failed\n");
        #if USE_RTSP
        if ((*sys)->rtsp_handle) {
            rtsp_del_demo((*sys)->rtsp_handle);
        }
        #endif
        RK_MPI_VI_DisableDev(0);
        RK_MPI_SYS_Exit();
        SAMPLE_COMM_ISP_Stop(0);
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    // 根据模式选择编码格式
    RK_CODEC_ID_E enCodecType;
    if (mode == VIDEO_MODE_PHOTO) {
        enCodecType = RK_VIDEO_ID_MJPEG;
        (*sys)->encode_format = ENCODE_FORMAT_JPEG;
    } else {
        enCodecType = RK_VIDEO_ID_AVC;
        (*sys)->encode_format = ENCODE_FORMAT_H264;
    }
    
    // 编码器初始化
    if (venc_init(0, width, height, enCodecType) != 0) {
        printf("[CAMERA] VENC init failed\n");
        #if USE_RTSP
        if ((*sys)->rtsp_handle) {
            rtsp_del_demo((*sys)->rtsp_handle);
        }
        #endif
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
        RK_MPI_SYS_Exit();
        SAMPLE_COMM_ISP_Stop(0);
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    // 绑定VI通道到VENC通道
    MPP_CHN_S stSrcChn, stDestChn;
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
    
    if (RK_MPI_SYS_Bind(&stSrcChn, &stDestChn) != RK_SUCCESS) {
        printf("[CAMERA] Module bind failed\n");
        #if USE_RTSP
        if ((*sys)->rtsp_handle) {
            rtsp_del_demo((*sys)->rtsp_handle);
        }
        #endif
        RK_MPI_VENC_DestroyChn(0);
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
        RK_MPI_SYS_Exit();
        SAMPLE_COMM_ISP_Stop(0);
        free((*sys)->stFrame.pstPack);
        free(*sys);
        *sys = NULL;
        return -1;
    }
    
    // 创建目录
    mkdir(PICTURE_PATH, 0755);
    mkdir(RECORD_PATH, 0755);
    
    // 初始化成功
    (*sys)->is_initialized = true;
    printf("[CAMERA] Video system init success!\n");
    
    return 0;
}

// 释放视频系统
int release_video_system(video_system_t **sys) {
    if (!(*sys) || !(*sys)->is_initialized) {
        printf("[CAMERA] Video system not initialized\n");
        return -1;
    }
    
    printf("[CAMERA] Release video system\n");
    
    // 停止视频流
    stop_video_stream(*sys);
    
    // 停止RTSP推流
    stop_rtsp_stream(*sys);

    // 解绑模块
    MPP_CHN_S stSrcChn, stDestChn;
    stSrcChn.enModId = RK_ID_VI;
    stSrcChn.s32DevId = 0;
    stSrcChn.s32ChnId = 0;
    
    stDestChn.enModId = RK_ID_VENC;
    stDestChn.s32DevId = 0;
    stDestChn.s32ChnId = 0;
    
    RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
    
    // 销毁编码器
    RK_MPI_VENC_StopRecvFrame(0);
    RK_MPI_VENC_DestroyChn(0);
    
    // 禁用VI通道
    RK_MPI_VI_DisableChn(0, 0);
    RK_MPI_VI_DisableDev(0);
    
    // 停止ISP
    SAMPLE_COMM_ISP_Stop(0);
    
    // 释放协议资源
    #if USE_RTSP
    if ((*sys)->rtsp_handle) {
        rtsp_del_demo((*sys)->rtsp_handle);
    }
    #elif USE_WEBRTC
    // WebRTC资源释放
    #endif
    
    // 释放内存
    free((*sys)->stFrame.pstPack);
    free(*sys);
    *sys = NULL;
    
    // 退出RKMPI
    RK_MPI_SYS_Exit();
    
    printf("[CAMERA] Video system released successfully\n");
    return 0;
}

// 开始视频流
int start_video_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("[CAMERA] Video system not initialized\n");
        return -1;
    }
    
    if (sys->is_streaming) {
        printf("[CAMERA] Video stream already started\n");
        return 0;
    }
    
    printf("[CAMERA] Start video stream\n");
    
    // 创建流处理线程
    if (pthread_create(&sys->stream_thread, NULL, stream_process_thread, sys) != 0) {
        printf("[CAMERA] Failed to create stream thread\n");
        return -1;
    }
    
    sys->is_streaming = true;
    return 0;
}

// 停止视频流
int stop_video_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        printf("[CAMERA] Video system not initialized\n");
        return -1;
    }
    
    if (!sys->is_streaming) {
        printf("[CAMERA] Video stream already stopped\n");
        return 0;
    }
    
    printf("[CAMERA] Stop video stream\n");
    
    sys->quit_flag = true;
    pthread_join(sys->stream_thread, NULL);
    
    sys->is_streaming = false;
    sys->quit_flag = false;
    
    return 0;
}

// 获取编码流
int get_encoded_stream(video_system_t *sys, VENC_STREAM_S *stFrame, int timeout_ms) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    return RK_MPI_VENC_GetStream(0, stFrame, timeout_ms);
}

// 释放编码流
int release_encoded_stream(video_system_t *sys, VENC_STREAM_S *stFrame) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    return RK_MPI_VENC_ReleaseStream(0, stFrame);
}

// 设置编码参数
int set_encoding_params(video_system_t *sys, encode_format_t format, int bitrate, int gop, int quality) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    sys->encode_format = format;
    sys->bitrate = bitrate;
    sys->gop = gop;
    sys->quality = quality;
    
    // 重新配置编码器
    VENC_CHN_ATTR_S stVencChnAttr;
    int ret = RK_MPI_VENC_GetChnAttr(0, &stVencChnAttr);
    if (ret != RK_SUCCESS) {
        return -1;
    }
    
    if (format == ENCODE_FORMAT_H264) {
        stVencChnAttr.stRcAttr.stH264Cbr.u32BitRate = bitrate;
        stVencChnAttr.stRcAttr.stH264Cbr.u32Gop = gop;
    } else if (format == ENCODE_FORMAT_H265) {
        stVencChnAttr.stRcAttr.stH265Cbr.u32BitRate = bitrate;
        stVencChnAttr.stRcAttr.stH265Cbr.u32Gop = gop;
    }
    
    return RK_MPI_VENC_SetChnAttr(0, &stVencChnAttr);
}

// 拍照
int take_picture(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    if (sys->current_mode != VIDEO_MODE_PHOTO) {
        printf("[CAMERA] Not in photo mode\n");
        return -1;
    }
    
    if (sys->photo_capturing) {
        printf("[CAMERA] Photo capture already in progress\n");
        return -1;
    }
    
    printf("[CAMERA] Start photo capture\n");
    sys->photo_capturing = true;
    sys->photo_capture_count = 0;
    
    return 0;
}

// 开始录像
int start_record(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    if (sys->current_mode != VIDEO_MODE_RECORD) {
        printf("[CAMERA] Not in record mode\n");
        return -1;
    }
    
    // 生成录像文件名
    snprintf(sys->record_filename, sizeof(sys->record_filename), 
             "%s/record_%d.h264", RECORD_PATH, sys->current_record_id);
    
    // 打开录像文件
    sys->record_file = fopen(sys->record_filename, "w");
    if (!sys->record_file) {
        printf("[CAMERA] Failed to open record file: %s\n", sys->record_filename);
        return -1;
    }
    
    printf("[CAMERA] Start record to: %s\n", sys->record_filename);
    sys->is_recording = true;
    sys->current_record_id++;
    
    return 0;
}

// 停止录像
int stop_record(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    if (sys->current_mode != VIDEO_MODE_RECORD) {
        printf("[CAMERA] Not in record mode\n");
        return -1;
    }
    
    printf("[CAMERA] Stop record\n");
    sys->is_recording = false;
    
    // 关闭录像文件
    if (sys->record_file) {
        fclose(sys->record_file);
        sys->record_file = NULL;
        printf("[CAMERA] Record file closed: %s\n", sys->record_filename);
    }
    
    return 0;
}

// 开始RTSP推流
int start_rtsp_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    if (sys->current_mode != VIDEO_MODE_RTSP) {
        printf("[CAMERA] Not in RTSP mode\n");
        return -1;
    }
    
    if (sys->is_rtsp_streaming) {
        printf("[CAMERA] RTSP stream already started\n");
        return 0;
    }
    
    #if USE_RTSP
    if (!sys->rtsp_handle || !sys->rtsp_session) {
        printf("[CAMERA] RTSP not initialized\n");
        return -1;
    }
    
    printf("[CAMERA] Start RTSP stream\n");
    sys->is_rtsp_streaming = true;
    #else
    printf("[CAMERA] RTSP not supported\n");
    return -1;
    #endif
    
    return 0;
}

// 停止RTSP推流
int stop_rtsp_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    if (sys->current_mode != VIDEO_MODE_RTSP) {
        printf("[CAMERA] Not in RTSP mode\n");
        return -1;
    }
    
    if (!sys->is_rtsp_streaming) {
        printf("[CAMERA] RTSP stream already stopped\n");
        return 0;
    }
    
    printf("[CAMERA] Stop RTSP stream\n");
    sys->is_rtsp_streaming = false;
    
    return 0;
}

// WebRTC推流
int webrtc_stream(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    if (sys->current_mode != VIDEO_MODE_WEBRTC) {
        printf("[CAMERA] Not in WebRTC mode\n");
        return -1;
    }
    
    printf("[CAMERA] WebRTC stream not implemented yet\n");
    return 0;
}

// 获取当前视频模式
video_mode_t get_video_mode(video_system_t *sys) {
    if (!sys) {
        return VIDEO_MODE_NONE;
    }
    return sys->current_mode;
}

// 设置视频模式
int set_video_mode(video_system_t *sys, video_mode_t mode) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    sys->current_mode = mode;
    printf("[CAMERA] Video mode set to: %d\n", mode);
    return 0;
}

// 获取当前FPS
float get_current_fps(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return 0.0;
    }
    return sys->current_fps;
}

// 检查系统是否就绪
bool is_system_ready(video_system_t *sys) {
    if (!sys || !sys->is_initialized) {
        return false;
    }
    return true;
}

// 设置视频质量
int set_video_quality(video_system_t *sys, int bitrate, int gop) {
    if (!sys || !sys->is_initialized) {
        return -1;
    }
    
    return set_encoding_params(sys, sys->encode_format, bitrate, gop, sys->quality);
}
