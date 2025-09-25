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
#include <rtc/rtc.hpp>
#include <rtc/track.hpp>
#include <rtc/rtp.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rembhandler.hpp>
#include <rtc/global.hpp>
#include "signaling.h"

#include "../../media/media_config.h"

namespace glasses {
namespace protocol {

// 前向声明
class Signaling;

// 任务队列调度器
class DispatchQueue {
    typedef std::function<void(void)> fp_t;

public:
    DispatchQueue(std::string name, size_t threadCount = 1);
    ~DispatchQueue();

    // dispatch and copy
    void dispatch(const fp_t& op);
    // dispatch and move
    void dispatch(fp_t&& op);

    void removePending();

    // Deleted operations
    DispatchQueue(const DispatchQueue& rhs) = delete;
    DispatchQueue& operator=(const DispatchQueue& rhs) = delete;
    DispatchQueue(DispatchQueue&& rhs) = delete;
    DispatchQueue& operator=(DispatchQueue&& rhs) = delete;

private:
    std::string name;
    std::mutex lockMutex;
    std::vector<std::thread> threads;
    std::queue<fp_t> queue;
    std::condition_variable condition;
    bool quit = false;

    void dispatchThreadHandler(void);
};

// WebRTC功能配置结构体
struct WebRTCConfig {
    // 功能开关
    bool enableDataChannel = false;      // 数据通道开关
    bool enableAudioSend = false;        // 音频发送开关
    bool enableAudioReceive = false;     // 音频接收开关
    bool enableVideoSend = false;        // 视频发送开关
    
    // 数据通道配置
    std::string dataChannelLabel = "glasses_data_channel";
    
    // 音频配置
    struct AudioConfig {
        std::string codec = "opus";
        int sampleRate = 48000;  
        int channels = 1;
        int bitrate = 32000;
    } audioConfig;
    
    // 视频配置
    struct VideoConfig {
        std::string codec = "h264";
        int width = CAMERA_WIDTH;
        int height = CAMERA_HEIGHT;
        int fps = CAMERA_FPS;
        int bitrate = H264_Default_Bitrate;
    } videoConfig;
    
    // SCTP传输配置
    struct SctpConfig {
        size_t recvBufferSize = 1 * 1024 * 1024;        // 接收缓冲区(默认1MB)
        size_t sendBufferSize = 1 * 1024 * 1024;        // 发送缓冲区(默认1MB)
        size_t maxChunksOnQueue = 10 * 1024;            // 队列最大块数(默认10K)
        size_t initialCongestionWindow = 10;            // 初始拥塞窗口: 10 MTUs (RFC 6928)
        size_t maxBurst = 10;                           // 最大突发: 10 MTUs
        unsigned int congestionControlModule = 0;       // 拥塞控制算法: 0=RFC2581, 1=HSTCP, 2=H-TCP, 3=RTCC
        std::chrono::milliseconds delayedSackTime = std::chrono::milliseconds(20);  // SACK延迟: 20ms
        std::chrono::milliseconds minRetransmitTimeout = std::chrono::milliseconds(200);  // 最小重传超时: 200ms
        std::chrono::milliseconds maxRetransmitTimeout = std::chrono::milliseconds(10000); // 最大重传超时: 10s
        std::chrono::milliseconds initialRetransmitTimeout = std::chrono::milliseconds(1000); // 初始重传超时: 1s
        unsigned int maxRetransmitAttempts = 5;         // 最大重传次数: 5次
        std::chrono::milliseconds heartbeatInterval = std::chrono::milliseconds(30000); // 心跳间隔: 30s
    } sctpConfig;
    
    // ICE服务器配置 (STUN/TURN)
    struct IceServerConfig {
        std::string url;                    // 服务器URL
        std::string username;               // TURN服务器用户名 (仅TURN需要)
        std::string password;               // TURN服务器密码 (仅TURN需要)
        std::string transport;              // 传输协议: udp/tcp/tls
        bool isTurn = false;                // 是否为TURN服务器
    };
    
    // STUN服务器列表
    std::vector<std::string> stunServers = {
        // "stun:stun.l.google.com:19302",
        // "stun:stun.miwifi.com:3478",
        // "stun:stun.chat.bilibili.com:3478"
    };
    
    // TURN服务器列表
    std::vector<IceServerConfig> turnServers = {
        // 示例1: UDP TURN服务器
        // {"turn:turnserver.example.com:3478", "username", "password", "udp", true},

        // 示例2: TCP TURN服务器
        // {"turn:turnserver.example.com:3478", "username", "password", "tcp", true},
        
        // 示例3: TLS TURN服务器
        // {"turns:turnserver.example.com:5349", "username", "password", "tls", true},
        
        // 示例4: 使用完整URL格式的TURN服务器
        // {"turn:username:password@turnserver.example.com:3478?transport=udp", "", "", "udp", true}
    
    };
    
    // ICE传输策略
    enum class IceTransportPolicy {
        ALL,        // 使用所有候选 (默认)
        RELAY       // 仅使用中继候选 (TURN)
    } iceTransportPolicy = IceTransportPolicy::ALL;
};

// WebRTC连接状态枚举
enum class WebRTCStatus {
    DISCONNECTED,   // 未连接
    CONNECTING,     // 连接中
    ICE_CONNECTING, // ICE连接中
    CONNECTED,      // 已连接
    FAILED          // 连接失败
};

// 回调函数类型定义
using WebRTCStatusCallback = std::function<void(WebRTCStatus)>;
using DataChannelMessageCallback = std::function<void(const std::string&)>;
using AudioDataCallback = std::function<void(const uint8_t*, size_t)>;
using VideoFrameCallback = std::function<void(const uint8_t*, size_t, uint64_t)>;

/**
 * WebRTC管理类 - 负责WebRTC连接建立后的媒体通信
 * 职责：PeerConnection管理、DataChannel管理、音视频传输
 */
class WebRTCManager {
public:
    /**
     * 构造函数
     * @param config WebRTC功能配置
     */
    WebRTCManager(const WebRTCConfig& config = WebRTCConfig{});
    
    /**
     * 析构函数
     */
    ~WebRTCManager();

    // ========== 初始化和清理接口 ==========
    /**
     * 初始化WebRTC管理器
     * @param signaling 信令模块实例
     * @return true 初始化成功，false 初始化失败
     */
    bool initialize(std::shared_ptr<Signaling> signaling);

    /**
     * 关闭WebRTC管理器
     */
    void shutdown();

    // ========== WebRTC连接管理 ==========
    /**
     * 创建PeerConnection
     * @return true 创建成功，false 创建失败
     */
    bool createPeerConnection();

    /**
     * 关闭PeerConnection
     */
    void closePeerConnection();

    /**
     * 处理角色分配 - 由信令模块调用
     * @param role 角色信息 (offerer/answerer)
     * @param peerDeviceId 对端设备ID
     */
    void handleRole(const std::string& role, const std::string& peerDeviceId);

    /**
     * 处理远程SDP Offer
     * @param sdp SDP内容
     */
    void handleRemoteOffer(const std::string& sdp);

    /**
     * 处理远程SDP Answer
     * @param sdp SDP内容
     */
    void handleRemoteAnswer(const std::string& sdp);

    /**
     * 处理远程ICE候选
     * @param candidate ICE候选信息
     */
    void handleRemoteIceCandidate(const std::string& candidate);

    // ========== 数据通道接口 ==========
    /**
     * 发送数据通道消息
     * @param message 消息内容
     * @return true 发送成功，false 发送失败
     */
    bool sendDataChannelMessage(const std::string& message);

    /**
     * 检查数据通道是否打开
     * @return true 已打开，false 未打开
     */
    bool isDataChannelOpen() const;

    // ========== 音频接口 ==========
    /**
     * 发送音频数据
     * @param data 音频数据
     * @param size 数据大小
     * @param timestamp 时间戳（微秒）
     */
    void sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp);

    // ========== 视频接口（预留空实现） ==========
    /**
     * 发送视频帧
     * @param data 视频帧数据
     * @param size 数据大小
     * @param timestamp 时间戳
     */
    void sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp);

    // ========== 状态查询接口 ==========
    WebRTCStatus getStatus() const { return status_; }
    const WebRTCConfig& getConfig() const { return config_; }
    
    // ICE状态查询接口
    bool isIceConnected() const { return isIceConnected_; }
    rtc::PeerConnection::IceState getIceState() const { return iceState_; }
    
    // 连接就绪检查
    bool isReadyForDataSending() const {
        return status_ == WebRTCStatus::CONNECTED && 
               isIceConnected_ && 
               videoTrack_ && 
               videoTrack_->isOpen();
    }

    // ========== 回调函数设置接口 ==========
    void onStatusChanged(WebRTCStatusCallback callback) { statusCallback_ = callback; }
    void onDataChannelMessage(DataChannelMessageCallback callback) { dataMessageCallback_ = callback; }
    void onAudioData(AudioDataCallback callback) { audioCallback_ = callback; }
    void onVideoFrame(VideoFrameCallback callback) { videoCallback_ = callback; }

private:
    // ========== 内部方法 ==========
    void setupPeerConnectionCallbacks();
    void setupDataChannel();
    void setupVideoTrack();
    void setupAudioTrack();
    void handleDataChannelOpen();
    void handleDataChannelMessage(const std::string& message);
    void handleAudioData(const uint8_t* data, size_t size, uint64_t timestamp);
    void setStatus(WebRTCStatus newStatus);
    
    // SCTP配置方法
    void configureSctpSettings();
    
    // ========== 成员变量 ==========
    WebRTCConfig config_;               // 配置信息
    WebRTCStatus status_;               // 连接状态
    rtc::PeerConnection::IceState iceState_;  // ICE连接状态
    bool isIceConnected_;                     // ICE是否已连接
    
    std::shared_ptr<Signaling> signaling_;          // 信令模块引用
    std::shared_ptr<rtc::PeerConnection> peerConnection_; // WebRTC连接
    std::shared_ptr<rtc::DataChannel> dataChannel_;       // 数据通道
    
    // 视频轨道相关成员
    std::shared_ptr<rtc::Track> videoTrack_;               // H264视频轨道
    std::shared_ptr<rtc::RtpPacketizationConfig> videoRtpConfig_; // 视频RTP配置
    std::shared_ptr<rtc::H264RtpPacketizer> videoPacketizer_;    // H264 RTP封装器
    std::shared_ptr<rtc::RtcpSrReporter> videoSrReporter_;       // RTCP SR报告器
    
    // 音频轨道相关成员
    std::shared_ptr<rtc::Track> audioTrack_;               // Opus音频轨道
    std::shared_ptr<rtc::RtpPacketizationConfig> audioRtpConfig_; // 音频RTP配置
    std::shared_ptr<rtc::OpusRtpPacketizer> audioPacketizer_;    // Opus RTP封装器
    std::shared_ptr<rtc::RtcpSrReporter> audioSrReporter_;       // 音频RTCP SR报告器
    
    // 拥塞控制相关成员
    std::shared_ptr<rtc::RtcpReceivingSession> videoRtcpSession_; // 视频RTCP接收会话
    std::shared_ptr<rtc::RtcpReceivingSession> audioRtcpSession_; // 音频RTCP接收会话
    std::shared_ptr<rtc::RembHandler> videoRembHandler_;          // 视频REMB处理器
    std::shared_ptr<rtc::RembHandler> audioRembHandler_;          // 音频REMB处理器
    
    std::string role_;                  // 角色信息
    std::string peerDeviceId_;          // 对端设备ID
    
    // 任务队列调度器
    std::unique_ptr<DispatchQueue> sendQueue_;              // 发送任务队列
    
    // 视频发送频率控制
    std::chrono::steady_clock::time_point lastVideoSendTime_;  // 上次视频发送时间
    std::chrono::steady_clock::time_point lastAudioSendTime_;  // 上次音频发送时间
    static constexpr int VIDEO_SEND_INTERVAL_MS = 1000 / CAMERA_FPS;          // 视频发送间隔
    static constexpr int AUDIO_SEND_INTERVAL_MS = AUDIO_FRAME_DURATION_MS;                         // 音频发送间隔(50fps)
    
    // 回调函数存储
    WebRTCStatusCallback statusCallback_;
    DataChannelMessageCallback dataMessageCallback_;
    AudioDataCallback audioCallback_;
    VideoFrameCallback videoCallback_;
    
    // 拥塞控制回调
    void onRembReceived(unsigned int bitrate);                 // REMB消息接收回调
};

} // namespace protocol
} // namespace glasses

#endif // WEBRTC_H
