#ifndef WEBRTC_HPP
#define WEBRTC_HPP

#include <memory>
#include <functional>
#include <string>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <chrono>
#include <atomic>
#include <array>
#include <cstring>
#include <rtc/rtc.hpp>
#include <rtc/track.hpp>
#include <rtc/rtp.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rembhandler.hpp>
#include <rtc/global.hpp>
#include "signaling.hpp"
#include "../../media/media_config.h"
#include "../../media/sync.hpp"
#include "../../tool/memory/mem_pool.hpp"


namespace app {
namespace protocol {
namespace webrtc {

// 媒体缓冲区
class MediaBuffer {
    private:
        uint8_t* data_;
        size_t capacity_;
        size_t size_;
        tool::memory::MemoryPool* pool_;

    public:
        MediaBuffer(uint8_t* data, size_t capacity, tool::memory::MemoryPool* pool)
            : data_(data)
            , capacity_(capacity)
            , size_(0)
            , pool_(pool) {}

        ~MediaBuffer() {
            if (data_ != nullptr && pool_ != nullptr) {
                pool_->deallocate(data_);
            }
        }

        MediaBuffer(const MediaBuffer&) = delete;
        MediaBuffer& operator=(const MediaBuffer&) = delete;

        uint8_t* data() { return data_; }
        const uint8_t* data() const { return data_; }

        size_t size() const { return size_; }
        size_t capacity() const { return capacity_; }

        void setSize(size_t size) { size_ = std::min(size, capacity_); }

        void reset() { size_ = 0; }

        bool write(const uint8_t* src, size_t length) {
            if (src == nullptr || length > capacity_) {
                return false;
            }
            std::memcpy(data_, src, length);
            size_ = length;
            return true;
        }
};

using MediaBufferPtr = std::shared_ptr<MediaBuffer>;

// 音频缓冲区池
class AudioBufferPool {
    private:
        static constexpr size_t AUDIO_BUFFER_SIZE = 4096;  // 4KB缓冲区
        static constexpr size_t POOL_SIZE = 100;            // 预分配100个缓冲区
        std::unique_ptr<tool::memory::MemoryPool> memPool_;
        std::queue<MediaBufferPtr> freeBuffers_;
        std::mutex mutex_;

    public:
        AudioBufferPool();
        ~AudioBufferPool();
        
        MediaBufferPtr acquire();
        void release(MediaBufferPtr buffer);  
};

// 视频缓冲区池
class VideoBufferPool {
    public:
        VideoBufferPool();
        ~VideoBufferPool();
        
        MediaBufferPtr acquire(size_t requiredSize);
        void release(MediaBufferPtr buffer);
    private:
        // 分级缓冲区
        static constexpr size_t SMALL_BUFFER_SIZE = 64 * 1024;    // 64KB (P帧)
        static constexpr size_t MEDIUM_BUFFER_SIZE = 256 * 1024;  // 256KB (小I帧)
        static constexpr size_t LARGE_BUFFER_SIZE = 1024 * 1024;  // 1MB (大I帧)
        
        static constexpr size_t SMALL_POOL_SIZE = 30;   // P帧较多
        static constexpr size_t MEDIUM_POOL_SIZE = 10;
        static constexpr size_t LARGE_POOL_SIZE = 5;
        
        std::unique_ptr<tool::memory::MemoryPool> memPool_;
        
        std::queue<MediaBufferPtr> smallBuffers_;
        std::queue<MediaBufferPtr> mediumBuffers_;
        std::queue<MediaBufferPtr> largeBuffers_;
        
        std::mutex mutex_;
};

// 任务优先级
enum class TaskPriority {
    HIGH = 0,     // 高优先级（音频）
    NORMAL = 1,   // 普通优先级（视频关键帧）
    LOW = 2       // 低优先级（视频P帧）
};

// 任务结构
struct Task {
    std::function<void()> func;
    TaskPriority priority;
    uint64_t timestamp;
    
    // 优先级比较
    bool operator<(const Task& other) const {
        if (priority != other.priority) {
            return priority > other.priority;  // 数值越小优先级越高
        }
        return timestamp > other.timestamp;    // 先进先出
    }
};

// 优先级任务队列
class PriorityTaskQueue {
    private:
        void workerThread();
        
        std::string name_;
        std::vector<std::thread> workers_;
        std::priority_queue<Task> tasks_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::atomic<bool> stop_{false};

    public:
        explicit PriorityTaskQueue(const std::string& name, size_t threadCount = 2);
        ~PriorityTaskQueue();
        
        void post(std::function<void()> func, TaskPriority priority);
        void clear();
        
        // 禁用拷贝和移动
        PriorityTaskQueue(const PriorityTaskQueue&) = delete;
        PriorityTaskQueue& operator=(const PriorityTaskQueue&) = delete;
};


/**
* @brief WebRTC系统状态
*/
enum class WebRTCState {
    UNINITIALIZED = 0,      // 尚未初始化
    INITIALIZING,           // 正在初始化内部资源
    CONNECTING,             // 正在建立PeerConnection(SDP协商+ICE候选交换)
    CONNECTED,              // PeerConnection已建立
    DISCONNECTING,          // 正在断开连接
    DISCONNECTED,           // 已断开或尚未连接
    FAILED                  // 发生错误导致失败
};

/**
* @brief WebRTC错误类型
*/
enum class WebRTCError {
    NONE = 0,               // 无错误
    SDP_NEGOTIATION_FAILED, // SDP协商失败
    ICE_CANDIDATE_FAILED,   // ICE候选交换失败
    CONNECTION_FAILED,      // 连接失败
    UNKNOWN                 // 未知错误
}; 

struct WebRTCConfig {
    // 功能开关
    bool enableDataChannel = false;
    bool enableAudioSend = false;
    bool enableAudioReceive = false;
    bool enableVideoSend = false;
    bool enableVideoReceive = false;

    // 基本配置
    std::string dataChannelLabel = "glasses_data_channel";

    // 音频配置
    struct {
        std::string codec = "opus";
        int sampleRate = AUDIO_SAMPLE_RATE;
        int channels = AUDIO_CHANNELS;
    } audio;

    // 视频配置
    struct {
        std::string codec = "h264";
        int width = CAMERA_WIDTH;
        int height = CAMERA_HEIGHT;
    } video;

    // ICE服务器配置
    struct {
        std::vector<std::string> stunServers;
        std::vector<std::string> turnServers;
        bool useRelayOnly = false;
    } ice;
    
    // SCTP配置
    struct {
        size_t recvBufferSize = 2 * 1024 * 1024;
        size_t sendBufferSize = 2 * 1024 * 1024;
        size_t maxChunksOnQueue = 20 * 1024;
        int initialCongestionWindow = 10;
        int maxBurst = 10;
        int congestionControlModule = 0;
        std::chrono::milliseconds delayedSackTime{20};
        std::chrono::milliseconds minRetransmitTimeout{200};
        std::chrono::milliseconds maxRetransmitTimeout{10000};
        std::chrono::milliseconds initialRetransmitTimeout{1000};
        int maxRetransmitAttempts = 5;
        std::chrono::milliseconds heartbeatInterval{30000};
    } sctp;

    // 性能配置
    struct {
        size_t audioThreadCount = 1;  // 音频处理线程数
        size_t videoThreadCount = 2;  // 视频处理线程数（编码和发送）
    } performance;
};

// 回调函数类型定义
using StateCallback = std::function<void(WebRTCState)>;
using DataMessageCallback = std::function<void(const std::string&)>;
using AudioDataCallback = std::function<void(const uint8_t*, size_t)>;
using VideoDataCallback = std::function<void(const uint8_t*, size_t, uint64_t)>;

/**
 * @brief WebRTC系统 - 现代C++实现
 * @details 特性：
 *          - RAII资源管理
 *          - 零拷贝缓冲区池
 *          - 优先级任务队列
 *          - 完整的RTCP支持
 *          - 状态机管理
 */
class WebRTCSystem {
    public:
        /**
         * @brief 构造函数
         * @param config WebRTC配置
         */
        explicit WebRTCSystem(const WebRTCConfig& config = WebRTCConfig{});
        
        /**
         * @brief 析构函数（RAII自动清理所有资源）
         */
        ~WebRTCSystem();
        
        // 禁用拷贝和移动
        WebRTCSystem(const WebRTCSystem&) = delete;
        WebRTCSystem& operator=(const WebRTCSystem&) = delete;
        WebRTCSystem(WebRTCSystem&&) = delete;
        WebRTCSystem& operator=(WebRTCSystem&&) = delete;
        
        // ========================================================================
        // 初始化和关闭
        // ========================================================================
        
        /**
         * @brief 初始化WebRTC系统
         * @param signaling 信令模块（必须）
         * @return WebRTCError::NONE 成功
         */
        WebRTCError open(std::shared_ptr<Signaling> signaling);
        
        /**
         * @brief 关闭WebRTC系统
         */
        void close();
        
        /**
         * @brief 检查是否已初始化
         */
        bool isOpen() const;
        
        // ========================================================================
        // 连接管理
        // ========================================================================
        
        /**
         * @brief 创建PeerConnection
         * @return true 成功
         */
        bool createConnection();
        
        /**
         * @brief 关闭连接
         */
        void closeConnection();
        
        /**
         * @brief 检查是否已连接
         */
        bool isConnected() const;
        
        /**
         * @brief 检查ICE是否已连接
         */
        bool isIceConnected() const;
        
        // ========================================================================
        // 信令处理
        // ========================================================================
        
        /**
         * @brief 处理角色分配
         * @param role "offerer" 或 "answerer"
         * @param peer_device_id 对端设备ID
         */
        void handleRole(const std::string& role, const std::string& peer_device_id);
        
        /**
         * @brief 处理远程Offer
         * @param sdp SDP字符串
         */
        void handleOffer(const std::string& sdp);
        
        /**
         * @brief 处理远程Answer
         * @param sdp SDP字符串
         */
        void handleAnswer(const std::string& sdp);
        
        /**
         * @brief 处理远程ICE候选
         * @param candidate ICE候选字符串
         */
        void handleIceCandidate(const std::string& candidate);
        
        // ========================================================================
        // 数据通道
        // ========================================================================
        
        /**
         * @brief 发送数据通道消息
         * @param message 消息内容
         * @return true 成功
         */
        bool sendDataMessage(const std::string& message);
        
        /**
         * @brief 检查数据通道是否打开
         */
        bool isDataChannelOpen() const;
        
        // ========================================================================
        // 媒体传输
        // ========================================================================
        
        /**
         * @brief 发送音频数据（Opus编码后）
         * @param data 音频数据指针
         * @param size 数据大小
         * @param timestamp 时间戳（微秒）
         */
        void sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp);
        
        /**
         * @brief 发送视频数据（H264编码后）
         * @param data 视频数据指针
         * @param size 数据大小
         * @param timestamp 时间戳（微秒）
         * @param is_key_frame 是否为关键帧
         */
        void sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp, bool is_key_frame = false);
        
        // ========================================================================
        // 状态查询
        // ========================================================================
        
        /**
         * @brief 获取当前状态
         */
        WebRTCState getState() const;
        
        /**
         * @brief 获取配置
         */
        const WebRTCConfig& getConfig() const;
        
        // ========================================================================
        // 回调设置
        // ========================================================================
        
        /**
         * @brief 设置状态变化回调
         */
        void onStateChanged(StateCallback callback);
        
        /**
         * @brief 设置数据通道消息回调
         */
        void onDataMessage(DataMessageCallback callback);
        
        /**
         * @brief 设置音频数据接收回调
         */
        void onAudioData(AudioDataCallback callback);
        
        /**
         * @brief 设置视频数据接收回调
         */
        void onVideoData(VideoDataCallback callback);
        
        // ========================================================================
        // 统计信息
        // ========================================================================
        
        /**
         * @brief 统计信息结构
         */
        struct Stats {
            uint64_t audio_packets_sent = 0;
            uint64_t audio_packets_received = 0;
            uint64_t video_packets_sent = 0;
            uint64_t video_packets_received = 0;
            uint64_t audio_bytes_sent = 0;
            uint64_t video_bytes_sent = 0;
        };
        
        /**
         * @brief 获取统计信息
         */
        Stats getStats() const;
        
        /**
         * @brief 重置统计信息
         */
        void resetStats();
        
        /**
         * @brief 输出统计日志
         */
        void logStats() const;
        
        /**
         * @brief 获取当前码率（通过 REMB 反馈）
         * @return 当前码率（bps），0 表示未收到反馈
         */
        uint32_t getCurrentBitrate() const { return current_bitrate_.load(); }

    private:
        // ========================================================================
        // 内部方法
        // ========================================================================
        
        // void setupPeerConnection();
        void setupCallbacks();
        void setupDataChannel();
        void setupAudioTrack();
        void setupVideoTrack();
        void handleDataChannel();
        void handleDataMessage(const std::string& message);
        void handleAudioData(const uint8_t* data, size_t size);
        void handleVideoData(const uint8_t* data, size_t size, uint64_t timestamp);
        void setState(WebRTCState new_state);
        void configureSctp();
        void cleanup();
        
        // RTP解析器
        bool parseRtpPacket(const uint8_t* rtp_data, size_t rtp_size, 
                           const uint8_t*& payload_data, size_t& payload_size);
        
        // 拥塞控制回调
        void onRembReceived(unsigned int bitrate);
        
        // ========================================================================
        // 成员变量
        // ========================================================================
        
        // 配置
        WebRTCConfig config_;
        
        // 状态
        std::atomic<WebRTCState> state_;
        std::atomic<bool> ice_connected_;
        std::atomic<bool> initialized_;
        
        // 信令和连接
        std::shared_ptr<Signaling> signaling_;
        std::shared_ptr<rtc::PeerConnection> peer_connection_;
        std::string role_;
        std::string peer_device_id_;
        
        // 数据通道
        std::shared_ptr<rtc::DataChannel> data_channel_;
        
        // 音频轨道
        std::shared_ptr<rtc::Track> audio_track_;
        std::shared_ptr<rtc::RtpPacketizationConfig> audio_rtp_config_;
        std::shared_ptr<rtc::OpusRtpPacketizer> audio_packetizer_;
        std::shared_ptr<rtc::RtcpSrReporter> audio_sr_reporter_;
        std::shared_ptr<rtc::RtcpReceivingSession> audio_rtcp_session_;
        std::shared_ptr<rtc::RembHandler> audio_remb_handler_;
        
        // 视频轨道
        std::shared_ptr<rtc::Track> video_track_;
        std::shared_ptr<rtc::RtpPacketizationConfig> video_rtp_config_;
        std::shared_ptr<rtc::H264RtpPacketizer> video_packetizer_;
        std::shared_ptr<rtc::RtcpSrReporter> video_sr_reporter_;
        std::shared_ptr<rtc::RtcpReceivingSession> video_rtcp_session_;
        std::shared_ptr<rtc::RembHandler> video_remb_handler_;
        
        // 优先级任务队列
        std::unique_ptr<PriorityTaskQueue> audio_task_queue_;
        std::unique_ptr<PriorityTaskQueue> video_task_queue_;
        
        // 缓冲区池
        std::unique_ptr<AudioBufferPool> audio_buffer_pool_;
        std::unique_ptr<VideoBufferPool> video_buffer_pool_;
        
        // 发送频率控制
        std::chrono::steady_clock::time_point last_audio_send_time_;
        std::chrono::steady_clock::time_point last_video_send_time_;
        static constexpr int AUDIO_SEND_INTERVAL_MS = AUDIO_FRAME_DURATION_MS;
        static constexpr int VIDEO_SEND_INTERVAL_MS = 1000 / CAMERA_FPS;
        
        // 回调函数
        mutable std::mutex callback_mutex_;
        StateCallback state_callback_;
        DataMessageCallback data_message_callback_;
        AudioDataCallback audio_callback_;
        VideoDataCallback video_callback_;
        
        // 统计信息
        mutable std::mutex stats_mutex_;
        Stats stats_;
        
        // 时间同步
        sync_context_t sync_context_;
        
        // 码率控制
        std::atomic<uint32_t> current_bitrate_{0};
};

} // namespace webrtc
} // namespace protocol
} // namespace app

#endif // WEBRTC_HPP