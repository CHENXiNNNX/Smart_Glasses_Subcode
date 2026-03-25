/* webrtc.hpp - WebRTC系统 */

#pragma once

#include <memory>
#include <functional>
#include <string>
#include <mutex>
#include <atomic>
#include <chrono>
#include <rtc/rtc.hpp>
#include <rtc/pacinghandler.hpp>
#include <rtc/plihandler.hpp>
#include <rtc/rembhandler.hpp>
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
                IDLE = 0,                // 空闲状态
                WAITING_CONNECT_REQUEST, // 等待连接请求
                SDP_CONNECTING,          // SDP协商中（发送offer）
                SDP_CONNECTED,           // SDP协商完成（收到answer）
                ICE_CONNECTING,          // ICE候选交换中
                ICE_CONNECTED,           // ICE连接建立
                CONNECTED,               // WebRTC连接完全建立
                DISCONNECTING,           // 正在断开连接
                DISCONNECTED,            // 已断开连接
                FAILED                   // 发生错误
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

                bool   enable_video_pacing      = true;
                double video_pacing_bps         = 0;
                int    video_pacing_interval_ms = 10;
                unsigned int video_remb_min_bps = 200000;
                unsigned int video_remb_max_bps = 12000000;
            };

            // 回调函数类型定义
            using StateCallback     = std::function<void(WebRTCState)>;
            using ErrorCallback     = std::function<void(WebRTCError, const std::string&)>;
            using AudioDataCallback = std::function<void(const uint8_t*, size_t)>;
            using VideoDataCallback = std::function<void(const uint8_t*, size_t, uint64_t)>;

            /**
             * @brief WebRTC系统
             */
            class WebRTCSystem
            {
            public:
                explicit WebRTCSystem(const WebRTCConfig& config = WebRTCConfig{});
                ~WebRTCSystem();

                WebRTCSystem(const WebRTCSystem&)            = delete;
                WebRTCSystem& operator=(const WebRTCSystem&) = delete;
                WebRTCSystem(WebRTCSystem&&)                 = delete;
                WebRTCSystem& operator=(WebRTCSystem&&)      = delete;

                WebRTCError init(std::shared_ptr<Signaling> signaling);
                void        deinit();
                bool        isInitialized() const
                {
                    return initialized_.load();
                }

                bool sendConnectionRequest(const std::string& peer_id, bool enable_message = true,
                                           bool enable_audio = true, bool enable_video = false);
                void handleConnectionAccepted(const std::string& peer_id);
                void disconnect();

                void handleRemoteAnswer(const std::string& sdp);
                void handleIceCandidate(const std::string& candidate);

                void sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp);
                void sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp,
                                   bool is_keyframe = false);

                bool sendDataMessage(const std::string& message);

                WebRTCState getState() const
                {
                    return current_state_.load();
                }
                std::string getPeerId() const;
                bool        isConnected() const;
                bool        isConnecting() const;

                static const char* stateToString(WebRTCState state);
                static const char* peerConnectionStateToString(rtc::PeerConnection::State state);
                static const char* iceStateToString(rtc::PeerConnection::IceState state);

                void onStateChanged(StateCallback callback);
                void onError(ErrorCallback callback);
                void onAudioData(AudioDataCallback callback);
                void onVideoData(VideoDataCallback callback);

                void setVideoNetworkCallbacks(
                    std::function<void(unsigned int bitrate_bps)> on_receiver_remb,
                    std::function<void()> on_receiver_keyframe_request);

                struct Stats
                {
                    uint64_t audio_packets_sent     = 0;
                    uint64_t audio_packets_received = 0;
                    uint64_t video_packets_sent     = 0;
                    uint64_t video_packets_received = 0;
                    uint64_t audio_bytes_sent       = 0;
                    uint64_t video_bytes_sent       = 0;
                    uint64_t connection_duration_ms = 0;
                };
                Stats get_stats() const;
                void  reset_stats();

            private:
                WebRTCConfig      config_;
                std::atomic<bool> initialized_{false};

                std::atomic<WebRTCState> current_state_{WebRTCState::IDLE};
                mutable std::mutex       state_mutex_;
                std::string              peer_device_id_;

                std::shared_ptr<Signaling>           signaling_;
                std::shared_ptr<rtc::PeerConnection> peer_connection_;
                std::shared_ptr<rtc::Track>          audio_track_;
                std::shared_ptr<rtc::Track>          video_track_;
                std::shared_ptr<rtc::DataChannel>    data_channel_;

                std::shared_ptr<rtc::RtpPacketizationConfig> audio_rtp_config_;
                std::shared_ptr<rtc::OpusRtpPacketizer>      audio_packetizer_;
                std::shared_ptr<rtc::RtcpSrReporter>         audio_sr_reporter_;
                std::shared_ptr<rtc::RtcpReceivingSession>   audio_rtcp_session_;

                std::shared_ptr<rtc::RtpPacketizationConfig> video_rtp_config_;
                std::shared_ptr<rtc::H264RtpPacketizer>      video_packetizer_;
                std::shared_ptr<rtc::RtcpSrReporter>         video_sr_reporter_;
                std::shared_ptr<rtc::RtcpReceivingSession>   video_rtcp_session_;
                std::shared_ptr<rtc::PliHandler>             video_pli_handler_;
                std::shared_ptr<rtc::RembHandler>            video_remb_handler_;
                std::shared_ptr<rtc::PacingHandler>          video_pacing_handler_;

                std::mutex                         video_net_cb_mutex_;
                std::function<void(unsigned int)>  video_on_remb_bps_;
                std::function<void()>              video_on_keyframe_req_;

                std::chrono::steady_clock::time_point last_audio_send_time_;
                static constexpr int AUDIO_SEND_INTERVAL_MS = AUDIO_FRAME_DURATION_MS;

                mutable std::mutex callback_mutex_;
                StateCallback      state_callback_;
                ErrorCallback      error_callback_;
                AudioDataCallback  audio_callback_;
                VideoDataCallback  video_callback_;

                mutable std::mutex                    stats_mutex_;
                Stats                                 stats_;
                std::chrono::steady_clock::time_point connection_start_time_;

                std::vector<std::string> pending_ice_candidates_;
                std::mutex               ice_candidates_mutex_;
                std::atomic<bool>        sdp_exchange_completed_{false};

                bool              connection_request_sent_ = false;
                ConnectionRequest current_connection_request_;

                void setState(WebRTCState new_state);
                void handleStateTransition(WebRTCState old_state, WebRTCState new_state);

                void startWebRTCConnection();
                void createPeerConnection();
                void createLocalTracks();
                void generateAndSendOffer();
                void processRemoteAnswer(const std::string& sdp);
                void flushPendingIceCandidates();

                void setupAudioTrack(rtc::Description::Direction direction);
                void setupVideoTrack(rtc::Description::Direction direction);
                void configureTrackCallbacks();

                void setupDataChannel();
                void configureDataChannelCallbacks();
                void handleDataChannelMessage(const std::string& message);

                void setupPeerConnectionCallbacks();
                void onLocalDescriptionGenerated(rtc::Description desc);
                void onIceCandidateGenerated(rtc::Candidate candidate);
                void onPeerConnectionStateChange(rtc::PeerConnection::State state);
                void onIceStateChange(rtc::PeerConnection::IceState state);

                void handleAudioDataReceived(const uint8_t* data, size_t size);
                void handleVideoDataReceived(const uint8_t* data, size_t size, uint64_t timestamp);

                void cleanup();
                bool validateConfiguration() const;
                bool parseRtpPacket(const uint8_t* rtp_data, size_t rtp_size,
                                    const uint8_t*& payload_data, size_t& payload_size);
                void invokeErrorCallback(WebRTCError error, const std::string& message);
            };

        } // namespace webrtc
    }     // namespace protocol
} // namespace app
