# AI状态机模块

## 概述

AI状态机模块负责管理xiaozhi AI对话过程中的状态转换和音频上传控制。

## 状态流转图

```
        onHello()
STARTING ────────→ IDLE
                    │
         onListenStart()
                    ↓
                LISTENING ←─────────┐
                    │               │
         onSTT()    │               │
         (final)    ↓               │
                THINKING            │
                    │               │
      onTTS_start() │               │
                    ↓               │
                SPEAKING            │
                    │               │
       onTTS_stop() │               │
       (延迟2秒)    │               │
                    └───────────────┘
```

## 核心功能

### 1. 状态管理

| 状态 | 说明 | 音频上传 |
|------|------|---------|
| `UNKNOWN` | 未知状态 | ❌ 禁用 |
| `STARTING` | 启动中 | ❌ 禁用 |
| `IDLE` | 空闲 | ❌ 禁用 |
| `LISTENING` | 监听中 | ✅ 启用 |
| `THINKING` | AI思考中 | ✅ 启用 |
| `SPEAKING` | AI说话中 | ❌ 禁用 |
| `ERROR` | 错误 | ❌ 禁用 |

### 2. 音频上传控制

**关键机制：防止回声**

```cpp
TTS开始 (onTTS_start):
  ├─ 立即禁用音频上传
  └─ 避免AI听到自己的声音

TTS结束 (onTTS_stop):
  ├─ 等待2秒（延迟参数可调）
  ├─ 恢复监听状态
  └─ 启用音频上传
```

**为什么需要延迟？**
- 扬声器播放有残留
- 避免AI听到自己说话的尾音
- xiaozhi默认延迟2秒

## 使用示例

### 基本使用

```cpp
#include "app/chatbot/statemachine/machine.h"

using namespace glasses::chatbot::statemachine;

// 1. 创建状态机
AIStateMachine state_machine;

// 2. 设置回调
state_machine.setStateChangeCallback([](AIState old_state, AIState new_state) {
    std::cout << "状态变化: " 
              << AIStateMachine::stateToString(old_state) << " → " 
              << AIStateMachine::stateToString(new_state) << std::endl;
    
    // 根据状态控制LED/屏幕等
    switch (new_state) {
        case AIState::LISTENING:
            // LED显示监听动画
            break;
        case AIState::THINKING:
            // LED显示思考动画
            break;
        case AIState::SPEAKING:
            // LED显示说话动画
            break;
    }
});

state_machine.setAudioUploadCallback([](bool enable) {
    if (enable) {
        std::cout << "✅ 开始上传音频" << std::endl;
        // 调用audio_system启用上传
    } else {
        std::cout << "❌ 停止上传音频" << std::endl;
        // 调用audio_system禁用上传
    }
});

// 3. 状态转换（由协议消息驱动）
state_machine.onHello();           // Hello消息
state_machine.onListenStart();     // Listen消息
state_machine.onSTT("你好", true);  // STT消息
state_machine.onTTS_start();       // TTS开始
state_machine.onTTS_stop(2000);    // TTS结束（延迟2秒）
```

### 与协议处理器集成

```cpp
#include "app/chatbot/protocol_handle/handle.h"
#include "app/chatbot/statemachine/machine.h"

using namespace glasses::chatbot;

// 创建模块
protocol::ProtocolHandler protocol_handler;
statemachine::AIStateMachine state_machine;

// 绑定协议回调到状态机
protocol_handler.setHelloCallback([&](const protocol::HelloMessage& msg) {
    std::cout << "收到Hello，Session: " << msg.session_id << std::endl;
    protocol_handler.setSessionId(msg.session_id);
    state_machine.onHello();
});

protocol_handler.setSTTCallback([&](const protocol::STTMessage& msg) {
    std::cout << "STT: " << msg.text << std::endl;
    state_machine.onSTT(msg.text, msg.is_final);
});

protocol_handler.setLLMCallback([&](const protocol::LLMMessage& msg) {
    std::cout << "LLM: " << msg.text 
              << " (emotion: " << protocol::ProtocolHandler::emotionTypeToString(msg.emotion) 
              << ")" << std::endl;
    state_machine.onLLM(msg.text, msg.is_final);
});

protocol_handler.setTTSCallback([&](const protocol::TTSMessage& msg) {
    switch (msg.state) {
        case protocol::TTSState::START:
            state_machine.onTTS_start();
            break;
        case protocol::TTSState::SENTENCE_START:
            state_machine.onTTS_sentenceStart(msg.text);
            break;
        case protocol::TTSState::STOP:
            state_machine.onTTS_stop(2000);  // 延迟2秒
            break;
    }
});

// 设置音频上传控制
state_machine.setAudioUploadCallback([](bool enable) {
    // 实际控制audio_system的上传开关
    audio_system.is_ai_streaming = enable;
});
```

### 完整对话流程示例

```
时间轴：对话流程
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

T0: 连接服务器
    state_machine.setState(AIState::STARTING)

T1: 收到服务器Hello
    ├─ state_machine.onHello()
    └─ 状态: STARTING → IDLE

T2: 发送Listen请求
    ├─ state_machine.onListenStart()
    ├─ 状态: IDLE → LISTENING
    └─ 音频上传: ✅ 启用

T3: 用户说话 "今天天气怎么样"
    └─ 麦克风录音 → Opus编码 → 上传

T4: 收到STT结果
    ├─ state_machine.onSTT("今天天气怎么样", true)
    ├─ 状态: LISTENING → THINKING
    └─ 音频上传: ✅ 保持启用

T5: 收到LLM回复
    ├─ state_machine.onLLM("今天北京晴天...", false)
    └─ 状态: 保持THINKING

T6: TTS开始
    ├─ state_machine.onTTS_start()
    ├─ 状态: THINKING → LISTENING (xiaozhi设计)
    └─ 音频上传: ❌ 禁用

T7: TTS句子开始
    ├─ state_machine.onTTS_sentenceStart("今天北京...")
    ├─ 状态: LISTENING → SPEAKING
    └─ 音频上传: ❌ 保持禁用

T8: TTS播放中
    └─ 音频上传: ❌ 禁用（AI说话期间）

T9: TTS结束
    ├─ state_machine.onTTS_stop(2000)
    ├─ 等待2秒...
    ├─ 状态: SPEAKING → LISTENING
    └─ 音频上传: ✅ 恢复启用

T10: 准备下一轮对话
    └─ 回到T3
```

## API参考

### 状态转换方法

| 方法 | 触发时机 | 状态转换 | 音频上传 |
|------|---------|---------|---------|
| `onHello()` | 收到服务器Hello | STARTING→IDLE | 禁用 |
| `onListenStart()` | 收到Listen开始 | IDLE→LISTENING | 启用 |
| `onSTT(text, final)` | 收到STT结果 | LISTENING→THINKING | 保持 |
| `onLLM(text, final)` | 收到LLM回复 | 保持THINKING | 保持 |
| `onTTS_start()` | TTS开始 | 保持 | **禁用** |
| `onTTS_sentenceStart(text)` | TTS句子开始 | LISTENING→SPEAKING | 保持 |
| `onTTS_stop(delay)` | TTS结束 | SPEAKING→LISTENING(延迟) | **启用(延迟)** |
| `onError(msg)` | 错误发生 | →ERROR | 禁用 |
| `reset()` | 手动重置 | →IDLE | 禁用 |

### 状态查询方法

```cpp
AIState getState() const;                 // 获取当前状态
bool isState(AIState state) const;        // 检查是否处于某状态
bool isAudioUploadEnabled() const;        // 检查音频上传是否启用
```

### 直接控制方法

```cpp
void enableAudioUpload();   // 手动启用音频上传
void disableAudioUpload();  // 手动禁用音频上传
void setState(AIState state);  // 手动设置状态（谨慎使用）
```

## 设计特点

### 1. 线程安全
- 使用`std::atomic`存储状态和标志
- 使用`std::mutex`保护回调函数
- 可以从任意线程调用

### 2. 回调驱动
- 状态变化时触发`StateChangeCallback`
- 音频上传开关变化时触发`AudioUploadCallback`
- 解耦状态机和业务逻辑

### 3. 延迟处理
- TTS结束后自动延迟2秒
- 使用独立线程处理延迟
- 不阻塞主流程

### 4. Pimpl模式
- 隐藏实现细节
- 编译依赖隔离
- ABI稳定性

## 注意事项

### 1. 音频上传控制的重要性

**为什么TTS时要禁用音频上传？**
```
AI说话（TTS播放） → 扬声器输出 → 麦克风拾取 → 上传到服务器 → AI误以为用户在说话
```

**解决方案：**
- TTS开始时立即禁用音频上传
- TTS结束后延迟2秒再恢复
- 这是软件级别的AEC（回声消除）

### 2. 状态转换的顺序

严格按照xiaozhi的设计：
```
LISTENING → THINKING → SPEAKING → LISTENING
```

不要跳过中间状态，否则可能导致音频上传控制混乱。

### 3. 延迟时间的选择

```cpp
onTTS_stop(2000);  // 默认2秒
```

- 太短：可能听到尾音回声
- 太长：响应延迟明显
- xiaozhi使用2秒，经过实际测试

## 下一步集成

状态机已完成，接下来需要：

1. **在主控制器中使用状态机**
   ```cpp
   // chatbot.cc
   protocol_handler.setTTSCallback([&](const TTSMessage& msg) {
       state_machine.onTTS_start();  // 自动控制音频上传
   });
   ```

2. **连接音频系统**
   ```cpp
   state_machine.setAudioUploadCallback([&](bool enable) {
       audio_system.is_ai_streaming = enable;
   });
   ```

3. **实现MCP管理器**
   - 处理IoT方法调用
   - 注册设备能力

---

**AI状态机模块完成！** ✅

