# AI智能眼镜音频问题修复记录

## 问题概述

在AI智能眼镜系统中，遇到了三个主要的音频问题：
1. **AI对话无法启动** - 唤醒后无法进行AI对话
2. **TTS音频播放异常** - 服务器下发的音频播放声音不正常
3. **连续对话失败** - TTS结束后无法继续对话

## 问题1：AI对话无法启动

### 症状
- 唤醒词检测正常
- 系统状态显示 `AI Stream: ✗ Inactive`
- 无法进行AI对话

### 根本原因分析

#### 1.1 Hello消息采样率不匹配
**问题**：设备发送的Hello消息中声明的采样率与服务器STT模型要求不匹配

**错误代码**：
```cpp
// chatbotv2.cc:1051-1055 (修复前)
std::string hello_msg = pImpl_->protocol_handler->generateHelloMessage(
    48000,  // 48kHz (错误：服务器STT要求16kHz)
    1,      // Mono
    20      // 20ms frame
);
```

**修复**：
```cpp
// chatbotv2.cc:1051-1055 (修复后)
std::string hello_msg = pImpl_->protocol_handler->generateHelloMessage(
    16000,  // 16kHz (xiaozhi服务器STT要求)
    1,      // Mono
    20      // 20ms frame
);
```

#### 1.2 音频系统死锁问题
**问题**：`AudioSystemV2::setMainState` 和 `setControlState` 中存在死锁

**错误代码**：
```cpp
// audiov2.cc (修复前)
void setMainState(AudioMainState new_state) {
    AudioMainState old_state = main_state.exchange(new_state, std::memory_order_acq_rel);
    if (old_state != new_state) {
        std::lock_guard<std::mutex> lock(callback_mutex);  // 死锁风险
        if (main_state_callback) {
            main_state_callback(old_state, new_state);
        }
    }
}
```

**修复**：
```cpp
// audiov2.cc (修复后)
void setMainState(AudioMainState new_state) {
    AudioMainState old_state = main_state.exchange(new_state, std::memory_order_acq_rel);
    if (old_state != new_state) {
        std::unique_lock<std::mutex> lock(callback_mutex, std::try_to_lock);
        if (lock.owns_lock() && main_state_callback) {
            try {
                main_state_callback(old_state, new_state);
            } catch (const std::exception& e) {
                LOG_ERROR("AudioSystemV2", "Main state callback exception: %s", e.what());
            }
        } else if (!lock.owns_lock()) {
            LOG_DEBUG("AudioSystemV2", "Callback mutex busy, skipping main state callback");
        }
    }
}
```

## 问题2：TTS音频播放异常

### 症状
- AI对话可以正常进行
- 服务器下发的TTS音频播放声音不正常
- 日志显示采样率不匹配警告

### 根本原因分析

#### 2.1 音频数据格式问题
**问题**：服务器发送的TTS音频数据包含16字节自定义头部，但代码直接将整个数据传给Opus解码器

**错误代码**：
```cpp
// chatbotv2.cc (修复前)
auto pcm_frame = audio_system->decodeOpus(data, size);  // 包含头部
```

**修复**：
```cpp
// chatbotv2.cc (修复后)
// 跳过16字节头部，只解码Opus数据
if (size <= 16) {
    LOG_WARN("ChatbotV2", "  ✗ TTS packet too small: %zu bytes", size);
    return;
}

const uint8_t* opus_data = data + 16;  // 跳过16字节头部
size_t opus_size = size - 16;

LOG_DEBUG("ChatbotV2", "  Opus data: %zu bytes (after skipping 16-byte header)", opus_size);

auto pcm_frame = audio_system->decodeOpus(opus_data, opus_size);
```

#### 2.2 编译错误修复
**问题**：`AudioFrame` 没有 `getFrameCount()` 方法

**错误代码**：
```cpp
// chatbotv2.cc (修复前)
size_t pcm_samples = pcm_frame->getFrameCount();  // 方法不存在
```

**修复**：
```cpp
// chatbotv2.cc (修复后)
size_t pcm_samples = pcm_frame->size / sizeof(int16_t);  // 计算PCM样本数
```

## 问题3：连续对话失败

### 症状
- AI对话可以正常进行
- TTS音频播放正常
- **TTS结束后无法继续对话** - 用户说话无响应

### 根本原因分析

#### 3.1 状态机音频上传控制缺失
**问题**：状态机切换到LISTENING状态，但未启用音频上传

**错误代码**：
```cpp
// machinev2.cc (修复前)
// onTTS_stop_delayed 事件只切换状态，没有启用音频上传
pImpl_->scheduleDelayedStateChange(AIState::LISTENING, delay_ms, "onTTS_stop_delayed");
```

**修复**：
```cpp
// machinev2.cc (修复后)
// 在 changeState 函数中添加音频上传控制
switch (new_state) {
    case AIState::LISTENING:
        setAudioUpload(true);   // 启用音频上传
        break;
    case AIState::THINKING:
    case AIState::SPEAKING:
    case AIState::IDLE:
    case AIState::ERROR:
        setAudioUpload(false);  // 禁用音频上传
        break;
}
```

#### 3.2 Listen消息处理缺失
**问题**：协议处理器没有处理LISTEN类型的消息

**错误代码**：
```cpp
// handlev2.cc (修复前)
bool dispatchMessage(MessageType type, const json& j) {
    switch (type) {
        case MessageType::HELLO:
            return handleHelloMessage(j);
        case MessageType::STT:
            return handleSTTMessage(j);
        // ... 其他类型
        // 缺少 LISTEN 类型处理
    }
}
```

**修复**：
```cpp
// handlev2.cc (修复后)
bool dispatchMessage(MessageType type, const json& j) {
    switch (type) {
        case MessageType::HELLO:
            return handleHelloMessage(j);
        case MessageType::LISTEN:        // 添加LISTEN类型处理
            return handleListenMessage(j);
        case MessageType::STT:
            return handleSTTMessage(j);
        // ... 其他类型
    }
}
```

#### 3.3 Listen消息处理函数实现
**问题**：缺少 `handleListenMessage` 函数实现

**修复**：
```cpp
// handlev2.cc (新增)
bool handleListenMessage(const json& j) {
    try {
        ListenMessage msg;
        msg.session_id = j.value("session_id", "");
        
        // 解析监听状态
        if (j.contains("state") && j["state"].is_string()) {
            std::string state_str = j["state"];
            if (state_str == "start") {
                msg.state = ListenState::START;
            } else if (state_str == "stop") {
                msg.state = ListenState::STOP;
            }
        }
        
        // 解析监听模式
        if (j.contains("mode") && j["mode"].is_string()) {
            std::string mode_str = j["mode"];
            msg.mode = stringToListenMode(mode_str);
        }
        
        // 调用回调
        invokeListenCallback(msg);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ProtocolV2", "Failed to handle Listen message: %s", e.what());
        return false;
    }
}
```

## 技术细节

### 音频数据格式分析
服务器发送的TTS音频数据格式：
```
[16字节自定义头部] + [Opus编码的音频数据]
```

头部示例：
```
00 00 00 00 00 00 00 00 00 00 19 A0 00 00 00 43
```

### 采样率配置
- **设备录音**：48kHz → 16kHz (AI编码)
- **TTS播放**：48kHz (直接播放，无需重采样)
- **Hello消息**：声明16kHz (匹配服务器STT要求)

### 死锁预防机制
使用 `std::try_to_lock` 避免在音频回调线程中发生死锁：
- 如果锁可用，执行回调
- 如果锁被占用，跳过回调，避免阻塞

## 修复效果

### 问题1修复后
- ✅ AI对话正常启动
- ✅ 系统状态显示 `AI Stream: ✓ Active`
- ✅ 可以进行完整的AI对话流程

### 问题2修复后
- ✅ TTS音频播放声音正常
- ✅ 无采样率不匹配警告
- ✅ 音频质量清晰

### 问题3修复后
- ✅ 连续对话正常工作
- ✅ TTS结束后自动进入LISTENING状态
- ✅ 音频上传正确启用
- ✅ 用户可以继续对话

## 经验总结

1. **协议匹配**：设备与服务器之间的音频格式声明必须一致
2. **死锁预防**：在音频回调中使用非阻塞锁机制
3. **数据格式解析**：正确处理自定义音频头部
4. **状态机设计**：状态切换必须包含相应的副作用（如音频控制）
5. **连续对话机制**：状态机驱动，不依赖服务器响应
6. **调试方法**：通过对比v1/v2代码发现差异
7. **系统思维**：连续对话涉及状态机、音频系统、网络通信多个模块
8. **调试日志**：详细的日志有助于快速定位问题
9. **编译错误处理**：使用正确的方法和数据结构

## 相关文件

- `app/chatbot/chatbotv2.cc` - AI对话主控制器
- `app/media/audio/audiov2.cc` - 音频系统实现
- `app/chatbot/statemachine/machinev2.cc` - 状态机实现
- `app/chatbot/protocol_handle/handlev2.cc` - 协议处理器
- `main.cpp` - 主程序入口

## 测试验证

修复后系统功能验证：
1. ✅ 唤醒词检测正常
2. ✅ AI对话启动正常
3. ✅ 音频录制正常
4. ✅ TTS音频播放正常
5. ✅ 连续对话正常工作
6. ✅ 系统状态显示正确

---
*修复完成时间：2024年*
*修复人员：AI Assistant*
