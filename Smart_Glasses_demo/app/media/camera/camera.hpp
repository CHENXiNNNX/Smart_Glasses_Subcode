/**
 * @file camera.hpp
 * @brief 视频系统
 * @details 实现视频采集、编码、拍照、录像等功能
 */

#ifndef CAMERA_H_
#define CAMERA_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <array>
// sample_comm.h
#if __has_include("sample_comm.h")
#define CAMERA_HAS_SAMPLE_COMM 1
#include "sample_comm.h"
#elif __has_include("rkmedia/sample_comm.h")
#define CAMERA_HAS_SAMPLE_COMM 1
#include "rkmedia/sample_comm.h"
#else
#define CAMERA_HAS_SAMPLE_COMM 0
#endif

#if !CAMERA_HAS_SAMPLE_COMM
extern "C"
{
    using RK_S32                           = int;
    using RK_U32                           = unsigned int;
    using RK_U64                           = unsigned long long;
    using RK_BOOL                          = int;
    constexpr RK_S32  RK_SUCCESS           = 0;
    constexpr RK_S32  RK_ERR_VI_NOT_CONFIG = -1;
    constexpr RK_S32  RK_ERR_VENC_BUSY     = -2;
    constexpr RK_BOOL RK_TRUE              = 1;
    constexpr RK_BOOL RK_FALSE             = 0;
    using MB_BLK                           = void*;

    struct VI_DEV_ATTR_S
    {
        int reserved{};
    };

    struct VI_DEV_BIND_PIPE_S
    {
        RK_U32 u32Num    = 0;
        RK_S32 PipeId[4] = {0};
    };

    struct VI_CHN_ATTR_S
    {
        struct
        {
            RK_U32 u32BufCount = 0;
            int    enMemoryType{0};
        } stIspOpt;
        struct
        {
            RK_U32 u32Width  = 0;
            RK_U32 u32Height = 0;
        } stSize;
        int    enPixelFormat{0};
        int    enCompressMode{0};
        RK_U32 u32Depth = 0;
    };

    struct VENC_ATTR_JPEGE_S
    {
        bool bSupportDCF{false};
        struct
        {
            unsigned char u8LargeThumbNailNum{0};
        } stMPFCfg;
        int enReceiveMode{0};
    };

    struct VENC_ATTR_S
    {
        int               enType{0};
        int               enPixelFormat{0};
        RK_U32            u32PicWidth{0};
        RK_U32            u32PicHeight{0};
        RK_U32            u32VirWidth{0};
        RK_U32            u32VirHeight{0};
        RK_U32            u32BufSize{0};
        RK_U32            u32StreamBufCnt{0};
        int               enMirror{0};
        RK_U32            u32Profile{0};
        VENC_ATTR_JPEGE_S stAttrJpege{};
    };

    struct VENC_RC_ATTR_S
    {
        int enRcMode{0};
        struct
        {
            RK_U32 u32BitRate{0};
            RK_U32 u32Gop{0};
        } stH264Cbr;
        struct
        {
            RK_U32 u32BitRate{0};
            RK_U32 u32Gop{0};
        } stH265Cbr;
        struct
        {
            RK_U32 u32Qfactor{0};
        } stMjpegFixQp;
    };

    struct VENC_CHN_ATTR_S
    {
        VENC_ATTR_S    stVencAttr{};
        VENC_RC_ATTR_S stRcAttr{};
    };

    struct VENC_PACK_S
    {
        RK_U32 u32Len{0};
        MB_BLK pMbBlk{nullptr};
        struct
        {
            int enH264EType{0};
            int enH265EType{0};
        } DataType;
        RK_U64 u64PTS{0};
    };

    struct VENC_STREAM_S
    {
        VENC_PACK_S* pstPack{nullptr};
        RK_U32       u32PackCount{0};
    };

    struct VENC_RECV_PIC_PARAM_S
    {
        RK_S32 s32RecvPicNum{0};
    };

    struct MPP_CHN_S
    {
        int    enModId{0};
        RK_S32 s32DevId{0};
        RK_S32 s32ChnId{0};
    };

    struct VIDEO_FRAME_INFO_S
    {
        struct
        {
            MB_BLK pMbBlk{nullptr};
            RK_U32 u32Width{0};
            RK_U32 u32Height{0};
            RK_U32 u32VirWidth{0};
            RK_U32 u32VirHeight{0};
            int    enPixelFormat{0};
            RK_U64 u64PTS{0};
        } stVFrame;
    };

    enum
    {
        RK_ID_VI   = 0,
        RK_ID_VENC = 1
    };

    enum
    {
        RK_FMT_YUV420SP = 0
    };

    enum
    {
        COMPRESS_MODE_NONE = 0
    };

    enum
    {
        VI_V4L2_MEMORY_TYPE_DMABUF = 0
    };

    enum
    {
        MIRROR_NONE = 0
    };

    enum RK_CODEC_ID_E
    {
        RK_VIDEO_ID_AVC   = 0,
        RK_VIDEO_ID_HEVC  = 1,
        RK_VIDEO_ID_MJPEG = 2
    };

    enum
    {
        VENC_RC_MODE_H264CBR    = 0,
        VENC_RC_MODE_H265CBR    = 1,
        VENC_RC_MODE_MJPEGFIXQP = 2
    };

    enum
    {
        VENC_PIC_RECEIVE_SINGLE = 0
    };

    enum
    {
        H264E_PROFILE_BASELINE = 0
    };

    enum
    {
        H264E_NALU_IDRSLICE = 5,
        H265E_NALU_IDRSLICE = 19
    };

    int   RK_MPI_SYS_Init();
    int   RK_MPI_SYS_Exit();
    int   RK_MPI_SYS_Malloc(MB_BLK* blk, RK_U32 size);
    void* RK_MPI_MB_Handle2VirAddr(MB_BLK blk);
    int   RK_MPI_SYS_Free(MB_BLK blk);
    int   RK_MPI_VI_GetDevAttr(int devId, VI_DEV_ATTR_S* attr);
    int   RK_MPI_VI_SetDevAttr(int devId, const VI_DEV_ATTR_S* attr);
    int   RK_MPI_VI_GetDevIsEnable(int devId);
    int   RK_MPI_VI_EnableDev(int devId);
    int   RK_MPI_VI_DisableDev(int devId);
    int   RK_MPI_VI_SetDevBindPipe(int devId, const VI_DEV_BIND_PIPE_S* pipe);
    int   RK_MPI_VI_SetChnAttr(int devId, int chnId, const VI_CHN_ATTR_S* attr);
    int   RK_MPI_VI_EnableChn(int devId, int chnId);
    int   RK_MPI_VI_DisableChn(int devId, int chnId);
    int   RK_MPI_VENC_CreateChn(int chnId, const VENC_CHN_ATTR_S* attr);
    int   RK_MPI_VENC_DestroyChn(int chnId);
    int   RK_MPI_VENC_StartRecvFrame(int chnId, const VENC_RECV_PIC_PARAM_S* param);
    int   RK_MPI_VENC_StopRecvFrame(int chnId);
    int   RK_MPI_VENC_GetStream(int chnId, VENC_STREAM_S* stream, int timeout);
    int   RK_MPI_VENC_ReleaseStream(int chnId, VENC_STREAM_S* stream);
    int   RK_MPI_VENC_GetChnAttr(int chnId, VENC_CHN_ATTR_S* attr);
    int   RK_MPI_VENC_SetChnAttr(int chnId, const VENC_CHN_ATTR_S* attr);
    int   RK_MPI_SYS_Bind(const MPP_CHN_S* src, const MPP_CHN_S* dest);
    int   RK_MPI_SYS_UnBind(const MPP_CHN_S* src, const MPP_CHN_S* dest);
    int RK_MPI_VI_GetChnFrame(int devId, int chnId, VIDEO_FRAME_INFO_S* pstFrameInfo, int timeout);
    int RK_MPI_VI_ReleaseChnFrame(int devId, int chnId, VIDEO_FRAME_INFO_S* pstFrameInfo);
} // extern "C"
#endif

#if !CAMERA_HAS_SAMPLE_COMM
#undef CAMERA_HAS_SAMPLE_COMM
#define CAMERA_HAS_SAMPLE_COMM 0
#else
#undef CAMERA_HAS_SAMPLE_COMM
#define CAMERA_HAS_SAMPLE_COMM 1
#endif

// RKAIQ 头文件
#if __has_include("rkaiq/uAPI2/rk_aiq_user_api2_imgproc.h")
#include "rkaiq/uAPI2/rk_aiq_user_api2_imgproc.h"
#include "rkaiq/uAPI2/rk_aiq_user_api2_sysctl.h"
#else
extern "C"
{
    struct rk_aiq_sys_ctx_s
    {
        int reserved{};
    };
    using rk_aiq_sys_ctx_t = rk_aiq_sys_ctx_s;

    struct rk_aiq_sensor_info_t
    {
        const char* sensor_name{nullptr};
    };

    struct rk_aiq_static_info_t
    {
        rk_aiq_sensor_info_t sensor_info;
    };

    struct paRange_t
    {
        float min{0.0F};
        float max{0.0F};
    };

    struct rk_aiq_wb_gain_t
    {
        float rgain{1.0F};
        float bgain{1.0F};
        float grgain{1.0F};
        float gbgain{1.0F};
    };

    enum opMode_t
    {
        OP_AUTO   = 0,
        OP_MANUAL = 1
    };

    enum rk_aiq_working_mode_t
    {
        RK_AIQ_WORKING_MODE_NORMAL = 0
    };

    enum XCamReturn
    {
        XCAM_RETURN_NO_ERROR = 0,
        XCAM_RETURN_ERROR    = -1
    };

    int               rk_aiq_uapi2_sysctl_enumStaticMetas(int cam_id, rk_aiq_static_info_t* info);
    rk_aiq_sys_ctx_t* rk_aiq_uapi2_sysctl_init(const char* sns_ent_name, const char* iq_dir,
                                               void* unused1, void* unused2);
    int rk_aiq_uapi2_sysctl_prepare(rk_aiq_sys_ctx_t* ctx, int, int, rk_aiq_working_mode_t mode);
    int rk_aiq_uapi2_sysctl_start(rk_aiq_sys_ctx_t* ctx);
    int rk_aiq_uapi2_sysctl_stop(rk_aiq_sys_ctx_t* ctx, bool);
    int rk_aiq_uapi2_sysctl_deinit(rk_aiq_sys_ctx_t* ctx);
    XCamReturn rk_aiq_uapi2_setExpMode(rk_aiq_sys_ctx_t* ctx, opMode_t mode);
    XCamReturn rk_aiq_uapi2_getExpMode(rk_aiq_sys_ctx_t* ctx, opMode_t* mode);
    XCamReturn rk_aiq_uapi2_setExpGainRange(rk_aiq_sys_ctx_t* ctx, const paRange_t* range);
    XCamReturn rk_aiq_uapi2_getExpGainRange(rk_aiq_sys_ctx_t* ctx, paRange_t* range);
    XCamReturn rk_aiq_uapi2_setExpTimeRange(rk_aiq_sys_ctx_t* ctx, const paRange_t* range);
    XCamReturn rk_aiq_uapi2_getExpTimeRange(rk_aiq_sys_ctx_t* ctx, paRange_t* range);
    XCamReturn rk_aiq_uapi2_setAeLock(rk_aiq_sys_ctx_t* ctx, bool lock);
    XCamReturn rk_aiq_uapi2_setWBMode(rk_aiq_sys_ctx_t* ctx, opMode_t mode);
    XCamReturn rk_aiq_uapi2_getWBMode(rk_aiq_sys_ctx_t* ctx, opMode_t* mode);
    XCamReturn rk_aiq_uapi2_setMWBGain(rk_aiq_sys_ctx_t* ctx, const rk_aiq_wb_gain_t* gain);
    XCamReturn rk_aiq_uapi2_getWBGain(rk_aiq_sys_ctx_t* ctx, rk_aiq_wb_gain_t* gain);
    XCamReturn rk_aiq_uapi2_setMWBCT(rk_aiq_sys_ctx_t* ctx, unsigned int ct);
    XCamReturn rk_aiq_uapi2_getWBCT(rk_aiq_sys_ctx_t* ctx, unsigned int* ct);
    XCamReturn rk_aiq_uapi2_lockAWB(rk_aiq_sys_ctx_t* ctx);
    XCamReturn rk_aiq_uapi2_unlockAWB(rk_aiq_sys_ctx_t* ctx);
    XCamReturn rk_aiq_uapi2_setBrightness(rk_aiq_sys_ctx_t* ctx, unsigned int level);
    XCamReturn rk_aiq_uapi2_getBrightness(rk_aiq_sys_ctx_t* ctx, unsigned int* level);
    XCamReturn rk_aiq_uapi2_setContrast(rk_aiq_sys_ctx_t* ctx, unsigned int level);
    XCamReturn rk_aiq_uapi2_getContrast(rk_aiq_sys_ctx_t* ctx, unsigned int* level);
    XCamReturn rk_aiq_uapi2_setSaturation(rk_aiq_sys_ctx_t* ctx, unsigned int level);
    XCamReturn rk_aiq_uapi2_getSaturation(rk_aiq_sys_ctx_t* ctx, unsigned int* level);
    XCamReturn rk_aiq_uapi2_setHue(rk_aiq_sys_ctx_t* ctx, unsigned int level);
    XCamReturn rk_aiq_uapi2_getHue(rk_aiq_sys_ctx_t* ctx, unsigned int* level);
    XCamReturn rk_aiq_uapi2_setSharpness(rk_aiq_sys_ctx_t* ctx, unsigned int level);
    XCamReturn rk_aiq_uapi2_getSharpness(rk_aiq_sys_ctx_t* ctx, unsigned int* level);
    XCamReturn rk_aiq_uapi2_setDehazeEnable(rk_aiq_sys_ctx_t* ctx, bool enable);
    XCamReturn rk_aiq_uapi2_setMDehazeStrth(rk_aiq_sys_ctx_t* ctx, unsigned int level);
    XCamReturn rk_aiq_uapi2_getMDehazeStrth(rk_aiq_sys_ctx_t* ctx, unsigned int* level);
}
#endif

#include "../sync.hpp"
#include "../../tool/memory/mem_pool.hpp"
#include "../../tool/file/file.hpp"

namespace app
{
    namespace media
    {
        namespace camera
        {

            // ============================================================================
            // 错误类型枚举
            // ============================================================================

            enum class VideoError
            {
                NONE = 0,            // 无错误
                INVALID_PARAM,       // 无效参数
                ALLOC_FAILED,        // 内存分配失败
                INIT_FAILED,         // 初始化失败
                NOT_INITIALIZED,     // 未初始化
                ALREADY_INITIALIZED, // 已初始化
                ALREADY_STARTED,     // 已启动
                NOT_STARTED,         // 未启动
                INVALID_STATE,       // 无效状态
                QUEUE_FULL,          // 队列满
                QUEUE_EMPTY,         // 队列空
                TIMEOUT,             // 超时
                ENCODE_FAILED,       // 编码失败
                FILE_OPEN_FAILED,    // 文件打开失败
                RKMPI_ERROR,         // RKMPI错误
                NOT_SUPPORTED,       // 功能暂不支持
                UNKNOWN              // 未知错误
            };

            // ============================================================================
            // 编码格式枚举
            // ============================================================================

            enum class EncodeFormat
            {
                JPEG = 0, // JPEG格式
                H264,     // H.264格式
                H265      // H.265格式
            };

            // ============================================================================
            // 视频主状态枚举
            // ============================================================================

            enum class VideoMainState
            {
                NONE = 0, // 空闲状态
                PHOTO,    // 拍照模式
                RECORD,   // 录像模式
                WEBRTC,   // WebRTC推流模式
                RTSP      // RTSP推流模式
            };

            // ============================================================================
            // 视频控制子状态枚举
            // ============================================================================

            enum class VideoControlState
            {
                IDLE = 0,  // 空闲
                CAPTURING, // 正在采集
                RECORDING, // 正在录像
                STREAMING  // 正在推流
            };

            // ============================================================================
            // 配置结构体
            // ============================================================================

            struct VideoConfig
            {
                // 基本参数
                int          width  = 1280;               // 图像宽度
                int          height = 720;                // 图像高度
                int          fps    = 30;                 // 帧率
                EncodeFormat format = EncodeFormat::H264; // 编码格式

                // 编码参数
                int bitrate = 10 * 1024; // 码率
                int gop     = 10;        // GOP大小
                int quality = 77;        // JPEG质量（0-100）

                // 拍照参数
                int         photo_capture_frames = 5; // 拍照采集帧数（取最后一帧）
                std::string photo_path           = "/root/picture/"; // 照片保存路径

                // 录像参数
                int         record_duration_sec = 15;             // 默认录像时长（秒）
                std::string record_path         = "/root/video/"; // 录像保存路径

                // 队列配置
                size_t max_frame_queue_size = 100; // 帧队列最大长度

                // 内存池配置
                size_t fixed_pool_size      = 200;              // 固定池块数
                size_t fixed_block_size     = 256 * 1024;       // 固定池块大小
                size_t dynamic_pool_size    = 10 * 1024 * 1024; // 动态池大小
                bool   enable_dma_zero_copy = true;
            };

            // ============================================================================
            // 视频帧结构体
            // ============================================================================

            struct VideoFrame
            {
                uint8_t*     data        = nullptr;            // 帧数据指针
                size_t       size        = 0;                  // 数据大小
                uint64_t     timestamp   = 0;                  // 时间戳（微秒）
                uint64_t     pts         = 0;                  // 原始PTS
                EncodeFormat format      = EncodeFormat::H264; // 编码格式
                bool         is_keyframe = false;              // 是否关键帧
                int          frame_index = -1; // 内存池块索引（用于释放）

                // DMA相关
                bool   is_dma_buffer = false;   // 是否为DMA缓冲区
                MB_BLK dma_mb_blk    = nullptr; // RKMPI MediaBuffer句柄（DMA时使用）

                // 删除器函数
                std::function<void(int)> deleter;

                VideoFrame()                             = default;
                VideoFrame(const VideoFrame&)            = delete;
                VideoFrame& operator=(const VideoFrame&) = delete;
                VideoFrame(VideoFrame&&)                 = default;
                VideoFrame& operator=(VideoFrame&&)      = default;
            };

            using VideoFramePtr = std::shared_ptr<VideoFrame>;

            // ============================================================================
            // 原始YUV帧结构体
            // ============================================================================

            struct RawVideoFrame
            {
                uint8_t* data      = nullptr; // YUV420SP数据指针
                size_t   size      = 0;       // 数据大小（字节）
                uint32_t width     = 0;       // 图像宽度
                uint32_t height    = 0;       // 图像高度
                uint64_t timestamp = 0;       // 时间戳（微秒）
                uint64_t pts       = 0;       // PTS

                // DMA相关
                bool                is_dma_buffer = false;   // 是否为DMA缓冲区
                MB_BLK              dma_mb_blk    = nullptr; // RKMPI MediaBuffer句柄
                VIDEO_FRAME_INFO_S* frame_info    = nullptr; // 原始帧信息（用于释放）

                // 删除器函数
                std::function<void()> deleter;

                RawVideoFrame()                                = default;
                RawVideoFrame(const RawVideoFrame&)            = delete;
                RawVideoFrame& operator=(const RawVideoFrame&) = delete;
                RawVideoFrame(RawVideoFrame&&)                 = default;
                RawVideoFrame& operator=(RawVideoFrame&&)      = default;
            };

            using RawVideoFramePtr = std::shared_ptr<RawVideoFrame>;

            // ============================================================================
            // 视频内存池
            // ============================================================================

            class VideoMemoryPool
            {
            public:
                // 统计信息
                struct Stats
                {
                    std::atomic<uint64_t> fixed_pool_hits{0};     // 固定池命中次数
                    std::atomic<uint64_t> dynamic_pool_hits{0};   // 动态池命中次数
                    std::atomic<uint64_t> dma_pool_hits{0};       // DMA池命中次数
                    std::atomic<uint64_t> total_allocations{0};   // 总分配次数
                    std::atomic<uint64_t> allocation_failures{0}; // 分配失败次数
                };

                struct VideoMemoryPoolConfig
                {
                    size_t fixed_block_size  = 64 * 1024;        // 固定池块大小
                    size_t fixed_block_count = 200;              // 固定池块数量
                    size_t dynamic_pool_size = 10 * 1024 * 1024; // 动态池大小
                    size_t alignment         = 64;               // 内存对齐（64字节）
                    bool   enable_dma        = false; // 是否启用DMA（从config传递）
                };

                explicit VideoMemoryPool(const VideoMemoryPoolConfig& config);
                ~VideoMemoryPool();

                // 禁用拷贝和移动
                VideoMemoryPool(const VideoMemoryPool&)            = delete;
                VideoMemoryPool& operator=(const VideoMemoryPool&) = delete;

                /**
                 * @brief 分配视频帧
                 * @param size 需要分配的大小
                 * @return 视频帧指针（失败返回nullptr）
                 */
                VideoFramePtr allocate(size_t size);

                /**
                 * @brief 获取统计信息
                 */
                void getStats(Stats& out_stats) const;

                /**
                 * @brief 重置统计信息
                 */
                void resetStats();

                /**
                 * @brief 输出统计日志
                 */
                void logStats() const;

            private:
                // 固定大小对象池
                struct FixedPool
                {
                    static constexpr size_t MAX_BLOCKS        = 512; // 最大支持512块
                    static constexpr size_t BITMAP_WORD_COUNT = 8;
                    static constexpr size_t BITS_PER_WORD     = 64;

                    size_t block_size;
                    size_t block_count;

                    // 使用位图管理分配状态（每个uint64_t管理64块）
                    alignas(64)
                        std::atomic<uint64_t> allocation_bitmap_[BITMAP_WORD_COUNT]; // 支持512块
                    alignas(64) std::vector<uint8_t> buffer; // 预分配的连续内存

                    VideoFrame frame_objects[MAX_BLOCKS]; // 帧对象池

                    FixedPool(size_t block_sz, size_t block_cnt);
                    ~FixedPool();

                    int      allocateBlock();
                    void     deallocateBlock(int index);
                    uint8_t* getBlockPtr(int index) const;
                };

                // 动态内存池
                std::unique_ptr<tool::memory::MemoryPool> dynamic_pool_;

                // DMA内存池
                struct DMAPool
                {
                    static constexpr size_t MAX_DMA_BLOCKS = 32; // 最大DMA块数

                    struct DMABlock
                    {
                        MB_BLK mb_blk   = nullptr; // RKMPI MediaBuffer句柄
                        void*  vir_addr = nullptr; // 虚拟地址
                        size_t size     = 0;       // 缓冲区大小
                        bool   in_use   = false;   // 是否正在使用
                    };

                    std::array<DMABlock, MAX_DMA_BLOCKS> blocks;
                    std::mutex                           mutex; // 保护DMA块分配
                    size_t                               block_size;

                    explicit DMAPool(size_t dma_block_size);
                    ~DMAPool();

                    VideoFrame* allocateDMAFrame(size_t size);
                    void        deallocateDMAFrame(VideoFrame* frame);
                };
                std::unique_ptr<DMAPool> dma_pool_;

                std::unique_ptr<FixedPool> fixed_pool_;
                Stats                      stats_;
                VideoMemoryPoolConfig      config_;
            };

            // ============================================================================
            // 资源包装器
            // ============================================================================

            /**
             * @brief ISP资源包装器（支持RK AIQ参数动态调整）
             */
            class ISPWrapper
            {
            public:
                ISPWrapper(int camera_id, const std::string& iq_dir);
                ~ISPWrapper();

                ISPWrapper(const ISPWrapper&)            = delete;
                ISPWrapper& operator=(const ISPWrapper&) = delete;

                bool isValid() const
                {
                    return valid_;
                }

                // ========================================================================
                // 曝光控制（AE - Auto Exposure）
                // ========================================================================

                /**
                 * @brief 设置曝光模式
                 * @param mode OP_AUTO=自动曝光, OP_MANUAL=手动曝光
                 */
                VideoError setExposureMode(opMode_t mode);
                VideoError getExposureMode(opMode_t& mode) const;

                /**
                 * @brief 设置曝光增益范围
                 * @param min_gain 最小增益 [1.0-32.0]
                 * @param max_gain 最大增益 [1.0-32.0]
                 */
                VideoError setExpGainRange(float min_gain, float max_gain);
                VideoError getExpGainRange(float& min_gain, float& max_gain) const;

                /**
                 * @brief 设置曝光时间范围（秒）
                 * @param min_time 最小曝光时间 [0.0001-1.0]
                 * @param max_time 最大曝光时间 [0.0001-1.0]
                 */
                VideoError setExpTimeRange(float min_time, float max_time);
                VideoError getExpTimeRange(float& min_time, float& max_time) const;

                /**
                 * @brief 锁定/解锁自动曝光
                 */
                VideoError lockAE(bool lock);

                // ========================================================================
                // 白平衡控制（AWB - Auto White Balance）
                // ========================================================================

                /**
                 * @brief 设置白平衡模式
                 * @param mode OP_AUTO=自动白平衡, OP_MANUAL=手动白平衡
                 */
                VideoError setWhiteBalanceMode(opMode_t mode);
                VideoError getWhiteBalanceMode(opMode_t& mode) const;

                /**
                 * @brief 设置手动白平衡增益（r_gain和b_gain）
                 * @param r_gain 红色增益 [0.0-4.0]
                 * @param b_gain 蓝色增益 [0.0-4.0]
                 */
                VideoError setWhiteBalanceGain(float r_gain, float b_gain);
                VideoError getWhiteBalanceGain(float& r_gain, float& b_gain) const;

                /**
                 * @brief 设置色温
                 * @param ct 色温值 [2800-7500]K
                 */
                VideoError setColorTemperature(unsigned int ct);
                VideoError getColorTemperature(unsigned int& ct) const;

                /**
                 * @brief 锁定/解锁自动白平衡
                 */
                VideoError lockAWB(bool lock);

                // ========================================================================
                // 图像质量控制
                // ========================================================================

                /**
                 * @brief 设置亮度
                 * @param level 亮度等级 [0-255]
                 */
                VideoError setBrightness(unsigned int level);
                VideoError getBrightness(unsigned int& level) const;

                /**
                 * @brief 设置对比度
                 * @param level 对比度等级 [0-255]
                 */
                VideoError setContrast(unsigned int level);
                VideoError getContrast(unsigned int& level) const;

                /**
                 * @brief 设置饱和度
                 * @param level 饱和度等级 [0-255]
                 */
                VideoError setSaturation(unsigned int level);
                VideoError getSaturation(unsigned int& level) const;

                /**
                 * @brief 设置色调
                 * @param level 色调等级 [0-255]
                 */
                VideoError setHue(unsigned int level);
                VideoError getHue(unsigned int& level) const;

                /**
                 * @brief 设置锐度
                 * @param level 锐度等级 [0-100]
                 */
                VideoError setSharpness(unsigned int level);
                VideoError getSharpness(unsigned int& level) const;

                // ========================================================================
                // 高级控制
                // ========================================================================

                /**
                 * @brief 设置去雾强度
                 * @param level 去雾强度 [0-255]，0=关闭，255=最强
                 */
                VideoError setDehazeLevel(unsigned int level);
                VideoError getDehazeLevel(unsigned int& level) const;

            private:
                int               camera_id_;
                bool              valid_   = false;
                rk_aiq_sys_ctx_t* aiq_ctx_ = nullptr; // AIQ系统上下文
            };

            /**
             * @brief VI设备资源包装器
             */
            class VIDeviceWrapper
            {
            public:
                explicit VIDeviceWrapper(int dev_id);
                ~VIDeviceWrapper();

                VIDeviceWrapper(const VIDeviceWrapper&)            = delete;
                VIDeviceWrapper& operator=(const VIDeviceWrapper&) = delete;

                bool isValid() const
                {
                    return valid_;
                }

            private:
                int  dev_id_;
                bool valid_ = false;
            };

            /**
             * @brief VI通道资源包装器
             */
            class VIChannelWrapper
            {
            public:
                /**
                 * @brief 构造函数
                 * @param dev_id VI设备ID
                 * @param chn_id VI通道ID
                 * @param width 图像宽度
                 * @param height 图像高度
                 * @param depth 通道深度
                 */
                VIChannelWrapper(int dev_id, int chn_id, int width, int height, int depth = 0);
                ~VIChannelWrapper();

                VIChannelWrapper(const VIChannelWrapper&)            = delete;
                VIChannelWrapper& operator=(const VIChannelWrapper&) = delete;

                bool isValid() const
                {
                    return valid_;
                }

                /**
                 * @brief 获取原始YUV帧
                 * @param frame 输出原始帧指针
                 * @param pool 内存池引用
                 * @param timeout_ms 超时时间（毫秒），-1表示阻塞等待
                 * @return VideoError::NONE 成功，其他值表示失败
                 */
                VideoError getRawFrame(RawVideoFramePtr& frame, VideoMemoryPool& pool,
                                       int timeout_ms = -1);

            private:
                int  dev_id_;
                int  chn_id_;
                bool valid_ = false;
            };

            /**
             * @brief VENC编码器资源包装器
             */
            class VENCWrapper
            {
            public:
                VENCWrapper(int chn_id, int width, int height, EncodeFormat format, int bitrate,
                            int gop);
                ~VENCWrapper();

                VENCWrapper(const VENCWrapper&)            = delete;
                VENCWrapper& operator=(const VENCWrapper&) = delete;

                bool isValid() const
                {
                    return valid_;
                }

                // 获取编码流
                VideoError getStream(VideoFramePtr& frame, VideoMemoryPool& pool, int timeout_ms);

                // 获取编码流
                VideoError getStreamZeroCopy(VideoFramePtr& frame, int timeout_ms);

                // 动态调整编码参数
                VideoError setBitrate(int bitrate_kbps);
                VideoError setGOP(int gop);
                VideoError setJPEGQuality(int quality); // JPEG质量 (1-100)

            private:
                int  chn_id_;
                bool valid_ = false;

                // 编码器参数跟踪
                EncodeFormat current_format_;
                int          current_width_;
                int          current_height_;
                int          current_bitrate_;
                int          current_gop_;
                int          current_jpeg_quality_;
            };

            // 使用文件工具类
            using FileWrapper = tool::file::FileWrapper;

            // ============================================================================
            // 回调函数类型定义
            // ============================================================================

            // WebRTC视频帧回调
            using WebRTCVideoCallback = std::function<void(VideoFramePtr frame)>;

            // 状态变化回调
            template <typename StateType>
            using StateChangeCallback =
                std::function<void(StateType old_state, StateType new_state)>;

            // ============================================================================
            // VideoSystem 主类
            // ============================================================================

            class VideoSystem
            {
            public:
                /**
                 * @brief 构造函数
                 * @param config 视频配置
                 */
                explicit VideoSystem(const VideoConfig& config = VideoConfig());

                /**
                 * @brief 析构函数
                 */
                ~VideoSystem();

                VideoSystem(const VideoSystem&)            = delete;
                VideoSystem& operator=(const VideoSystem&) = delete;

                // ========================================================================
                // 系统生命周期管理
                // ========================================================================

                /**
                 * @brief 初始化视频系统
                 * @param sync_ctx 时间同步上下文
                 */
                VideoError init(std::shared_ptr<sync_context_t> sync_ctx = nullptr);

                /**
                 * @brief 关闭视频系统
                 */
                void deinit();

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return is_initialized_.load();
                }

                // ========================================================================
                // 视频流控制
                // ========================================================================

                /**
                 * @brief 启动视频流处理
                 */
                VideoError startStream();

                /**
                 * @brief 停止视频流处理
                 */
                VideoError stopStream();

                /**
                 * @brief 检查流是否运行中
                 */
                bool isStreaming() const
                {
                    return is_streaming_.load();
                }

                // ========================================================================
                // 拍照功能
                // ========================================================================

                /**
                 * @brief 拍照（异步）
                 * @param filename 文件名（可选）
                 * @param switch_encoder 是否临时切换到JPEG编码器
                 */
                VideoError takePhoto(const std::string& filename = "", bool switch_encoder = true);

                /**
                 * @brief 检查是否正在拍照
                 */
                bool isPhotoCapturing() const
                {
                    return photo_capturing_.load();
                }

                /**
                 * @brief 恢复H264编码器
                 */
                VideoError restoreH264Encoder();

                // ========================================================================
                // 录像功能
                // ========================================================================

                /**
                 * @brief 开始录像
                 * @param filename 文件名（可选）
                 * @param duration_sec 录像时长（秒，0表示手动停止）
                 */
                VideoError startRecord(const std::string& filename = "", int duration_sec = 0);

                /**
                 * @brief 停止录像
                 */
                VideoError stopRecord();

                /**
                 * @brief 检查是否正在录像
                 */
                bool isRecording() const
                {
                    return is_recording_.load();
                }

                // ========================================================================
                // WebRTC推流
                // ========================================================================

                /**
                 * @brief 设置WebRTC视频回调
                 */
                void setWebRTCVideoCallback(WebRTCVideoCallback callback);

                /**
                 * @brief 检查是否正在WebRTC推流
                 */
                bool isWebRTCStreaming() const
                {
                    return is_webrtc_streaming_.load();
                }

                /**
                 * @brief 启动WebRTC模式
                 */
                VideoError startWebRTCMode();

                /**
                 * @brief 停止WebRTC模式
                 */
                VideoError stopWebRTCMode();

                // ========================================================================
                // RTSP推流
                // ========================================================================

                /**
                 * @brief 启动RTSP推流模式
                 * @param port RTSP服务器端口（默认554）
                 * @param path RTSP路径（默认"/live/0"）
                 */
                VideoError startRTSPMode(int port = 554, const std::string& path = "/live/0");

                /**
                 * @brief 停止RTSP推流模式
                 */
                VideoError stopRTSPMode();

                /**
                 * @brief 检查是否正在RTSP推流
                 */
                bool isRTSPStreaming() const
                {
                    return is_rtsp_streaming_.load();
                }

                // ========================================================================
                // 状态机管理
                // ========================================================================

                /**
                 * @brief 设置主状态
                 */
                VideoError setMainState(VideoMainState state);

                /**
                 * @brief 获取主状态
                 */
                VideoMainState getMainState() const
                {
                    return main_state_.load();
                }

                /**
                 * @brief 获取控制子状态
                 */
                VideoControlState getControlState() const
                {
                    return control_state_.load();
                }

                /**
                 * @brief 设置主状态变化回调
                 */
                void setMainStateCallback(StateChangeCallback<VideoMainState> callback);

                /**
                 * @brief 设置控制状态变化回调
                 */
                void setControlStateCallback(StateChangeCallback<VideoControlState> callback);

                // ========================================================================
                // 参数配置
                // ========================================================================

                /**
                 * @brief 设置编码参数
                 * @param bitrate 码率（kbps），-1表示不修改
                 * @param gop GOP大小，-1表示不修改
                 */
                VideoError setEncodingParams(int bitrate = -1, int gop = -1);

                /**
                 * @brief 设置JPEG拍照质量 (1-100)
                 * @param quality 质量等级，1=最低质量/最小文件，100=最高质量/最大文件
                 */
                VideoError setJPEGQuality(int quality);

                /**
                 * @brief 获取当前FPS
                 */
                float getCurrentFPS() const;

                // ========================================================================
                // 原始YUV帧获取功能
                // ========================================================================

                /**
                 * @brief 获取原始YUV帧（从VI通道1）
                 * @param frame 输出原始帧指针
                 * @param timeout_ms 超时时间（毫秒），-1表示阻塞等待
                 * @return VideoError::NONE 成功，其他值表示失败
                 */
                VideoError getRawFrame(RawVideoFramePtr& frame, int timeout_ms = -1);

                // ========================================================================
                // AI图像解析功能
                // ========================================================================

                /**
                 * @brief 设置AI解析服务器URL和认证令牌
                 * @param url AI服务器URL
                 * @param token 认证令牌（可选）
                 */
                void setExplainUrl(const std::string& url, const std::string& token = "");

                /**
                 * @brief 将当前图像发送到AI服务器进行分析
                 * @param question 要向AI提出的问题
                 * @return JSON格式的响应字符串
                 */
                std::string explainImage(const std::string& question);

                // ========================================================================
                // ISP参数控制
                // ========================================================================

                /**
                 * @brief 设置曝光模式
                 * @param mode OP_AUTO=自动曝光, OP_MANUAL=手动曝光
                 */
                VideoError setExposureMode(opMode_t mode);

                /**
                 * @brief 设置曝光增益范围
                 */
                VideoError setExpGainRange(float min_gain, float max_gain);

                /**
                 * @brief 设置曝光时间范围（秒）
                 */
                VideoError setExpTimeRange(float min_time, float max_time);

                /**
                 * @brief 锁定/解锁自动曝光
                 */
                VideoError lockAE(bool lock);

                /**
                 * @brief 设置白平衡模式
                 */
                VideoError setWhiteBalanceMode(opMode_t mode);

                /**
                 * @brief 设置手动白平衡增益
                 */
                VideoError setWhiteBalanceGain(float r_gain, float b_gain);

                /**
                 * @brief 设置色温
                 */
                VideoError setColorTemperature(unsigned int ct);

                /**
                 * @brief 锁定/解锁自动白平衡
                 */
                VideoError lockAWB(bool lock);

                /**
                 * @brief 设置亮度 [0-255]
                 */
                VideoError setBrightness(unsigned int level);

                /**
                 * @brief 设置对比度 [0-255]
                 */
                VideoError setContrast(unsigned int level);

                /**
                 * @brief 设置饱和度 [0-255]
                 */
                VideoError setSaturation(unsigned int level);

                /**
                 * @brief 设置色调 [0-255]
                 */
                VideoError setHue(unsigned int level);

                /**
                 * @brief 设置锐度 [0-100]
                 */
                VideoError setSharpness(unsigned int level);

                /**
                 * @brief 设置去雾强度 [0-255]
                 */
                VideoError setDehazeLevel(unsigned int level);

                // ========================================================================
                // 统计信息
                // ========================================================================

                struct Stats
                {
                    VideoMemoryPool::Stats mem_stats;             // 内存池统计
                    std::atomic<uint64_t>  frames_captured{0};    // 已采集帧数
                    std::atomic<uint64_t>  frames_dropped{0};     // 丢弃帧数
                    std::atomic<uint64_t>  photos_taken{0};       // 拍照次数
                    std::atomic<uint64_t>  record_duration_ms{0}; // 录像总时长(ms)
                };

                /**
                 * @brief 获取统计信息
                 */
                void getStats(Stats& out_stats) const;

                /**
                 * @brief 重置统计信息
                 */
                void resetStats();

                /**
                 * @brief 输出统计日志
                 */
                void logStats() const;

            private:
                // Pimpl模式实现类
                class Impl;
                std::unique_ptr<Impl> pImpl_;

                // 基本状态
                std::atomic<bool> is_initialized_{false};
                std::atomic<bool> is_streaming_{false};

                // 主状态和控制状态
                std::atomic<VideoMainState>    main_state_{VideoMainState::NONE};
                std::atomic<VideoControlState> control_state_{VideoControlState::IDLE};

                // 功能状态
                std::atomic<bool> photo_capturing_{false};
                std::atomic<bool> is_recording_{false};
                std::atomic<bool> is_webrtc_streaming_{false};
                std::atomic<bool> is_rtsp_streaming_{false};
            };

        } // namespace camera
    }     // namespace media
} // namespace app

#endif // CAMERA_H_
