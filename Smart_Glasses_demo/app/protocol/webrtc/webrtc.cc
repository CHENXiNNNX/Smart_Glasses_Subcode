#include "webrtc.h"
#include <chrono>
#include "../../../common/common.h"

using namespace glasses::protocol;

// ========== PriorityTaskQueue 实现 ==========

PriorityTaskQueue::PriorityTaskQueue(const std::string& name, size_t threadCount) 
    : name_(name) {
    for (size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back(&PriorityTaskQueue::workerThread, this);
    }
    std::cout << "[TaskQueue] " << name_ << " 启动，线程数: " << threadCount << std::endl;
}

PriorityTaskQueue::~PriorityTaskQueue() {
    stop_ = true;
    condition_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    std::cout << "[TaskQueue] " << name_ << " 已停止" << std::endl;
}

void PriorityTaskQueue::post(std::function<void()> func, TaskPriority priority) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Task task;
        task.func = std::move(func);
        task.priority = priority;
        task.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
}

void PriorityTaskQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::priority_queue<Task> empty;
    tasks_.swap(empty);
}

void PriorityTaskQueue::workerThread() {
    while (!stop_) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return !tasks_.empty() || stop_; });
        
        if (stop_) break;
        
        if (!tasks_.empty()) {
            Task task = std::move(const_cast<Task&>(tasks_.top()));
            tasks_.pop();
            lock.unlock();
            
            try {
                task.func();
            } catch (const std::exception& e) {
                std::cout << "[TaskQueue] " << name_ << " 任务执行异常: " << e.what() << std::endl;
            }
        }
    }
}

// ========== AudioBufferPool 实现 ==========

AudioBufferPool::AudioBufferPool() {
    // 初始化内存池（1MB初始大小，16字节对齐）
    memPool_ = std::make_unique<tool::memory::MemoryPool>(1024 * 1024, 16, 1.5);
    
    // 预分配缓冲区
    for (size_t i = 0; i < POOL_SIZE; ++i) {
        uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(AUDIO_BUFFER_SIZE));
        if (data) {
            auto buffer = std::make_shared<MediaBuffer>(data, AUDIO_BUFFER_SIZE, memPool_.get());
            freeBuffers_.push(buffer);
        }
    }
    
    std::cout << "[AudioBufferPool] 初始化完成，预分配: " << freeBuffers_.size() << " 个缓冲区" << std::endl;
}

AudioBufferPool::~AudioBufferPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    // shared_ptr会自动释放，MediaBuffer析构时会归还内存池
    while (!freeBuffers_.empty()) {
        freeBuffers_.pop();
    }
}

MediaBufferPtr AudioBufferPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!freeBuffers_.empty()) {
        auto buffer = freeBuffers_.front();
        freeBuffers_.pop();
        buffer->reset();  // 重置大小
        return buffer;
    }
    
    // 如果池空了，临时分配一个
    uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(AUDIO_BUFFER_SIZE));
    if (data) {
        return std::make_shared<MediaBuffer>(data, AUDIO_BUFFER_SIZE, memPool_.get());
    }
    
    return nullptr;
}

void AudioBufferPool::release(MediaBufferPtr buffer) {
    if (!buffer) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (freeBuffers_.size() < POOL_SIZE * 2) {  // 限制池大小
        buffer->reset();
        freeBuffers_.push(buffer);
    }
    // 否则让shared_ptr自动释放
}

// ========== VideoBufferPool 实现 ==========

VideoBufferPool::VideoBufferPool() {
    // 初始化内存池（50MB初始大小）
    memPool_ = std::make_unique<tool::memory::MemoryPool>(50 * 1024 * 1024, 16, 1.5);
    
    // 预分配小缓冲区（P帧）
    for (size_t i = 0; i < SMALL_POOL_SIZE; ++i) {
        uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(SMALL_BUFFER_SIZE));
        if (data) {
            auto buffer = std::make_shared<MediaBuffer>(data, SMALL_BUFFER_SIZE, memPool_.get());
            smallBuffers_.push(buffer);
        }
    }
    
    // 预分配中缓冲区
    for (size_t i = 0; i < MEDIUM_POOL_SIZE; ++i) {
        uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(MEDIUM_BUFFER_SIZE));
        if (data) {
            auto buffer = std::make_shared<MediaBuffer>(data, MEDIUM_BUFFER_SIZE, memPool_.get());
            mediumBuffers_.push(buffer);
        }
    }
    
    // 预分配大缓冲区（I帧）
    for (size_t i = 0; i < LARGE_POOL_SIZE; ++i) {
        uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(LARGE_BUFFER_SIZE));
        if (data) {
            auto buffer = std::make_shared<MediaBuffer>(data, LARGE_BUFFER_SIZE, memPool_.get());
            largeBuffers_.push(buffer);
        }
    }
    
    std::cout << "[VideoBufferPool] 初始化完成，预分配: " 
              << smallBuffers_.size() << " 小/" 
              << mediumBuffers_.size() << " 中/" 
              << largeBuffers_.size() << " 大 缓冲区" << std::endl;
}

VideoBufferPool::~VideoBufferPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!smallBuffers_.empty()) smallBuffers_.pop();
    while (!mediumBuffers_.empty()) mediumBuffers_.pop();
    while (!largeBuffers_.empty()) largeBuffers_.pop();
}

MediaBufferPtr VideoBufferPool::acquire(size_t requiredSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 根据大小选择合适的缓冲区
    if (requiredSize <= SMALL_BUFFER_SIZE && !smallBuffers_.empty()) {
        auto buffer = smallBuffers_.front();
        smallBuffers_.pop();
        buffer->reset();
        return buffer;
    }
    
    if (requiredSize <= MEDIUM_BUFFER_SIZE && !mediumBuffers_.empty()) {
        auto buffer = mediumBuffers_.front();
        mediumBuffers_.pop();
        buffer->reset();
        return buffer;
    }
    
    if (requiredSize <= LARGE_BUFFER_SIZE && !largeBuffers_.empty()) {
        auto buffer = largeBuffers_.front();
        largeBuffers_.pop();
        buffer->reset();
        return buffer;
    }
    
    // 如果没有合适的，临时分配
    size_t allocSize = requiredSize;
    if (allocSize <= SMALL_BUFFER_SIZE) allocSize = SMALL_BUFFER_SIZE;
    else if (allocSize <= MEDIUM_BUFFER_SIZE) allocSize = MEDIUM_BUFFER_SIZE;
    else if (allocSize <= LARGE_BUFFER_SIZE) allocSize = LARGE_BUFFER_SIZE;
    
    uint8_t* data = static_cast<uint8_t*>(memPool_->allocate(allocSize));
    if (data) {
        return std::make_shared<MediaBuffer>(data, allocSize, memPool_.get());
    }
    
    return nullptr;
}

void VideoBufferPool::release(MediaBufferPtr buffer) {
    if (!buffer) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t capacity = buffer->capacity();
    buffer->reset();
    
    // 归还到对应的池
    if (capacity == SMALL_BUFFER_SIZE && smallBuffers_.size() < SMALL_POOL_SIZE * 2) {
        smallBuffers_.push(buffer);
    } else if (capacity == MEDIUM_BUFFER_SIZE && mediumBuffers_.size() < MEDIUM_POOL_SIZE * 2) {
        mediumBuffers_.push(buffer);
    } else if (capacity == LARGE_BUFFER_SIZE && largeBuffers_.size() < LARGE_POOL_SIZE * 2) {
        largeBuffers_.push(buffer);
    }
    // 否则让shared_ptr自动释放
}

// ========== WebRTCManage 实现 ==========

WebRTCManage::WebRTCManage(const WebRTCConfig& config)
    : config_(config)
    , state_(WebRTCState::DISCONNECTED)
    , iceConnected_(false)
    , initialized_(false)
    , lastAudioSendTime_{}
    , lastVideoSendTime_{} {
    
    std::cout << "[WebRTC] 初始化WebRTC管理器" << std::endl;
    std::cout << "[WebRTC] 数据通道: " << (config_.enableDataChannel ? "启用" : "禁用") << std::endl;
    std::cout << "[WebRTC] 音频发送: " << (config_.enableAudioSend ? "启用" : "禁用") << std::endl;
    std::cout << "[WebRTC] 音频接收: " << (config_.enableAudioReceive ? "启用" : "禁用") << std::endl;
    std::cout << "[WebRTC] 视频发送: " << (config_.enableVideoSend ? "启用" : "禁用") << std::endl;
    std::cout << "[WebRTC] 零拷贝模式: " << (config_.performance.enableZeroCopy ? "启用" : "禁用") << std::endl;
    
    // 创建优先级任务队列
    audioTaskQueue_ = std::make_unique<PriorityTaskQueue>("Audio_Queue", config_.performance.audioThreadCount);
    videoTaskQueue_ = std::make_unique<PriorityTaskQueue>("Video_Queue", config_.performance.videoThreadCount);
    
    // 创建缓冲区池
    if (config_.performance.enableZeroCopy) {
        audioBufferPool_ = std::make_unique<AudioBufferPool>();
        videoBufferPool_ = std::make_unique<VideoBufferPool>();
    }
    
    configureSctp();
}

WebRTCManage::~WebRTCManage() {
    shutdown();
}

bool WebRTCManage::initialize(std::shared_ptr<Signaling> signaling) {
    if (!signaling) {
        std::cout << "[WebRTC] 信令模块为空，初始化失败" << std::endl;
        return false;
    }
    
    signaling_ = signaling;
    
    // 设置信令回调
    signaling_->onWebRTCReady([this](const std::string& role, const std::string& peerDeviceId) {
        handleRole(role, peerDeviceId);
    });
    
    signaling_->onOfferReceived([this](const nlohmann::json& msg) {
        if (msg.contains("data") && msg["data"].contains("sdp")) {
            std::string sdp = msg["data"]["sdp"].get<std::string>();
            handleRemoteOffer(sdp);
        }
    });
    
    signaling_->onAnswerReceived([this](const nlohmann::json& msg) {
        if (msg.contains("data") && msg["data"].contains("sdp")) {
            std::string sdp = msg["data"]["sdp"].get<std::string>();
            handleRemoteAnswer(sdp);
        }
    });
    
    signaling_->onIceCandidateReceived([this](const nlohmann::json& msg) {
        if (msg.contains("data") && msg["data"].contains("candidate")) {
            std::string candidate = msg["data"]["candidate"].get<std::string>();
            handleRemoteIceCandidate(candidate);
        }
    });
    
    initialized_ = true;
    std::cout << "[WebRTC] 初始化完成，等待信令配对" << std::endl;
    return true;
}

void WebRTCManage::shutdown() {
    if (!initialized_) return;
    
    closeConnection();
    cleanup();
    initialized_ = false;
    setState(WebRTCState::DISCONNECTED);
    std::cout << "[WebRTC] WebRTC管理器已关闭" << std::endl;
}

bool WebRTCManage::createConnection() {
    try {
        rtc::Configuration rtcConfig;
        
        // 添加STUN服务器
        for (const auto& stunServer : config_.ice.stunServers) {
            rtcConfig.iceServers.emplace_back(stunServer);
            std::cout << "[WebRTC] 添加STUN服务器: " << stunServer << std::endl;
        }
        
        // 添加TURN服务器
        for (const auto& turnServer : config_.ice.turnServers) {
            rtcConfig.iceServers.emplace_back(turnServer);
            std::cout << "[WebRTC] 添加TURN服务器: " << turnServer << std::endl;
        }
        
        // 设置ICE传输策略
        rtcConfig.iceTransportPolicy = config_.ice.useRelayOnly ? 
            rtc::TransportPolicy::Relay : rtc::TransportPolicy::All;
        
        rtcConfig.disableAutoNegotiation = true;

        peerConnection_ = std::make_shared<rtc::PeerConnection>(rtcConfig);
        setupCallbacks();
        
        std::cout << "[WebRTC] PeerConnection创建成功" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 创建PeerConnection失败: " << e.what() << std::endl;
        return false;
    }
}

void WebRTCManage::closeConnection() {
    cleanup();
    setState(WebRTCState::DISCONNECTED);
}

void WebRTCManage::handleRole(const std::string& role, const std::string& peerDeviceId) {
    role_ = role;
    peerDeviceId_ = peerDeviceId;
    
    std::cout << "[WebRTC] 收到角色分配 - 角色: " << role_ 
              << ", 对端设备: " << peerDeviceId_ << std::endl;
    
    if (!createConnection()) {
        std::cout << "[WebRTC] 创建PeerConnection失败" << std::endl;
        return;
    }
    
    setState(WebRTCState::CONNECTING);
    
    if (role_ == "offerer") {
        // 创建数据通道
        if (config_.enableDataChannel) {
            setupDataChannel();
        }
        
        // 创建音频轨道
        if (config_.enableAudioSend) {
            setupAudioTrack();
        }
        
        // 创建视频轨道
        if (config_.enableVideoSend) {
            setupVideoTrack();
        }

        peerConnection_->setLocalDescription();
        
    } else if (role_ == "answerer") {
        // 作为应答方，等待接收轨道
        if (config_.enableAudioReceive) {
            setupAudioTrack();
        }
        
        if (config_.enableVideoReceive) {
            setupVideoTrack();
        }
    }
}

void WebRTCManage::handleRemoteOffer(const std::string& sdp) {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法处理Offer" << std::endl;
        return;
    }
    
    try {
        std::cout << "[WebRTC] 接收SDP Offer" << std::endl;
        
        // 打印接收到的远程SDP内容
        std::cout << "[WebRTC] ========== 远程SDP Offer内容 ==========" << std::endl;
        std::cout << sdp << std::endl;
        std::cout << "[WebRTC] ========== SDP Offer内容结束 ==========" << std::endl;
        
        rtc::Description remoteDesc(sdp, rtc::Description::Type::Offer);
        peerConnection_->setRemoteDescription(remoteDesc);
        std::cout << "[WebRTC] 设置远程SDP Offer成功" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 设置远程SDP Offer失败: " << e.what() << std::endl;
        setState(WebRTCState::FAILED);
    }
}

void WebRTCManage::handleRemoteAnswer(const std::string& sdp) {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法处理Answer" << std::endl;
        return;
    }
    
    try {
        std::cout << "[WebRTC] 接收SDP Answer" << std::endl;
        
        // 打印接收到的远程SDP内容
        std::cout << "[WebRTC] ========== 远程SDP Answer内容 ==========" << std::endl;
        std::cout << sdp << std::endl;
        std::cout << "[WebRTC] ========== SDP Answer内容结束 ==========" << std::endl;
        
        rtc::Description remoteDesc(sdp, rtc::Description::Type::Answer);
        peerConnection_->setRemoteDescription(remoteDesc);
        std::cout << "[WebRTC] 设置远程SDP Answer成功" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 设置远程SDP Answer失败: " << e.what() << std::endl;
        setState(WebRTCState::FAILED);
    }
}

void WebRTCManage::handleRemoteIceCandidate(const std::string& candidate) {
    if (!peerConnection_) {
        std::cout << "[WebRTC] PeerConnection未创建，无法处理ICE候选" << std::endl;
        return;
    }
    
    try {
        std::cout << "[WebRTC] 接收并添加远程ICE候选" << std::endl;
        rtc::Candidate rtcCandidate(candidate);
        peerConnection_->addRemoteCandidate(rtcCandidate);
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 添加远程ICE候选失败: " << e.what() << std::endl;
    }
}

bool WebRTCManage::sendDataMessage(const std::string& message) {
    if (!isConnected() || !dataChannel_ || !dataChannel_->isOpen()) {
        return false;
    }
    
    try {
        dataChannel_->send(message);
        return true;
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 数据通道消息发送失败: " << e.what() << std::endl;
        return false;
    }
}

bool WebRTCManage::isDataChannelOpen() const {
    return dataChannel_ && dataChannel_->isOpen();
}

// ========== 零拷贝音频发送 ==========
void WebRTCManage::sendAudioData(const uint8_t* data, size_t size, uint64_t timestamp) {
    if (!isConnected() || !audioTrack_ || !audioTrack_->isOpen()) {
        return;
    }
    
    if (!data || size == 0) {
        return;
    }
    
    // 发送频率控制
    auto now = std::chrono::steady_clock::now();
    if (lastAudioSendTime_ != std::chrono::steady_clock::time_point{}) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAudioSendTime_);
        if (elapsed.count() < AUDIO_SEND_INTERVAL_MS) {
            return;
        }
    }
    lastAudioSendTime_ = now;
    
    // 使用缓冲区池
    if (config_.performance.enableZeroCopy && audioBufferPool_) {
        auto buffer = audioBufferPool_->acquire();
        if (!buffer || !buffer->write(data, size)) {
            // 缓冲区不够，降级为拷贝模式
            std::cout << "[WebRTC] 音频缓冲区不足，降级为拷贝模式" << std::endl;
        } else {
            // 提交到高优先级队列
            audioTaskQueue_->post([this, buffer, timestamp]() {
                try {
                    auto sampleTime = std::chrono::duration<double, std::micro>(timestamp);
                    audioTrack_->sendFrame(reinterpret_cast<const std::byte*>(buffer->data()), 
                                         buffer->size(), sampleTime);
                    
                    // 统计
                    {
                        std::lock_guard<std::mutex> lock(statsMutex_);
                        stats_.audioPacketsSent++;
                        stats_.audioBytesSent += buffer->size();
                    }
                    
                    // 归还缓冲区
                    audioBufferPool_->release(buffer);
                } catch (const std::exception& e) {
                    std::cout << "[WebRTC] 发送音频数据失败: " << e.what() << std::endl;
                }
            }, TaskPriority::HIGH);
            return;
        }
    }
    
    // 失败退回使用传统拷贝模式
    auto dataPtr = std::make_shared<std::vector<uint8_t>>(data, data + size);
    audioTaskQueue_->post([this, dataPtr, timestamp]() {
        try {
            auto sampleTime = std::chrono::duration<double, std::micro>(timestamp);
            audioTrack_->sendFrame(reinterpret_cast<const std::byte*>(dataPtr->data()), 
                                 dataPtr->size(), sampleTime);
            
            // 统计
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.audioPacketsSent++;
                stats_.audioBytesSent += dataPtr->size();
            }
        } catch (const std::exception& e) {
            std::cout << "[WebRTC] 发送音频数据失败: " << e.what() << std::endl;
        }
    }, TaskPriority::HIGH);
}

// ========== 零拷贝视频发送 ==========
void WebRTCManage::sendVideoData(const uint8_t* data, size_t size, uint64_t timestamp, bool isKeyFrame) {
    if (!isConnected() || !videoTrack_ || !videoTrack_->isOpen()) {
        return;
    }
    
    if (!data || size == 0) {
        return;
    }
    
    // 发送频率控制
    auto now = std::chrono::steady_clock::now();
    if (lastVideoSendTime_ != std::chrono::steady_clock::time_point{}) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVideoSendTime_);
        if (elapsed.count() < VIDEO_SEND_INTERVAL_MS) {
            return;
        }
    }
    lastVideoSendTime_ = now;
    
    // 缓冲区池
    if (config_.performance.enableZeroCopy && videoBufferPool_) {
        auto buffer = videoBufferPool_->acquire(size);
        if (!buffer || !buffer->write(data, size)) {
            std::cout << "[WebRTC] 视频缓冲区不足，降级为拷贝模式" << std::endl;
        } else {
            // I帧高优先级，P帧普通优先级
            TaskPriority priority = isKeyFrame ? TaskPriority::NORMAL : TaskPriority::LOW;
            
            videoTaskQueue_->post([this, buffer, timestamp]() {
                try {
                    auto sampleTime = std::chrono::duration<double, std::micro>(timestamp);
                    videoTrack_->sendFrame(reinterpret_cast<const std::byte*>(buffer->data()), 
                                         buffer->size(), sampleTime);
                    
                    // 统计
                    {
                        std::lock_guard<std::mutex> lock(statsMutex_);
                        stats_.videoPacketsSent++;
                        stats_.videoBytesSent += buffer->size();
                    }
                    
                    // 归还缓冲区
                    videoBufferPool_->release(buffer);
                } catch (const std::exception& e) {
                    std::cout << "[WebRTC] 发送视频数据失败: " << e.what() << std::endl;
                }
            }, priority);
            return;
        }
    }
    
    // 失败退回使用传统拷贝模式
    auto dataPtr = std::make_shared<std::vector<uint8_t>>(data, data + size);
    TaskPriority priority = isKeyFrame ? TaskPriority::NORMAL : TaskPriority::LOW;
    
    videoTaskQueue_->post([this, dataPtr, timestamp]() {
        try {
            auto sampleTime = std::chrono::duration<double, std::micro>(timestamp);
            videoTrack_->sendFrame(reinterpret_cast<const std::byte*>(dataPtr->data()), 
                                 dataPtr->size(), sampleTime);
            
            // 统计
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.videoPacketsSent++;
                stats_.videoBytesSent += dataPtr->size();
            }
        } catch (const std::exception& e) {
            std::cout << "[WebRTC] 发送视频数据失败: " << e.what() << std::endl;
        }
    }, priority);
}

// ========== 回调设置 ==========

void WebRTCManage::setupCallbacks() {
    if (!peerConnection_) return;
    
    // 本地描述生成回调
    peerConnection_->onLocalDescription([this](rtc::Description description) {
        std::cout << "[WebRTC] 本地SDP生成: " << description.typeString() << std::endl;
        
        if (signaling_ && signaling_->isPaired()) {
            std::string sdp = std::string(description);
            
            // 打印完整的SDP内容
            std::cout << "[WebRTC] ========== 本地SDP内容 (" << description.typeString() << ") ==========" << std::endl;
            std::cout << sdp << std::endl;
            std::cout << "[WebRTC] ========== SDP内容结束 ==========" << std::endl;
            
            if (description.type() == rtc::Description::Type::Offer) {
                signaling_->sendOffer(sdp, peerDeviceId_);
            } else if (description.type() == rtc::Description::Type::Answer) {
                signaling_->sendAnswer(sdp, peerDeviceId_);
            }
        }
    });
    
    // ICE候选生成回调
    peerConnection_->onLocalCandidate([this](rtc::Candidate candidate) {
        if (signaling_ && signaling_->isPaired()) {
            std::string candidateStr = std::string(candidate);
            signaling_->sendIceCandidate(candidateStr, peerDeviceId_);
        }
    });
    
    // 连接状态变化回调
    peerConnection_->onStateChange([this](rtc::PeerConnection::State state) {
        std::cout << "[WebRTC] 连接状态变化: " << static_cast<int>(state) << std::endl;
        
        if (state == rtc::PeerConnection::State::Connected) {
            if (iceConnected_) {
                setState(WebRTCState::CONNECTED);
            }
        } else if (state == rtc::PeerConnection::State::Failed) {
            setState(WebRTCState::FAILED);
        }
    });
    
    // ICE状态变化回调
    peerConnection_->onIceStateChange([this](rtc::PeerConnection::IceState state) {
        std::cout << "[WebRTC] ICE状态变化: " << static_cast<int>(state) << std::endl;
        
        switch (state) {
            case rtc::PeerConnection::IceState::New:
                iceConnected_ = false;
                setState(WebRTCState::CONNECTING);
                break;
                
            case rtc::PeerConnection::IceState::Checking:
                iceConnected_ = false;
                setState(WebRTCState::ICE_CONNECTING);
                break;
                
            case rtc::PeerConnection::IceState::Connected:
            case rtc::PeerConnection::IceState::Completed:
                iceConnected_ = true;
                if (state_ == WebRTCState::ICE_CONNECTING) {
                    setState(WebRTCState::CONNECTED);
                    std::cout << "[WebRTC] ICE连接建立完成，可以开始发送数据" << std::endl;
                }
                break;
                
            case rtc::PeerConnection::IceState::Failed:
            case rtc::PeerConnection::IceState::Disconnected:
            case rtc::PeerConnection::IceState::Closed:
                iceConnected_ = false;
                setState(WebRTCState::FAILED);
                break;
        }
    });
    
    // 数据通道接收回调（应答方）
    if (role_ == "answerer") {
        peerConnection_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            std::cout << "[WebRTC] 接收到远程DataChannel: " << dc->label() << std::endl;
            dataChannel_ = dc;
            
            dataChannel_->onOpen([this]() {
                handleDataChannelOpen();
            });
            
            dataChannel_->onMessage([this](auto data) {
                if (std::holds_alternative<std::string>(data)) {
                    std::string message = std::get<std::string>(data);
                    handleDataMessage(message);
                }
            });
        });
    }
    
    // 音频轨道接收回调
    if (config_.enableAudioReceive) {
        peerConnection_->onTrack([this](std::shared_ptr<rtc::Track> track) {
            if (track->description().type() == "audio") {
                audioTrack_ = track;
                
                // 使用自定义RTP解析
                audioTrack_->onMessage([this](rtc::message_variant data) {
                    if (std::holds_alternative<rtc::binary>(data)) {
                        auto& binaryData = std::get<rtc::binary>(data);
                        
                        // 提取Opus数据
                        const uint8_t* opusData;
                        size_t opusSize;
                        if (parseRtpPacket(reinterpret_cast<const uint8_t*>(binaryData.data()), 
                                         binaryData.size(), opusData, opusSize)) {
                            handleAudioData(opusData, opusSize);
                            
                            // 统计
                            {
                                std::lock_guard<std::mutex> lock(statsMutex_);
                                stats_.audioPacketsReceived++;
                            }
                        }
                    }
                });
                
                std::cout << "[WebRTC] 音频轨道接收回调已设置" << std::endl;
            }
        });
    }
}

void WebRTCManage::setupDataChannel() {
    if (!config_.enableDataChannel || !peerConnection_) {
        return;
    }
    
    try {
        dataChannel_ = peerConnection_->createDataChannel(config_.dataChannelLabel);
        
        dataChannel_->onOpen([this]() {
            handleDataChannelOpen();
        });
        
        dataChannel_->onMessage([this](auto data) {
            if (std::holds_alternative<std::string>(data)) {
                std::string message = std::get<std::string>(data);
                handleDataMessage(message);
            }
        });
        
        std::cout << "[WebRTC] DataChannel创建成功: " << config_.dataChannelLabel << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 创建DataChannel失败: " << e.what() << std::endl;
    }
}

void WebRTCManage::setupAudioTrack() {
    if (!peerConnection_) {
        return;
    }
    
    try {
        // 根据功能开关确定音频方向
        rtc::Description::Direction audioDirection;

        if (config_.enableAudioSend && config_.enableAudioReceive) {
            audioDirection = rtc::Description::Direction::SendRecv;
        } else if (config_.enableAudioSend) {
            audioDirection = rtc::Description::Direction::SendOnly;
        } else if (config_.enableAudioReceive) {
            audioDirection = rtc::Description::Direction::RecvOnly;
        } else {
            audioDirection = rtc::Description::Direction::Inactive;
        }
        
        // 创建音频描述
        auto audio = rtc::Description::Audio("audio", audioDirection);
        audio.addOpusCodec(111);
        audio.addSSRC(2, "audio", "stream1", "audio");
        
        // 添加轨道到PeerConnection
        audioTrack_ = peerConnection_->addTrack(audio);
        
        // 根据方向配置处理链
        if (audioDirection == rtc::Description::Direction::SendOnly || 
            audioDirection == rtc::Description::Direction::SendRecv) {

            // 创建RTP配置
            audioRtpConfig_ = std::make_shared<rtc::RtpPacketizationConfig>(
                2, "audio", 111, rtc::OpusRtpPacketizer::DefaultClockRate);
            
            // 创建Opus RTP封装器
            audioPacketizer_ = std::make_shared<rtc::OpusRtpPacketizer>(audioRtpConfig_);
            
            // 创建RTCP SR报告器
            audioSrReporter_ = std::make_shared<rtc::RtcpSrReporter>(audioRtpConfig_);
            audioPacketizer_->addToChain(audioSrReporter_);
            
            // 创建RTCP接收会话
            audioRtcpSession_ = std::make_shared<rtc::RtcpReceivingSession>();
            audioPacketizer_->addToChain(audioRtcpSession_);
            
            // 创建REMB处理器
            audioRembHandler_ = std::make_shared<rtc::RembHandler>([this](unsigned int bitrate) {
                onRembReceived(bitrate);
            });
            audioPacketizer_->addToChain(audioRembHandler_);
            
            // 添加RTCP NACK处理器
            auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
            audioPacketizer_->addToChain(nackResponder);
            
            // 设置媒体处理器
            audioTrack_->setMediaHandler(audioPacketizer_);
            
            // 设置轨道打开回调
            audioTrack_->onOpen([this]() {
                std::cout << "[WebRTC] Opus音频轨道已打开，可以开始发送音频数据" << std::endl;
            });
            
            if (audioDirection == rtc::Description::Direction::SendRecv) {
                // 设置双向音频接收回调
                audioTrack_->onMessage([this](rtc::message_variant data) {
                    if (std::holds_alternative<rtc::binary>(data)) {
                        auto& binaryData = std::get<rtc::binary>(data);
                        
                        // 使用RTP解析函数提取Opus数据
                        const uint8_t* opusData;
                        size_t opusSize;
                        if (parseRtpPacket(reinterpret_cast<const uint8_t*>(binaryData.data()), 
                                         binaryData.size(), opusData, opusSize)) {
                            handleAudioData(opusData, opusSize);
                        }
                    }
                });
            }

        } else if (audioDirection == rtc::Description::Direction::RecvOnly) {
            // 接收端只需要接收会话
            auto receiveSession = std::make_shared<rtc::RtcpReceivingSession>();
            audioTrack_->setMediaHandler(receiveSession);
            
            // 设置接收回调
            audioTrack_->onMessage([this](rtc::message_variant data) {
                if (std::holds_alternative<rtc::binary>(data)) {
                    auto& binaryData = std::get<rtc::binary>(data);
                    
                    // 使用RTP解析函数提取Opus数据
                    const uint8_t* opusData;
                    size_t opusSize;
                    if (parseRtpPacket(reinterpret_cast<const uint8_t*>(binaryData.data()), 
                                     binaryData.size(), opusData, opusSize)) {
                        handleAudioData(opusData, opusSize);
                    }
                }
            });
        }
        
        std::cout << "[WebRTC] Opus音频轨道创建成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 创建Opus音频轨道失败: " << e.what() << std::endl;
    }
}

void WebRTCManage::setupVideoTrack() {
    if (!peerConnection_) {
        return;
    }
    
    try {
        // 根据功能开关确定视频方向
        rtc::Description::Direction videoDirection;

        if (config_.enableVideoSend && config_.enableVideoReceive) {
            videoDirection = rtc::Description::Direction::SendRecv;
        } else if (config_.enableVideoSend) {
            videoDirection = rtc::Description::Direction::SendOnly;
        } else if (config_.enableVideoReceive) {
            videoDirection = rtc::Description::Direction::RecvOnly;
        } else {
            videoDirection = rtc::Description::Direction::Inactive;
        }
        
        // 创建视频描述
        auto video = rtc::Description::Video("video", videoDirection);
        video.addH264Codec(102);
        video.addSSRC(1, "video", "stream1", "video");

        // 添加轨道到PeerConnection
        videoTrack_ = peerConnection_->addTrack(video);
        
        // 根据方向配置处理链
        if (videoDirection == rtc::Description::Direction::SendOnly || 
            videoDirection == rtc::Description::Direction::SendRecv) {

            // 创建RTP配置
            videoRtpConfig_ = std::make_shared<rtc::RtpPacketizationConfig>(
                1, "video", 102, rtc::H264RtpPacketizer::ClockRate);
            
            // 创建H264 RTP封装器
            videoPacketizer_ = std::make_shared<rtc::H264RtpPacketizer>(
                rtc::NalUnit::Separator::StartSequence, videoRtpConfig_, 1200);  // MTU 1200
            
            // 创建RTCP SR报告器
            videoSrReporter_ = std::make_shared<rtc::RtcpSrReporter>(videoRtpConfig_);
            videoPacketizer_->addToChain(videoSrReporter_);
            
            // 创建RTCP接收会话
            videoRtcpSession_ = std::make_shared<rtc::RtcpReceivingSession>();
            videoPacketizer_->addToChain(videoRtcpSession_);
            
            // 创建REMB处理器
            videoRembHandler_ = std::make_shared<rtc::RembHandler>([this](unsigned int bitrate) {
                onRembReceived(bitrate);
            });
            videoPacketizer_->addToChain(videoRembHandler_);
            
            // 添加RTCP NACK处理器
            auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
            videoPacketizer_->addToChain(nackResponder);
            
            // 设置媒体处理器
            videoTrack_->setMediaHandler(videoPacketizer_);
            
            // 设置轨道打开回调
            videoTrack_->onOpen([this]() {
                std::cout << "[WebRTC] H264视频轨道已打开，可以开始发送视频数据" << std::endl;
            });
            
        } else if (videoDirection == rtc::Description::Direction::RecvOnly) {
            // 视频接收端（暂留空实现）
            auto receiveSession = std::make_shared<rtc::RtcpReceivingSession>();
            videoTrack_->setMediaHandler(receiveSession);
        }
        
        std::cout << "[WebRTC] H264视频轨道创建成功" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] 创建H264视频轨道失败: " << e.what() << std::endl;
    }
}

void WebRTCManage::handleDataChannelOpen() {
    std::cout << "[WebRTC] DataChannel连接已打开" << std::endl;
    
    if (role_ == "offerer") {
        std::string testMessage = "Hello from glasses device !";
        sendDataMessage(testMessage);
    }
}

void WebRTCManage::handleDataMessage(const std::string& message) {
    std::cout << "[WebRTC] 收到DataChannel消息: " << message << std::endl;
    
    if (role_ == "answerer") {
        std::string reply = "Reply from peer : " + message;
        sendDataMessage(reply);
    }
    
    if (dataMessageCallback_) {
        dataMessageCallback_(message);
    }
}

void WebRTCManage::handleAudioData(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return;
    }
    
    if (audioCallback_) {
        audioCallback_(data, size);
    }
}

void WebRTCManage::handleVideoFrame(const uint8_t* data, size_t size, uint64_t timestamp) {
    if (!data || size == 0) {
        return;
    }
    
    if (videoCallback_) {
        videoCallback_(data, size, timestamp);
    }
}

void WebRTCManage::setState(WebRTCState newState) {
    if (state_ != newState) {
        WebRTCState oldState = state_;
        state_ = newState;
        
        std::cout << "[WebRTC] 状态变更: " << static_cast<int>(oldState) 
                  << " -> " << static_cast<int>(newState) << std::endl;
        
        if (stateCallback_) {
            stateCallback_(newState);
        }
    }
}

void WebRTCManage::configureSctp() {
    try {
        rtc::SctpSettings sctpSettings;
        
        sctpSettings.recvBufferSize = config_.sctp.recvBufferSize;
        sctpSettings.sendBufferSize = config_.sctp.sendBufferSize;
        sctpSettings.maxChunksOnQueue = config_.sctp.maxChunksOnQueue;
        sctpSettings.initialCongestionWindow = config_.sctp.initialCongestionWindow;
        sctpSettings.maxBurst = config_.sctp.maxBurst;
        sctpSettings.congestionControlModule = config_.sctp.congestionControlModule;
        sctpSettings.delayedSackTime = config_.sctp.delayedSackTime;
        sctpSettings.minRetransmitTimeout = config_.sctp.minRetransmitTimeout;
        sctpSettings.maxRetransmitTimeout = config_.sctp.maxRetransmitTimeout;
        sctpSettings.initialRetransmitTimeout = config_.sctp.initialRetransmitTimeout;
        sctpSettings.maxRetransmitAttempts = config_.sctp.maxRetransmitAttempts;
        sctpSettings.heartbeatInterval = config_.sctp.heartbeatInterval;
        
        rtc::SetSctpSettings(std::move(sctpSettings));
        
    } catch (const std::exception& e) {
        std::cout << "[WebRTC] SCTP配置失败: " << e.what() << std::endl;
    }
}

void WebRTCManage::cleanup() {
    // 清空任务队列
    if (audioTaskQueue_) audioTaskQueue_->clear();
    if (videoTaskQueue_) videoTaskQueue_->clear();
    
    // 清理音频轨道相关资源
    audioRembHandler_.reset();
    audioRtcpSession_.reset();
    audioSrReporter_.reset();
    audioPacketizer_.reset();
    audioRtpConfig_.reset();
    audioTrack_.reset();
    
    // 清理视频轨道相关资源
    videoRembHandler_.reset();
    videoRtcpSession_.reset();
    videoSrReporter_.reset();
    videoPacketizer_.reset();
    videoRtpConfig_.reset();
    videoTrack_.reset();
    
    // 清理数据通道相关资源
    dataChannel_.reset();
    
    // 清理对等连接相关资源
    peerConnection_.reset();
    
    role_.clear();
    peerDeviceId_.clear();
    iceConnected_ = false;
}

void WebRTCManage::onRembReceived(unsigned int bitrate) {
    if (bitrate == 0) {
        return;
    }

    // 暂留空处理，后续实现

    std::cout << "[WebRTC] Received REMB: " << bitrate << " bps" << std::endl;
}

bool WebRTCManage::parseRtpPacket(const uint8_t* rtpData, size_t rtpSize, const uint8_t*& payloadData, size_t& payloadSize) {
    if (!rtpData || rtpSize < 12) {
        return false;
    }
    
    size_t headerSize = 12;
    
    // 检查是否有扩展头
    uint8_t extensionBit = (rtpData[0] >> 4) & 0x01;
    if (extensionBit && rtpSize > 16) {
        uint16_t extensionLength = (rtpData[14] << 8) | rtpData[15];
        headerSize = 16 + extensionLength * 4;
        
        if (headerSize >= rtpSize) {
            return false;
        }
    }
    
    payloadData = rtpData + headerSize;
    payloadSize = rtpSize - headerSize;
    
    return payloadSize > 0;
}

WebRTCManage::Stats WebRTCManage::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}
