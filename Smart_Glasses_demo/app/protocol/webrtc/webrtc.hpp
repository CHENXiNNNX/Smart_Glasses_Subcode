#ifndef WEBRTC_HPP
#define WEBRTC_HPP

#include <memory>
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <rtc/rtc.hpp>
#include <rtc/track.hpp>
#include "signaling.hpp"
#include "../../media/media_config.hpp"

namespace app
{
    namespace protocol
    {
        namespace webrtc
        {
            /**
             * @brief WebRTC系统状态
             */
            enum class WebRTCState
            {
                IDLE = 0,       // 空闲状态
                WAITING_CONNECT_REQUEST, // 等待连接请求
                SDP_CONNECTING, // SDP协商中（发送offer）
                SDP_CONNECTED,  // SDP协商完成（收到answer）
                ICE_CONNECTING, // ICE候选交换中
                ICE_CONNECTED,  // ICE连接建立
                CONNECTED,      // WebRTC连接完全建立
                DISCONNECTING,  // 正在断开连接
                DISCONNECTED,   // 已断开连接
                FAILED          // 发生错误
            };

            /**
             * @brief WebRTC错误类型
             */
            enum class WebRTCError
            {
                NONE = 0,               // 无错误
                SIGNALING_FAILED,       // 信令失败
                SDP_NEGOTIATION_FAILED, // SDP协商失败
                ICE_CANDIDATE_FAILED,   // ICE候选交换失败
                CONNECTION_FAILED,      // 连接失败
                TIMEOUT,                // 超时
                UNKNOWN                 // 未知错误
            };

            /**
             * @brief WebRTC配置
             */
            struct WebRTCConfig
            {
                // 音频配置
                struct
                {
                    std::string codec        = "opus";
                    int         sample_rate  = AUDIO_SAMPLE_RATE; // 48000
                    int         channels     = AUDIO_CHANNELS;    // 1
                    int         payload_type = 111;
                    uint32_t    ssrc         = 2;
                } audio;

                // 视频配置
                struct
                {
                    std::string codec        = "h264";
                    int         width        = CAMERA_WIDTH;
                    int         height       = CAMERA_HEIGHT;
                    int         payload_type = 103;
                    uint32_t    ssrc         = 1;
                } video;

                // ICE服务器配置
                struct
                {
                    std::vector<std::string> stun_servers = {"stun:stun.l.google.com:19302"};
                    std::vector<std::string> turn_servers;
                    bool                     use_relay_only = false;
                } ice;

                // ICE超时配置
                struct
                {
                    int ice_timeout_ms = 15000; // 15秒ICE超时
                } timeout;

                // 性能配置
                struct
                {
                    size_t audio_thread_count = 1;
                    size_t video_thread_count = 2;
                } performance;
            };

            // 回调函数类型定义
            using StateCallback       = std::function<void(WebRTCState)>;
            using ErrorCallback       = std::function<void(WebRTCError, const std::string&)>;
            using AudioDataCallback   = std::function<void(const uint8_t*, size_t)>;
            using VideoDataCallback   = std::function<void(const uint8_t*, size_t, uint64_t)>;

            /**
             * @brief WebRTC系统
             */
            class WebRTCSystem
            {
            public:
                // ========================================================================
                // 构造和析构
                // ========================================================================
                explicit WebRTCSystem(const WebRTCConfig& config = WebRTCConfig{});
                ~WebRTCSystem();

                // 禁用拷贝和移动
                WebRTCSystem(const WebRTCSystem&)            = delete;
                WebRTCSystem& operator=(const WebRTCSystem&) = delete;
                WebRTCSystem(WebRTCSystem&&)                 = delete;
                WebRTCSystem& operator=(WebRTCSystem&&)      = delete;

                // ========================================================================
                // 生命周期管理
                // ========================================================================

                /**
                 * @brief 初始化WebRTC系统
                 * @param signaling 信令模块
                 * @return 错误码
                 */
                WebRTCError initialize(std::shared_ptr<Signaling> signaling);

                /**
                 * @brief 关闭WebRTC系统
                 */
                void shutdown();

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_.load();
                }

                // ========================================================================
                // 连接管理 - 设备端连接流程
                // ========================================================================

                /**
                 * @brief 发送连接请求
                 * 设备端发送get_connect消息到APP端，请求建立WebRTC连接
                 * @param peer_id 对端设备ID
                 * @param enable_message 是否开启消息通道
                 * @param enable_audio 是否开启音频通道
                 * @param enable_video 是否开启视频通道
                 * @return 是否发送成功
                 */
                bool sendConnectionRequest(const std::string& peer_id,
                                         bool enable_message = true,
                                         bool enable_audio = true,
                                         bool enable_video = false);

                /**
                 * @brief 处理连接请求响应
                 * 当APP端同意连接请求后，开始建立WebRTC连接
                 * @param peer_id 对端设备ID
                 */
                void handleConnectionAccepted(const std::string& peer_id);

                /**
                 * @brief 断开连接
                 */
                void disconnect();

                // ========================================================================
                // 信令处理 
                // ========================================================================

                /**
                 * @brief 处理远程Answer
                 * 设备端接收APP端发送的SDP Answer
                 * @param sdp SDP字符串
                 */
                void handleRemoteAnswer(const std::string& sdp);

                /**
                 * @brief 处理ICE候选
                 * 处理APP端发送的ICE候选信息
                 * @param candidate ICE候选字符串
                 */
                void handleIceCandidate(const std::string& candidate);

                // ========================================================================
                // 媒体传输
                // ========================================================================

                /**
                 * @brief 发送音频数据
                 * @param data 音频数据指针
                 * @param size 数据大小
                 * @param timestamp 时间戳（微秒）
                 */
                void sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp);

                /**
                 * @brief 发送视频数据
                 * @param data 视频数据指针
                 * @param size 数据大小
                 * @param timestamp 时间戳（微秒）
                 * @param is_keyframe 是否为关键帧
                 */
                void sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp,
                                   bool is_keyframe = false);

                /**
                 * @brief 发送数据通道消息
                 * @param message 消息内容
                 */
                bool sendDataMessage(const std::string& message);

                // ========================================================================
                // 状态查询
                // ========================================================================

                /**
                 * @brief 获取当前状态
                 */
                WebRTCState getState() const
                {
                    return current_state_.load();
                }

                /**
                 * @brief 获取对端设备ID
                 */
                std::string getPeerId() const
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    return peer_device_id_;
                }

                /**
                 * @brief 检查是否已连接
                 */
                bool isConnected() const
                {
                    WebRTCState state = current_state_.load();
                    return state == WebRTCState::ICE_CONNECTED || state == WebRTCState::CONNECTED;
                }

                // ========================================================================
                // 状态转字符串辅助函数
                // ========================================================================

                /**
                 * @brief 将WebRTCState转换为字符串
                 */
                static const char* stateToString(WebRTCState state);

                /**
                 * @brief 将PeerConnection::State转换为字符串
                 */
                static const char* peerConnectionStateToString(rtc::PeerConnection::State state);

                /**
                 * @brief 将IceState转换为字符串
                 */
                static const char* iceStateToString(rtc::PeerConnection::IceState state);

                /**
                 * @brief 检查是否正在连接中
                 */
                bool isConnecting() const;

                // ========================================================================
                // 回调设置
                // ========================================================================

                /**
                 * @brief 设置状态变化回调
                 */
                void onStateChanged(StateCallback callback);

                /**
                 * @brief 设置错误回调
                 */
                void onError(ErrorCallback callback);

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
                struct Stats
                {
                    uint64_t audio_packets_sent     = 0;
                    uint64_t audio_packets_received = 0;
                    uint64_t video_packets_sent     = 0;
                    uint64_t video_packets_received = 0;
                    uint64_t audio_bytes_sent       = 0;
                    uint64_t video_bytes_sent       = 0;
                    uint64_t connection_duration_ms = 0; // 连接持续时间
                };

                /**
                 * @brief 获取统计信息
                 */
                Stats getStats() const;

                /**
                 * @brief 重置统计信息
                 */
                void resetStats();

            private:
                // ========================================================================
                // 私有成员变量
                // ========================================================================

                // 配置和初始化
                WebRTCConfig      config_;
                std::atomic<bool> initialized_{false};

                // 状态管理
                std::atomic<WebRTCState> current_state_{WebRTCState::IDLE};
                mutable std::mutex       state_mutex_;
                std::string              peer_device_id_;

                // WebRTC核心组件
                std::shared_ptr<Signaling>           signaling_;
                std::shared_ptr<rtc::PeerConnection> peer_connection_;
                std::shared_ptr<rtc::Track>          audio_track_;
                std::shared_ptr<rtc::Track>          video_track_;
                std::shared_ptr<rtc::DataChannel>    data_channel_;

                // 媒体处理组件
                std::shared_ptr<rtc::RtpPacketizationConfig> audio_rtp_config_;
                std::shared_ptr<rtc::OpusRtpPacketizer>      audio_packetizer_;
                std::shared_ptr<rtc::RtcpSrReporter>         audio_sr_reporter_;
                std::shared_ptr<rtc::RtcpReceivingSession>   audio_rtcp_session_;

                std::shared_ptr<rtc::RtpPacketizationConfig> video_rtp_config_;
                std::shared_ptr<rtc::H264RtpPacketizer>      video_packetizer_;
                std::shared_ptr<rtc::RtcpSrReporter>         video_sr_reporter_;
                std::shared_ptr<rtc::RtcpReceivingSession>   video_rtcp_session_;

                // 发送控制
                std::chrono::steady_clock::time_point last_audio_send_time_;
                std::chrono::steady_clock::time_point last_video_send_time_;
                static constexpr int AUDIO_SEND_INTERVAL_MS = AUDIO_FRAME_DURATION_MS;
                static constexpr int VIDEO_SEND_INTERVAL_MS = 1000 / CAMERA_FPS;

                // 回调函数
                mutable std::mutex  callback_mutex_;
                StateCallback       state_callback_;
                ErrorCallback       error_callback_;
                AudioDataCallback   audio_callback_;
                VideoDataCallback   video_callback_;

                // 统计信息
                mutable std::mutex stats_mutex_;
                Stats              stats_;
                std::chrono::steady_clock::time_point connection_start_time_;

                // ICE候选缓存
                std::vector<std::string> pending_ice_candidates_;
                std::mutex              ice_candidates_mutex_;
                std::atomic<bool>       sdp_exchange_completed_{false};

                // 连接请求相关
                bool connection_request_sent_ = false;
                ConnectionRequest current_connection_request_;  // 保存当前连接请求参数

                // ========================================================================
                // 私有方法
                // ========================================================================

                // 状态管理
                void setState(WebRTCState new_state);
                void handleStateTransition(WebRTCState old_state, WebRTCState new_state);

                // WebRTC建立流程
                void startWebRTCConnection();
                void createPeerConnection();
                void createLocalTracks();
                void generateAndSendOffer();
                void processRemoteAnswer(const std::string& sdp);
                void flushPendingIceCandidates();

                // 轨道管理
                void setupAudioTrack(rtc::Description::Direction direction);
                void setupVideoTrack(rtc::Description::Direction direction);
                void configureTrackCallbacks();

                // 数据通道管理
                void setupDataChannel();
                void configureDataChannelCallbacks();
                void handleDataChannelMessage(const std::string& message);

                // PeerConnection回调
                void setupPeerConnectionCallbacks();
                void onLocalDescriptionGenerated(rtc::Description desc);
                void onIceCandidateGenerated(rtc::Candidate candidate);
                void onPeerConnectionStateChange(rtc::PeerConnection::State state);
                void onIceStateChange(rtc::PeerConnection::IceState state);

                // 媒体数据处理
                void handleAudioDataReceived(const uint8_t* data, size_t size);
                void handleVideoDataReceived(const uint8_t* data, size_t size, uint64_t timestamp);

                // 工具方法
                void cleanup();
                bool validateConfiguration() const;
                bool parseRtpPacket(const uint8_t* rtp_data, size_t rtp_size,
                                    const uint8_t*& payload_data, size_t& payload_size);

                // 错误回调触发
                void invokeErrorCallback(WebRTCError error, const std::string& message);

            };

        } // namespace webrtc
    }     // namespace protocol
} // namespace app

#endif // WEBRTC_HPP
