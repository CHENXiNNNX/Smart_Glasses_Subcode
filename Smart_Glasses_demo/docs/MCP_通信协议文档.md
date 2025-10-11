# AR眼镜 MCP 服务器通信协议文档

## 文档信息
- **协议版本**: MCP 2024-11-05
- **基础协议**: JSON-RPC 2.0
- **传输层**: WebSocket / MQTT
- **最后更新**: 2025-01-11

---

## 目录
1. [概述](#1-概述)
2. [连接建立流程](#2-连接建立流程)
3. [MCP 协议层](#3-mcp-协议层)
4. [消息传输格式](#4-消息传输格式)
5. [工具管理机制](#5-工具管理机制)
6. [完整通信流程示例](#6-完整通信流程示例)
7. [错误处理机制](#7-错误处理机制)
8. [安全与认证](#8-安全与认证)

---

## 1. 概述

### 1.1 架构概览

```
┌──────────────────────────────────────────────────────────┐
│                    应用服务器                              │
│        (LLM + MCP Client + Vision Service)               │
└───────────────────────┬──────────────────────────────────┘
                        │
                        │ WebSocket / MQTT
                        │
┌───────────────────────▼──────────────────────────────────┐
│                  AR 眼镜设备                              │
├──────────────────────────────────────────────────────────┤
│  ┌────────────────────────────────────────────────────┐  │
│  │              MCP Server                            │  │
│  │  - 工具注册与管理                                   │  │
│  │  - JSON-RPC 2.0 消息处理                          │  │
│  │  - 能力协商                                        │  │
│  └────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────┐  │
│  │           Protocol Layer (抽象层)                  │  │
│  │  - WebSocket Protocol                             │  │
│  │  - MQTT Protocol                                  │  │
│  └────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────┐  │
│  │           Transport Layer                         │  │
│  │  - ESP WebSocket Client                           │  │
│  │  - ESP MQTT Client                                │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### 1.2 通信特点

- **双向实时通信**: 支持设备和服务器之间的双向消息传递
- **异步工具调用**: 工具调用在独立线程中执行，不阻塞主通信
- **分页支持**: 工具列表支持分页传输，适配嵌入式设备内存限制
- **能力协商**: 支持客户端和服务器之间的能力交换
- **会话管理**: 每个连接分配唯一 `session_id`

---

## 2. 连接建立流程

### 2.1 WebSocket 连接建立

#### 2.1.1 连接参数

| 参数 | 说明 | 示例 |
|------|------|------|
| URL | WebSocket 服务器地址 | `ws://server.com/ws` 或 `wss://server.com/ws` |
| Token | 认证令牌 | `Bearer eyJhbGciOiJIUzI1NiIs...` |
| Version | 协议版本 | `1`, `2`, `3` |

#### 2.1.2 请求头设置

```
GET /ws HTTP/1.1
Host: server.com
Upgrade: websocket
Connection: Upgrade
Authorization: Bearer <token>
Protocol-Version: 1
Device-Id: 94:A9:90:27:3D:50
Client-Id: 550e8400-e29b-41d4-a716-446655440000
```

### 2.2 Hello 握手协议

#### 2.2.1 设备 Hello 消息（客户端 → 服务器）

设备连接成功后，立即发送 Hello 消息：

```json
{
  "type": "hello",
  "version": 1,
  "transport": "websocket",
  "features": {
    "aec": true,
    "mcp": true
  },
  "audio_params": {
    "format": "opus",
    "sample_rate": 16000,
    "channels": 1,
    "frame_duration": 60
  }
}
```

**字段说明:**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `type` | string | ✅ | 固定为 `"hello"` |
| `version` | number | ✅ | 协议版本 (1/2/3) |
| `transport` | string | ✅ | 传输类型 (`websocket` / `udp`) |
| `features` | object | ✅ | 设备支持的功能 |
| `features.aec` | boolean | ❌ | 是否支持服务器端回声消除 |
| `features.mcp` | boolean | ❌ | 是否支持 MCP 协议 |
| `audio_params` | object | ✅ | 音频参数配置 |
| `audio_params.format` | string | ✅ | 音频格式 (通常为 `opus`) |
| `audio_params.sample_rate` | number | ✅ | 采样率 (Hz) |
| `audio_params.channels` | number | ✅ | 声道数 |
| `audio_params.frame_duration` | number | ✅ | 帧持续时间 (ms) |

#### 2.2.2 服务器 Hello 响应（服务器 → 客户端）

服务器返回 Hello 响应，分配 `session_id`：

```json
{
  "type": "hello",
  "transport": "websocket",
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "audio_params": {
    "sample_rate": 24000,
    "frame_duration": 60
  }
}
```

**字段说明:**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `type` | string | ✅ | 固定为 `"hello"` |
| `transport` | string | ✅ | 确认传输类型 |
| `session_id` | string | ✅ | 会话唯一标识符（UUID） |
| `audio_params` | object | ✅ | 服务器音频参数 |
| `audio_params.sample_rate` | number | ✅ | 服务器期望的采样率 |
| `audio_params.frame_duration` | number | ✅ | 服务器期望的帧持续时间 |

#### 2.2.3 握手流程图

```
设备                                  服务器
 │                                     │
 ├─── TCP 连接 ──────────────────────→│
 │                                     │
 ├─── WebSocket Upgrade ─────────────→│
 │                                     │
 │←────── 101 Switching Protocols ────┤
 │                                     │
 ├─── Hello (设备信息) ───────────────→│
 │                                     │
 │        等待响应 (10s 超时)           │
 │                                     │
 │←────── Hello (session_id) ─────────┤
 │                                     │
 ├─── 连接建立成功 ─────────────────→│
 │                                     │
 ├─── 开始 MCP 通信 ──────────────────→│
 │                                     │
```

### 2.3 连接状态管理

#### 2.3.1 心跳与超时

- **超时时间**: 120 秒
- **检测机制**: 根据最后一次接收消息的时间判断
- **超时处理**: 自动关闭连接并触发重连

#### 2.3.2 断开连接

**主动断开 (Goodbye 消息):**

```json
{
  "type": "goodbye",
  "session_id": "550e8400-e29b-41d4-a716-446655440000"
}
```

**被动断开处理:**
- 触发 `OnDisconnected` 回调
- 通知应用层连接已关闭
- 等待重连指令

---

## 3. MCP 协议层

### 3.1 消息封装格式

所有 MCP 消息都封装在应用层消息中：

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    // JSON-RPC 2.0 消息体
  }
}
```

**外层封装字段:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | string | 会话 ID（由服务器分配） |
| `type` | string | 固定为 `"mcp"` |
| `payload` | object | MCP JSON-RPC 2.0 消息 |

### 3.2 JSON-RPC 2.0 基础格式

#### 3.2.1 请求格式

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "方法名",
  "params": {
    // 方法参数
  }
}
```

#### 3.2.2 响应格式

**成功响应:**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    // 返回结果
  }
}
```

**错误响应:**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "message": "错误描述"
  }
}
```

### 3.3 支持的 MCP 方法

#### 3.3.1 initialize（初始化）

**用途**: 建立 MCP 连接，交换能力信息

**请求 (客户端 → 服务器):**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "capabilities": {
        "vision": {
          "url": "https://vision-api.example.com/analyze",
          "token": "Bearer xxx"
        }
      }
    }
  }
}
```

**响应 (服务器 → 客户端):**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
      "protocolVersion": "2024-11-05",
      "capabilities": {
        "tools": {}
      },
      "serverInfo": {
        "name": "AR_Glasses",
        "version": "1.0.0"
      }
    }
  }
}
```

**能力协商说明:**

| 能力 | 方向 | 说明 |
|------|------|------|
| `vision` | 客户端 → 服务器 | 提供视觉分析 API 的地址和令牌 |
| `tools` | 服务器 → 客户端 | 声明支持工具调用 |

#### 3.3.2 tools/list（获取工具列表）

**用途**: 获取设备支持的所有工具

**请求:**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/list",
    "params": {
      "cursor": ""  // 可选，用于分页
    }
  }
}
```

**响应（无分页）:**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
      "tools": [
        {
          "name": "self.audio_speaker.set_volume",
          "description": "设置音量",
          "inputSchema": {
            "type": "object",
            "properties": {
              "volume": {
                "type": "integer",
                "minimum": 0,
                "maximum": 100
              }
            },
            "required": ["volume"]
          }
        },
        {
          "name": "self.screen.set_brightness",
          "description": "设置屏幕亮度",
          "inputSchema": {
            "type": "object",
            "properties": {
              "brightness": {
                "type": "integer",
                "minimum": 0,
                "maximum": 100
              }
            },
            "required": ["brightness"]
          }
        }
      ]
    }
  }
}
```

**响应（带分页）:**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
      "tools": [
        // 工具列表...
      ],
      "nextCursor": "self.camera.take_photo"
    }
  }
}
```

**分页机制说明:**
- 单次响应最大负载: **8000 字节**
- 超过限制时，设置 `nextCursor` 指向下一批的起始工具名
- 客户端使用 `cursor` 参数获取后续内容

#### 3.3.3 tools/call（调用工具）

**用途**: 执行指定的设备工具

**请求:**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "method": "tools/call",
    "params": {
      "name": "self.audio_speaker.set_volume",
      "arguments": {
        "volume": 75
      },
      "stackSize": 6144  // 可选，工具执行的线程栈大小
    }
  }
}
```

**响应（成功）:**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "true"
        }
      ],
      "isError": false
    }
  }
}
```

**响应（错误）:**

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 3,
    "error": {
      "message": "Missing valid argument: volume"
    }
  }
}
```

#### 3.3.4 notifications/*（通知消息）

**用途**: 单向通知，不需要响应

**特点:**
- 方法名以 `notifications` 开头
- 设备接收后直接忽略，不做任何处理
- 用于服务器端的日志或状态更新

---

## 4. 消息传输格式

### 4.1 完整消息流转示例

#### 场景: 设置音量为 50

**步骤 1: 客户端发送请求**

WebSocket 文本帧:
```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 10,
    "method": "tools/call",
    "params": {
      "name": "self.audio_speaker.set_volume",
      "arguments": {
        "volume": 50
      }
    }
  }
}
```

**步骤 2: 设备接收并解析**

1. WebSocket 层接收文本消息
2. 解析外层 JSON，识别 `type = "mcp"`
3. 提取 `payload` 传递给 MCP Server
4. MCP Server 解析 JSON-RPC 2.0 消息
5. 查找工具 `self.audio_speaker.set_volume`
6. 验证参数 `volume` 在 0-100 范围内
7. 创建线程执行工具回调
8. 调用音频编解码器设置音量

**步骤 3: 设备返回响应**

WebSocket 文本帧:
```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 10,
    "result": {
      "content": [
        {
          "type": "text",
          "text": "true"
        }
      ],
      "isError": false
    }
  }
}
```

### 4.2 多层消息封装

```
┌─────────────────────────────────────────────────────────┐
│                   WebSocket 文本帧                       │
├─────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────┐  │
│  │          应用层消息 (Application Message)          │  │
│  ├───────────────────────────────────────────────────┤  │
│  │  session_id: "550e8400-..."                       │  │
│  │  type: "mcp"                                      │  │
│  │  ┌─────────────────────────────────────────────┐  │  │
│  │  │       MCP Payload (JSON-RPC 2.0)           │  │  │
│  │  ├─────────────────────────────────────────────┤  │  │
│  │  │  jsonrpc: "2.0"                             │  │  │
│  │  │  id: 1                                      │  │  │
│  │  │  method: "tools/call"                       │  │  │
│  │  │  ┌───────────────────────────────────────┐  │  │  │
│  │  │  │         Params (工具调用参数)          │  │  │  │
│  │  │  ├───────────────────────────────────────┤  │  │  │
│  │  │  │  name: "self.audio_speaker..."        │  │  │  │
│  │  │  │  arguments: { volume: 50 }            │  │  │  │
│  │  │  └───────────────────────────────────────┘  │  │  │
│  │  └─────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### 4.3 消息类型总览

设备与服务器之间的消息类型：

| 消息类型 | 说明 | 方向 |
|---------|------|------|
| `hello` | 连接建立握手 | 双向 |
| `goodbye` | 连接关闭通知 | 服务器 → 设备 |
| `mcp` | MCP 协议消息 | 双向 |
| `listen` | 语音识别控制 | 服务器 → 设备 |
| `llm` | LLM 响应消息 | 服务器 → 设备 |
| `text` | 文本消息 | 服务器 → 设备 |
| `iot` | IoT 设备控制 | 双向 |
| `system` | 系统命令 | 服务器 → 设备 |
| `abort` | 中断播放 | 设备 → 服务器 |

---

## 5. 工具管理机制

### 5.1 工具定义结构

每个工具包含以下信息：

```json
{
  "name": "self.audio_speaker.set_volume",
  "description": "设置音频音量。如果当前音量未知，需先调用 `self.get_device_status` 工具。",
  "inputSchema": {
    "type": "object",
    "properties": {
      "volume": {
        "type": "integer",
        "minimum": 0,
        "maximum": 100
      }
    },
    "required": ["volume"]
  }
}
```

### 5.2 参数类型系统

支持的参数类型：

| 类型 | JSON Schema 类型 | 说明 | 示例 |
|------|-----------------|------|------|
| Boolean | `boolean` | 布尔值 | `true`, `false` |
| Integer | `integer` | 整数，支持范围限制 | `50` |
| String | `string` | 字符串 | `"light"` |

**整数范围限制:**
```json
{
  "type": "integer",
  "minimum": 0,
  "maximum": 100,
  "default": 50
}
```

### 5.3 工具分类

#### 5.3.1 设备状态工具

| 工具名 | 说明 |
|--------|------|
| `self.get_device_status` | 获取设备完整状态（音量、亮度、电量等） |

#### 5.3.2 音频控制工具

| 工具名 | 说明 |
|--------|------|
| `self.audio_speaker.set_volume` | 设置扬声器音量 (0-100) |

#### 5.3.3 屏幕控制工具

| 工具名 | 说明 |
|--------|------|
| `self.screen.set_brightness` | 设置屏幕亮度 (0-100) |
| `self.screen.set_theme` | 设置屏幕主题 (light/dark) |

#### 5.3.4 相机工具

| 工具名 | 说明 |
|--------|------|
| `self.camera.take_photo` | 拍照并通过 Vision API 分析 |
| `self.camera.open_stream_mode` | 开启视频流推送模式 |
| `self.camera.take_photo_and_send` | 拍照并通过 HTTP 发送 |

#### 5.3.5 电池管理工具

| 工具名 | 说明 |
|--------|------|
| `self.battery.get_level` | 获取电池电量百分比 |
| `self.battery.get_status` | 获取充电状态 |

#### 5.3.6 LED 控制工具

| 工具名 | 说明 |
|--------|------|
| `self.led.control_ws2812` | 控制 WS2812 LED 颜色 |
| `self.led.start_flow_light` | 启动流水灯效果 |
| `self.led.stop_flow_light` | 停止流水灯效果 |
| `self.led.start_random_color_flow_light` | 启动随机彩色流水灯 |

#### 5.3.7 音频通话工具

| 工具名 | 说明 |
|--------|------|
| `self.audio_call.start` | 开始音频通话 |
| `self.audio_call.stop` | 停止音频通话 |
| `self.audio_call.mute` | 静音/取消静音麦克风 |
| `self.audio_call.status` | 获取通话状态 |

### 5.4 工具调用流程

```
客户端 (LLM)                      设备 (MCP Server)
     │                                 │
     ├── tools/list ──────────────────→│
     │                                 │
     │←──────── tools (JSON Schema) ───┤
     │                                 │
     │  (LLM 决策需要调用工具)           │
     │                                 │
     ├── tools/call ──────────────────→│
     │   name: "self.audio_..."        │
     │   arguments: {volume: 50}       │
     │                                 ├──┐
     │                                 │  │ 1. 查找工具
     │                                 │  │ 2. 验证参数
     │                                 │  │ 3. 检查内存
     │                                 │  │ 4. 创建线程
     │                                 │  │ 5. 执行回调
     │                                 │←─┘
     │                                 │
     │←──────── result ────────────────┤
     │   { success: true }             │
     │                                 │
```

### 5.5 工具调用优化

#### 5.5.1 内存管理

- **可用内存检查**: 执行前检查可用堆内存
- **内存不足处理**: 小于 10KB 时同步执行，避免线程创建失败
- **线程栈大小**: 默认 6144 字节，最大限制 3KB

#### 5.5.2 异步执行

- **线程隔离**: 每个工具调用在独立线程中执行
- **非阻塞**: 不阻塞主通信线程
- **线程分离**: 使用 `detach()` 模式，自动释放资源

#### 5.5.3 Prompt Cache 优化

- **工具顺序**: 常用工具放在列表开头
- **缓存利用**: 利用 LLM 的 Prompt Cache 减少响应时间

---

## 6. 完整通信流程示例

### 6.1 完整交互序列

以下是一个完整的 MCP 会话示例：

```
设备                                      服务器
 │                                         │
 ├─── TCP + WebSocket 握手 ───────────────→│
 │                                         │
 ├─── Hello (features: {mcp: true}) ─────→│
 │                                         │
 │←────── Hello (session_id) ─────────────┤
 │                                         │
 │                                         │
 │       === MCP 会话开始 ===               │
 │                                         │
 │←────── MCP: initialize ────────────────┤
 │   params: {                             │
 │     capabilities: {                     │
 │       vision: {                         │
 │         url: "https://...",             │
 │         token: "xxx"                    │
 │       }                                 │
 │     }                                   │
 │   }                                     │
 │                                         │
 ├─── MCP: initialize result ─────────────→│
 │   result: {                             │
 │     protocolVersion: "2024-11-05",      │
 │     serverInfo: {                       │
 │       name: "AR_Glasses",               │
 │       version: "1.0.0"                  │
 │     }                                   │
 │   }                                     │
 │                                         │
 │←────── MCP: tools/list ────────────────┤
 │                                         │
 ├─── MCP: tools/list result ─────────────→│
 │   result: {                             │
 │     tools: [                            │
 │       {name: "self.audio_...", ...},    │
 │       {name: "self.screen_...", ...},   │
 │       ...                               │
 │     ]                                   │
 │   }                                     │
 │                                         │
 │       === 用户交互开始 ===               │
 │                                         │
 │←────── 语音识别: "把音量调到 50" ────────┤
 │                                         │
 │←────── MCP: tools/call ────────────────┤
 │   params: {                             │
 │     name: "self.get_device_status"      │
 │   }                                     │
 │                                         │
 ├─── MCP: tools/call result ─────────────→│
 │   result: {                             │
 │     content: [{                         │
 │       type: "text",                     │
 │       text: '{"volume": 30, ...}'       │
 │     }]                                  │
 │   }                                     │
 │                                         │
 │←────── MCP: tools/call ────────────────┤
 │   params: {                             │
 │     name: "self.audio_speaker.set_...", │
 │     arguments: {volume: 50}             │
 │   }                                     │
 │                                         │
 ├─ 执行: 设置音量到 50                     │
 │                                         │
 ├─── MCP: tools/call result ─────────────→│
 │   result: {                             │
 │     content: [{                         │
 │       type: "text",                     │
 │       text: "true"                      │
 │     }]                                  │
 │   }                                     │
 │                                         │
 │←────── LLM 响应: "已将音量设置为 50" ───┤
 │                                         │
```

### 6.2 拍照并分析流程

```
设备                                      服务器
 │                                         │
 │←────── 用户: "看看这是什么" ─────────────┤
 │                                         │
 │←────── MCP: tools/call ────────────────┤
 │   params: {                             │
 │     name: "self.camera.take_photo",     │
 │     arguments: {                        │
 │       question: "这是什么？"             │
 │     }                                   │
 │   }                                     │
 │                                         │
 ├─ 打开相机 LED                           │
 ├─ 捕获图像                               │
 ├─ 压缩为 JPEG                           │
 │                                         │
 ├─── HTTP POST ──────────────────────────→│ Vision API
 │   Content-Type: image/jpeg              │
 │   Authorization: Bearer xxx             │
 │                                         │
 │←────── Vision 响应 ────────────────────┤
 │   {                                     │
 │     "objects": ["笔记本电脑"],          │
 │     "description": "一台银色笔记本..."   │
 │   }                                     │
 │                                         │
 ├─ 关闭相机 LED                           │
 │                                         │
 ├─── MCP: tools/call result ─────────────→│
 │   result: {                             │
 │     content: [{                         │
 │       type: "text",                     │
 │       text: '{"objects": [...], ...}'   │
 │     }]                                  │
 │   }                                     │
 │                                         │
 │←────── LLM: "这是一台笔记本电脑..." ────┤
 │                                         │
```

---

## 7. 错误处理机制

### 7.1 错误类型

#### 7.1.1 连接层错误

| 错误类型 | 处理方式 |
|---------|---------|
| 连接失败 | 返回错误，等待重连 |
| 握手超时 | 10 秒后自动断开 |
| 心跳超时 | 120 秒无消息自动断开 |
| 网络断开 | 触发 `OnDisconnected` 回调 |

#### 7.1.2 协议层错误

| 错误 | 错误消息 | HTTP 等价 |
|------|---------|----------|
| 无效的 JSON | `"Failed to parse MCP message"` | 400 |
| 无效的 JSON-RPC 版本 | `"Invalid JSONRPC version"` | 400 |
| 缺少方法名 | `"Missing method"` | 400 |
| 未知方法 | `"Method not implemented: xxx"` | 404 |
| 无效参数 | `"Invalid params"` | 400 |

#### 7.1.3 工具调用错误

| 错误 | 错误消息 |
|------|---------|
| 工具不存在 | `"Unknown tool: xxx"` |
| 缺少必需参数 | `"Missing valid argument: xxx"` |
| 参数超出范围 | `"Value exceeds maximum allowed: xxx"` |
| 参数类型错误 | `"Value is below minimum allowed: xxx"` |
| 内存不足 | `"Low memory detected"` (降级为同步执行) |
| 线程创建失败 | `"Failed to create tool call thread"` (降级为同步执行) |

### 7.2 错误响应格式

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "id": 5,
    "error": {
      "message": "具体的错误描述"
    }
  }
}
```

### 7.3 降级策略

#### 7.3.1 内存不足降级

当可用堆内存 < 10KB 时：
- 不创建新线程
- 同步执行工具调用
- 记录警告日志

#### 7.3.2 线程创建失败降级

当线程创建失败时：
- 捕获 `std::system_error` 异常
- 降级为同步执行
- 返回正常结果

---

## 8. 安全与认证

### 8.1 传输层安全

#### 8.1.1 TLS/SSL

- **推荐**: 使用 `wss://` (WebSocket Secure)
- **证书验证**: 支持服务器证书验证
- **加密套件**: 支持 TLS 1.2 及以上

#### 8.1.2 Token 认证

**请求头格式:**
```
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
```

**Token 处理:**
- 自动添加 `Bearer ` 前缀（如果不存在）
- 在 WebSocket 握手阶段发送
- 服务器验证后分配 `session_id`

### 8.2 设备标识

每个设备使用两个标识符：

| 标识符 | 类型 | 用途 | 示例 |
|--------|------|------|------|
| `Device-Id` | MAC 地址 | 设备唯一硬件标识 | `94:A9:90:27:3D:50` |
| `Client-Id` | UUID | 客户端实例标识 | `550e8400-e29b-41d4-a716-446655440000` |

### 8.3 Vision API 安全

**能力协商中的 Vision 配置:**

```json
{
  "capabilities": {
    "vision": {
      "url": "https://vision-api.example.com/analyze",
      "token": "Bearer vision_token_xxx"
    }
  }
}
```

- 服务器在 `initialize` 时传递 Vision API 配置
- 设备存储并在调用 `self.camera.take_photo` 时使用
- 支持独立的 Vision API Token

---

## 9. 最佳实践

### 9.1 客户端（LLM）侧

1. **工具调用顺序**
   - 先调用 `self.get_device_status` 获取当前状态
   - 再调用具体的控制工具

2. **错误处理**
   - 捕获并显示错误消息
   - 提供用户友好的错误提示

3. **超时处理**
   - 设置合理的工具调用超时（建议 30 秒）
   - 超时后重试或提示用户

### 9.2 服务器（设备）侧

1. **内存管理**
   - 定期检查可用内存
   - 使用线程池避免频繁创建/销毁线程

2. **工具注册**
   - 按使用频率排序工具列表
   - 常用工具放在前面利用 Prompt Cache

3. **日志记录**
   - 记录所有 MCP 方法调用
   - 记录工具执行结果

---

## 10. 版本历史

| 版本 | 日期 | 变更内容 |
|------|------|---------|
| 1.0.0 | 2025-01-11 | 初始版本，基于 MCP 2024-11-05 规范 |

---

## 11. 参考资料

- **MCP 规范**: https://modelcontextprotocol.io/specification/2024-11-05
- **JSON-RPC 2.0**: https://www.jsonrpc.org/specification
- **WebSocket Protocol**: RFC 6455

---

## 附录 A: 工具完整列表

### A.1 设备状态

#### self.get_device_status

**描述**: 获取设备实时状态信息

**参数**: 无

**返回值**: JSON 字符串，包含：
- `audio.volume`: 当前音量 (0-100)
- `screen.brightness`: 屏幕亮度 (0-100)
- `screen.theme`: 屏幕主题 ("light" / "dark")
- `battery.level`: 电池电量百分比
- `battery.status`: 充电状态
- `network.connected`: 网络连接状态

**示例**:
```json
{
  "audio": { "volume": 75 },
  "screen": { "brightness": 80, "theme": "light" },
  "battery": { "level": 65, "status": "充电中" },
  "network": { "connected": true, "ssid": "MyWiFi" }
}
```

### A.2 音频控制

#### self.audio_speaker.set_volume

**描述**: 设置扬声器音量

**参数**:
- `volume` (integer, required): 音量值，范围 0-100

**返回值**: `true` (成功)

### A.3 屏幕控制

#### self.screen.set_brightness

**描述**: 设置屏幕亮度

**参数**:
- `brightness` (integer, required): 亮度值，范围 0-100

**返回值**: `true` (成功)

#### self.screen.set_theme

**描述**: 设置屏幕主题

**参数**:
- `theme` (string, required): 主题名称，可选值: `"light"`, `"dark"`

**返回值**: `true` (成功)

### A.4 相机功能

#### self.camera.take_photo

**描述**: 拍照并通过 Vision API 分析

**参数**:
- `question` (string, required): 关于照片的问题

**返回值**: JSON 字符串，包含 Vision API 的分析结果

**示例**:
```json
{
  "success": true,
  "objects": ["笔记本电脑", "鼠标"],
  "description": "一台银色笔记本电脑放在桌面上",
  "colors": ["银色", "黑色"]
}
```

#### self.camera.open_stream_mode

**描述**: 开启视频流推送模式

**参数**: 无

**返回值**: `true` (成功)

**注意**: 该功能会切换设备到流推送模式

#### self.camera.take_photo_and_send

**描述**: 拍照并通过 HTTP 发送到服务器

**参数**: 无

**返回值**: JSON 字符串
```json
{
  "success": true,
  "message": "拍照功能执行成功"
}
```

**限制**: 只能在小智模式下执行

### A.5 电池管理

#### self.battery.get_level

**描述**: 获取电池电量百分比

**参数**: 无

**返回值**: JSON 字符串
```json
{
  "success": true,
  "batteryLevel": 85
}
```

#### self.battery.get_status

**描述**: 获取电池充电状态

**参数**: 无

**返回值**: JSON 字符串
```json
{
  "success": true,
  "batteryStatus": "充电中"
}
```

**状态值**:
- `"未充电"`: 未连接充电器
- `"充电中"`: 正在充电
- `"已充满"`: 充电完成
- `"未知"`: 状态未知

### A.6 LED 控制

#### self.led.control_ws2812

**描述**: 控制 WS2812 LED 灯带颜色

**参数**:
- `begin` (integer, required): 起始位置 (1-5)
- `end` (integer, required): 结束位置 (1-5)
- `red` (integer, required): 红色值 (0-255)
- `green` (integer, required): 绿色值 (0-255)
- `blue` (integer, required): 蓝色值 (0-255)

**返回值**: JSON 字符串
```json
{
  "success": true,
  "message": "LED strip color set successfully"
}
```

#### self.led.start_flow_light

**描述**: 启动流水灯效果

**参数**:
- `red` (integer, required): 红色值 (0-255)
- `green` (integer, required): 绿色值 (0-255)
- `blue` (integer, required): 蓝色值 (0-255)
- `speed` (integer, required): 速度，单位毫秒 (10-1000)
- `direction` (integer, required): 方向，1=正向，-1=反向

**返回值**: JSON 字符串
```json
{
  "success": true,
  "message": "Flowing light started"
}
```

#### self.led.stop_flow_light

**描述**: 停止流水灯效果

**参数**: 无

**返回值**: JSON 字符串
```json
{
  "success": true,
  "message": "Flowing light stopped"
}
```

#### self.led.start_random_color_flow_light

**描述**: 启动随机彩色流水灯效果

**参数**:
- `speed` (integer, required): 速度，单位毫秒 (10-1000)
- `direction` (integer, required): 方向，1=正向，-1=反向

**返回值**: JSON 字符串
```json
{
  "success": true,
  "message": "Random color flowing light started"
}
```

### A.7 音频通话

#### self.audio_call.start

**描述**: 开始音频通话

**参数**:
- `client_id` (string, required): 客户端标识符
- `send` (boolean, optional, default: true): 启用音频发送
- `recv` (boolean, optional, default: true): 启用音频接收

**返回值**: JSON 字符串
```json
{
  "success": true,
  "running": true,
  "audio_send_running": true,
  "audio_recv_running": true
}
```

**注意**: 视频功能始终禁用

#### self.audio_call.stop

**描述**: 停止音频通话

**参数**: 无

**返回值**: JSON 字符串
```json
{
  "success": true,
  "running": false,
  "audio_send_running": false,
  "audio_recv_running": false
}
```

#### self.audio_call.mute

**描述**: 静音或取消静音麦克风

**参数**:
- `mute` (boolean, required): true=静音，false=取消静音

**返回值**: JSON 字符串
```json
{
  "success": true,
  "send_enabled": false,
  "recv_enabled": true
}
```

#### self.audio_call.status

**描述**: 获取音频通话状态

**参数**: 无

**返回值**: JSON 字符串
```json
{
  "running": true,
  "audio_send_enabled": true,
  "audio_recv_enabled": true,
  "ws_audio_send_running": true,
  "ws_audio_recv_running": true,
  "video_enabled": false
}
```

---

## 附录 B: 错误代码参考

| 错误代码 | 错误消息 | 原因 | 解决方法 |
|---------|---------|------|---------|
| -32700 | Parse error | JSON 格式错误 | 检查 JSON 格式 |
| -32600 | Invalid Request | 请求格式不符合 JSON-RPC 2.0 | 检查 jsonrpc、id、method 字段 |
| -32601 | Method not found | 方法不存在 | 检查方法名是否正确 |
| -32602 | Invalid params | 参数无效 | 检查参数类型和范围 |
| -32603 | Internal error | 服务器内部错误 | 查看设备日志 |

---

**文档版本**: 1.0.0  
**最后更新**: 2025-01-11  
**维护者**: AR Glasses Team

