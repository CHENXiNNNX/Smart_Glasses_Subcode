# xiaozhi AI集成问题记录与解决方案

## 文档信息
- **项目**: Smart_Glasses
- **集成目标**: xiaozhi云端AI服务
- **日期**: 2025-10-10
- **状态**: ✅ 已完成

---

## 一、项目概述

### 1.1 集成目标

将xiaozhi云端AI服务集成到Smart_Glasses智能眼镜项目中，实现语音对话功能。

### 1.2 技术架构对比

| 特性 | xiaozhi原版 | Smart_Glasses | 说明 |
|------|------------|---------------|------|
| **架构** | 多进程(sound_app + control_center) | 单进程 | 简化架构 |
| **IPC** | UDP Socket | 直接函数回调 | 更高效 |
| **音频采集率** | 48kHz → 重采样 → 16kHz | 48kHz → 重采样 → 16kHz | 相同 |
| **帧时长** | 60ms | 20ms | 更低延迟 |
| **音频库** | ALSA | PortAudio | 跨平台 |
| **WebSocket库** | libdatachannel | websocketpp | 更灵活 |
| **语言** | C | C++11/14 | 现代化 |

---

## 二、关键问题与解决方案

### 问题1: 🔴 音频识别结果错误

#### **现象**
```
用户说: "你好，今天天气怎么样"
服务器识别: "A." "2是。" ""  ← 完全错误
```

#### **问题分析**

**根本原因**: **采样率不匹配导致Opus解码错误**

```
发送给服务器: 48kHz Opus数据
服务器期望:   16kHz Opus数据
结果:         解码失败 → 识别出乱码
```

**详细分析流程**:

```
Smart_Glasses发送:
  麦克风采集 (48kHz, 1ch, 20ms)
       ↓
  3A算法处理
       ↓
  Opus编码器 (48kHz配置) ← ❌ 错误！
       ↓
  发送到服务器

xiaozhi服务器接收:
  收到Opus数据
       ↓
  Opus解码器 (16kHz配置) ← ❌ 采样率不匹配！
       ↓
  解码失败/数据错误
       ↓
  STT识别出乱码
```

**为什么Hello消息中声明48kHz还是不行？**

虽然Hello消息中可以声明`"sample_rate": 48000`，但xiaozhi服务器的STT模型是基于**16kHz音频**训练的，内部解码器固定使用16kHz配置。服务器回复的`"sample_rate": 24000`只是建议值，实际并不使用。

#### **解决方案**

**方案**: **48kHz → 重采样 → 16kHz → Opus编码**

```cpp
// 1. 在audio.h中添加AI专用编码器
typedef struct {
    OpusEncoder* encoder;      // 主编码器（48kHz，WebRTC用）
    OpusEncoder* ai_encoder;   // AI专用编码器（16kHz）← 新增
    // ...
} audio_system_t;

// 2. 在recordCallback中重采样
if (audio_system->is_ai_streaming) {
    // 初始化AI编码器（16kHz）
    if (!audio_system->ai_encoder) {
        audio_system->ai_encoder = opus_encoder_create(
            16000,  // ← 16kHz
            1, 
            OPUS_APPLICATION_VOIP, 
            &error
        );
        
        // 初始化重采样器 48kHz → 16kHz
        init_audio_resample(audio_system, 48000, 16000, 1, SRC_SINC_BEST_QUALITY);
    }
    
    // 重采样 960 samples (48kHz) → 320 samples (16kHz)
    std::vector<int16_t> resampled_frame;
    process_audio_resample(audio_system, frame, resampled_frame);
    
    // 使用AI专用编码器编码
    int encoded_bytes = opus_encode(
        audio_system->ai_encoder,  // ← 16kHz编码器
        resampled_frame.data(),
        320,  // 16kHz 20ms = 320 samples
        opus_buffer,
        2048
    );
    
    // 发送
    audio_system->ai_audio_callback(opus_buffer, encoded_bytes, 0);
}

// 3. 更新Hello消息
config.hello_message = R"({
    "audio_params": {
        "sample_rate": 16000,  // ← 改为16000
        "channels": 1,
        "frame_duration": 20
    }
})";
```

**关键代码位置**:
- `app/media/audio/audio.h` 第73行: 添加`ai_encoder`字段
- `app/media/audio/audio.cc` 第61-131行: AI音频处理逻辑
- `app/protocol/websocket/websocket.cc` 第479行: Hello消息参数

---

### 问题2: 🔴 Opus编码器状态污染

#### **现象**
```
第一次对话: 识别正确 ✅
第二次对话: 识别失败 ❌
```

#### **问题分析**

**根本原因**: **Opus编码器有内部状态，不释放会导致残留**

```
第一次对话:
  初始化AI编码器和重采样器 ✅
  编码音频数据 ✅
  识别成功 ✅

TTS播放结束 → stop_ai_audio_stream()
  停止录音 ✅
  但AI编码器和重采样器未释放 ← ❌ 问题！

第二次对话:
  重新开始录音 ✅
  重采样器有残留状态 ← ❌ 导致数据错误
  Opus编码器也有残留 ← ❌
  发送损坏的数据 ❌
  识别失败 ❌
```

#### **解决方案**

**在`stop_ai_audio_stream()`中释放编码器和重采样器**:

```cpp
audio_error_t stop_ai_audio_stream(audio_system_t *audio_system) {
    // 停止录音
    stop_recording(audio_system);
    
    // ✅ 释放AI编码器（避免状态残留）
    if (audio_system->ai_encoder) {
        opus_encoder_destroy(audio_system->ai_encoder);
        audio_system->ai_encoder = nullptr;
    }
    
    // ✅ 释放重采样器（避免状态残留）
    if (audio_system->resample_config.is_initialized) {
        release_audio_resample(audio_system);
    }
    
    audio_system->is_ai_streaming = false;
    return AUDIO_ERROR_NONE;
}
```

**关键代码位置**:
- `app/media/audio/audio.cc` 第1035-1067行: `stop_ai_audio_stream()`

**教训**: 
- Opus编码器和重采样器都有**内部状态**
- 每次重新开始时必须**重新初始化**
- 不能复用旧的编码器实例

---

### 问题3: 🔴 连续对话失败

#### **现象**
```
第一次对话: 正常 ✅
AI播放TTS完毕 → 用户继续说话 → 无响应 ❌
```

#### **问题分析**

**根本原因**: **TTS结束后没有重新发送listen消息**

xiaozhi的对话流程：
```
用户按's' → 发送listen start → AI监听
用户说话 → STT识别 → LLM思考 → TTS播放
TTS结束 → ❌ 需要重新发送listen start！← 这步缺失了
```

**状态机流转**:
```
LISTENING → (STT) → THINKING → (TTS start) → SPEAKING
    ↓                                              ↓
   (TTS stop + 延迟2秒)  ←←←←←←←←←←←←←←←←←←←←

状态机恢复LISTENING ✅
但服务器不知道要继续监听 ❌ ← 需要重新发送listen消息
```

#### **解决方案**

**在TTS stop回调中，延迟后重新发送listen消息**:

```cpp
protocol_handler->setTTSCallback([this](const TTSMessage& msg) {
    switch (msg.state) {
        case TTSState::STOP:
            state_machine->onTTS_stop(2000);  // 延迟2秒
            
            // ✅ 延迟后重新监听
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(2500));
                
                if (state_machine->getState() == AIState::LISTENING) {
                    // 检查音频流状态
                    if (!audio_system->is_ai_streaming) {
                        start_ai_audio_stream(audio_system);
                    }
                    
                    // 重新发送listen消息
                    std::string listen_msg = protocol_handler->generateListenMessage(
                        ListenState::START, ListenMode::AUTO);
                    ws_client->sendText(listen_msg.c_str(), listen_msg.length());
                    
                    std::cout << "[AIManager] → Continue listening" << std::endl;
                }
            }).detach();
            break;
    }
});
```

**关键代码位置**:
- `app/chatbot/chatbot.cc` 第508-539行: TTS stop处理

**延迟时间选择**:
- 状态机延迟: 2000ms (避免听到TTS尾音)
- listen重发延迟: 2500ms (比状态机稍晚，确保状态已恢复)

---

### 问题4: 🔴 WebRTC和AI模式冲突

#### **现象**
```
同时启用WebRTC和AI时:
  - Opus编码器被调用两次
  - 编码器状态混乱
  - 数据损坏
```

#### **问题分析**

**根本原因**: **单个Opus编码器不能同时用于两个目的**

```cpp
#if USE_WEBRTC
if (is_webrtc_streaming) {
    encode_opus(audio_system, frame, opus_buffer, &opus_size);  // 第1次
    webrtc_audio_callback(opus_buffer, opus_size, timestamp);
}
#endif

if (is_ai_streaming) {
    encode_opus(audio_system, frame, opus_buffer, &opus_size);  // 第2次
    ai_audio_callback(opus_buffer, opus_size, 0);
}
```

问题:
- 同一个`audio_system->encoder`被连续调用2次
- Opus编码器有内部状态，第2次编码会被第1次影响
- 导致第2次编码的数据错误

#### **解决方案**

**方案1: 模式互斥（推荐）**

```cpp
// 通过audio_mode确保互斥
if (audio_system->current_mode == AUDIO_MODE_WEBRTC && is_webrtc_streaming) {
    encode_opus(...);
    webrtc_audio_callback(...);
}

if (audio_system->current_mode == AUDIO_MODE_AI && is_ai_streaming) {
    encode_opus(...);
    ai_audio_callback(...);
}
```

**方案2: 独立编码器**

```cpp
// AI使用专用编码器
OpusEncoder* ai_encoder;  // 16kHz专用

if (is_ai_streaming) {
    opus_encode(ai_encoder, ...);  // 使用独立编码器
}
```

**最终采用**: 方案1 + 方案2组合
- AI使用独立的16kHz编码器
- 通过`audio_mode`确保不同时运行

**关键代码位置**:
- `app/media/audio/audio.cc` 第45-48行: WebRTC模式检查
- `app/media/audio/audio.cc` 第67-69行: AI模式检查

---

## 三、完整的音频处理流程

### 3.1 xiaozhi AI音频流程图

```
┌──────────────────────────────────────────────────────────────────┐
│                    xiaozhi AI音频处理完整流程                     │
└──────────────────────────────────────────────────────────────────┘

1️⃣ 音频采集 (PortAudio)
   ├─ 采样率: 48000 Hz
   ├─ 声道数: 1 (单声道)
   ├─ 帧时长: 20 ms
   └─ 帧大小: 960 samples

2️⃣ 3A算法处理 (SpeexDSP)
   ├─ 降噪 (Denoise)
   ├─ 自动增益控制 (AGC)
   ├─ 语音活动检测 (VAD)
   └─ 去混响 (Dereverb)

3️⃣ 重采样 (libsamplerate)
   ├─ 输入: 48000 Hz, 960 samples
   ├─ 输出: 16000 Hz, 320 samples
   └─ 算法: SRC_SINC_BEST_QUALITY

4️⃣ Opus编码 (AI专用编码器)
   ├─ 采样率: 16000 Hz
   ├─ 声道数: 1
   ├─ 帧大小: 320 samples (16kHz × 20ms)
   ├─ 应用类型: OPUS_APPLICATION_VOIP
   ├─ 比特率: 32000 bps
   └─ 输出大小: 约80-150字节/帧

5️⃣ 状态机控制
   ├─ LISTENING状态: 音频上传 ENABLED ✅
   └─ SPEAKING状态: 音频上传 DISABLED ❌ (避免回声)

6️⃣ WebSocket发送
   ├─ 协议: WSS (TLS加密)
   ├─ 服务器: wss://api.tenclass.net/xiaozhi/v1/
   └─ 消息类型: Binary (Opus数据)

7️⃣ 服务器处理
   ├─ Opus解码 (16kHz)
   ├─ STT语音识别
   ├─ LLM大模型回复
   └─ TTS语音合成

8️⃣ TTS音频接收
   ├─ 接收Opus数据 (Binary消息)
   ├─ Opus解码 (48kHz解码器)
   ├─ 应用音量控制 (AUDIO_MASTER_VOLUME)
   └─ 播放队列 → 扬声器输出
```

### 3.2 关键参数配置

#### **Hello消息参数**
```json
{
    "type": "hello",
    "version": 1,
    "transport": "websocket",
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,    // ← 必须是16000！
        "channels": 1,
        "frame_duration": 20
    }
}
```

#### **音频配置 (media_config.h)**
```cpp
#define AUDIO_SAMPLE_RATE 48000      // 采集率48kHz
#define AUDIO_CHANNELS 1              // 单声道
#define AUDIO_FRAME_DURATION_MS 20    // 20ms帧
#define AUDIO_BIT_RATE 32000          // Opus比特率
#define AUDIO_MASTER_VOLUME 0.5f      // 播放音量
```

#### **Opus编码器配置**
```cpp
// WebRTC编码器 (48kHz)
encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);

// AI专用编码器 (16kHz) ← 关键！
ai_encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
opus_encoder_ctl(ai_encoder, OPUS_SET_BITRATE(32000));
opus_encoder_ctl(ai_encoder, OPUS_SET_VBR(1));
opus_encoder_ctl(ai_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
```

---

## 四、AI状态机与音频上传控制

### 4.1 状态流转

```
IDLE → LISTENING → THINKING → SPEAKING → LISTENING (循环)
        ↑           ↑          ↑           ↑
        │           │          │           │
    listen_start  STT_final  TTS_start  TTS_stop
```

### 4.2 音频上传控制规则

| 状态 | 音频上传 | 原因 |
|------|---------|------|
| IDLE | ❌ DISABLED | 未激活 |
| LISTENING | ✅ ENABLED | 监听用户 |
| THINKING | ✅ ENABLED | 继续监听 |
| SPEAKING | ❌ DISABLED | **避免回声** |
| ERROR | ❌ DISABLED | 错误状态 |

**关键机制**: **TTS期间禁用音频上传**

```cpp
// 为什么TTS时要禁用？
AI说话 (TTS播放) 
    → 扬声器输出 
    → 麦克风拾取 
    → 上传到服务器 
    → AI误以为用户在说话 
    → 形成回声循环 ❌

// 解决方案：
onTTS_start() → 立即禁用音频上传 ✅
onTTS_stop()  → 延迟2秒后恢复 ✅ (等待扬声器残留消失)
```

**代码实现**:
```cpp
// chatbot/statemachine/machine.cc
void AIStateMachine::onTTS_start() {
    setAudioUpload(false);  // ← 立即禁用
}

void AIStateMachine::onTTS_stop(int delay_ms) {
    std::thread([this, delay_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        changeState(AIState::LISTENING);
        setAudioUpload(true);  // ← 延迟启用
    }).detach();
}

// audio.cc
void AIManager::Impl::handleAudioData(const uint8_t* data, size_t size) {
    if (!state_machine->isAudioUploadEnabled()) {
        return;  // ← 状态机控制，TTS期间丢弃音频
    }
    ws_client->sendBinary(data, size);
}
```

---

## 五、连续对话实现

### 5.1 问题

xiaozhi不会自动恢复监听，需要客户端主动重新发送`listen start`消息。

### 5.2 解决方案

**在TTS stop后自动重发listen消息**:

```cpp
case TTSState::STOP:
    state_machine->onTTS_stop(2000);
    
    // 延迟后重新监听
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        
        if (state_machine->getState() == AIState::LISTENING) {
            // 检查并重启音频流
            if (!audio_system->is_ai_streaming) {
                start_ai_audio_stream(audio_system);
            }
            
            // 重新发送listen消息
            std::string listen_msg = protocol_handler->generateListenMessage(
                ListenState::START, ListenMode::AUTO);
            ws_client->sendText(listen_msg.c_str(), listen_msg.length());
        }
    }).detach();
    break;
```

**时间线**:
```
T0: TTS stop收到
T1: 状态机启动延迟线程 (2000ms)
T2: 主线程启动listen重发线程 (2500ms)
T+2000ms: 状态机恢复LISTENING + 音频上传ENABLED
T+2500ms: 重新发送listen start消息
```

**关键代码位置**:
- `app/chatbot/chatbot.cc` 第512-538行: TTS stop处理

---

## 六、C/C++混合架构设计

### 6.1 音频回调架构

**挑战**: C函数指针回调 vs C++类成员函数

```cpp
// C函数指针签名
typedef void (*AudioCallback)(void *data, int len, uint64_t timestamp);

// 但我们需要访问C++类成员
class AIManager {
    void handleAudioData(...);  // ← 需要调用这个
};
```

**解决方案**: **全局指针 + 命名空间函数**

```cpp
// 1. 全局实例指针
namespace glasses::chatbot {
    static AIManager* g_ai_manager_instance = nullptr;
    static std::mutex g_callback_mutex;
}

// 2. 命名空间函数（C兼容）
void audioDataCallback(void* data, int len, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    if (g_ai_manager_instance) {
        g_ai_manager_instance->pImpl_->handleAudioData(
            reinterpret_cast<const uint8_t*>(data), 
            static_cast<size_t>(len)
        );
    }
}

// 3. 构造函数中注册
AIManager::AIManager() {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    g_ai_manager_instance = this;
}

// 4. 析构函数中注销
AIManager::~AIManager() {
    std::lock_guard<std::mutex> lock(g_callback_mutex);
    if (g_ai_manager_instance == this) {
        g_ai_manager_instance = nullptr;
    }
}

// 5. 设置回调时使用命名空间函数
set_ai_audio_callback(audio_system, this, glasses::chatbot::audioDataCallback);
```

**关键代码位置**:
- `app/chatbot/chatbot.cc` 第36-37行: 全局指针定义
- `app/chatbot/chatbot.cc` 第117-129行: 构造/析构注册
- `app/chatbot/chatbot.cc` 第653-681行: audioDataCallback实现
- `app/chatbot/chatbot.h` 第275行: friend声明

**教训**:
- C函数指针无法直接调用C++成员函数
- 必须通过全局指针或静态函数桥接
- 注意线程安全（使用mutex保护）

---

## 七、调试技巧总结

### 7.1 音频能量监控

**用于检测麦克风是否正常工作**:

```cpp
// 计算帧能量
int64_t energy = 0;
int16_t max_sample = 0;
for (size_t i = 0; i < frame.size(); i++) {
    energy += abs(frame[i]);
    max_sample = std::max(max_sample, (int16_t)abs(frame[i]));
}

// 能量参考值:
// 静音:    energy < 50,000,    max < 100
// 轻声:    50,000 - 500,000,   max: 100-1000
// 正常:    500,000 - 5,000,000, max: 1000-10000
// 大声:    > 5,000,000,         max > 10000
```

### 7.2 Opus数据大小监控

**用于检测编码是否正常**:

```cpp
// 正常Opus大小范围:
// 静音帧: 60-80 字节
// 语音帧: 80-150 字节
// 异常:   < 50 或 > 200 字节

if (opus_size < 50 || opus_size > 200) {
    std::cerr << "异常Opus大小: " << opus_size << std::endl;
}
```

### 7.3 状态机日志

**用于追踪对话流程**:

```cpp
// 每个状态转换都打印
[StateMachine] State changed: IDLE → LISTENING
[StateMachine] Audio upload: ENABLED

// 每个协议消息都打印
[AIManager] ← Hello received
[AIManager] ← STT: "你好" (final: ✓)
[AIManager] ← LLM: "你好！"
[AIManager] ← TTS: START
[AIManager] ← TTS: STOP
```

---

## 八、最佳实践建议

### 8.1 音频参数配置

```cpp
// ✅ 推荐配置
采样率: 48kHz (采集) → 16kHz (发送)
声道数: 1 (单声道)
帧时长: 20ms (低延迟)
编码器: Opus VOIP模式
比特率: 32kbps (语音足够)

// ❌ 不推荐
采样率: 直接48kHz发送 (不兼容xiaozhi)
帧时长: 60ms (延迟高)
编码器: OPUS_APPLICATION_AUDIO (体积大)
```

### 8.2 资源管理

```cpp
// ✅ 正确的资源管理
启动时:
  - 创建编码器
  - 初始化重采样器

停止时:
  - 销毁编码器      ← 必须！避免状态残留
  - 释放重采样器    ← 必须！
  - 停止录音流

重新启动时:
  - 重新创建所有资源 ← 保证干净状态
```

### 8.3 错误处理

```cpp
// ✅ 完善的错误处理
if (encode_opus(...) != AUDIO_ERROR_NONE) {
    std::cerr << "Opus encode failed: frame_size=" << frame.size() << std::endl;
    return;  // 跳过此帧，不中断整个流程
}

if (!ws_client->isConnected()) {
    std::cerr << "WebSocket not connected, audio dropped" << std::endl;
    return;  // 优雅降级
}
```

---

## 九、代码统计

### 9.1 总代码量

```
xiaozhi AI集成总代码: 12,090行

核心模块:
├─ chatbot/chatbot.cc/.h         704行  (主控制器)
├─ chatbot/protocol_handle/      973行  (协议处理)
├─ chatbot/statemachine/         475行  (AI状态机)
├─ chatbot/mcp/                  657行  (MCP管理器)
├─ chatbot/uuid/                 361行  (UUID工具)
├─ protocol/websocket/           708行  (WebSocket客户端)
├─ protocol/udp/                 432行  (UDP IPC)
├─ media/audio/                2,405行  (音频系统+AI集成)
├─ tool/mac/                     281行  (MAC地址工具)
└─ test/test_ai_chatbot.cpp      263行  (测试程序)
```

### 9.2 关键修改文件

| 文件 | 修改内容 | 行数 |
|------|---------|------|
| `media/audio/audio.h` | 添加ai_encoder字段 | +3 |
| `media/audio/audio.cc` | AI音频处理逻辑 | +70 |
| `chatbot/chatbot.cc` | 主控制器实现 | +704 |
| `chatbot/chatbot.h` | 主控制器接口 | +286 |
| `protocol/websocket/websocket.cc` | 修改Hello消息 | ~10 |

---

## 十、常见问题FAQ

### Q1: 为什么不能直接发送48kHz音频？

**A**: xiaozhi服务器的STT模型是基于16kHz音频训练的。发送48kHz音频虽然不会报错，但会导致：
- 解码器采样率不匹配
- 音频数据损坏
- 识别结果错误

### Q2: 为什么需要两个Opus编码器？

**A**: 
- WebRTC需要48kHz Opus（对端是移动APP，支持高采样率）
- xiaozhi AI需要16kHz Opus（服务器STT要求）
- 单个编码器有内部状态，不能同时用于两个目的

### Q3: 为什么TTS后不能自动继续监听？

**A**: xiaozhi协议设计中，TTS结束后需要客户端**重新发送listen start消息**。状态机只负责本地状态管理，不会自动发送协议消息。

### Q4: 如何调整AI播放音量？

**A**: 
```cpp
// 方法1: 修改配置文件
// media_config.h
#define AUDIO_MASTER_VOLUME 0.3f  // 0.0-1.0

// 方法2: 动态调整
audio_system->playback_volume = 0.3f;
```

### Q5: 如何添加更多MCP设备？

**A**:
```cpp
// 1. 创建描述符
IoTDescriptor device = createSimpleDescriptor("device_name", "描述");
addProperty(device, "prop", "属性描述", "number");
addMethod(device, "method", "方法描述");

// 2. 实现处理函数
bool handler(const std::string& device, const std::string& method, 
             const std::map<std::string, std::string>& params) {
    // 处理方法调用
    return true;
}

// 3. 实现状态获取
std::map<std::string, std::string> getter(const std::string& device) {
    return {{"state", "value"}};
}

// 4. 注册
ai_manager.registerDevice(device, handler, getter);
```

---

## 十一、关键经验总结

### 11.1 必须遵守的规则

1. ✅ **xiaozhi必须发送16kHz Opus音频**（不是48kHz）
2. ✅ **每次stop后必须释放编码器和重采样器**
3. ✅ **TTS结束后必须重新发送listen消息**
4. ✅ **TTS期间必须禁用音频上传**（避免回声）
5. ✅ **WebRTC和AI模式必须互斥**（或使用独立编码器）

### 11.2 调试流程

遇到音频问题时的检查顺序：

```
1. 检查音频设备是否正确
   → 打印设备列表
   → 检查默认输入设备

2. 检查音频能量是否正常
   → 计算帧能量
   → 正常说话应该 > 500,000

3. 检查Opus编码是否成功
   → 检查返回值
   → 检查编码大小 (80-150B)

4. 检查状态机是否允许上传
   → isAudioUploadEnabled()
   → 打印状态

5. 检查WebSocket连接是否正常
   → isConnected()
   → 查看发送日志

6. 检查服务器响应
   → STT识别结果是否正确
   → 是否收到TTS stop
```

### 11.3 性能优化建议

```cpp
// 1. 重采样优化
// 使用最快的重采样算法（牺牲一点质量换速度）
SRC_LINEAR  // 最快，质量最低
SRC_SINC_FASTEST  // 快速，质量中等 ← 推荐
SRC_SINC_BEST_QUALITY  // 最慢，质量最高（当前使用）

// 2. 减少日志输出
// 每50帧打印一次，而不是每帧都打印

// 3. 音频缓冲区优化
// 使用内存池避免频繁分配

// 4. 线程优化
// TTS stop的延迟线程使用detach()，避免阻塞
```

---

## 十二、文件清单

### 12.1 核心文件

```
app/chatbot/
├── chatbot.h              (主控制器头文件)
├── chatbot.cc             (主控制器实现)
├── protocol_handle/       (协议处理器)
├── statemachine/          (AI状态机)
├── mcp/                   (MCP工具管理器)
└── uuid/                  (UUID配置管理)

app/protocol/
├── websocket/             (WebSocket客户端)
└── udp/                   (UDP IPC)

app/media/
└── audio/                 (音频系统+AI集成)

app/tool/
├── mac/                   (MAC地址工具)
└── memory/                (内存池)

test/
└── test_ai_chatbot.cpp    (完整测试程序)
```

### 12.2 配置文件

```
app/media/media_config.h   (音频参数配置)
system_para.conf           (UUID持久化存储)
```

---

## 十三、测试验证

### 13.1 功能测试清单

- [x] WebSocket连接和握手
- [x] Hello消息交换
- [x] Session ID获取
- [x] 音频采集和编码
- [x] 音频重采样 (48kHz→16kHz)
- [x] STT语音识别
- [x] LLM对话回复
- [x] TTS语音播放
- [x] 音频上传控制（TTS时禁用）
- [x] 连续对话
- [x] MCP设备注册
- [x] 状态机流转
- [x] 错误处理和恢复

### 13.2 测试结果

```
✅ 音频识别准确
✅ 连续对话流畅
✅ TTS播放正常
✅ 无回声问题
✅ 状态流转正确
✅ MCP功能正常
```

---

## 十四、后续优化方向

### 14.1 功能增强

1. **唤醒词检测** (`chatbot/wakeword/`)
   - 本地唤醒词识别
   - 替代手动按's'开始监听

2. **HTTP设备激活** (`protocol/http/`)
   - 设备注册和绑定
   - 账户关联

3. **电池状态MCP** (`tool/battery/`)
   - 电池电量上报
   - AI可以查询电池状态

### 14.2 性能优化

1. **重采样算法选择**
   - 当前: `SRC_SINC_BEST_QUALITY` (最高质量，最慢)
   - 建议: `SRC_SINC_FASTEST` (平衡质量和速度)

2. **内存池优化**
   - 减少Opus缓冲区的分配次数
   - 使用对象池

3. **线程优化**
   - 考虑使用线程池管理延迟任务
   - 避免频繁创建/销毁线程

---

## 十五、xiaozhi协议要点

### 15.1 必须遵守的协议

```json
// 1. Hello消息 (客户端→服务器)
{
    "type": "hello",
    "version": 1,
    "transport": "websocket",
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,  // ← 必须16000
        "channels": 1,
        "frame_duration": 20
    }
}

// 2. Listen消息 (客户端→服务器)
{
    "type": "listen",
    "state": "start",  // or "stop"
    "mode": "auto",    // or "manual", "realtime"
    "session_id": "xxx"
}

// 3. 音频数据 (客户端→服务器)
Binary message: Opus encoded data (16kHz)

// 4. STT消息 (服务器→客户端)
{
    "type": "stt",
    "text": "识别文本",
    "is_final": true,
    "session_id": "xxx"
}

// 5. TTS消息 (服务器→客户端)
{
    "type": "tts",
    "state": "start" | "sentence_start" | "stop",
    "text": "TTS文本",
    "session_id": "xxx"
}

// 6. TTS音频 (服务器→客户端)
Binary message: Opus encoded audio (24kHz)
```

### 15.2 HTTP Headers

```
Device-Id: MAC地址 (如: 8c:bd:37:36:60:b7)
Client-Id: UUID (如: b1c302a7-76c8-46b5-a71d-aeac4f051acb)
Protocol-Version: 1
Authorization: Bearer test-token (可选)
```

---

## 十六、完成情况

### 16.1 已完成模块

| 模块 | 完成度 | 代码行数 |
|------|--------|---------|
| UUID配置管理 | ✅ 100% | 361行 |
| MAC地址获取 | ✅ 100% | 281行 |
| UDP IPC通信 | ✅ 100% | 432行 |
| WebSocket客户端 | ✅ 100% | 708行 |
| 协议处理器 | ✅ 100% | 973行 |
| AI状态机 | ✅ 100% | 475行 |
| MCP工具管理器 | ✅ 100% | 657行 |
| 音频系统集成 | ✅ 100% | 2,405行 |
| 主控制器 | ✅ 100% | 704行 |
| 测试程序 | ✅ 100% | 263行 |
| **总计** | **✅ 100%** | **12,090行** |

### 16.2 功能验证

- ✅ WebSocket连接成功
- ✅ 音频识别准确
- ✅ 连续对话流畅
- ✅ TTS播放正常
- ✅ 回声控制有效
- ✅ MCP设备注册
- ✅ 状态机正常

---

## 十七、参考资料

1. **xiaozhi-linux源码**: https://github.com/100askTeam/xiaozhi-linux
2. **Opus编解码器**: https://opus-codec.org/
3. **libsamplerate**: http://www.mega-nerd.com/SRC/
4. **websocketpp**: https://github.com/zaphoyd/websocketpp
5. **PortAudio**: http://www.portaudio.com/

---

## 附录A: 问题排查检查表

遇到音频识别问题时，按此顺序检查：

```
□ 1. 音频设备是否正确？
    → Pa_GetDefaultInputDevice()
    → 打印设备名称

□ 2. 音频能量是否正常？
    → 计算帧能量
    → 正常说话 > 500,000

□ 3. 采样率是否正确？
    → Hello消息: 16000 ✅
    → AI编码器: 16000 ✅
    → 重采样: 48000→16000 ✅

□ 4. Opus编码是否成功？
    → 检查encode返回值
    → 检查编码大小 (80-150B)

□ 5. 状态机是否允许上传？
    → isAudioUploadEnabled()
    → LISTENING/THINKING允许 ✅
    → SPEAKING禁止 ✅

□ 6. WebSocket是否连接？
    → isConnected()
    → isHandshaked()

□ 7. 是否收到服务器响应？
    → STT消息
    → TTS stop消息

□ 8. 连续对话是否重发listen？
    → TTS stop后2.5秒
    → 重新发送listen start
```

---

## 附录B: 快速故障恢复

### B.1 识别结果错误

```bash
# 检查Hello消息中的sample_rate
grep "sample_rate" app/protocol/websocket/websocket.cc
# 应该是: "sample_rate": 16000

# 检查AI编码器配置
grep "ai_encoder.*16000" app/media/audio/audio.cc
# 应该看到: opus_encoder_create(16000, ...)
```

### B.2 连续对话失败

```bash
# 检查TTS stop处理
grep -A 20 "TTSState::STOP" app/chatbot/chatbot.cc
# 应该看到: generateListenMessage(ListenState::START, ...)
```

### B.3 音频流中断

```bash
# 检查stop_ai_audio_stream是否释放资源
grep -A 10 "stop_ai_audio_stream" app/media/audio/audio.cc
# 应该看到: 
#   opus_encoder_destroy(ai_encoder)
#   release_audio_resample()
```

---

**文档结束**

**最后更新**: 2025-10-10  
**版本**: 1.0  
**状态**: ✅ xiaozhi AI集成完成

