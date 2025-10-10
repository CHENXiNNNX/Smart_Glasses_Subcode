# UDP IPC 模块使用文档

## 功能介绍

UDP IPC模块用于Smart_Glasses多进程架构中的本地进程间通信：

```
┌─────────────────┐         ┌──────────────────┐
│   main进程      │         │   ai_service     │
│  (Smart_Glasses)│ ←─UDP─→ │   (xiaozhi AI)   │
│                 │         │                  │
│ • PortAudio     │         │ • WebSocket      │
│ • WebRTC        │         │ • xiaozhi协议    │
│ • Video         │         │ • AI状态机       │
└─────────────────┘         └──────────────────┘
   端口: 5678/5679            端口: 5679/5678
```

### 基本用法

```cpp
#include "app/protocol/udp/udp.h"

using namespace glasses::protocol::udp;

// 定义接收回调
int onDataReceived(char* buffer, size_t size, void* user_data) {
    std::cout << "收到数据: " << size << " bytes" << std::endl;
    return 0;
}

// 创建端点
UdpEndpoint* endpoint = createMainEndpoint(onDataReceived, nullptr);

// 发送数据
const char* msg = "Hello!";
endpoint->send(msg, strlen(msg));

// 清理
delete endpoint;
```

---

## 端口配置

### 主进程端点

```cpp
UdpEndpoint* main_ep = createMainEndpoint(callback, user_data);
// 监听: 5678
// 发送: 5679
```

### AI进程端点

```cpp
UdpEndpoint* ai_ep = createAIEndpoint(callback, user_data);
// 监听: 5679
// 发送: 5678
```

---

## 消息协议

### 消息类型

```cpp
enum class MessageType : uint8_t {
    AUDIO_DATA      = 0x01,  // 音频数据（Opus编码）
    CONTROL_CMD     = 0x02,  // 控制命令
    STATE_UPDATE    = 0x03,  // 状态更新
    TEXT_DATA       = 0x04,  // 文本数据（STT/LLM）
    TTS_DATA        = 0x05,  // TTS音频数据
    MCP_TOOL_CALL   = 0x06,  // MCP工具调用
    HEARTBEAT       = 0xFF   // 心跳包
};
```

### 消息格式

```
+--------+----------+--------+------------------+
| type   | reserved | length | payload          |
| 1 byte | 1 byte   | 2 byte | length bytes     |
+--------+----------+--------+------------------+
```

### 发送消息示例

```cpp
// 构造消息
MessageHeader header;
header.type = static_cast<uint8_t>(MessageType::AUDIO_DATA);
header.reserved = 0;
header.length = 120;  // Opus帧大小

char packet[sizeof(MessageHeader) + 120];
memcpy(packet, &header, sizeof(MessageHeader));
memcpy(packet + sizeof(MessageHeader), opus_data, 120);

// 发送
endpoint->send(packet, sizeof(packet));
```

### 接收消息示例

```cpp
int callback(char* buffer, size_t size, void* user_data) {
    // 解析消息头
    if (size < sizeof(MessageHeader)) return -1;
    
    MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer);
    char* payload = buffer + sizeof(MessageHeader);
    
    switch (static_cast<MessageType>(header->type)) {
        case MessageType::AUDIO_DATA:
            // 处理音频数据
            processAudio(payload, header->length);
            break;
        case MessageType::TEXT_DATA:
            // 处理文本数据
            processText(payload, header->length);
            break;
        // ...
    }
    
    return 0;
}
```

---

## API参考

### UdpEndpoint 类

#### 构造函数

```cpp
UdpEndpoint(int port_local, int port_remote, 
            TransferCallback callback = nullptr, 
            void* user_data = nullptr);
```

**参数**:
- `port_local`: 本地监听端口
- `port_remote`: 远程发送端口
- `callback`: 数据接收回调（可选）
- `user_data`: 用户自定义数据

#### send()

```cpp
int send(const char* data, int len);
```

发送数据到远程端点。

**返回**: 0表示成功，-1表示失败

#### recv()

```cpp
int recv(unsigned char* data, int maxlen, int* retlen);
```

同步接收数据（阻塞）。

**注意**: 如果使用了回调，通常不需要调用此函数。

#### isValid()

```cpp
bool isValid() const;
```

检查端点是否有效。

