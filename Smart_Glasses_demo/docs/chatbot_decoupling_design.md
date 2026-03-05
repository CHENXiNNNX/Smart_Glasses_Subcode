# Chatbot 模块全量解耦设计文档

## 一、目标

chatbot 目录下**全部代码**与外部实现解耦，通过接口层实现依赖倒置，使：
- chatbot 不依赖 media、network、protocol 的具体实现
- activation 不依赖 protocol/http 的具体实现
- mcp_tool 不依赖 media、network、protocol 的具体实现
- 各模块可独立编译、测试、替换实现

---

## 二、当前依赖概览

```
chatbot/
├── chatbot.hpp/cc
│   └── 依赖: activation, wakeword, mcp, protocol_handle, websocket,
│             media/audio, media/camera, protocol/webrtc, tool/mac, tool/uuid
├── activation/
│   └── 依赖: protocol/http, tool/log, common
├── wakeword/
│   └── 依赖: tool/log, snowboy (third_party)
├── mcp/
│   └── 依赖: tool/log, common
├── mcp_tool/
│   └── 依赖: mcp, tool/log, media/audio, media/camera, network/wifi,
│             protocol/webrtc, chatbot
└── protocol_handle/
    └── 依赖: tool/log, common
```

---

## 三、解耦后架构

```
app/
├── interfaces/                    # 新建：纯接口层（无实现、无外部依赖）
│   ├── iaudio_service.hpp
│   ├── ivideo_service.hpp
│   ├── inetwork_service.hpp
│   ├── ihttp_client.hpp
│   ├── iwebsocket_client.hpp
│   ├── idevice_info.hpp
│   └── iwebrtc_service.hpp       # 可选
│
├── impl/                         # 新建：接口实现（依赖 media/network/protocol）
│   ├── audio_service_impl.hpp/cc
│   ├── video_service_impl.hpp/cc
│   ├── network_service_impl.hpp/cc
│   ├── http_client_impl.hpp/cc
│   ├── websocket_client_impl.hpp/cc
│   └── device_info_impl.hpp/cc
│
├── chatbot/                      # 只依赖 interfaces
│   ├── chatbot.hpp/cc
│   ├── activation/
│   ├── wakeword/
│   ├── mcp/
│   ├── mcp_tool/
│   └── protocol_handle/
│
├── media/                        # 不依赖 chatbot
├── network/
│
└── protocol/
```

**依赖方向：**

```
interfaces/  ←  chatbot/, mcp_tool/, activation/
     ↑
impl/        ←  main（组装层）
     ↑
media/, network/, protocol/
```

---

## 四、接口定义

### 4.1 IAudioService

```cpp
// app/interfaces/iaudio_service.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <memory>

namespace app {

struct AudioFrameView {
    const void* data;
    size_t      size;
    uint32_t    samples;
    uint32_t    rate;
};

class IAudioService {
public:
    virtual ~IAudioService() = default;

    // TTS 播放
    virtual bool decodeAndPlay(const uint8_t* opus_data, size_t opus_len) = 0;
    virtual bool startPlayback() = 0;
    virtual void stopPlayback() = 0;
    virtual bool isPlaybackRunning() const = 0;

    // 采集（AI 对话）
    virtual bool startCapture() = 0;
    virtual void stopCapture() = 0;
    virtual bool isCaptureRunning() const = 0;

    // 唤醒词回调
    using WakewordCb = std::function<void(const int16_t* pcm, size_t samples)>;
    virtual void setWakewordCallback(WakewordCb cb) = 0;

    // AI 流回调（发送到 WebSocket）
    using CaptureCb = std::function<void(const AudioFrameView& frame)>;
    virtual void setCaptureCallback(CaptureCb cb) = 0;

    // 音量
    virtual void setVolume(uint8_t vol) = 0;
    virtual uint8_t getVolume() const = 0;
};

} // namespace app
```

### 4.2 IVideoService

```cpp
// app/interfaces/ivideo_service.hpp
#pragma once
#include <string>

namespace app {

// 视频主状态（用于 MCP 工具）
enum class VideoMainState { NONE, PHOTO, RECORD };

struct VideoError {
    int code;  // 0=成功
    std::string message;
};

class IVideoService {
public:
    virtual ~IVideoService() = default;

    // AI 识图
    virtual void setExplainUrl(const std::string& url, const std::string& token = "") = 0;
    virtual std::string explainImage(const std::string& question) = 0;

    // 拍照
    virtual VideoError takePhoto(const std::string& filename, bool with_explain = false) = 0;
    virtual bool isPhotoCapturing() const = 0;

    // 录像
    virtual VideoError startRecord(const std::string& filename, int duration_sec = 0) = 0;
    virtual VideoError stopRecord() = 0;
    virtual bool isRecording() const = 0;

    // 流控制
    virtual bool isStreaming() const = 0;
    virtual VideoError startStream() = 0;
    virtual VideoMainState getMainState() const = 0;
    virtual VideoError setMainState(VideoMainState state) = 0;
};

} // namespace app
```

### 4.3 INetworkService

```cpp
// app/interfaces/inetwork_service.hpp
#pragma once
#include <string>
#include <vector>

namespace app {

struct WifiConnectionInfo {
    std::string ssid;
    int signal_strength;  // dBm
    std::string ip_address;
};

struct WifiNetwork {
    std::string ssid;
    int signal_strength;
    bool is_secured;
};

class INetworkService {
public:
    virtual ~INetworkService() = default;

    virtual bool getConnectionInfo(WifiConnectionInfo& info) const = 0;
    virtual bool scan(std::vector<WifiNetwork>& networks) = 0;
};

} // namespace app
```

### 4.4 IHttpClient

```cpp
// app/interfaces/ihttp_client.hpp
#pragma once
#include <string>
#include <map>

namespace app {

struct HttpResponse {
    bool success;
    int status_code;
    std::string body;
    std::string error_message;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    virtual HttpResponse post(const std::string& url, const std::string& body,
                              const std::map<std::string, std::string>& headers,
                              int timeout_ms, bool verify_ssl) = 0;

    virtual bool valid() const = 0;
};

} // namespace app
```

### 4.5 IWebSocketClient

```cpp
// app/interfaces/iwebsocket_client.hpp
#pragma once
#include <string>
#include <functional>
#include <memory>

namespace app {

enum class WsConnectionState { DISCONNECTED, CONNECTING, CONNECTED, HANDSHAKED, CLOSING, CLOSED, ERROR };
enum class WsError { NONE, CONNECTION_FAILED, SEND_FAILED, /* ... */ };

struct WsConfig {
    std::string url;
    std::map<std::string, std::string> headers;
    std::string hello_message;
    bool auto_reconnect = false;
    int connect_timeout_ms = 10000;
    bool verify_ssl = false;
};

using WsTextCb = std::function<bool(const char* data, size_t size)>;
using WsBinaryCb = std::function<bool(const char* data, size_t size)>;
using WsStateCb = std::function<void(WsConnectionState old_s, WsConnectionState new_s)>;
using WsErrorCb = std::function<void(WsError err, const std::string& msg)>;

class IWebSocketClient {
public:
    virtual ~IWebSocketClient() = default;

    virtual WsError connect() = 0;
    virtual void disconnect() = 0;
    virtual WsError sendText(const std::string& msg) = 0;
    virtual WsError sendBinary(const char* data, size_t size) = 0;
    virtual bool isHandshaked() const = 0;

    virtual void setTextCallback(WsTextCb cb) = 0;
    virtual void setBinaryCallback(WsBinaryCb cb) = 0;
    virtual void setStateCallback(WsStateCb cb) = 0;
    virtual void setErrorCallback(WsErrorCb cb) = 0;
};

} // namespace app
```

### 4.6 IDeviceInfo

```cpp
// app/interfaces/idevice_info.hpp
#pragma once
#include <string>

namespace app {

class IDeviceInfo {
public:
    virtual ~IDeviceInfo() = default;

    virtual std::string getDeviceId() const = 0;   // MAC 地址
    virtual std::string getClientId() const = 0;   // UUID
};

} // namespace app
```

### 4.7 IWebRTCService（可选）

```cpp
// app/interfaces/iwebrtc_service.hpp
#pragma once

namespace app {

class IWebRTCService {
public:
    virtual ~IWebRTCService() = default;
    virtual bool isOpen() const = 0;
    virtual int getState() const = 0;
    // ...
};

class ISignalingService {
public:
    virtual ~ISignalingService() = default;
    virtual bool isConnected() const = 0;
    virtual bool connect() = 0;
    virtual bool joinRoom() = 0;
    // ...
};

} // namespace app
```

---

## 五、各模块解耦改造

### 5.1 ChatbotSystem

| 原依赖 | 替换为 |
|-------|--------|
| `AudioSystem*` | `IAudioService*` |
| `VideoSystem*` | `IVideoService*` |
| `WifiManager*` | `INetworkService*`（可选） |
| `Signaling*` | `ISignalingService*`（可选） |
| `WebRTCSystem*` | `IWebRTCService*`（可选） |
| `tool::mac` | `IDeviceInfo*` |
| `tool::uuid` | `IDeviceInfo*` |
| `WebSocketClient` | `IWebSocketClient*`（工厂创建或注入） |

### 5.2 Activation (DeviceActivation)

| 原依赖 | 替换为 |
|-------|--------|
| `protocol::http::HttpClient` | `IHttpClient*`（工厂或注入） |

### 5.3 McpToolManager

| 原依赖 | 替换为 |
|-------|--------|
| `AudioSystem*` | `IAudioService*` |
| `VideoSystem*` | `IVideoService*` |
| `WifiManager*` | `INetworkService*` |
| `Signaling*` | `ISignalingService*`（可选） |
| `WebRTCSystem*` | `IWebRTCService*`（可选） |
| `ChatbotSystem*` | 保持（chatbot 内部类型） |

### 5.4 McpServer (VisionConfigCallback)

| 原依赖 | 替换为 |
|-------|--------|
| `video_system->setExplainUrl()` | 通过 `IVideoService*` 注入，McpServer 持有 `std::function<void(url, token)>` |

### 5.5 Wakeword / ProtocolHandle / MCP 核心

- **wakeword**：仅依赖 snowboy、tool/log，无 media 依赖，可保持现状
- **protocol_handle**：仅依赖 common、tool/log，可保持现状
- **mcp**：仅依赖 common、tool/log，可保持现状

---

## 六、保留的直接依赖

以下依赖可保留，不强制抽象：

| 模块 | 依赖 | 说明 |
|------|------|------|
| 全部 | `tool/log` | 日志工具，基础设施 |
| activation, protocol_handle, mcp | `common` | get_nowus 等，轻量工具 |
| 全部 | `nlohmann/json` | MCP 协议依赖 |
| wakeword | `snowboy` | 第三方唤醒词库 |

---

## 七、实现层 (impl)

在 `app/impl/` 中实现各接口，依赖 media、network、protocol：

```
impl/
├── audio_service_impl.hpp/cc    → 用 AudioDrv 实现 IAudioService
├── video_service_impl.hpp/cc    → 用 CameraDrv 实现 IVideoService
├── network_service_impl.hpp/cc  → 用 WifiManager 实现 INetworkService
├── http_client_impl.hpp/cc      → 用 HttpClient 实现 IHttpClient
├── websocket_client_impl.hpp/cc → 用 WebSocketClient 实现 IWebSocketClient
└── device_info_impl.hpp/cc      → 用 tool::mac、tool::uuid 实现 IDeviceInfo
```

---

## 八、组装层 (main)

```cpp
// main.cpp 伪代码
int main() {
    // 创建具体实现
    auto audio_drv = std::make_unique<AudioDrv>();
    auto camera_drv = std::make_unique<CameraDrv>();
    auto wifi_mgr = std::make_unique<WifiManager>();
    // ...

    auto audio_svc = std::make_unique<AudioServiceImpl>(audio_drv.get());
    auto video_svc = std::make_unique<VideoServiceImpl>(camera_drv.get());
    auto network_svc = std::make_unique<NetworkServiceImpl>(wifi_mgr.get());
    auto http_svc = std::make_unique<HttpClientImpl>();
    auto device_info = std::make_unique<DeviceInfoImpl>("./system_para.conf");

    ChatbotConfig cfg;
    ChatbotSystem chatbot(cfg);

    chatbot.setAudioService(audio_svc.get());
    chatbot.setVideoService(video_svc.get());
    chatbot.setNetworkService(network_svc.get());
    chatbot.setDeviceInfo(device_info.get());

    // Activation 需要 IHttpClient
    // 通过 ChatbotConfig 或 ChatbotSystem::setHttpClient 注入

    chatbot.init();
    // ...
}
```

---

## 九、CMake 调整

```
# app/CMakeLists.txt

# 1. 接口库（无实现）
add_library(app_interfaces INTERFACE)
target_include_directories(app_interfaces INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interfaces)

# 2. 实现库（依赖 media、network、protocol）
add_library(app_impl STATIC impl/*.cc)
target_link_libraries(app_impl app_interfaces media network protocol ...)

# 3. chatbot 库（只依赖 app_interfaces）
add_library(app_chatbot STATIC chatbot/*.cc ...)
target_link_libraries(app_chatbot app_interfaces ...)

# 4. 主程序（链接 app_impl + app_chatbot）
add_executable(main main.cpp)
target_link_libraries(main app_chatbot app_impl ...)
```

---

## 十、实施顺序

1. **阶段 1**：创建 `app/interfaces/`，定义所有接口
2. **阶段 2**：创建 `app/impl/`，实现各接口
3. **阶段 3**：改造 activation，注入 IHttpClient
4. **阶段 4**：改造 chatbot，注入 IAudioService、IVideoService、IDeviceInfo、IWebSocketClient
5. **阶段 5**：改造 mcp_tool，注入 IAudioService、IVideoService、INetworkService
6. **阶段 6**：移除 chatbot 对 media、network、protocol 的 include
7. **阶段 7**：更新 main、CMake 完成组装

---

## 十一、解耦效果

| 维度 | 效果 |
|------|------|
| **编译** | chatbot 不依赖 media，media 变更不触发 chatbot 重编 |
| **测试** | 可用 Mock 实现接口，对 chatbot 做单元测试 |
| **替换** | 可替换不同音频/视频/网络实现，无需改 chatbot |
| **分层** | 依赖方向清晰：chatbot → interfaces ← impl |
| **可移植** | 接口稳定，实现可针对不同平台替换 |
