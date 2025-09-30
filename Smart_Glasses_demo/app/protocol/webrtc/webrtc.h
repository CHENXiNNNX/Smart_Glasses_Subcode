#ifndef WEBRTC_H
#define WEBRTC_H

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
#include <rtc/rtc.hpp>
#include <rtc/track.hpp>
#include <rtc/rtp.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rembhandler.hpp>
#include <rtc/global.hpp>
#include "signaling.h"
#include "../../media/media_config.h"
#include "../../tool/memory/mem_pool.h"

namespace glasses {
namespace protocol {

// 前向声明
class Signaling;

// ========== 零拷贝缓冲区管理 ==========

// 媒体缓冲区
class MediaBuffer {
public:
    MediaBuffer(uint8_t* data, size_t capacity, tool::memory::MemoryPool* pool)
        : data_(data), capacity_(capacity), size_(0), pool_(pool) {}
    
    ~MediaBuffer() {
        if (data_ && pool_) {
            pool_->deallocate(data_);
        }
    }
    
    // 禁用拷贝
    MediaBuffer(const MediaBuffer&) = delete;
    MediaBuffer& operator=(const MediaBuffer&) = delete;
    
    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    
    void setSize(size_t size) { size_ = std::min(size, capacity_); }
    void reset() { size_ = 0; }
    
    // 写入数据（零拷贝写入）
    bool write(const uint8_t* src, size_t len) {
        if (len > capacity_) return false;
        std::memcpy(data_, src, len);
        size_ = len;
        return true;
    }
    
private:
    uint8_t* data_;
    size_t capacity_;
    size_t size_;
    tool::memory::MemoryPool* pool_;
};

using MediaBufferPtr = std::shared_ptr<MediaBuffer>;

// ========== 优先级任务队列 ==========

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
public:
    explicit PriorityTaskQueue(const std::string& name, size_t threadCount = 2);
    ~PriorityTaskQueue();
    
    void post(std::function<void()> func, TaskPriority priority);
    void clear();
    
    // 禁用拷贝和移动
    PriorityTaskQueue(const PriorityTaskQueue&) = delete;
    PriorityTaskQueue& operator=(const PriorityTaskQueue&) = delete;

private:
    void workerThread();
    
    std::string name_;
    std::vector<std::thread> workers_;
    std::priority_queue<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
};

// ========== 缓冲区池 ==========

// 音频缓冲区池（小块快速分配）
class AudioBufferPool {
public:
    AudioBufferPool();
    ~AudioBufferPool();
    
    MediaBufferPtr acquire();
    void release(MediaBufferPtr buffer);
    
private:
    static constexpr size_t AUDIO_BUFFER_SIZE = 4096;  // 4KB缓冲区
    static constexpr size_t POOL_SIZE = 100;            // 预分配100个缓冲区
    
    std::unique_ptr<tool::memory::MemoryPool> memPool_;
    std::queue<MediaBufferPtr> freeBuffers_;
    std::mutex mutex_;
};

// 视频缓冲区池（大块优化分配）
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

// ========== WebRTC配置结构体 ==========

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
        int sampleRate = 48000;
        int channels = 1;
        int bitrate = 32000;
    } audio;
    
    // 视频配置
    struct {
        std::string codec = "h264";
        int width = CAMERA_WIDTH;
        int height = CAMERA_HEIGHT;
        int fps = CAMERA_FPS;
        int bitrate = H264_Default_Bitrate;
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
    
    // 性能优化配置
    struct {
        size_t audioThreadCount = 1;  // 音频处理线程数
        size_t videoThreadCount = 2;  // 视频处理线程数（分离编码和发送）
        bool enableZeroCopy = true;   // 启用零拷贝
        size_t maxQueueSize = 100;    // 最大队列长度
    } performance;
};

// WebRTC连接状态
enum class WebRTCState {
    DISCONNECTED,
    CONNECTING,
    ICE_CONNECTING,
    CONNECTED,
    FAILED
};

// 回调函数类型定义
using StateCallback = std::function<void(WebRTCState)>;
using DataMessageCallback = std::function<void(const std::string&)>;
using AudioDataCallback = std::function<void(const uint8_t*, size_t)>;
using VideoFrameCallback = std::function<void(const uint8_t*, size_t, uint64_t)>;

// ========== WebRTC管理器 ==========

class WebRTCManage {
public:
    explicit WebRTCManage(const WebRTCConfig& config = WebRTCConfig{});
    ~WebRTCManage();
    
    // 禁用拷贝和移动
    WebRTCManage(const WebRTCManage&) = delete;
    WebRTCManage& operator=(const WebRTCManage&) = delete;
    WebRTCManage(WebRTCManage&&) = delete;
    WebRTCManage& operator=(WebRTCManage&&) = delete;
    
    // ========== 初始化和清理 ==========
    bool initialize(std::shared_ptr<Signaling> signaling);
    void shutdown();
    
    // ========== 连接管理 ==========
    bool createConnection();
    void closeConnection();
    
    // ========== 信令处理 ==========
    void handleRole(const std::string& role, const std::string& peerDeviceId);
    void handleRemoteOffer(const std::string& sdp);
    void handleRemoteAnswer(const std::string& sdp);
    void handleRemoteIceCandidate(const std::string& candidate);
    
    // ========== 数据通道 ==========
    bool sendDataMessage(const std::string& message);
    bool isDataChannelOpen() const;
    
    // ========== 媒体传输 ==========
    // 音频发送
    void sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp);
    
    // 视频发送
    void sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp, bool isKeyFrame = false);
    
    // ========== 状态查询 ==========
    WebRTCState getState() const { return state_; }
    const WebRTCConfig& getConfig() const { return config_; }
    bool isConnected() const { return state_ == WebRTCState::CONNECTED; }
    bool isIceConnected() const { return iceConnected_; }
    
    // ========== 回调设置 ==========
    void onStateChanged(StateCallback callback) { stateCallback_ = callback; }
    void onDataMessage(DataMessageCallback callback) { dataMessageCallback_ = callback; }
    void onAudioData(AudioDataCallback callback) { audioCallback_ = callback; }
    void onVideoFrame(VideoFrameCallback callback) { videoCallback_ = callback; }
    
    // ========== 统计信息 ==========
    struct Stats {
        uint64_t audioPacketsSent = 0;
        uint64_t audioPacketsReceived = 0;
        uint64_t videoPacketsSent = 0;
        uint64_t audioBytesSent = 0;
        uint64_t videoBytesSent = 0;
        float audioSendFps = 0.0f;
        float videoSendFps = 0.0f;
    };
    
    Stats getStats() const;

private:
    // ========== 内部方法 ==========
    void setupPeerConnection();
    void setupCallbacks();
    void setupDataChannel();
    void setupAudioTrack();
    void setupVideoTrack();
    void handleDataChannelOpen();
    void handleDataMessage(const std::string& message);
    void handleAudioData(const uint8_t* data, size_t size);
    void handleVideoFrame(const uint8_t* data, size_t size, uint64_t timestamp);
    void setState(WebRTCState newState);
    void configureSctp();
    void cleanup();
    
    // RTP解析器
    bool parseRtpPacket(const uint8_t* rtpData, size_t rtpSize, const uint8_t*& payloadData, size_t& payloadSize);
    
    // ========== 成员变量 ==========
    WebRTCConfig config_;
    WebRTCState state_;
    std::atomic<bool> iceConnected_;
    std::atomic<bool> initialized_;
    
    // 信令和连接
    std::shared_ptr<Signaling> signaling_;
    std::shared_ptr<rtc::PeerConnection> peerConnection_;
    std::string role_;
    std::string peerDeviceId_;
    
    // 数据通道
    std::shared_ptr<rtc::DataChannel> dataChannel_;
    
    // 音频轨道
    std::shared_ptr<rtc::Track> audioTrack_;
    std::shared_ptr<rtc::RtpPacketizationConfig> audioRtpConfig_;
    std::shared_ptr<rtc::OpusRtpPacketizer> audioPacketizer_;
    std::shared_ptr<rtc::RtcpSrReporter> audioSrReporter_;
    std::shared_ptr<rtc::RtcpReceivingSession> audioRtcpSession_;
    std::shared_ptr<rtc::RembHandler> audioRembHandler_;
    
    // 视频轨道
    std::shared_ptr<rtc::Track> videoTrack_;
    std::shared_ptr<rtc::RtpPacketizationConfig> videoRtpConfig_;
    std::shared_ptr<rtc::H264RtpPacketizer> videoPacketizer_;
    std::shared_ptr<rtc::RtcpSrReporter> videoSrReporter_;
    std::shared_ptr<rtc::RtcpReceivingSession> videoRtcpSession_;
    std::shared_ptr<rtc::RembHandler> videoRembHandler_;
    
    // 优先级任务队列
    std::unique_ptr<PriorityTaskQueue> audioTaskQueue_;   // 音频队列
    std::unique_ptr<PriorityTaskQueue> videoTaskQueue_;   // 视频队列
    
    // 缓冲区池
    std::unique_ptr<AudioBufferPool> audioBufferPool_;
    std::unique_ptr<VideoBufferPool> videoBufferPool_;
    
    // 发送频率控制
    std::chrono::steady_clock::time_point lastAudioSendTime_;
    std::chrono::steady_clock::time_point lastVideoSendTime_;
    static constexpr int AUDIO_SEND_INTERVAL_MS = AUDIO_FRAME_DURATION_MS;
    static constexpr int VIDEO_SEND_INTERVAL_MS = 1000 / CAMERA_FPS;
    
    // 回调函数
    StateCallback stateCallback_;
    DataMessageCallback dataMessageCallback_;
    AudioDataCallback audioCallback_;
    VideoFrameCallback videoCallback_;
    
    // 统计信息
    mutable std::mutex statsMutex_;
    Stats stats_;
    
    // 拥塞控制
    void onRembReceived(unsigned int bitrate);
};

} // namespace protocol
} // namespace glasses

#endif // WEBRTC_V2_H
