# Chatbot 重构 - 集成说明

## 一、新架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│  App 层                                                          │
│  创建 MediaHandles（从 AudioDrv/CameraDrv 填充回调）              │
│  调用 chatbot->set_media_handles(handles)                        │
│  调用 chatbot->set_http_client(http_client)                     │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  ChatbotSystem（编排器）                                          │
│  - 不依赖 AudioDrv/CameraDrv 类型                               │
│  - 通过 MediaHandles 调用媒体能力                                │
│  - 组装 activation、mcp、protocol、websocket、wakeword           │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  MCP + mcp_tool                                                  │
│  - McpServer: JSON-RPC 2.0 处理                                  │
│  - register_tools(server, MediaHandles): 回调式工具注册           │
│  - 无媒体类型依赖                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 二、API 变更

| 旧 API | 新 API |
|--------|--------|
| `setAudioService(AudioService*)` | `set_media_handles(MediaHandles)`，handles 含 `set_volume`、`get_volume` 等 |
| `setVideoService(VideoService*)` | 同上，handles 含 `explain_image`、`save_photo`、`start_record` 等 |
| `setNetworkService(...)` | 暂未实现，可扩展 MediaHandles |
| `setDeviceInfo(...)` | 移除，device_id/client_id 在 init 内通过 mac/uuid 获取 |
| `setHttpClient(...)` | `set_http_client(IHttpClient*)` |
| `setWebSocketFactory(...)` | 移除，WebSocket 在 Chatbot 内部创建 |
| `getState()` | `get_state()` |
| `stateToString(s)` | `state_to_string(s)` |
| `errorToString(e)` | `error_to_string(e)` |

## 三、App 层集成示例

```cpp
// app.cc initChatbot() 中

// 1. 创建 MediaHandles，从 audio_drv_/camera_drv_ 填充
mcp::mcp_tool::MediaHandles handles;

if (audio_drv_ && audio_drv_->is_init()) {
    handles.set_volume = [this](int v) {
        audio_drv_->playback().set_volume(static_cast<uint8_t>(v));
    };
    handles.get_volume = [this]() {
        return static_cast<int>(audio_drv_->playback().volume());
    };
}

if (camera_drv_ && camera_drv_->is_init()) {
    handles.set_explain_url = [this](const std::string& url, const std::string& token) {
        camera_drv_->set_explain_url(url, token);
    };
    handles.explain_image = [this](const std::string& q) {
        return camera_drv_->explain_image(q);
    };
    handles.save_photo = [this](const std::string& path, std::function<void(bool)> cb) {
        camera_drv_->jpeg().save(path, [cb](const std::string&, camera::Error e) {
            if (cb) cb(e == camera::Error::OK);
        });
        return true;
    };
    handles.start_record = [this](const std::string& path, int dur) {
        return camera_drv_->recorder().start(path, dur) == camera::Error::OK;
    };
    handles.stop_record = [this]() { camera_drv_->recorder().stop(); };
    handles.is_recording = [this]() { return camera_drv_->recorder().is_recording(); };
    handles.is_running = [this]() { return camera_drv_->is_running(); };
    handles.start_stream = [this]() {
        return camera_drv_->start() == camera::Error::OK;
    };
    handles.stop_stream = [this]() { camera_drv_->stop(); };
}

// 2. 注入
chatbot_->set_media_handles(handles);
chatbot_->set_http_client(http_svc_.get());

// 3. 初始化
ChatbotError err = chatbot_->init();
if (err != ChatbotError::NONE) {
    LOG_ERROR("APP", "Chatbot init failed: %s", chatbot::error_to_string(err));
    return false;
}
```

## 四、文件清单

| 文件 | 说明 |
|------|------|
| `chatbot/chatbot.hpp` | ChatbotSystem、ChatbotConfig、MediaHandles 注入、state_to_string、error_to_string |
| `chatbot/chatbot.cc` | 编排器实现 |
| `chatbot/mcp/mcp.hpp` | 保留现有 McpServer（未改动） |
| `chatbot/mcp/mcp.cc` | 保留现有实现（未改动） |
| `chatbot/mcp/mcp_tool/mcp_tool.hpp` | MediaHandles、register_tools |
| `chatbot/mcp/mcp_tool/mcp_tool.cc` | 回调式工具注册 |

## 五、mcp copy / chatbot copy

用户提到的 `mcp copy`、`chatbot copy` 若存在，可删除。新设计已合并到主文件。
