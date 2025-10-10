# WebSocket 客户端模块

## 概述

基于 `libdatachannel` 实现的 WebSocket 客户端，用于连接 xiaozhi 云端 AI 服务。

**连接地址**: `wss://api.tenclass.net/xiaozhi/v1/`

## 功能特性

- ✅ **WSS 安全连接** - 支持 TLS 加密通信
- ✅ **自动重连** - 断线后自动尝试重连
- ✅ **消息回调** - 分别处理二进制（音频）和文本（JSON）消息
- ✅ **自定义 Headers** - 支持设置 HTTP 头部（Device-Id、Client-Id 等）
- ✅ **Hello 消息** - 连接成功后自动发送握手消息
- ✅ **线程安全** - 使用互斥锁保护共享资源

## 目录结构

```
app/protocol/websocket/
├── websocket.h         # WebSocket 客户端头文件
├── websocket.cc        # WebSocket 客户端实现
└── README.md           # 本文档
```

## 核心API

### 1. WebSocket 客户端类

```cpp
#include "app/protocol/websocket/websocket.h"
using namespace glasses::protocol::websocket;

// 创建客户端
WebSocketConfig config;
config.url = "wss://example.com";
config.auto_reconnect = true;
config.reconnect_interval_ms = 5000;

WebSocketClient client(config);

// 设置回调
client.setCallbacks(onBinary, onText, user_data);

// 连接
client.connect();

// 发送消息
client.sendText("Hello", 5);
client.sendBinary(data, size);

// 断开
client.disconnect();
```

### 2. xiaozhi 客户端创建辅助函数

```cpp
// 快速创建 xiaozhi 客户端
WebSocketClient* client = createXiaozhiClient(
    "8c:bd:37:36:60:b7",                          // Device-Id (MAC)
    "b1c302a7-76c8-46b5-a71d-aeac4f051acb",       // Client-Id (UUID)
    onBinaryMessage,                              // TTS 音频回调
    onTextMessage,                                // JSON 消息回调
    nullptr                                       // 用户数据
);

client->connect();
```

## 消息回调

### 二进制消息回调（TTS 音频）

```cpp
void onBinaryMessage(const char* buffer, size_t size, void* user_data) {
    // buffer: Opus 编码的音频数据
    // size: 数据大小（字节）
    // 将音频数据送入播放队列...
}
```

### 文本消息回调（JSON 协议）

```cpp
void onTextMessage(const char* buffer, size_t size, void* user_data) {
    std::string message(buffer, size);
    // 解析 JSON 消息
    // 处理 STT/LLM/IoT 消息...
}
```

## xiaozhi 协议

### Hello 消息（连接握手）

```json
{
    "type": "hello",
    "version": 1,
    "transport": "websocket",
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

### HTTP Headers

- `Device-Id`: 设备 MAC 地址（如 `8c:bd:37:36:60:b7`）
- `Client-Id`: 客户端 UUID（如 `b1c302a7-76c8-46b5-a71d-aeac4f051acb`）
- `Protocol-Version`: 协议版本（固定为 `1`）

## 测试结果

```bash
cd /home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo
./bin/main  # 运行测试程序
```

### 测试覆盖

✅ **测试1: 基本连接测试**
- 连接到 `wss://echo.websocket.org`
- 发送和接收消息
- 正常断开连接

✅ **测试2: xiaozhi 客户端创建**
- 自动获取 MAC 地址
- 自动生成/读取 UUID
- 创建 xiaozhi 客户端配置

✅ **测试3: 配置管理测试**
- URL 设置
- Headers 添加
- Hello 消息设置
- 自动重连配置

✅ **测试4: 状态检查测试**
- 连接状态检查
- 握手状态检查

## 依赖项

### 内部依赖
- `app/chatbot/uuid/uuid.h` - UUID 生成和持久化
- `app/tool/mac/mac.h` - MAC 地址获取

### 外部依赖
- `libdatachannel` - WebSocket 和 WebRTC 库
- `nlohmann/json` - JSON 解析（libdatachannel 提供）

## 连接状态

```cpp
enum class ConnectionState {
    DISCONNECTED = 0,  // 未连接
    CONNECTING   = 1,  // 连接中
    CONNECTED    = 2,  // 已连接
    CLOSING      = 3,  // 关闭中
    CLOSED       = 4   // 已关闭
};

// 检查状态
ConnectionState state = client.getState();
bool connected = client.isConnected();
bool handshaked = client.isHandshaked();
```

## 设计模式

### Pimpl 惯用法
使用 Pimpl（Pointer to Implementation）模式隐藏实现细节：
- 公共接口：`WebSocketClient` 类
- 私有实现：`WebSocketClient::Impl` 类
- 好处：编译时间短、ABI 稳定、隐藏依赖

### 回调机制
使用 `std::function` 实现灵活的回调系统，支持：
- Lambda 表达式
- 函数指针
- 成员函数（通过 `std::bind`）

## 自动重连机制

```cpp
// 启用自动重连（默认启用）
client.setAutoReconnect(true);

// 设置重连间隔（默认 5000ms）
config.reconnect_interval_ms = 5000;
```

**重连逻辑**：
1. 连接断开时触发 `onClosed` 回调
2. 如果 `auto_reconnect` 为 true，等待指定时间
3. 自动调用 `connect()` 尝试重连
4. 重连线程在后台运行，不阻塞主线程

## 注意事项

### 1. 线程安全
- 内部使用 `std::mutex` 保护共享资源
- 回调在 libdatachannel 的 I/O 线程中执行
- 回调中访问外部数据需要额外同步

### 2. 资源管理
- 使用 `std::shared_ptr` 管理 WebSocket 对象
- 析构函数自动清理资源
- 重连线程在析构时自动 join

### 3. 错误处理
- 所有异常都被捕获并记录
- 发送失败返回 false
- 连接失败触发自动重连（如果启用）

## 下一步集成

### 第4步：实现 xiaozhi 协议处理层

需要实现的消息类型：
- `hello` - 握手消息（已实现）
- `listen` - 开始监听
- `stt` - 语音识别结果
- `llm` - 大语言模型回复
- `tts` - 文本转语音
- `iot` - IoT 设备控制（MCP）

### 第5步：实现 MCP 工具管理器

支持本地功能调用：
- 注册设备能力描述符
- 处理云端调用请求
- 返回执行结果

### 第6步：实现 AI 状态机

管理 AI 交互状态：
- `idle` - 空闲
- `listening` - 监听中
- `thinking` - 思考中
- `speaking` - 说话中

## 参考资料

- [libdatachannel WebSocket API](https://github.com/paullouisageneau/libdatachannel)
- [xiaozhi-linux 项目](https://github.com/100askTeam/xiaozhi-linux)
- WebSocket 协议规范：RFC 6455

