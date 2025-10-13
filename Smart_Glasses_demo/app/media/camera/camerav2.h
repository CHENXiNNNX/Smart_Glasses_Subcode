#ifndef GLASSES_MEDIA_CAMERA_CAMERAV2_H_
#define GLASSES_MEDIA_CAMERA_CAMERAV2_H_

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
#include "../../../rkmpi/include/sample_comm.h"
#include "../sync.h"
#include "../../tool/memory/mem_pool.h"

namespace glasses {
namespace media {
namespace camera {

// ============================================================================
// 错误类型枚举
// ============================================================================

enum class VideoError {
    NONE = 0,              // 无错误
    INVALID_PARAM,         // 无效参数
    ALLOC_FAILED,          // 内存分配失败
    INIT_FAILED,           // 初始化失败
    NOT_INITIALIZED,       // 未初始化
    ALREADY_INITIALIZED,   // 已初始化
    ALREADY_STARTED,       // 已启动
    NOT_STARTED,           // 未启动
    INVALID_STATE,         // 无效状态
    QUEUE_FULL,            // 队列满
    QUEUE_EMPTY,           // 队列空
    TIMEOUT,               // 超时
    ENCODE_FAILED,         // 编码失败
    FILE_OPEN_FAILED,      // 文件打开失败
    RKMPI_ERROR,           // RKMPI错误
    UNKNOWN                // 未知错误
};

// ============================================================================
// 编码格式枚举
// ============================================================================

enum class EncodeFormat {
    JPEG = 0,              // JPEG格式
    H264,                  // H.264格式
    H265                   // H.265格式
};

// ============================================================================
// 视频主状态枚举
// ============================================================================

enum class VideoMainState {
    NONE = 0,              // 空闲状态
    PHOTO,                 // 拍照模式
    RECORD,                // 录像模式
    RTSP,                  // RTSP推流模式
    WEBRTC                 // WebRTC推流模式
};

// ============================================================================
// 视频控制子状态枚举
// ============================================================================

enum class VideoControlState {
    IDLE = 0,              // 空闲
    CAPTURING,             // 正在采集
    RECORDING,             // 正在录像
    STREAMING              // 正在推流
};

// ============================================================================
// 配置结构体
// ============================================================================

struct VideoConfig {
    // 基本参数
    int width = 1280;                  // 图像宽度
    int height = 720;                  // 图像高度
    int fps = 30;                      // 帧率
    EncodeFormat format = EncodeFormat::H264;  // 编码格式
    
    // 编码参数
    int bitrate = 5 * 1024;            // 码率
    int gop = 10;                      // GOP大小
    int quality = 77;                  // JPEG质量（0-100）
    
    // 拍照参数
    int photo_capture_frames = 5;      // 拍照采集帧数（取最后一帧）
    std::string photo_path = "/root/picture/";  // 照片保存路径
    
    // 录像参数
    int record_duration_sec = 15;      // 默认录像时长（秒）
    std::string record_path = "/root/video/";   // 录像保存路径
    
    // 队列配置
    size_t max_frame_queue_size = 100; // 帧队列最大长度
    
    // 内存池配置
    size_t fixed_pool_size = 200;      // 固定池块数
    size_t fixed_block_size = 256 * 1024;  // 固定池块大小
    size_t dynamic_pool_size = 10 * 1024 * 1024;  // 动态池大小
    bool enable_dma_zero_copy = true;  // 默认开启DMA零拷贝
}; 

// ============================================================================
// 视频帧结构体
// ============================================================================

struct VideoFrame {
    uint8_t* data = nullptr;           // 帧数据指针
    size_t size = 0;                   // 数据大小
    uint64_t timestamp = 0;            // 时间戳（微秒）
    uint64_t pts = 0;                  // 原始PTS
    EncodeFormat format = EncodeFormat::H264;  // 编码格式
    bool is_keyframe = false;          // 是否关键帧
    int frame_index = -1;              // 内存池块索引（用于释放）
    
    // DMA相关
    bool is_dma_buffer = false;        // 是否为DMA缓冲区
    MB_BLK dma_mb_blk = nullptr;       // RKMPI MediaBuffer句柄（DMA时使用）
    
    // 删除器函数（用于智能指针）
    std::function<void(int)> deleter;
    
    // 禁用拷贝，只允许移动
    VideoFrame() = default;
    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;
    VideoFrame(VideoFrame&&) = default;
    VideoFrame& operator=(VideoFrame&&) = default;
};

using VideoFramePtr = std::shared_ptr<VideoFrame>;

// ============================================================================
// 三级视频内存池
// ============================================================================

class VideoMemoryPool {
public:
    // 统计信息
    struct Stats {
        std::atomic<uint64_t> fixed_pool_hits{0};      // 固定池命中次数
        std::atomic<uint64_t> dynamic_pool_hits{0};    // 动态池命中次数
        std::atomic<uint64_t> dma_pool_hits{0};        // DMA池命中次数
        std::atomic<uint64_t> total_allocations{0};    // 总分配次数
        std::atomic<uint64_t> allocation_failures{0};  // 分配失败次数
    };
    
    struct VideoMemoryPoolConfig {
        size_t fixed_block_size = 64 * 1024;      // 固定池块大小
        size_t fixed_block_count = 200;           // 固定池块数量
        size_t dynamic_pool_size = 10 * 1024 * 1024;  // 动态池大小
        size_t alignment = 64;                    // 内存对齐（64字节）
        bool enable_dma = false;                  // 是否启用DMA（从config传递）
    };
    
    explicit VideoMemoryPool(const VideoMemoryPoolConfig& config);
    ~VideoMemoryPool();
    
    // 禁用拷贝和移动
    VideoMemoryPool(const VideoMemoryPool&) = delete;
    VideoMemoryPool& operator=(const VideoMemoryPool&) = delete;
    
    /**
     * @brief 分配视频帧
     * @param size 需要分配的大小
     * @return 视频帧智能指针（失败返回nullptr）
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
    // 第一级：固定大小对象池（小帧：JPEG、IDR帧）
    struct FixedPool {
        static constexpr size_t MAX_BLOCKS = 512;  // 最大支持512块
        
        size_t block_size;
        size_t block_count;
        
        // 使用位图管理分配状态（每个uint64_t管理64块）
        alignas(64) std::atomic<uint64_t> allocation_bitmap_[8];  // 支持512块
        alignas(64) std::vector<uint8_t> buffer;  // 预分配的连续内存
        
        VideoFrame frame_objects[MAX_BLOCKS];  // 帧对象池
        
        FixedPool(size_t block_sz, size_t block_cnt);
        ~FixedPool();
        
        int allocateBlock();
        void deallocateBlock(int index);
        uint8_t* getBlockPtr(int index);
    };
    
    // 第二级：动态内存池（大帧：H.264/H.265 P帧）
    std::unique_ptr<tool::memory::MemoryPool> dynamic_pool_;
    
    // 第三级：DMA零拷贝内存池
    struct DMAPool {
        static constexpr size_t MAX_DMA_BLOCKS = 32;  // 最大DMA块数
        
        struct DMABlock {
            MB_BLK mb_blk = nullptr;          // RKMPI MediaBuffer句柄
            void* vir_addr = nullptr;         // 虚拟地址
            size_t size = 0;                  // 缓冲区大小
            bool in_use = false;              // 是否正在使用
        };
        
        std::array<DMABlock, MAX_DMA_BLOCKS> blocks;
        std::mutex mutex;                     // 保护DMA块分配
        size_t block_size;                    // 统一的DMA块大小
        
        explicit DMAPool(size_t dma_block_size);
        ~DMAPool();
        
        VideoFrame* allocateDMAFrame(size_t size);
        void deallocateDMAFrame(VideoFrame* frame);
    };
    std::unique_ptr<DMAPool> dma_pool_;
    
    std::unique_ptr<FixedPool> fixed_pool_;
    Stats stats_;
    VideoMemoryPoolConfig config_;
};

// ============================================================================
// RAII资源包装器
// ============================================================================

/**
 * @brief ISP资源包装器
 */
class ISPWrapper {
public:
    ISPWrapper(int camera_id, const std::string& iq_dir);
    ~ISPWrapper();
    
    ISPWrapper(const ISPWrapper&) = delete;
    ISPWrapper& operator=(const ISPWrapper&) = delete;
    
    bool isValid() const { return valid_; }

private:
    int camera_id_;
    bool valid_ = false;
};

/**
 * @brief VI设备资源包装器
 */
class VIDeviceWrapper {
public:
    explicit VIDeviceWrapper(int dev_id);
    ~VIDeviceWrapper();
    
    VIDeviceWrapper(const VIDeviceWrapper&) = delete;
    VIDeviceWrapper& operator=(const VIDeviceWrapper&) = delete;
    
    bool isValid() const { return valid_; }

private:
    int dev_id_;
    bool valid_ = false;
};

/**
 * @brief VI通道资源包装器
 */
class VIChannelWrapper {
public:
    VIChannelWrapper(int dev_id, int chn_id, int width, int height);
    ~VIChannelWrapper();
    
    VIChannelWrapper(const VIChannelWrapper&) = delete;
    VIChannelWrapper& operator=(const VIChannelWrapper&) = delete;
    
    bool isValid() const { return valid_; }

private:
    int dev_id_;
    int chn_id_;
    bool valid_ = false;
};

/**
 * @brief VENC编码器资源包装器
 */
class VENCWrapper {
public:
    VENCWrapper(int chn_id, int width, int height, EncodeFormat format, int bitrate, int gop);
    ~VENCWrapper();
    
    VENCWrapper(const VENCWrapper&) = delete;
    VENCWrapper& operator=(const VENCWrapper&) = delete;
    
    bool isValid() const { return valid_; }
    
    // 获取编码流
    VideoError getStream(VideoFramePtr& frame, VideoMemoryPool& pool, int timeout_ms);
    
    // 获取编码流（DMA零拷贝版本）
    VideoError getStreamZeroCopy(VideoFramePtr& frame, int timeout_ms);
    
    // ✅ 动态调整编码参数
    VideoError setBitrate(int bitrate_kbps);
    VideoError setGOP(int gop);
    VideoError setJPEGQuality(int quality);  // JPEG质量 (1-100)

private:
    int chn_id_;
    bool valid_ = false;
    
    // ✅ 编码器参数跟踪
    EncodeFormat current_format_;
    int current_width_;
    int current_height_;
    int current_bitrate_;
    int current_gop_;
    int current_jpeg_quality_;
};

/**
 * @brief 文件资源包装器
 */
class FileWrapper {
public:
    explicit FileWrapper(const std::string& filename, bool write = true);
    ~FileWrapper();
    
    FileWrapper(const FileWrapper&) = delete;
    FileWrapper& operator=(const FileWrapper&) = delete;
    
    bool isValid() const { return valid_; }
    bool write(const void* data, size_t size);
    void flush();
    std::string getFilename() const { return filename_; }

private:
    std::string filename_;
    FILE* file_ = nullptr;
    bool valid_ = false;
};

// ============================================================================
// 回调函数类型定义
// ============================================================================

// WebRTC视频帧回调
using WebRTCVideoCallback = std::function<void(VideoFramePtr frame)>;

// 状态变化回调
template<typename StateType>
using StateChangeCallback = std::function<void(StateType old_state, StateType new_state)>;

// ============================================================================
// VideoSystemV2 主类
// ============================================================================

class VideoSystemV2 {
public:
    /**
     * @brief 构造函数
     * @param config 视频配置
     */
    explicit VideoSystemV2(const VideoConfig& config = VideoConfig());
    
    /**
     * @brief 析构函数
     */
    ~VideoSystemV2();
    
    // 禁用拷贝和移动
    VideoSystemV2(const VideoSystemV2&) = delete;
    VideoSystemV2& operator=(const VideoSystemV2&) = delete;
    
    // ========================================================================
    // 系统生命周期管理
    // ========================================================================
    
    /**
     * @brief 初始化视频系统
     * @param sync_ctx 时间同步上下文
     * @return 错误码
     */
    VideoError initialize(std::shared_ptr<sync_context_t> sync_ctx = nullptr);
    
    /**
     * @brief 关闭视频系统
     */
    void shutdown();
    
    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const { return is_initialized_.load(); }
    
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
    bool isStreaming() const { return is_streaming_.load(); }
    
    // ========================================================================
    // 拍照功能
    // ========================================================================
    
    /**
     * @brief 拍照（异步）
     * @param filename 文件名（可选，默认自动生成）
     * @param switch_encoder 是否临时切换到JPEG编码器（从H264切换）
     * @return 错误码
     * 
     * 注意：如果switch_encoder=true且当前是H264编码器，
     *      拍照完成后需要手动调用restoreH264Encoder()恢复编码器
     */
    VideoError takePhoto(const std::string& filename = "", bool switch_encoder = true);
    
    /**
     * @brief 检查是否正在拍照
     */
    bool isPhotoCapturing() const { return photo_capturing_.load(); }
    
    /**
     * @brief 恢复H264编码器（拍照完成后调用）
     * @return 错误码
     * 
     * 用法：等待isPhotoCapturing()返回false后调用此方法
     */
    VideoError restoreH264Encoder();
    
    // ========================================================================
    // 录像功能
    // ========================================================================
    
    /**
     * @brief 开始录像
     * @param filename 文件名（可选，默认自动生成）
     * @param duration_sec 录像时长（秒，0表示手动停止）
     * @return 错误码
     */
    VideoError startRecord(const std::string& filename = "", int duration_sec = 0);
    
    /**
     * @brief 停止录像
     * @return 错误码
     */
    VideoError stopRecord();
    
    /**
     * @brief 检查是否正在录像
     */
    bool isRecording() const { return is_recording_.load(); }
    
    // ========================================================================
    // WebRTC推流
    // ========================================================================
    
    /**
     * @brief 设置WebRTC视频回调
     */
    void setWebRTCCallback(WebRTCVideoCallback callback);
    
    /**
     * @brief 启动WebRTC推流
     */
    VideoError startWebRTCStream();
    
    /**
     * @brief 停止WebRTC推流
     */
    VideoError stopWebRTCStream();
    
    /**
     * @brief 检查是否正在WebRTC推流
     */
    bool isWebRTCStreaming() const { return is_webrtc_streaming_.load(); }
    
    // ========================================================================
    // 便利函数
    // ========================================================================
    
    /**
     * @brief 一键启动WebRTC模式（设置模式+启动流+启动推流）
     */
    VideoError startWebRTCMode();
    
    /**
     * @brief 一键停止WebRTC模式
     */
    VideoError stopWebRTCMode();
    
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
    VideoMainState getMainState() const { return main_state_.load(); }
    
    /**
     * @brief 获取控制子状态
     */
    VideoControlState getControlState() const { return control_state_.load(); }
    
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
     */
    VideoError setEncodingParams(int bitrate, int gop);
    
    /**
     * @brief 设置视频码率（kbps）
     */
    VideoError setBitrate(int bitrate_kbps);
    
    /**
     * @brief 设置视频GOP大小
     */
    VideoError setGOP(int gop);
    
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
    // 统计信息
    // ========================================================================
    
    struct Stats {
        VideoMemoryPool::Stats mem_stats;       // 内存池统计
        std::atomic<uint64_t> frames_captured{0};   // 已采集帧数
        std::atomic<uint64_t> frames_dropped{0};    // 丢弃帧数
        std::atomic<uint64_t> photos_taken{0};      // 拍照次数
        std::atomic<uint64_t> record_duration_ms{0}; // 录像总时长(ms)
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
    std::atomic<VideoMainState> main_state_{VideoMainState::NONE};
    std::atomic<VideoControlState> control_state_{VideoControlState::IDLE};
    
    // 功能状态
    std::atomic<bool> photo_capturing_{false};
    std::atomic<bool> is_recording_{false};
    std::atomic<bool> is_webrtc_streaming_{false};
};

} // namespace camera
} // namespace media
} // namespace glasses

#endif // GLASSES_MEDIA_CAMERA_CAMERAV2_H_

