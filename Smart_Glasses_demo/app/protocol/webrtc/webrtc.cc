#include "webrtc.hpp"
#include "../../tool/log/log.hpp"
#include "../../../common/common.hpp"
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
                constexpr const char* LOG_TAG                      = "WEBRTC";
                // RTP 包解析常量
                constexpr size_t      RTP_HEADER_MIN_SIZE          = 12;
                constexpr uint8_t     RTP_CC_MASK                  = 0x0F;
                constexpr uint8_t     RTP_EXTENSION_MASK           = 0x10;
                constexpr uint8_t     RTP_PADDING_MASK             = 0x20;
                constexpr size_t      RTP_EXTENSION_HEADER_LEN     = 4;
                constexpr size_t      RTP_CSRC_SIZE                = 4;
                constexpr size_t      RTP_EXT_LENGTH_SHIFT         = 8;
                // 码率变化阈值（10%）
                constexpr double      BITRATE_CHANGE_THRESHOLD     = 0.1;
            } // namespace

            // ========== PriorityTaskQueue 实现 ==========

            PriorityTaskQueue::PriorityTaskQueue(const std::string& name, size_t thread_count)
                : name_(name)
            {
                for (size_t i = 0; i < thread_count; ++i)
                {
                    workers_.emplace_back(&PriorityTaskQueue::workerThread, this);
                }
                LOG_INFO(LOG_TAG, "%s 启动，线程数: %zu", name_.c_str(), thread_count);
            }

            PriorityTaskQueue::~PriorityTaskQueue()
            {
                stop_.store(true);
                condition_.notify_all();

                for (auto& worker : workers_)
                {
                    if (worker.joinable())
                    {
                        worker.join();
                    }
                }
                LOG_INFO(LOG_TAG, "%s 已停止", name_.c_str());
            }

            void PriorityTaskQueue::post(std::function<void()> func, TaskPriority priority)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    Task                        task;
                    task.func      = std::move(func);
                    task.priority  = priority;
                    task.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
                    tasks_.push(std::move(task));
                }
                condition_.notify_one();
            }

            void PriorityTaskQueue::clear()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                std::priority_queue<Task>   empty_queue;
                tasks_.swap(empty_queue);
            }

            void PriorityTaskQueue::workerThread()
            {
                while (!stop_.load())
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    condition_.wait(lock, [this] { return !tasks_.empty() || stop_.load(); });

                    if (stop_.load())
                    {
                        break;
                    }

                    if (!tasks_.empty())
                    {
                        Task task = std::move(const_cast<Task&>(tasks_.top()));
                        tasks_.pop();
                        lock.unlock();

                        try
                        {
                            task.func();
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "%s 任务执行异常: %s", name_.c_str(),
                                      e.what());
                        }
                    }
                }
            }

            // ========== AudioBufferPool 实现 ==========

            AudioBufferPool::AudioBufferPool()
            {
                // 初始化内存池（1MB初始大小，16字节对齐，1.5倍增长因子）
                memPool_ = std::make_unique<tool::memory::MemoryPool>(1024 * 1024, 16, 1.5);

                // 预分配缓冲区
                for (size_t i = 0; i < POOL_SIZE; ++i)
                {
                    uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(AUDIO_BUFFER_SIZE));
                    if (data)
                    {
                        auto buffer =
                            std::make_shared<MediaBuffer>(data, AUDIO_BUFFER_SIZE, memPool_.get());
                        freeBuffers_.push(buffer);
                    }
                }

                LOG_INFO(LOG_TAG, "初始化完成，预分配: %zu 个缓冲区",
                         freeBuffers_.size());
            }

            AudioBufferPool::~AudioBufferPool()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // shared_ptr会自动释放，MediaBuffer析构时会归还内存池
                while (!freeBuffers_.empty())
                {
                    freeBuffers_.pop();
                }
            }

            MediaBufferPtr AudioBufferPool::acquire()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!freeBuffers_.empty())
                {
                    auto buffer = freeBuffers_.front();
                    freeBuffers_.pop();
                    buffer->reset(); // 重置大小
                    return buffer;
                }

                // 如果池空了，临时分配一个
                uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(AUDIO_BUFFER_SIZE));
                if (data)
                {
                    return std::make_shared<MediaBuffer>(data, AUDIO_BUFFER_SIZE, memPool_.get());
                }

                return nullptr;
            }

            void AudioBufferPool::release(MediaBufferPtr buffer)
            {
                if (!buffer)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(mutex_);
                if (freeBuffers_.size() < POOL_SIZE * 2)
                { // 限制池大小
                    buffer->reset();
                    freeBuffers_.push(buffer);
                }
                // 否则让shared_ptr自动释放
            }

            // ========== VideoBufferPool 实现 ==========

            VideoBufferPool::VideoBufferPool()
            {
                // 初始化内存池（50MB初始大小）
                memPool_ = std::make_unique<tool::memory::MemoryPool>(50 * 1024 * 1024, 16, 1.5);

                // 预分配小缓冲区（P帧）
                for (size_t i = 0; i < SMALL_POOL_SIZE; ++i)
                {
                    uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(SMALL_BUFFER_SIZE));
                    if (data)
                    {
                        auto buffer =
                            std::make_shared<MediaBuffer>(data, SMALL_BUFFER_SIZE, memPool_.get());
                        smallBuffers_.push(buffer);
                    }
                }

                // 预分配中缓冲区
                for (size_t i = 0; i < MEDIUM_POOL_SIZE; ++i)
                {
                    uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(MEDIUM_BUFFER_SIZE));
                    if (data)
                    {
                        auto buffer =
                            std::make_shared<MediaBuffer>(data, MEDIUM_BUFFER_SIZE, memPool_.get());
                        mediumBuffers_.push(buffer);
                    }
                }

                // 预分配大缓冲区（I帧）
                for (size_t i = 0; i < LARGE_POOL_SIZE; ++i)
                {
                    uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(LARGE_BUFFER_SIZE));
                    if (data)
                    {
                        auto buffer =
                            std::make_shared<MediaBuffer>(data, LARGE_BUFFER_SIZE, memPool_.get());
                        largeBuffers_.push(buffer);
                    }
                }

                LOG_INFO(LOG_TAG, "初始化完成，预分配: %zu 小/%zu 中/%zu 大 缓冲区",
                         smallBuffers_.size(), mediumBuffers_.size(), largeBuffers_.size());
            }

            VideoBufferPool::~VideoBufferPool()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                while (!smallBuffers_.empty())
                {
                    smallBuffers_.pop();
                }
                while (!mediumBuffers_.empty())
                {
                    mediumBuffers_.pop();
                }
                while (!largeBuffers_.empty())
                {
                    largeBuffers_.pop();
                }
            }

            MediaBufferPtr VideoBufferPool::acquire(size_t required_size)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                // 根据大小选择合适的缓冲区
                if (required_size <= SMALL_BUFFER_SIZE && !smallBuffers_.empty())
                {
                    auto buffer = smallBuffers_.front();
                    smallBuffers_.pop();
                    buffer->reset();
                    return buffer;
                }

                if (required_size <= MEDIUM_BUFFER_SIZE && !mediumBuffers_.empty())
                {
                    auto buffer = mediumBuffers_.front();
                    mediumBuffers_.pop();
                    buffer->reset();
                    return buffer;
                }

                if (required_size <= LARGE_BUFFER_SIZE && !largeBuffers_.empty())
                {
                    auto buffer = largeBuffers_.front();
                    largeBuffers_.pop();
                    buffer->reset();
                    return buffer;
                }

                // 如果没有合适的，临时分配
                size_t alloc_size = required_size;
                if (alloc_size <= SMALL_BUFFER_SIZE)
                {
                    alloc_size = SMALL_BUFFER_SIZE;
                }
                else if (alloc_size <= MEDIUM_BUFFER_SIZE)
                {
                    alloc_size = MEDIUM_BUFFER_SIZE;
                }
                else if (alloc_size <= LARGE_BUFFER_SIZE)
                {
                    alloc_size = LARGE_BUFFER_SIZE;
                }

                uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(alloc_size));
                if (data)
                {
                    return std::make_shared<MediaBuffer>(data, alloc_size, memPool_.get());
                }

                return nullptr;
            }

            void VideoBufferPool::release(MediaBufferPtr buffer)
            {
                if (!buffer)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(mutex_);

                size_t capacity = buffer->capacity();
                buffer->reset();

                // 归还到对应的池
                if (capacity == SMALL_BUFFER_SIZE && smallBuffers_.size() < SMALL_POOL_SIZE * 2)
                {
                    smallBuffers_.push(buffer);
                }
                else if (capacity == MEDIUM_BUFFER_SIZE &&
                         mediumBuffers_.size() < MEDIUM_POOL_SIZE * 2)
                {
                    mediumBuffers_.push(buffer);
                }
                else if (capacity == LARGE_BUFFER_SIZE &&
                         largeBuffers_.size() < LARGE_POOL_SIZE * 2)
                {
                    largeBuffers_.push(buffer);
                }
                // 否则让shared_ptr自动释放
            }

            WebRTCSystem::WebRTCSystem(const WebRTCConfig& config)
                : config_(config), state_(WebRTCState::UNINITIALIZED), ice_connected_(false),
                  initialized_(false), last_audio_send_time_(), last_video_send_time_()
            {
                LOG_INFO(LOG_TAG, "系统实例创建，AudioSend=%d, VideoSend=%d",
                         config_.enableAudioSend, config_.enableVideoSend);

                // 初始化时间同步
                sync_init(&sync_context_);
            }

            WebRTCSystem::~WebRTCSystem()
            {
                close();

                // 释放时间同步资源
                sync_deinit(&sync_context_);

                LOG_INFO(LOG_TAG, "系统实例销毁完成");
            }

            WebRTCError WebRTCSystem::open(std::shared_ptr<Signaling> signaling)
            {
                if (!signaling)
                {
                    LOG_ERROR(LOG_TAG, "信令模块为空，初始化失败");
                    return WebRTCError::UNKNOWN;
                }

                if (initialized_)
                {
                    LOG_WARN(LOG_TAG, "重复调用open，无需重新初始化");
                    return WebRTCError::NONE;
                }

                LOG_INFO(LOG_TAG, "开始初始化WebRTC系统...");
                state_.store(WebRTCState::INITIALIZING);
                signaling_ = std::move(signaling);
                ice_connected_.store(false);

                configureSctp();

                audio_task_queue_ = std::make_unique<PriorityTaskQueue>(
                    "AudioQueue", config_.performance.audioThreadCount);
                video_task_queue_ = std::make_unique<PriorityTaskQueue>(
                    "VideoQueue", config_.performance.videoThreadCount);

                audio_buffer_pool_ = std::make_unique<AudioBufferPool>();
                video_buffer_pool_ = std::make_unique<VideoBufferPool>();

                // ========== 信令回调 ==========
                signaling_->onWebRTCReady(
                    [this](const std::string& role, const std::string& peer_device_id)
                    { handleRole(role, peer_device_id); });

                signaling_->onOfferReceived(
                    [this](const nlohmann::json& msg)
                    {
                        if (msg.contains("data") && msg["data"].contains("sdp"))
                        {
                            handleOffer(msg["data"]["sdp"].get<std::string>());
                        }
                    });

                signaling_->onAnswerReceived(
                    [this](const nlohmann::json& msg)
                    {
                        if (msg.contains("data") && msg["data"].contains("sdp"))
                        {
                            handleAnswer(msg["data"]["sdp"].get<std::string>());
                        }
                    });

                signaling_->onIceCandidateReceived(
                    [this](const nlohmann::json& msg)
                    {
                        if (msg.contains("data") && msg["data"].contains("candidate"))
                        {
                            handleIceCandidate(msg["data"]["candidate"].get<std::string>());
                        }
                    });

                initialized_.store(true);
                setState(WebRTCState::DISCONNECTED);

                LOG_INFO(LOG_TAG, "WebRTC系统初始化完成");
                return WebRTCError::NONE;
            }

            void WebRTCSystem::close()
            {
                if (!initialized_)
                {
                    return;
                }

                LOG_INFO(LOG_TAG, "开始关闭WebRTC系统...");

                // 先清空任务队列（防止新任务提交和执行）
                if (audio_task_queue_)
                {
                    audio_task_queue_->clear();
                }
                if (video_task_queue_)
                {
                    video_task_queue_->clear();
                }

                // 关闭连接（此时不会再有新任务）
                closeConnection();

                // 清理所有资源
                cleanup();

                // 释放信令模块
                signaling_.reset();

                // 更新状态
                initialized_.store(false);
                setState(WebRTCState::UNINITIALIZED);

                LOG_INFO(LOG_TAG, "WebRTC系统关闭完成");
            }

            bool WebRTCSystem::isOpen() const
            {
                return initialized_.load();
            }

            bool WebRTCSystem::createConnection()
            {
                try
                {
                    rtc::Configuration rtc_config{};

                    for (const auto& stun : config_.ice.stunServers)
                    {
                        rtc_config.iceServers.emplace_back(stun);
                        LOG_INFO(LOG_TAG, "添加STUN服务器: %s", stun.c_str());
                    }
                    for (const auto& turn : config_.ice.turnServers)
                    {
                        rtc_config.iceServers.emplace_back(turn);
                        LOG_INFO(LOG_TAG, "添加TURN服务器: %s", turn.c_str());
                    }

                    rtc_config.iceTransportPolicy     = config_.ice.useRelayOnly
                                                            ? rtc::TransportPolicy::Relay
                                                            : rtc::TransportPolicy::All;
                    rtc_config.disableAutoNegotiation = true;

                    peer_connection_ = std::make_shared<rtc::PeerConnection>(rtc_config);
                    setupCallbacks();

                    LOG_INFO(LOG_TAG, "PeerConnection 创建成功");
                    return true;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "创建PeerConnection失败: %s", e.what());
                    return false;
                }
            }

            void WebRTCSystem::closeConnection()
            {
                if (!peer_connection_)
                {
                    return;
                }

                LOG_INFO(LOG_TAG, "关闭 PeerConnection...");

                // 先清除回调，打破循环引用
                try
                {
                    peer_connection_->onLocalDescription(nullptr);
                    peer_connection_->onLocalCandidate(nullptr);
                    peer_connection_->onStateChange(nullptr);
                    peer_connection_->onIceStateChange(nullptr);
                    peer_connection_->onDataChannel(nullptr);
                }
                catch (const std::exception& e)
                {
                    LOG_WARN(LOG_TAG, "清除 PeerConnection 回调异常: %s", e.what());
                }

                // 关闭数据通道
                try
                {
                    if (data_channel_)
                    {
                        data_channel_->close();
                        data_channel_.reset();
                    }
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "关闭 DataChannel 异常: %s", e.what());
                }

                // 关闭音频轨道
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
                    LOG_ERROR(LOG_TAG, "关闭音频轨道异常: %s", e.what());
                }
                audio_packetizer_.reset();
                audio_rtp_config_.reset();
                audio_sr_reporter_.reset();
                audio_rtcp_session_.reset();
                audio_remb_handler_.reset();

                // 关闭视频轨道
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
                    LOG_ERROR(LOG_TAG, "关闭视频轨道异常: %s", e.what());
                }
                video_packetizer_.reset();
                video_rtp_config_.reset();
                video_sr_reporter_.reset();
                video_rtcp_session_.reset();
                video_remb_handler_.reset();

                // 关闭 PeerConnection
                try
                {
                    peer_connection_->close();
                    peer_connection_.reset();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "关闭 PeerConnection 异常: %s", e.what());
                }

                LOG_INFO(LOG_TAG, "PeerConnection 已关闭");
            }

            bool WebRTCSystem::isConnected() const
            {
                WebRTCState current = state_.load();
                return (current == WebRTCState::CONNECTED);
            }

            bool WebRTCSystem::isIceConnected() const
            {
                return ice_connected_.load();
            }

            void WebRTCSystem::handleRole(const std::string& role,
                                          const std::string& peer_device_id)
            {
                role_           = role;
                peer_device_id_ = peer_device_id;

                LOG_INFO(LOG_TAG, "收到角色分配: %s, 对端设备: %s", role_.c_str(),
                         peer_device_id_.c_str());

                if (!createConnection())
                {
                    LOG_ERROR(LOG_TAG, "PeerConnection创建失败");
                    setState(WebRTCState::FAILED);
                    return;
                }

                setState(WebRTCState::CONNECTING);

                // 谁需要发流，谁就创建对应的轨道，收流只需等待接收对端发的轨道
                if (role_ == "offerer")
                {
                    if (config_.enableDataChannel)
                    {
                        setupDataChannel();
                    }
                    if (config_.enableAudioSend || config_.enableAudioReceive)
                    {
                        setupAudioTrack();
                    }
                    if (config_.enableVideoSend || config_.enableVideoReceive)
                    {
                        setupVideoTrack();
                    }
                }
                else if (role_ == "answerer")
                {
                    if (config_.enableDataChannel)
                    {
                        setupDataChannel();
                    }
                    if (config_.enableAudioSend || config_.enableAudioReceive)
                    {
                        setupAudioTrack();
                    }
                    if (config_.enableVideoSend || config_.enableVideoReceive)
                    {
                        setupVideoTrack();
                    }
                }
                else
                {
                    LOG_ERROR(LOG_TAG, "无效的角色: %s", role_.c_str());
                    return;
                }

                if (role_ == "offerer")
                {
                    try
                    {
                        peer_connection_->setLocalDescription();
                        LOG_INFO(LOG_TAG, "已触发本地SDP(Offer)生成");
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "生成本地 Offer 失败: %s", e.what());
                        setState(WebRTCState::FAILED);
                    }
                }
                else
                {
                    LOG_INFO(LOG_TAG, "等待对端 Offer");
                }
            }

            void WebRTCSystem::handleOffer(const std::string& sdp)
            {
                if (!peer_connection_)
                {
                    LOG_WARN(LOG_TAG, "PeerConnection 未初始化, 不能处理 Offer");
                    return;
                }
                try
                {
                    rtc::Description remote_desc(sdp, rtc::Description::Type::Offer);
                    peer_connection_->setRemoteDescription(remote_desc);
                    LOG_INFO(LOG_TAG, "设置远程 Offer 成功");
                    peer_connection_->setLocalDescription(); // 触发 Answer 生成
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "处理远程 Offer 失败: %s", e.what());
                    setState(WebRTCState::FAILED);
                }
            }

            void WebRTCSystem::handleAnswer(const std::string& sdp)
            {
                if (!peer_connection_)
                {
                    LOG_WARN(LOG_TAG, "PeerConnection 未初始化, 不能处理 Answer");
                    return;
                }
                try
                {
                    rtc::Description remote_desc(sdp, rtc::Description::Type::Answer);
                    peer_connection_->setRemoteDescription(remote_desc);
                    LOG_INFO(LOG_TAG, "设置远程 Answer 成功");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "处理远程 Answer 失败: %s", e.what());
                    setState(WebRTCState::FAILED);
                }
            }

            void WebRTCSystem::handleIceCandidate(const std::string& candidate)
            {
                if (!peer_connection_)
                {
                    LOG_WARN(LOG_TAG, "PeerConnection 未初始化, 不能处理 ICE");
                    return;
                }
                try
                {
                    LOG_INFO(LOG_TAG, "远程ICE候选: %s", candidate.c_str());
                    rtc::Candidate rtc_candidate(candidate);
                    peer_connection_->addRemoteCandidate(rtc_candidate);
                    LOG_DEBUG(LOG_TAG, "远程 ICE 候选添加成功");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "添加远程 ICE 候选失败: %s", e.what());
                }
            }

            bool WebRTCSystem::sendDataMessage(const std::string& message)
            {
                if (!data_channel_ || !data_channel_->isOpen())
                {
                    LOG_WARN(LOG_TAG, "DataChannel 未打开，无法发送消息");
                    return false;
                }

                try
                {
                    data_channel_->send(message);
                    LOG_DEBUG(LOG_TAG, "DataChannel 发送消息: %s", message.c_str());
                    return true;
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "DataChannel 发送失败: %s", e.what());
                    return false;
                }
            }

            bool WebRTCSystem::isDataChannelOpen() const
            {
                return data_channel_ && data_channel_->isOpen();
            }

            void WebRTCSystem::sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp)
            {
                if (!isConnected() || !audio_track_ || !audio_track_->isOpen())
                {
                    return;
                }

                auto now = std::chrono::steady_clock::now();
                if (last_audio_send_time_ != std::chrono::steady_clock::time_point{})
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_audio_send_time_);
                    if (elapsed.count() < AUDIO_SEND_INTERVAL_MS)
                    {
                        return;
                    }
                }
                last_audio_send_time_ = now;

                // 使用时间同步模块校正时间戳
                uint64_t synced_timestamp = sync_get_timestamp(&sync_context_, timestamp, true);

                if (audio_buffer_pool_)
                {
                    auto buffer = audio_buffer_pool_->acquire();
                    if (buffer && buffer->write(data, size))
                    {
                        audio_task_queue_->post(
                            [this, buffer, synced_timestamp]()
                            {
                                try
                                {
                                    auto sample_time =
                                        std::chrono::duration<double, std::micro>(synced_timestamp);
                                    audio_track_->sendFrame(
                                        reinterpret_cast<const std::byte*>(buffer->data()),
                                        buffer->size(), sample_time);
                                    {
                                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                                        stats_.audio_packets_sent++;
                                        stats_.audio_bytes_sent += buffer->size();
                                    }
                                    audio_buffer_pool_->release(buffer);
                                }
                                catch (const std::exception& e)
                                {
                                    LOG_ERROR(LOG_TAG, "发送音频失败: %s", e.what());
                                }
                            },
                            TaskPriority::HIGH);
                        return;
                    }
                }

                auto data_ptr = std::make_shared<std::vector<uint8_t>>(data, data + size);
                audio_task_queue_->post(
                    [this, data_ptr, synced_timestamp]()
                    {
                        try
                        {
                            auto sample_time =
                                std::chrono::duration<double, std::micro>(synced_timestamp);
                            audio_track_->sendFrame(
                                reinterpret_cast<const std::byte*>(data_ptr->data()),
                                data_ptr->size(), sample_time);
                            {
                                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                                stats_.audio_packets_sent++;
                                stats_.audio_bytes_sent += data_ptr->size();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "发送音频失败: %s", e.what());
                        }
                    },
                    TaskPriority::HIGH);
            }

            void WebRTCSystem::sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp,
                                             bool is_key_frame)
            {
                if (!isConnected() || !video_track_ || !video_track_->isOpen())
                {
                    return;
                }

                auto now = std::chrono::steady_clock::now();
                if (last_video_send_time_ != std::chrono::steady_clock::time_point{})
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_video_send_time_);
                    if (elapsed.count() < VIDEO_SEND_INTERVAL_MS)
                    {
                        return;
                    }
                }
                last_video_send_time_ = now;

                // 使用时间同步模块校正时间戳
                uint64_t synced_timestamp = sync_get_timestamp(&sync_context_, timestamp, false);

                TaskPriority priority = is_key_frame ? TaskPriority::NORMAL : TaskPriority::LOW;

                if (video_buffer_pool_)
                {
                    auto buffer = video_buffer_pool_->acquire(size);
                    if (buffer && buffer->write(data, size))
                    {
                        video_task_queue_->post(
                            [this, buffer, synced_timestamp]()
                            {
                                try
                                {
                                    auto sample_time =
                                        std::chrono::duration<double, std::micro>(synced_timestamp);
                                    video_track_->sendFrame(
                                        reinterpret_cast<const std::byte*>(buffer->data()),
                                        buffer->size(), sample_time);
                                    {
                                        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                                        stats_.video_packets_sent++;
                                        stats_.video_bytes_sent += buffer->size();
                                    }
                                    video_buffer_pool_->release(buffer);
                                }
                                catch (const std::exception& e)
                                {
                                    LOG_ERROR(LOG_TAG, "发送视频失败: %s", e.what());
                                }
                            },
                            priority);
                        return;
                    }
                }

                auto data_ptr = std::make_shared<std::vector<uint8_t>>(data, data + size);
                video_task_queue_->post(
                    [this, data_ptr, synced_timestamp]()
                    {
                        try
                        {
                            auto sample_time =
                                std::chrono::duration<double, std::micro>(synced_timestamp);
                            video_track_->sendFrame(
                                reinterpret_cast<const std::byte*>(data_ptr->data()),
                                data_ptr->size(), sample_time);
                            {
                                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                                stats_.video_packets_sent++;
                                stats_.video_bytes_sent += data_ptr->size();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "发送视频失败: %s", e.what());
                        }
                    },
                    priority);
            }

            WebRTCState WebRTCSystem::getState() const
            {
                return state_.load();
            }

            const WebRTCConfig& WebRTCSystem::getConfig() const
            {
                return config_;
            }

            void WebRTCSystem::onStateChanged(StateCallback callback)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                state_callback_ = std::move(callback);
            }

            void WebRTCSystem::onDataMessage(DataMessageCallback callback)
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                data_message_callback_ = std::move(callback);
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

            WebRTCSystem::Stats WebRTCSystem::getStats() const
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                return stats_;
            }

            void WebRTCSystem::resetStats()
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_ = Stats{};
            }

            void WebRTCSystem::logStats() const
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                LOG_INFO(LOG_TAG, "=== WebRTC 统计 ===");
                LOG_INFO(LOG_TAG, "  音频: 发送 %llu 包, %llu 字节",
                         stats_.audio_packets_sent, stats_.audio_bytes_sent);
                LOG_INFO(LOG_TAG, "  视频: 发送 %llu 包, %llu 字节",
                         stats_.video_packets_sent, stats_.video_bytes_sent);
                LOG_INFO(LOG_TAG, "==================");
            }

            void WebRTCSystem::setupCallbacks()
            {
                if (!peer_connection_)
                {
                    return;
                }

                peer_connection_->onLocalDescription(
                    [this](rtc::Description description)
                    {
                        std::string sdp = std::string(description);
                        LOG_INFO(LOG_TAG, "本地SDP生成: %s", sdp.c_str());
                        if (signaling_ && signaling_->isPaired())
                        {
                            std::string sdp = std::string(description);
                            if (description.type() == rtc::Description::Type::Offer)
                            {
                                signaling_->sendOffer(sdp, peer_device_id_);
                            }
                            else if (description.type() == rtc::Description::Type::Answer)
                            {
                                signaling_->sendAnswer(sdp, peer_device_id_);
                            }
                        }
                    });

                peer_connection_->onLocalCandidate(
                    [this](rtc::Candidate candidate)
                    {
                        LOG_INFO(LOG_TAG, "本地ICE候选: %s", std::string(candidate).c_str());
                        if (signaling_ && signaling_->isPaired())
                        {
                            signaling_->sendIceCandidate(std::string(candidate), peer_device_id_);
                        }
                    });

                peer_connection_->onStateChange(
                    [this](rtc::PeerConnection::State state)
                    {
                        LOG_INFO(LOG_TAG, "PeerConnection 状态: %d",
                                 static_cast<int>(state));
                        if (state == rtc::PeerConnection::State::Connected)
                        {
                            if (ice_connected_)
                            {
                                setState(WebRTCState::CONNECTED);
                            }
                        }
                        else if (state == rtc::PeerConnection::State::Failed)
                        {
                            setState(WebRTCState::FAILED);
                        }
                        else if (state == rtc::PeerConnection::State::Closed)
                        {
                            setState(WebRTCState::DISCONNECTED);
                        }
                    });

                peer_connection_->onIceStateChange(
                    [this](rtc::PeerConnection::IceState state)
                    {
                        LOG_INFO(LOG_TAG, "ICE 状态: %d", static_cast<int>(state));
                        switch (state)
                        {
                        case rtc::PeerConnection::IceState::Checking:
                            setState(WebRTCState::CONNECTING);
                            ice_connected_ = false;
                            break;
                        case rtc::PeerConnection::IceState::Connected:
                        case rtc::PeerConnection::IceState::Completed:
                            ice_connected_ = true;
                            setState(WebRTCState::CONNECTED);
                            break;
                        case rtc::PeerConnection::IceState::Failed:
                            ice_connected_ = false;
                            setState(WebRTCState::FAILED);
                            break;
                        default:
                            break;
                        }
                    });

                if (role_ == "answerer")
                {
                    peer_connection_->onDataChannel(
                        [this](std::shared_ptr<rtc::DataChannel> dc)
                        {
                            LOG_INFO(LOG_TAG, "接收到对端 DataChannel: %s",
                                     dc->label().c_str());
                            data_channel_ = dc;
                            setupDataChannel();
                        });
                }
            }

            void WebRTCSystem::setupDataChannel()
            {
                if (!peer_connection_ || !config_.enableDataChannel)
                {
                    return;
                }

                if (!data_channel_)
                {
                    try
                    {
                        data_channel_ =
                            peer_connection_->createDataChannel(config_.dataChannelLabel);
                        LOG_INFO(LOG_TAG, "创建 DataChannel: %s",
                                 config_.dataChannelLabel.c_str());
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "创建 DataChannel 失败: %s", e.what());
                        return;
                    }
                }

                data_channel_->onOpen(
                    [this]()
                    {
                        LOG_INFO(LOG_TAG, "DataChannel 打开完成");
                        handleDataChannel();
                    });

                data_channel_->onClosed([this]()
                                        { LOG_INFO(LOG_TAG, "DataChannel 已关闭"); });

                data_channel_->onMessage(
                    [this](auto data)
                    {
                        if (std::holds_alternative<std::string>(data))
                        {
                            handleDataMessage(std::get<std::string>(data));
                        }
                    });
            }

            void WebRTCSystem::setupAudioTrack()
            {
                if (!peer_connection_)
                {
                    return;
                }

                try
                {
                    rtc::Description::Direction direction = rtc::Description::Direction::Inactive;
                    if (config_.enableAudioSend && config_.enableAudioReceive)
                    {
                        direction = rtc::Description::Direction::SendRecv;
                    }
                    else if (config_.enableAudioSend)
                    {
                        direction = rtc::Description::Direction::SendOnly;
                    }
                    else if (config_.enableAudioReceive)
                    {
                        direction = rtc::Description::Direction::RecvOnly;
                    }

                    auto audio_desc = rtc::Description::Audio("audio", direction);
                    audio_desc.addOpusCodec(111);
                    audio_desc.addSSRC(2, "audio", "stream1", "audio");

                    audio_track_ = peer_connection_->addTrack(audio_desc);
                    LOG_INFO(LOG_TAG, "音频轨道已创建, direction=%d",
                             static_cast<int>(direction));

                    // 根据不同的方向设置MediaHandler
                    if (direction == rtc::Description::Direction::SendOnly ||
                        direction == rtc::Description::Direction::SendRecv)
                    {

                        // 创建RTP打包配置（发送端）
                        audio_rtp_config_ = std::make_shared<rtc::RtpPacketizationConfig>(
                            2, "audio", 111, rtc::OpusRtpPacketizer::DefaultClockRate);

                        // 创建Opus RTP打包器
                        audio_packetizer_ =
                            std::make_shared<rtc::OpusRtpPacketizer>(audio_rtp_config_);

                        // 添加RTCP SR报告器
                        audio_sr_reporter_ =
                            std::make_shared<rtc::RtcpSrReporter>(audio_rtp_config_);
                        audio_packetizer_->addToChain(audio_sr_reporter_);

                        // 添加RTCP接收会话（用于接收RTCP反馈）
                        audio_rtcp_session_ = std::make_shared<rtc::RtcpReceivingSession>();
                        audio_packetizer_->addToChain(audio_rtcp_session_);

                        // 添加REMB处理器（带宽估计反馈）
                        audio_remb_handler_ = std::make_shared<rtc::RembHandler>(
                            [this](unsigned int bitrate) { onRembReceived(bitrate); });
                        audio_packetizer_->addToChain(audio_remb_handler_);

                        // 添加NACK响应器
                        auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
                        audio_packetizer_->addToChain(nack_responder);

                        // 设置MediaHandler
                        audio_track_->setMediaHandler(audio_packetizer_);

                        // 设置轨道打开回调
                        audio_track_->onOpen(
                            []()
                            { LOG_INFO(LOG_TAG, "音频轨道已打开，可以开始发送音频数据"); });
                    }
                    else if (direction == rtc::Description::Direction::RecvOnly)
                    {
                        // 接收只需要处理接收会话
                        auto receive_session = std::make_shared<rtc::RtcpReceivingSession>();
                        audio_track_->setMediaHandler(receive_session);
                    }

                    // 设置接收回调
                    if (direction == rtc::Description::Direction::RecvOnly ||
                        direction == rtc::Description::Direction::SendRecv)
                    {

                        audio_track_->onMessage(
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
                                    {
                                        handleAudioData(payload, payload_size);
                                    }
                                }
                            });
                    }
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "创建音频轨道失败: %s", e.what());
                }
            }

            void WebRTCSystem::setupVideoTrack()
            {
                if (!peer_connection_)
                {
                    return;
                }

                try
                {
                    rtc::Description::Direction direction{};
                    direction = rtc::Description::Direction::Inactive;
                    if (config_.enableVideoSend && config_.enableVideoReceive)
                    {
                        direction = rtc::Description::Direction::SendRecv;
                    }
                    else if (config_.enableVideoSend)
                    {
                        direction = rtc::Description::Direction::SendOnly;
                    }
                    else if (config_.enableVideoReceive)
                    {
                        direction = rtc::Description::Direction::RecvOnly;
                    }

                    auto video_desc = rtc::Description::Video("video", direction);
                    video_desc.addH264Codec(102);
                    video_desc.addSSRC(1, "video", "stream1", "video");

                    video_track_ = peer_connection_->addTrack(video_desc);
                    LOG_INFO(LOG_TAG, "视频轨道已创建, direction=%d",
                             static_cast<int>(direction));

                    // 根据不同的方向设置MediaHandler
                    if (direction == rtc::Description::Direction::SendOnly ||
                        direction == rtc::Description::Direction::SendRecv)
                    {

                        // 创建RTP打包配置（发送端）
                        video_rtp_config_ = std::make_shared<rtc::RtpPacketizationConfig>(
                            1, "video", 102, rtc::H264RtpPacketizer::ClockRate);

                        // 创建H264 RTP打包器（MTU=1200）
                        video_packetizer_ = std::make_shared<rtc::H264RtpPacketizer>(
                            rtc::NalUnit::Separator::StartSequence, video_rtp_config_, 1200);

                        // 添加RTCP SR报告器
                        video_sr_reporter_ =
                            std::make_shared<rtc::RtcpSrReporter>(video_rtp_config_);
                        video_packetizer_->addToChain(video_sr_reporter_);

                        // 添加RTCP接收会话（用于接收RTCP反馈）
                        video_rtcp_session_ = std::make_shared<rtc::RtcpReceivingSession>();
                        video_packetizer_->addToChain(video_rtcp_session_);

                        // 添加REMB处理器（带宽估计反馈）
                        video_remb_handler_ = std::make_shared<rtc::RembHandler>(
                            [this](unsigned int bitrate) { onRembReceived(bitrate); });
                        video_packetizer_->addToChain(video_remb_handler_);

                        // 添加NACK响应器（重传机制）
                        auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
                        video_packetizer_->addToChain(nack_responder);

                        // 设置MediaHandler（只设置一次！）
                        video_track_->setMediaHandler(video_packetizer_);

                        // 设置轨道打开回调
                        video_track_->onOpen(
                            []()
                            { LOG_INFO(LOG_TAG, "视频轨道已打开，可以开始发送视频数据"); });

                        LOG_INFO(LOG_TAG, "视频发送链已配置完成");
                    }

                    if (direction == rtc::Description::Direction::RecvOnly)
                    {
                        // 纯接收模式：只需要接收会话
                        auto receive_session = std::make_shared<rtc::RtcpReceivingSession>();
                        video_track_->setMediaHandler(receive_session);
                        LOG_INFO(LOG_TAG, "视频接收链已配置完成（RecvOnly）");
                    }

                    // 设置接收回调（SendRecv和RecvOnly都需要）
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
                                    {
                                        handleVideoData(payload, payload_size, get_nowus());
                                    }
                                }
                            });

                        LOG_INFO(LOG_TAG, "视频接收回调已设置");
                    }
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "创建视频轨道失败: %s", e.what());
                }
            }

            void WebRTCSystem::handleDataChannel()
            {
                LOG_INFO(LOG_TAG, "DataChannel 已打开，可进行数据通信");
                if (role_ == "offerer")
                {
                    std::string test_message = "Hello from glasses device !";
                    sendDataMessage(test_message);
                }
            }

            void WebRTCSystem::handleDataMessage(const std::string& message)
            {
                LOG_INFO(LOG_TAG, "收到 DataChannel 消息: %s", message.c_str());
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (data_message_callback_)
                {
                    try
                    {
                        data_message_callback_(message);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "数据回调异常: %s", e.what());
                    }
                }
            }

            void WebRTCSystem::handleAudioData(const uint8_t* data, size_t size)
            {
                if (!data || size == 0)
                {
                    return;
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

            void WebRTCSystem::handleVideoData(const uint8_t* data, size_t size, uint64_t timestamp)
            {
                if (!data || size == 0)
                {
                    return;
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

            void WebRTCSystem::setState(WebRTCState new_state)
            {
                WebRTCState old_state = state_.exchange(new_state, std::memory_order_acq_rel);
                if (old_state != new_state)
                {
                    LOG_INFO(LOG_TAG, "状态变更: %d -> %d", static_cast<int>(old_state),
                             static_cast<int>(new_state));
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    if (state_callback_)
                    {
                        try
                        {
                            state_callback_(new_state);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "状态回调异常: %s", e.what());
                        }
                    }
                }
            }

            void WebRTCSystem::configureSctp()
            {
                try
                {
                    rtc::SctpSettings settings{};
                    settings.recvBufferSize           = config_.sctp.recvBufferSize;
                    settings.sendBufferSize           = config_.sctp.sendBufferSize;
                    settings.maxChunksOnQueue         = config_.sctp.maxChunksOnQueue;
                    settings.initialCongestionWindow  = config_.sctp.initialCongestionWindow;
                    settings.maxBurst                 = config_.sctp.maxBurst;
                    settings.congestionControlModule  = config_.sctp.congestionControlModule;
                    settings.delayedSackTime          = config_.sctp.delayedSackTime;
                    settings.minRetransmitTimeout     = config_.sctp.minRetransmitTimeout;
                    settings.maxRetransmitTimeout     = config_.sctp.maxRetransmitTimeout;
                    settings.initialRetransmitTimeout = config_.sctp.initialRetransmitTimeout;
                    settings.maxRetransmitAttempts    = config_.sctp.maxRetransmitAttempts;
                    settings.heartbeatInterval        = config_.sctp.heartbeatInterval;

                    rtc::SetSctpSettings(settings);
                    LOG_INFO(LOG_TAG, "SCTP 配置完成");
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "SCTP 配置失败: %s", e.what());
                }
            }

            void WebRTCSystem::cleanup()
            {
                LOG_INFO(LOG_TAG, "清理内部资源...");

                // 销毁任务队列（会等待所有工作线程退出）
                if (audio_task_queue_)
                {
                    audio_task_queue_.reset(); // 触发析构，join 所有线程
                }
                if (video_task_queue_)
                {
                    video_task_queue_.reset();
                }

                // 此时所有任务已执行完毕，可以安全释放缓冲池
                audio_buffer_pool_.reset();
                video_buffer_pool_.reset();

                // 清空回调函数
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    state_callback_        = nullptr;
                    data_message_callback_ = nullptr;
                    audio_callback_        = nullptr;
                    video_callback_        = nullptr;
                }

                // 重置统计信息
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_ = Stats{};
                }

                LOG_INFO(LOG_TAG, "资源清理完成");
            }

            bool WebRTCSystem::parseRtpPacket(const uint8_t* rtp_data, size_t rtp_size,
                                              const uint8_t*& payload_data, size_t& payload_size)
            {
                if (!rtp_data || rtp_size < RTP_HEADER_MIN_SIZE)
                {
                    return false;
                }

                size_t  header_size = RTP_HEADER_MIN_SIZE;
                uint8_t cc          = rtp_data[0] & RTP_CC_MASK;
                header_size += static_cast<size_t>(cc) * RTP_CSRC_SIZE; // CSRC 列表

                bool extension_bit = (rtp_data[0] & RTP_EXTENSION_MASK) != 0;
                bool padding_bit   = (rtp_data[0] & RTP_PADDING_MASK) != 0;

                if (header_size >= rtp_size)
                {
                    return false;
                }

                size_t ext_size = 0;
                if (extension_bit)
                {
                    if (header_size + RTP_EXTENSION_HEADER_LEN > rtp_size)
                    {
                        return false;
                    }
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    auto ext_length = static_cast<uint16_t>(
                        (static_cast<uint16_t>(rtp_data[header_size + 2]) << RTP_EXT_LENGTH_SHIFT) |
                        static_cast<uint16_t>(rtp_data[header_size + 3]));
                    ext_size =
                        RTP_EXTENSION_HEADER_LEN + static_cast<size_t>(ext_length) * RTP_CSRC_SIZE;
                    header_size += ext_size;
                    if (header_size >= rtp_size)
                    {
                        return false;
                    }
                }

                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                payload_data = rtp_data + header_size;
                payload_size = rtp_size - header_size;

                if (padding_bit && payload_size > 0)
                {
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    uint8_t padding_count = rtp_data[rtp_size - 1];
                    if (padding_count <= payload_size)
                    {
                        payload_size -= static_cast<size_t>(padding_count);
                    }
                }

                return payload_size > 0;
            }

            void WebRTCSystem::onRembReceived(unsigned int bitrate)
            {
                if (bitrate == 0)
                {
                    return;
                }

                uint32_t old_bitrate = current_bitrate_.exchange(bitrate);

                // 只在码率变化超过阈值时记录日志
                if (old_bitrate == 0 ||
                    std::abs(static_cast<int64_t>(bitrate) - static_cast<int64_t>(old_bitrate)) >
                        static_cast<int64_t>(static_cast<double>(old_bitrate) *
                                             BITRATE_CHANGE_THRESHOLD))
                {
                    LOG_INFO(LOG_TAG, "码率调整: %u -> %u bps (%.2f Mbps)", old_bitrate,
                             bitrate, bitrate / 1000000.0);
                }

                // 注意：实际的码率控制需要在编码器层面实现
                // 这里只记录码率变化，供外部监控使用
                // 如果需要动态调整编码器，可以通过回调通知上层
            }

        } // namespace webrtc
    }     // namespace protocol
} // namespace app