#include "webrtc.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/time/time.hpp"
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>

using namespace app::tool::log;

namespace app
{
    namespace protocol
    {
        namespace webrtc
        {
            namespace
            {
                constexpr const char* LOG_TAG                  = "WEBRTC";
                constexpr size_t      RTP_HEADER_MIN_SIZE      = 12;
                constexpr uint8_t     RTP_CC_MASK              = 0x0F;
                constexpr uint8_t     RTP_EXTENSION_MASK       = 0x10;
                constexpr uint8_t     RTP_PADDING_MASK         = 0x20;
                constexpr size_t      RTP_EXTENSION_HEADER_LEN = 4;
                constexpr size_t      RTP_CSRC_SIZE            = 4;
            } // namespace

            WebRTCSystem::WebRTCSystem(const WebRTCConfig& config)
                : config_(config), last_audio_send_time_()
            {
                LOG_INFO(LOG_TAG, "WebRTC就绪");
            }

            WebRTCSystem::~WebRTCSystem()
            {
                deinit();
                LOG_INFO(LOG_TAG, "WebRTC已销毁");
            }

            WebRTCError WebRTCSystem::init(std::shared_ptr<Signaling> signaling)
            {
                if (!signaling)
                {
                    LOG_ERROR(LOG_TAG, "信令模块为空");
                    return WebRTCError::SIGNALING_FAILED;
                }
                if (initialized_.load())
                {
                    LOG_WARN(LOG_TAG, "重复初始化");
                    return WebRTCError::NONE;
                }
                if (!validateConfiguration())
                {
                    LOG_ERROR(LOG_TAG, "配置验证失败");
                    return WebRTCError::UNKNOWN;
                }

                LOG_INFO(LOG_TAG, "初始化WebRTC");
                signaling_ = std::move(signaling);

                signaling_->onWebRTCReady(
                    [this](const std::string& role, const std::string& peer_id) {
                        LOG_INFO(LOG_TAG, "配对成功，角色: %s, 对端: %s", role.c_str(),
                                 peer_id.c_str());
                    });

                signaling_->onAnswerReceived(
                    [this](const nlohmann::json& msg)
                    {
                        if (msg.contains("data") && msg["data"].contains("sdp"))
                            handleRemoteAnswer(msg["data"]["sdp"].get<std::string>());
                    });

                signaling_->onIceCandidateReceived(
                    [this](const nlohmann::json& msg)
                    {
                        if (msg.contains("data") && msg["data"].contains("candidate"))
                            handleIceCandidate(msg["data"]["candidate"].get<std::string>());
                    });

                signaling_->onConnectionRequestReceived(
                    [this](const ConnectionRequest& request)
                    {
                        {
                            std::lock_guard<std::mutex> lock(state_mutex_);
                            current_connection_request_ = request;
                        }
                        LOG_INFO(
                            LOG_TAG,
                            "收到连接请求 (message=%d, audio=%d, video=%d)，开始建立WebRTC连接",
                            request.message, request.audio, request.video);
                        std::string peer_id = signaling_->getPeerDeviceId();
                        if (!peer_id.empty())
                            handleConnectionAccepted(peer_id);
                        else
                            LOG_WARN(LOG_TAG, "无法获取对端设备ID");
                    });

                signaling_->onConnectionResponseReceived(
                    [this](bool accepted)
                    {
                        if (accepted)
                        {
                            LOG_INFO(LOG_TAG, "APP端同意连接请求，开始建立WebRTC连接");
                            std::string peer_id = signaling_->getPeerDeviceId();
                            if (!peer_id.empty())
                                handleConnectionAccepted(peer_id);
                            else
                                LOG_WARN(LOG_TAG, "无法获取对端设备ID");
                        }
                        else
                        {
                            LOG_WARN(LOG_TAG, "APP端拒绝连接请求");
                            setState(WebRTCState::FAILED);
                            invokeErrorCallback(WebRTCError::CONNECTION_FAILED,
                                                "APP端拒绝连接请求");
                        }
                    });

                initialized_.store(true);
                LOG_INFO(LOG_TAG, "WebRTC初始化完成");
                return WebRTCError::NONE;
            }

            void WebRTCSystem::deinit()
            {
                if (!initialized_.load())
                    return;
                LOG_INFO(LOG_TAG, "关闭WebRTC");
                disconnect();
                cleanup();
                signaling_.reset();
                initialized_.store(false);
                setState(WebRTCState::IDLE);
                LOG_INFO(LOG_TAG, "WebRTC已关闭");
            }

            bool WebRTCSystem::sendConnectionRequest(const std::string& peer_id,
                                                     bool enable_message, bool enable_audio,
                                                     bool enable_video)
            {
                if (!isInitialized() || !signaling_)
                {
                    LOG_ERROR(LOG_TAG, "系统未初始化或信令未就绪");
                    return false;
                }
                if (current_state_.load() != WebRTCState::IDLE &&
                    current_state_.load() != WebRTCState::DISCONNECTED)
                {
                    LOG_WARN(LOG_TAG, "当前状态不允许发送连接请求: %d",
                             static_cast<int>(current_state_.load()));
                    return false;
                }

                ConnectionRequest request;
                request.message = enable_message;
                request.audio   = enable_audio;
                request.video   = enable_video;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    current_connection_request_ = request;
                    peer_device_id_             = peer_id;
                }

                if (signaling_->sendConnectionRequest(peer_id, request))
                {
                    connection_request_sent_ = true;
                    setState(WebRTCState::WAITING_CONNECT_REQUEST);
                    LOG_INFO(LOG_TAG, "发送连接请求到: %s (message=%d, audio=%d, video=%d)",
                             peer_id.c_str(), enable_message, enable_audio, enable_video);
                    return true;
                }
                LOG_ERROR(LOG_TAG, "发送连接请求失败");
                return false;
            }

            void WebRTCSystem::handleConnectionAccepted(const std::string& peer_id)
            {
                if (!isInitialized())
                {
                    LOG_ERROR(LOG_TAG, "系统未初始化");
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    if (peer_device_id_ != peer_id)
                        peer_device_id_ = peer_id;
                }
                WebRTCState current = current_state_.load();
                if (current == WebRTCState::WAITING_CONNECT_REQUEST || current == WebRTCState::IDLE)
                {
                    LOG_INFO(LOG_TAG, "连接请求已接受，开始建立WebRTC连接");
                    startWebRTCConnection();
                }
                else
                    LOG_WARN(LOG_TAG, "收到连接接受但状态不正确: %d", static_cast<int>(current));
            }

            void WebRTCSystem::disconnect()
            {
                WebRTCState current = current_state_.load();
                if (current == WebRTCState::IDLE || current == WebRTCState::DISCONNECTED ||
                    current == WebRTCState::DISCONNECTING)
                    return;
                LOG_INFO(LOG_TAG, "主动断开连接");
                setState(WebRTCState::DISCONNECTING);
            }

            void WebRTCSystem::handleRemoteAnswer(const std::string& sdp)
            {
                if (!peer_connection_)
                {
                    LOG_ERROR(LOG_TAG, "PeerConnection未创建");
                    return;
                }
                WebRTCState current = current_state_.load();
                if (current != WebRTCState::SDP_CONNECTING)
                {
                    LOG_WARN(LOG_TAG, "收到Answer但状态不正确: %d", static_cast<int>(current));
                    return;
                }
                try
                {
                    LOG_INFO(LOG_TAG, "处理远程Answer");
                    processRemoteAnswer(sdp);
                    setState(WebRTCState::SDP_CONNECTED);
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "处理Answer失败: %s", e.what());
                    setState(WebRTCState::FAILED);
                    invokeErrorCallback(WebRTCError::SDP_NEGOTIATION_FAILED, e.what());
                }
            }

            void WebRTCSystem::handleIceCandidate(const std::string& candidate)
            {
                if (!peer_connection_)
                {
                    LOG_WARN(LOG_TAG, "PeerConnection未创建，忽略ICE候选");
                    return;
                }
                try
                {
                    rtc::Candidate rtc_candidate(candidate);
                    peer_connection_->addRemoteCandidate(rtc_candidate);
                    LOG_DEBUG(LOG_TAG, "添加远程ICE候选成功");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "添加远程ICE候选失败: %s", e.what());
                }
            }

            void WebRTCSystem::sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp)
            {
                if (!isConnected() || !audio_track_ || !audio_track_->isOpen())
                    return;

                auto now = std::chrono::steady_clock::now();
                if (last_audio_send_time_ != std::chrono::steady_clock::time_point{})
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_audio_send_time_);
                    if (elapsed.count() < AUDIO_SEND_INTERVAL_MS)
                        return;
                }
                last_audio_send_time_ = now;

                try
                {
                    auto sample_time = std::chrono::duration<double, std::micro>(timestamp);
                    audio_track_->sendFrame(reinterpret_cast<const std::byte*>(data), size,
                                            sample_time);
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.audio_packets_sent++;
                    stats_.audio_bytes_sent += size;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "发送音频失败: %s", e.what());
                }
            }

            void WebRTCSystem::sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp,
                                             bool /* is_keyframe */)
            {
                if (!isConnected() || !video_track_ || !video_track_->isOpen())
                    return;

                try
                {
                    auto sample_time = std::chrono::duration<double, std::micro>(timestamp);
                    video_track_->sendFrame(reinterpret_cast<const std::byte*>(data), size,
                                            sample_time);
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.video_packets_sent++;
                    stats_.video_bytes_sent += size;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "发送视频失败: %s", e.what());
                }
            }

            bool WebRTCSystem::isConnecting() const
            {
                WebRTCState state = current_state_.load();
                return state == WebRTCState::WAITING_CONNECT_REQUEST ||
                       state == WebRTCState::SDP_CONNECTING ||
                       state == WebRTCState::SDP_CONNECTED ||
                       state == WebRTCState::ICE_CONNECTING || state == WebRTCState::ICE_CONNECTED;
            }

            void WebRTCSystem::onStateChanged(StateCallback callback)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                state_callback_ = std::move(callback);
            }

            void WebRTCSystem::onError(ErrorCallback callback)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                error_callback_ = std::move(callback);
            }

            void WebRTCSystem::onAudioData(AudioDataCallback callback)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                audio_callback_ = std::move(callback);
            }

            void WebRTCSystem::onVideoData(VideoDataCallback callback)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                video_callback_ = std::move(callback);
            }

            void WebRTCSystem::setVideoNetworkCallbacks(
                std::function<void(unsigned int bitrate_bps)> on_receiver_remb,
                std::function<void()> on_receiver_keyframe_request)
            {
                std::lock_guard<std::mutex> lock(video_net_cb_mutex_);
                video_on_remb_bps_     = std::move(on_receiver_remb);
                video_on_keyframe_req_ = std::move(on_receiver_keyframe_request);
            }

            WebRTCSystem::Stats WebRTCSystem::get_stats() const
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                Stats                       result = stats_;
                if (connection_start_time_ != std::chrono::steady_clock::time_point{} &&
                    isConnected())
                {
                    auto now  = std::chrono::steady_clock::now();
                    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - connection_start_time_);
                    result.connection_duration_ms = diff.count();
                }
                return result;
            }

            void WebRTCSystem::reset_stats()
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_ = Stats{};
            }

            std::string WebRTCSystem::getPeerId() const
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                return peer_device_id_;
            }

            bool WebRTCSystem::isConnected() const
            {
                WebRTCState state = current_state_.load();
                return state == WebRTCState::ICE_CONNECTED || state == WebRTCState::CONNECTED;
            }

            void WebRTCSystem::setState(WebRTCState new_state)
            {
                WebRTCState old_state = current_state_.exchange(new_state);
                if (old_state != new_state)
                {
                    LOG_INFO(LOG_TAG, "状态转换: %s -> %s", stateToString(old_state),
                             stateToString(new_state));
                    handleStateTransition(old_state, new_state);
                }
            }

            void WebRTCSystem::handleStateTransition(WebRTCState /* old_state */,
                                                     WebRTCState new_state)
            {
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    if (state_callback_)
                        state_callback_(new_state);
                }
                switch (new_state)
                {
                case WebRTCState::CONNECTED:
                    connection_start_time_ = std::chrono::steady_clock::now();
                    LOG_INFO(LOG_TAG, "WebRTC连接已建立，可以进行媒体传输");
                    break;
                case WebRTCState::DISCONNECTED:
                case WebRTCState::FAILED:
                    connection_start_time_ = std::chrono::steady_clock::time_point{};
                    break;
                default:
                    break;
                }
            }

            void WebRTCSystem::startWebRTCConnection()
            {
                try
                {
                    LOG_INFO(LOG_TAG, "建立WebRTC连接");
                    createPeerConnection();
                    createLocalTracks();
                    setState(WebRTCState::SDP_CONNECTING);
                    generateAndSendOffer();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "建立WebRTC连接失败: %s", e.what());
                    setState(WebRTCState::FAILED);
                    invokeErrorCallback(WebRTCError::CONNECTION_FAILED, e.what());
                }
            }

            void WebRTCSystem::createPeerConnection()
            {
                rtc::Configuration rtc_config;
                for (const auto& stun : config_.ice.stun_servers)
                {
                    rtc_config.iceServers.emplace_back(stun);
                    LOG_DEBUG(LOG_TAG, "STUN %s", stun.c_str());
                }
                for (const auto& turn : config_.ice.turn_servers)
                {
                    rtc_config.iceServers.emplace_back(turn);
                    LOG_DEBUG(LOG_TAG, "TURN %s", turn.c_str());
                }
                rtc_config.iceTransportPolicy     = config_.ice.use_relay_only
                                                        ? rtc::TransportPolicy::Relay
                                                        : rtc::TransportPolicy::All;
                rtc_config.disableAutoNegotiation = true;

                peer_connection_ = std::make_shared<rtc::PeerConnection>(rtc_config);
                setupPeerConnectionCallbacks();
                sdp_exchange_completed_.store(false);
                {
                    std::lock_guard<std::mutex> lock(ice_candidates_mutex_);
                    pending_ice_candidates_.clear();
                }
                LOG_INFO(LOG_TAG, "PeerConnection创建成功");
            }

            void WebRTCSystem::createLocalTracks()
            {
                LOG_INFO(LOG_TAG, "创建本地轨道");
                ConnectionRequest request;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    request = current_connection_request_;
                }
                if (request.message)
                    setupDataChannel();
                if (request.audio)
                    setupAudioTrack(rtc::Description::Direction::SendRecv);
                if (request.video)
                    setupVideoTrack(rtc::Description::Direction::SendOnly);
            }

            void WebRTCSystem::generateAndSendOffer()
            {
                if (!peer_connection_)
                {
                    LOG_ERROR(LOG_TAG, "PeerConnection未创建");
                    return;
                }
                try
                {
                    LOG_INFO(LOG_TAG, "生成Offer");
                    peer_connection_->setLocalDescription();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "生成Offer失败: %s", e.what());
                    setState(WebRTCState::FAILED);
                    invokeErrorCallback(WebRTCError::SDP_NEGOTIATION_FAILED, e.what());
                }
            }

            void WebRTCSystem::processRemoteAnswer(const std::string& sdp)
            {
                if (!peer_connection_)
                {
                    LOG_ERROR(LOG_TAG, "PeerConnection未创建");
                    return;
                }
                rtc::Description remote_desc(sdp, rtc::Description::Type::Answer);
                peer_connection_->setRemoteDescription(remote_desc);
                sdp_exchange_completed_.store(true);
                LOG_INFO(LOG_TAG, "SDP交换完成，开始发送ICE候选");
                flushPendingIceCandidates();
            }

            void WebRTCSystem::flushPendingIceCandidates()
            {
                std::lock_guard<std::mutex> lock(ice_candidates_mutex_);
                if (pending_ice_candidates_.empty())
                    return;
                LOG_INFO(LOG_TAG, "发送缓存ICE候选 数量=%u",
                         static_cast<unsigned>(pending_ice_candidates_.size()));
                std::string peer_id;
                {
                    std::lock_guard<std::mutex> state_lock(state_mutex_);
                    peer_id = peer_device_id_;
                }
                for (const auto& candidate : pending_ice_candidates_)
                {
                    if (signaling_ && signaling_->isPaired())
                        signaling_->sendIceCandidate(candidate, peer_id);
                }
                pending_ice_candidates_.clear();
            }

            void WebRTCSystem::setupAudioTrack(rtc::Description::Direction direction)
            {
                if (!peer_connection_)
                {
                    LOG_ERROR(LOG_TAG, "PeerConnection未创建");
                    return;
                }
                try
                {
                    LOG_INFO(LOG_TAG, "创建音频轨道 (opus, %dHz, %d通道, direction=%d)",
                             config_.audio.sample_rate, config_.audio.channels,
                             static_cast<int>(direction));
                    auto audio_desc = rtc::Description::Audio("audio", direction);
                    audio_desc.addOpusCodec(config_.audio.payload_type);
                    audio_desc.addSSRC(config_.audio.ssrc, "audio", "stream1", "audio");
                    audio_track_ = peer_connection_->addTrack(audio_desc);

                    if (direction == rtc::Description::Direction::SendOnly ||
                        direction == rtc::Description::Direction::SendRecv)
                    {
                        audio_rtp_config_ = std::make_shared<rtc::RtpPacketizationConfig>(
                            config_.audio.ssrc, "audio", config_.audio.payload_type,
                            rtc::OpusRtpPacketizer::DefaultClockRate);
                        audio_packetizer_ =
                            std::make_shared<rtc::OpusRtpPacketizer>(audio_rtp_config_);
                        audio_sr_reporter_ =
                            std::make_shared<rtc::RtcpSrReporter>(audio_rtp_config_);
                        audio_packetizer_->addToChain(audio_sr_reporter_);
                        audio_rtcp_session_ = std::make_shared<rtc::RtcpReceivingSession>();
                        audio_packetizer_->addToChain(audio_rtcp_session_);
                        audio_track_->setMediaHandler(audio_packetizer_);
                        audio_track_->onOpen(
                            []() { LOG_INFO(LOG_TAG, "音频轨道已打开，可以发送数据"); });
                    }
                    else if (direction == rtc::Description::Direction::RecvOnly)
                    {
                        auto receive_session = std::make_shared<rtc::RtcpReceivingSession>();
                        audio_track_->setMediaHandler(receive_session);
                    }

                    if (direction == rtc::Description::Direction::RecvOnly ||
                        direction == rtc::Description::Direction::SendRecv)
                        configureTrackCallbacks();
                    LOG_INFO(LOG_TAG, "音频轨道创建成功");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "创建音频轨道失败: %s", e.what());
                }
            }

            void WebRTCSystem::setupVideoTrack(rtc::Description::Direction direction)
            {
                if (!peer_connection_)
                {
                    LOG_ERROR(LOG_TAG, "PeerConnection未创建");
                    return;
                }
                try
                {
                    LOG_INFO(LOG_TAG, "创建视频轨道 (h264, %dx%d, direction=%d)",
                             config_.video.width, config_.video.height,
                             static_cast<int>(direction));
                    auto video_desc = rtc::Description::Video("video", direction);
                    video_desc.addH264Codec(config_.video.payload_type);
                    video_desc.addSSRC(config_.video.ssrc, "video", "stream1", "video");
                    video_track_ = peer_connection_->addTrack(video_desc);

                    if (direction == rtc::Description::Direction::SendOnly ||
                        direction == rtc::Description::Direction::SendRecv)
                    {
                        video_rtp_config_ = std::make_shared<rtc::RtpPacketizationConfig>(
                            config_.video.ssrc, "video", config_.video.payload_type,
                            rtc::H264RtpPacketizer::ClockRate);

                        video_packetizer_ = std::make_shared<rtc::H264RtpPacketizer>(
                            rtc::NalUnit::Separator::LongStartSequence, video_rtp_config_, 1200);

                        video_sr_reporter_ =
                            std::make_shared<rtc::RtcpSrReporter>(video_rtp_config_);
                        video_rtcp_session_ = std::make_shared<rtc::RtcpReceivingSession>();
                        video_pli_handler_ = std::make_shared<rtc::PliHandler>([this]() {
                            std::function<void()> cb;
                            {
                                std::lock_guard<std::mutex> lock(video_net_cb_mutex_);
                                cb = video_on_keyframe_req_;
                            }
                            if (!cb)
                                return;
                            try
                            {
                                cb();
                            }
                            catch (const std::exception& e)
                            {
                                LOG_ERROR(LOG_TAG, "PLI异常 %s", e.what());
                            }
                        });
                        video_remb_handler_ = std::make_shared<rtc::RembHandler>(
                            [this](unsigned int bps)
                            {
                                unsigned int lo      = config_.video_remb_min_bps;
                                unsigned int hi      = std::max(lo, config_.video_remb_max_bps);
                                unsigned int clamped = std::clamp(bps, lo, hi);
                                std::function<void(unsigned int)> cb;
                                {
                                    std::lock_guard<std::mutex> lock(video_net_cb_mutex_);
                                    cb = video_on_remb_bps_;
                                }
                                if (!cb)
                                    return;
                                try
                                {
                                    cb(clamped);
                                }
                                catch (const std::exception& e)
                                {
                                    LOG_ERROR(LOG_TAG, "REMB异常 %s", e.what());
                                }
                            });

                        video_packetizer_->addToChain(video_sr_reporter_);
                        video_packetizer_->addToChain(video_rtcp_session_);
                        video_packetizer_->addToChain(video_pli_handler_);
                        video_packetizer_->addToChain(video_remb_handler_);

                        if (config_.enable_video_pacing)
                        {
                            double pacing_bps = config_.video_pacing_bps;
                            if (pacing_bps <= 0.)
                                pacing_bps = static_cast<double>(H264_Default_Bitrate) * 1000.0 * 1.35;
                            video_pacing_handler_ = std::make_shared<rtc::PacingHandler>(
                                pacing_bps,
                                std::chrono::milliseconds(
                                    std::max(5, config_.video_pacing_interval_ms)));
                            video_packetizer_->addToChain(video_pacing_handler_);
                            LOG_DEBUG(LOG_TAG, "pacing %.0fbps %dms", pacing_bps,
                                      std::max(5, config_.video_pacing_interval_ms));
                        }

                        video_track_->setMediaHandler(video_packetizer_);
                        video_track_->onOpen([]() { LOG_DEBUG(LOG_TAG, "视频轨就绪"); });
                    }
                    else if (direction == rtc::Description::Direction::RecvOnly)
                    {
                        auto receive_session = std::make_shared<rtc::RtcpReceivingSession>();
                        video_track_->setMediaHandler(receive_session);
                    }

                    if (direction == rtc::Description::Direction::RecvOnly ||
                        direction == rtc::Description::Direction::SendRecv)
                    {
                        video_track_->onMessage(
                            [this](rtc::message_variant data)
                            {
                                if (std::holds_alternative<rtc::binary>(data))
                                {
                                    auto&          binary       = std::get<rtc::binary>(data);
                                    const uint8_t* payload      = nullptr;
                                    size_t         payload_size = 0;
                                    if (parseRtpPacket(
                                            reinterpret_cast<const uint8_t*>(binary.data()),
                                            binary.size(), payload, payload_size))
                                        handleVideoDataReceived(
                                            payload, payload_size,
                                            static_cast<uint64_t>(app::tool::time::uptime_us()));
                                }
                            });
                    }
                    LOG_INFO(LOG_TAG, "视频轨道创建成功");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "创建视频轨道失败: %s", e.what());
                }
            }

            void WebRTCSystem::configureTrackCallbacks()
            {
                if (audio_track_)
                {
                    audio_track_->onMessage(
                        [this](rtc::message_variant data)
                        {
                            if (std::holds_alternative<rtc::binary>(data))
                            {
                                auto&          binary = std::get<rtc::binary>(data);
                                const uint8_t* payload_data;
                                size_t         payload_size;
                                if (parseRtpPacket(reinterpret_cast<const uint8_t*>(binary.data()),
                                                   binary.size(), payload_data, payload_size))
                                    handleAudioDataReceived(payload_data, payload_size);
                            }
                        });
                }
            }

            void WebRTCSystem::setupPeerConnectionCallbacks()
            {
                if (!peer_connection_)
                    return;
                peer_connection_->onLocalDescription(
                    [this](rtc::Description desc)
                    { onLocalDescriptionGenerated(std::move(desc)); });
                peer_connection_->onLocalCandidate(
                    [this](rtc::Candidate candidate)
                    { onIceCandidateGenerated(std::move(candidate)); });
                peer_connection_->onStateChange([this](rtc::PeerConnection::State state)
                                                { onPeerConnectionStateChange(state); });
                peer_connection_->onIceStateChange([this](rtc::PeerConnection::IceState state)
                                                   { onIceStateChange(state); });
                peer_connection_->onDataChannel(
                    [this](std::shared_ptr<rtc::DataChannel> dc)
                    {
                        LOG_INFO(LOG_TAG, "收到DataChannel: %s", dc->label().c_str());
                        data_channel_ = dc;
                        configureDataChannelCallbacks();
                    });
            }

            void WebRTCSystem::onLocalDescriptionGenerated(rtc::Description desc)
            {
                if (!signaling_ || !signaling_->isPaired())
                {
                    LOG_WARN(LOG_TAG, "信令未准备好，跳过SDP发送");
                    return;
                }
                if (desc.type() == rtc::Description::Type::Offer)
                {
                    std::string peer_id;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        peer_id = peer_device_id_;
                    }
                    LOG_INFO(LOG_TAG, "发送Offer SDP");
                    signaling_->sendOffer(std::string(desc), peer_id);
                }
            }

            void WebRTCSystem::onIceCandidateGenerated(rtc::Candidate candidate)
            {
                if (!signaling_ || !signaling_->isPaired())
                    return;
                std::string candidate_str = std::string(candidate);
                if (sdp_exchange_completed_.load())
                {
                    std::string peer_id;
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        peer_id = peer_device_id_;
                    }
                    signaling_->sendIceCandidate(candidate_str, peer_id);
                    LOG_DEBUG(LOG_TAG, "发送ICE候选");
                }
                else
                {
                    std::lock_guard<std::mutex> lock(ice_candidates_mutex_);
                    pending_ice_candidates_.push_back(candidate_str);
                }
            }

            void WebRTCSystem::onPeerConnectionStateChange(rtc::PeerConnection::State state)
            {
                LOG_INFO(LOG_TAG, "PeerConnection状态: %s", peerConnectionStateToString(state));
                switch (state)
                {
                case rtc::PeerConnection::State::Connected:
                {
                    WebRTCState current = current_state_.load();
                    if (current == WebRTCState::ICE_CONNECTED ||
                        current == WebRTCState::ICE_CONNECTING)
                        setState(WebRTCState::CONNECTED);
                }
                break;
                case rtc::PeerConnection::State::Failed:
                    setState(WebRTCState::FAILED);
                    invokeErrorCallback(WebRTCError::CONNECTION_FAILED, "PeerConnection失败");
                    break;
                case rtc::PeerConnection::State::Closed:
                    if (current_state_.load() != WebRTCState::DISCONNECTING)
                    {
                        WebRTCState current = current_state_.load();
                        setState(current == WebRTCState::CONNECTED ? WebRTCState::DISCONNECTING
                                                                   : WebRTCState::DISCONNECTED);
                    }
                    break;
                default:
                    break;
                }
            }

            void WebRTCSystem::setupDataChannel()
            {
                if (!peer_connection_)
                {
                    LOG_ERROR(LOG_TAG, "PeerConnection未创建，无法创建DataChannel");
                    return;
                }
                try
                {
                    data_channel_ = peer_connection_->createDataChannel("message");
                    configureDataChannelCallbacks();
                    LOG_INFO(LOG_TAG, "DataChannel创建成功: message");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "创建DataChannel失败: %s", e.what());
                }
            }

            void WebRTCSystem::configureDataChannelCallbacks()
            {
                if (!data_channel_)
                    return;
                data_channel_->onOpen([this]() { LOG_INFO(LOG_TAG, "DataChannel已打开"); });
                data_channel_->onClosed([this]() { LOG_INFO(LOG_TAG, "DataChannel已关闭"); });
                data_channel_->onMessage(
                    [this](auto data)
                    {
                        if (std::holds_alternative<std::string>(data))
                            handleDataChannelMessage(std::get<std::string>(data));
                    });
            }

            void WebRTCSystem::handleDataChannelMessage(const std::string& message)
            {
                LOG_INFO(LOG_TAG, "收到消息: %s", message.c_str());
            }

            bool WebRTCSystem::sendDataMessage(const std::string& message)
            {
                if (!data_channel_)
                {
                    LOG_WARN(LOG_TAG, "DataChannel未创建");
                    return false;
                }
                if (!data_channel_->isOpen())
                {
                    LOG_WARN(LOG_TAG, "DataChannel未打开");
                    return false;
                }
                try
                {
                    data_channel_->send(message);
                    return true;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "发送消息失败: %s", e.what());
                    return false;
                }
            }

            void WebRTCSystem::onIceStateChange(rtc::PeerConnection::IceState state)
            {
                LOG_INFO(LOG_TAG, "ICE状态: %s", iceStateToString(state));
                WebRTCState current_state = current_state_.load();
                switch (state)
                {
                case rtc::PeerConnection::IceState::Checking:
                    if (current_state == WebRTCState::SDP_CONNECTED)
                        setState(WebRTCState::ICE_CONNECTING);
                    break;
                case rtc::PeerConnection::IceState::Connected:
                case rtc::PeerConnection::IceState::Completed:
                    if (current_state == WebRTCState::ICE_CONNECTING ||
                        current_state == WebRTCState::SDP_CONNECTED)
                        setState(WebRTCState::ICE_CONNECTED);
                    break;
                case rtc::PeerConnection::IceState::Failed:
                    setState(WebRTCState::FAILED);
                    invokeErrorCallback(WebRTCError::ICE_CANDIDATE_FAILED, "ICE连接失败");
                    break;
                default:
                    break;
                }
            }

            void WebRTCSystem::handleAudioDataReceived(const uint8_t* data, size_t size)
            {
                if (!data || size == 0)
                    return;
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.audio_packets_received++;
                }
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (audio_callback_)
                {
                    try
                    {
                        audio_callback_(data, size);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "音频回调异常: %s", e.what());
                    }
                }
            }

            void WebRTCSystem::handleVideoDataReceived(const uint8_t* data, size_t size,
                                                       uint64_t timestamp)
            {
                if (!data || size == 0)
                    return;
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.video_packets_received++;
                }
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (video_callback_)
                {
                    try
                    {
                        video_callback_(data, size, timestamp);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "视频回调异常: %s", e.what());
                    }
                }
            }

            void WebRTCSystem::cleanup()
            {
                LOG_INFO(LOG_TAG, "清理资源");
                try
                {
                    if (audio_track_)
                    {
                        audio_track_->close();
                        audio_track_.reset();
                    }
                }
                catch (const std::exception& e)
                {
                    LOG_WARN(LOG_TAG, "关闭音频轨道异常: %s", e.what());
                }
                try
                {
                    if (video_track_)
                    {
                        video_track_->close();
                        video_track_.reset();
                    }
                }
                catch (const std::exception& e)
                {
                    LOG_WARN(LOG_TAG, "关闭视频轨道异常: %s", e.what());
                }
                audio_packetizer_.reset();
                audio_rtp_config_.reset();
                audio_sr_reporter_.reset();
                audio_rtcp_session_.reset();
                video_packetizer_.reset();
                video_rtp_config_.reset();
                video_sr_reporter_.reset();
                video_rtcp_session_.reset();
                video_pli_handler_.reset();
                video_remb_handler_.reset();
                video_pacing_handler_.reset();
                {
                    std::lock_guard<std::mutex> lock(video_net_cb_mutex_);
                    video_on_remb_bps_     = nullptr;
                    video_on_keyframe_req_ = nullptr;
                }
                try
                {
                    if (data_channel_)
                        data_channel_.reset();
                }
                catch (const std::exception& e)
                {
                    LOG_WARN(LOG_TAG, "关闭DataChannel异常: %s", e.what());
                }
                try
                {
                    if (peer_connection_)
                    {
                        peer_connection_->onLocalDescription(nullptr);
                        peer_connection_->onLocalCandidate(nullptr);
                        peer_connection_->onStateChange(nullptr);
                        peer_connection_->onIceStateChange(nullptr);
                        peer_connection_->onDataChannel(nullptr);
                        peer_connection_->close();
                        peer_connection_.reset();
                    }
                }
                catch (const std::exception& e)
                {
                    LOG_WARN(LOG_TAG, "关闭PeerConnection异常: %s", e.what());
                }
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    state_callback_ = nullptr;
                    error_callback_ = nullptr;
                    audio_callback_ = nullptr;
                    video_callback_ = nullptr;
                }
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_ = Stats{};
                }
                {
                    std::lock_guard<std::mutex> lock(ice_candidates_mutex_);
                    pending_ice_candidates_.clear();
                }
                sdp_exchange_completed_.store(false);
                connection_request_sent_ = false;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    current_connection_request_ = ConnectionRequest{};
                }
                LOG_INFO(LOG_TAG, "资源已清理");
            }

            bool WebRTCSystem::validateConfiguration() const
            {
                if (config_.audio.ssrc == config_.video.ssrc)
                {
                    LOG_ERROR(LOG_TAG, "音频和视频SSRC不能相同");
                    return false;
                }
                if (config_.audio.payload_type == config_.video.payload_type)
                {
                    LOG_ERROR(LOG_TAG, "音频和视频PayloadType不能相同");
                    return false;
                }
                if (config_.audio.payload_type < 96 || config_.audio.payload_type > 127)
                {
                    LOG_ERROR(LOG_TAG, "音频PayloadType必须在96-127范围内");
                    return false;
                }
                if (config_.video.payload_type < 96 || config_.video.payload_type > 127)
                {
                    LOG_ERROR(LOG_TAG, "视频PayloadType必须在96-127范围内");
                    return false;
                }
                if (config_.audio.codec != "opus")
                {
                    LOG_ERROR(LOG_TAG, "音频编解码器必须是opus");
                    return false;
                }
                if (config_.audio.sample_rate != 48000)
                    LOG_WARN(LOG_TAG, "音频采样率建议为48000Hz");
                if (config_.audio.channels != 1)
                    LOG_WARN(LOG_TAG, "音频通道数建议为1（文档规范）");
                if (config_.video.codec != "h264")
                {
                    LOG_ERROR(LOG_TAG, "视频编解码器必须是h264");
                    return false;
                }
                LOG_INFO(LOG_TAG, "配置验证通过");
                return true;
            }

            bool WebRTCSystem::parseRtpPacket(const uint8_t* rtp_data, size_t rtp_size,
                                              const uint8_t*& payload_data, size_t& payload_size)
            {
                if (!rtp_data || rtp_size < RTP_HEADER_MIN_SIZE)
                    return false;
                size_t  header_size = RTP_HEADER_MIN_SIZE;
                uint8_t cc          = rtp_data[0] & RTP_CC_MASK;
                header_size += static_cast<size_t>(cc) * RTP_CSRC_SIZE;
                bool extension_bit = (rtp_data[0] & RTP_EXTENSION_MASK) != 0;
                bool padding_bit   = (rtp_data[0] & RTP_PADDING_MASK) != 0;
                if (header_size >= rtp_size)
                    return false;
                if (extension_bit)
                {
                    if (header_size + RTP_EXTENSION_HEADER_LEN > rtp_size)
                        return false;
                    auto ext_length = static_cast<uint16_t>(
                        (static_cast<uint16_t>(rtp_data[header_size + 2]) << 8) |
                        static_cast<uint16_t>(rtp_data[header_size + 3]));
                    size_t ext_size =
                        RTP_EXTENSION_HEADER_LEN + static_cast<size_t>(ext_length) * 4;
                    header_size += ext_size;
                    if (header_size >= rtp_size)
                        return false;
                }
                payload_data = rtp_data + header_size;
                payload_size = rtp_size - header_size;
                if (padding_bit && payload_size > 0)
                {
                    uint8_t padding_count = rtp_data[rtp_size - 1];
                    if (padding_count <= payload_size)
                        payload_size -= static_cast<size_t>(padding_count);
                }
                return payload_size > 0;
            }

            void WebRTCSystem::invokeErrorCallback(WebRTCError error, const std::string& message)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (error_callback_)
                {
                    try
                    {
                        error_callback_(error, message);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "错误回调异常: %s", e.what());
                    }
                }
            }

            const char* WebRTCSystem::stateToString(WebRTCState state)
            {
                switch (state)
                {
                case WebRTCState::IDLE:
                    return "IDLE";
                case WebRTCState::WAITING_CONNECT_REQUEST:
                    return "WAITING_CONNECT_REQUEST";
                case WebRTCState::SDP_CONNECTING:
                    return "SDP_CONNECTING";
                case WebRTCState::SDP_CONNECTED:
                    return "SDP_CONNECTED";
                case WebRTCState::ICE_CONNECTING:
                    return "ICE_CONNECTING";
                case WebRTCState::ICE_CONNECTED:
                    return "ICE_CONNECTED";
                case WebRTCState::CONNECTED:
                    return "CONNECTED";
                case WebRTCState::DISCONNECTING:
                    return "DISCONNECTING";
                case WebRTCState::DISCONNECTED:
                    return "DISCONNECTED";
                case WebRTCState::FAILED:
                    return "FAILED";
                default:
                    return "UNKNOWN";
                }
            }

            const char* WebRTCSystem::peerConnectionStateToString(rtc::PeerConnection::State state)
            {
                switch (state)
                {
                case rtc::PeerConnection::State::New:
                    return "New";
                case rtc::PeerConnection::State::Connecting:
                    return "Connecting";
                case rtc::PeerConnection::State::Connected:
                    return "Connected";
                case rtc::PeerConnection::State::Disconnected:
                    return "Disconnected";
                case rtc::PeerConnection::State::Failed:
                    return "Failed";
                case rtc::PeerConnection::State::Closed:
                    return "Closed";
                default:
                    return "Unknown";
                }
            }

            const char* WebRTCSystem::iceStateToString(rtc::PeerConnection::IceState state)
            {
                switch (state)
                {
                case rtc::PeerConnection::IceState::New:
                    return "New";
                case rtc::PeerConnection::IceState::Checking:
                    return "Checking";
                case rtc::PeerConnection::IceState::Connected:
                    return "Connected";
                case rtc::PeerConnection::IceState::Completed:
                    return "Completed";
                case rtc::PeerConnection::IceState::Failed:
                    return "Failed";
                case rtc::PeerConnection::IceState::Disconnected:
                    return "Disconnected";
                case rtc::PeerConnection::IceState::Closed:
                    return "Closed";
                default:
                    return "Unknown";
                }
            }

        } // namespace webrtc
    }     // namespace protocol
} // namespace app
