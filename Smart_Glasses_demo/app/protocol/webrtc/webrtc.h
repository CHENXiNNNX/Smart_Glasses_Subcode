#ifndef WEBRTC_H
#define WEBRTC_H

#include <memory>
#include <functional>
#include <string>
#include <rtc/rtc.hpp>

namespace glasses {
namespace protocol {

// 前向声明
class Signaling;

// WebRTC功能配置结构体
struct WebRTCConfig {
    // 功能开关
    bool enableDataChannel = true;      // 数据通道开关
    bool enableAudioSend = false;       // 音频发送开关（预留）
    bool enableAudioReceive = false;    // 音频接收开关（预留）
    bool enableVideoSend = false;       // 视频发送开关（预留）
    
    // 数据通道配置
    std::string dataChannelLabel = "glasses_data_channel";
    
    // 音频配置（预留）
    struct AudioConfig {
        std::string codec = "opus";
        int sampleRate = 16000;
        int channels = 1;
        int bitrate = 32000;
    } audioConfig;
    
    // 视频配置（预留）
    struct VideoConfig {
        std::string codec = "h264";
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int bitrate = 2000000;
    } videoConfig;
    
    // STUN服务器配置
    std::vector<std::string> stunServers = {
        "stun:stun.l.google.com:19302"
    };
};

// WebRTC连接状态枚举
enum class WebRTCStatus {
    DISCONNECTED,   // 未连接
    CONNECTING,     // 连接中
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
 * 职责：PeerConnection管理、DataChannel管理、音视频传输（预留）
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

    // ========== 音频接口（预留空实现） ==========
    /**
     * 开始音频发送
     * @return true 成功，false 失败
     */
    bool startAudioSend() { 
        // TODO: 实现音频发送功能
        return false; 
    }

    /**
     * 停止音频发送
     * @return true 成功，false 失败
     */
    bool stopAudioSend() { 
        // TODO: 实现音频停止功能
        return false; 
    }

    /**
     * 开始音频接收
     * @return true 成功，false 失败
     */
    bool startAudioReceive() { 
        // TODO: 实现音频接收功能
        return false; 
    }

    /**
     * 停止音频接收
     * @return true 成功，false 失败
     */
    bool stopAudioReceive() { 
        // TODO: 实现音频停止功能
        return false; 
    }

    /**
     * 发送音频数据
     * @param data 音频数据
     * @param size 数据大小
     */
    void sendAudioData(const uint8_t* data, size_t size) {
        // TODO: 实现音频数据发送
        (void)data; (void)size; // 避免编译警告
    }

    // ========== 视频接口（预留空实现） ==========
    /**
     * 开始视频发送
     * @return true 成功，false 失败
     */
    bool startVideoSend() { 
        // TODO: 实现视频发送功能
        return false; 
    }

    /**
     * 停止视频发送
     * @return true 成功，false 失败
     */
    bool stopVideoSend() { 
        // TODO: 实现视频停止功能
        return false; 
    }

    /**
     * 发送视频帧
     * @param data 视频帧数据
     * @param size 数据大小
     * @param timestamp 时间戳
     */
    void sendVideoFrame(const uint8_t* data, size_t size, uint64_t timestamp) {
        // TODO: 实现视频帧发送
        (void)data; (void)size; (void)timestamp; // 避免编译警告
    }

    // ========== 状态查询接口 ==========
    WebRTCStatus getStatus() const { return status_; }
    const WebRTCConfig& getConfig() const { return config_; }

    // ========== 回调函数设置接口 ==========
    void onStatusChanged(WebRTCStatusCallback callback) { statusCallback_ = callback; }
    void onDataChannelMessage(DataChannelMessageCallback callback) { dataMessageCallback_ = callback; }
    void onAudioData(AudioDataCallback callback) { audioCallback_ = callback; }
    void onVideoFrame(VideoFrameCallback callback) { videoCallback_ = callback; }

private:
    // ========== 内部方法 ==========
    void setupPeerConnectionCallbacks();
    void setupDataChannel();
    void handleDataChannelOpen();
    void handleDataChannelMessage(const std::string& message);
    void setStatus(WebRTCStatus newStatus);
    
    // ========== 成员变量 ==========
    WebRTCConfig config_;               // 配置信息
    WebRTCStatus status_;               // 连接状态
    
    std::shared_ptr<Signaling> signaling_;          // 信令模块引用
    std::shared_ptr<rtc::PeerConnection> peerConnection_; // WebRTC连接
    std::shared_ptr<rtc::DataChannel> dataChannel_;       // 数据通道
    
    std::string role_;                  // 角色信息
    std::string peerDeviceId_;          // 对端设备ID
    
    // 回调函数存储
    WebRTCStatusCallback statusCallback_;
    DataChannelMessageCallback dataMessageCallback_;
    AudioDataCallback audioCallback_;
    VideoFrameCallback videoCallback_;
};

} // namespace protocol
} // namespace glasses

#endif // WEBRTC_H