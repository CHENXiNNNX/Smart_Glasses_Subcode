# VideoSystem 使用文档

## 目录

1. [系统概述](#系统概述)
2. [架构设计](#架构设计)
3. [初始化流程](#初始化流程)
4. [拍照功能](#拍照功能)
5. [录像功能](#录像功能)
6. [状态管理](#状态管理)
7. [错误处理](#错误处理)
8. [示例代码](#示例代码)

---

## 系统概述

`VideoSystem` 是一个基于 RKMPI（Rockchip Media Process Interface）的现代 C++ 视频系统，提供以下核心功能：

- **视频采集**：通过 VI（Video Input）模块从摄像头采集视频
- **硬件编码**：支持 H.264、H.265、JPEG 编码格式
- **拍照功能**：支持 JPEG 格式照片捕获和保存
- **录像功能**：支持 H.264 格式视频录制
- **WebRTC 推流**：支持实时视频流推送
- **ISP 控制**：支持亮度、对比度、饱和度、锐度等参数动态调整
- **AI 图像解析**：支持将图像上传到 AI 服务器进行分析

### 核心特性

- ✅ **RAII 资源管理**：自动管理资源生命周期
- ✅ **智能指针**：无裸指针，内存安全
- ✅ **三级内存池**：固定池 + 动态池 + DMA 池，高效内存管理
- ✅ **线程安全**：使用互斥锁和原子变量保证并发安全
- ✅ **状态机管理**：清晰的状态转换逻辑
- ✅ **硬件加速**：基于 RKMPI 硬件编码，性能优异

---

## 架构设计

### 类层次结构

```
VideoSystem (主类)
├── VideoSystem::Impl (Pimpl 实现)
│   ├── ISPWrapper (ISP/AIQ 包装器)
│   ├── VIDeviceWrapper (VI 设备包装器)
│   ├── VIChannelWrapper (VI 通道包装器)
│   ├── VENCWrapper (编码器包装器)
│   ├── VideoMemoryPool (三级内存池)
│   └── FileWrapper (文件操作包装器)
└── VideoConfig (配置结构体)
```

### 数据流

```
摄像头传感器
    ↓
ISP (图像信号处理)
    ↓
VI (视频输入) → VI设备 → VI通道
    ↓
VENC (视频编码) → H.264/H.265/JPEG
    ↓
流处理线程 → 根据主状态分发
    ├── PHOTO → handlePhotoFrame() → 保存 JPEG
    ├── RECORD → handleRecordFrame() → 保存 H.264
    └── WEBRTC → handleWebRTCFrame() → 推流回调
```

### 状态机

系统使用 `VideoMainState` 枚举管理主状态：

```cpp
enum class VideoMainState {
    NONE = 0,   // 空闲状态
    PHOTO,      // 拍照模式
    RECORD,     // 录像模式
    WEBRTC      // WebRTC推流模式
};
```

**状态转换规则**：
- `NONE` → `PHOTO`：开始拍照
- `NONE` → `RECORD`：开始录像
- `NONE` → `WEBRTC`：开始推流
- 任何状态 → `NONE`：停止当前功能

---

## 初始化流程

### 步骤 1：创建配置对象

```cpp
#include "app/media/camera/camera.hpp"

using namespace app::media::camera;

// 使用默认配置
VideoConfig config;

// 或自定义配置
VideoConfig config;
config.width  = 1920;              // 图像宽度
config.height = 1080;               // 图像高度
config.fps    = 30;                 // 帧率
config.format = EncodeFormat::H264; // 编码格式
config.bitrate = 10 * 1024;         // 码率 (kbps)
config.gop     = 10;                // GOP 大小

// 拍照参数
config.photo_path = "/root/picture/";        // 照片保存路径
config.photo_capture_frames = 5;            // 拍照采集帧数（取最后一帧）

// 录像参数
config.record_path = "/root/video/";         // 录像保存路径
config.record_duration_sec = 15;             // 默认录像时长（秒）
```

### 步骤 2：创建 VideoSystem 实例

```cpp
// 使用配置创建 VideoSystem
VideoSystem video_system(config);
```

### 步骤 3：初始化系统

```cpp
// 初始化视频系统（可选：传入时间同步上下文）
VideoError err = video_system.initialize();
if (err != VideoError::NONE) {
    LOG_ERROR("App", "视频系统初始化失败: %d", static_cast<int>(err));
    return -1;
}
```

**初始化过程**（内部自动执行）：
1. 初始化 RKMPI 系统
2. 初始化 ISP（图像信号处理）
3. 初始化 VI 设备
4. 初始化 VI 通道
5. 初始化 VENC 编码器
6. 绑定 VI 到 VENC
7. 创建输出目录（照片和录像）

### 步骤 4：启动视频流（可选）

```cpp
// 启动视频流处理线程
err = video_system.startStream();
if (err != VideoError::NONE) {
    LOG_ERROR("App", "启动视频流失败: %d", static_cast<int>(err));
    return -1;
}
```

**注意**：
- 拍照和录像功能**必须**先启动视频流
- 视频流启动后会创建一个后台线程持续处理视频帧
- 根据 `VideoMainState` 状态决定如何处理每一帧

### 完整初始化示例

```cpp
#include "app/media/camera/camera.hpp"
#include "app/tool/log/log.hpp"

using namespace app::media::camera;
using namespace app::tool::log;

int main() {
    // 1. 创建配置
    VideoConfig config;
    config.width  = 1920;
    config.height = 1080;
    config.fps    = 30;
    config.format = EncodeFormat::H264;
    
    // 2. 创建系统
    VideoSystem video_system(config);
    
    // 3. 初始化
    VideoError err = video_system.initialize();
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "初始化失败");
        return -1;
    }
    
    // 4. 启动流（拍照/录像需要）
    err = video_system.startStream();
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "启动流失败");
        video_system.shutdown();
        return -1;
    }
    
    LOG_INFO("App", "视频系统初始化完成");
    
    // ... 使用拍照/录像功能 ...
    
    // 5. 清理资源
    video_system.stopStream();
    video_system.shutdown();
    
    return 0;
}
```

---

## 拍照功能

### 基本用法

#### 1. 简单拍照（自动生成文件名）

```cpp
// 设置主状态为 PHOTO（必须）
VideoError err = video_system.setMainState(VideoMainState::PHOTO);
if (err != VideoError::NONE) {
    LOG_ERROR("App", "设置主状态失败");
    return;
}

// 开始拍照（自动生成文件名，自动切换编码器）
err = video_system.takePhoto();
if (err != VideoError::NONE) {
    LOG_ERROR("App", "拍照失败: %d", static_cast<int>(err));
    return;
}

// 等待拍照完成
while (video_system.isPhotoCapturing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// 恢复主状态
video_system.setMainState(VideoMainState::NONE);
```

#### 2. 指定文件名拍照

```cpp
// 设置主状态
video_system.setMainState(VideoMainState::PHOTO);

// 指定文件名拍照
VideoError err = video_system.takePhoto("/root/picture/my_photo.jpg");
if (err != VideoError::NONE) {
    LOG_ERROR("App", "拍照失败");
    return;
}

// 等待完成
while (video_system.isPhotoCapturing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// 恢复状态
video_system.setMainState(VideoMainState::NONE);
```

#### 3. 不切换编码器拍照（当前已是 JPEG 编码器）

```cpp
video_system.setMainState(VideoMainState::PHOTO);

// switch_encoder=false 表示不切换编码器
VideoError err = video_system.takePhoto("", false);
if (err != VideoError::NONE) {
    LOG_ERROR("App", "拍照失败");
    return;
}

// 等待完成
while (video_system.isPhotoCapturing()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

video_system.setMainState(VideoMainState::NONE);
```

### 工作原理

1. **编码器切换**：
   - 如果当前是 H.264 编码器，`takePhoto(..., true)` 会临时切换到 JPEG 编码器
   - 拍照完成后，如果之前切换了编码器，需要调用 `restoreH264Encoder()` 恢复

2. **帧采集**：
   - 系统会采集 `photo_capture_frames` 帧（默认 5 帧）
   - 只保存最后一帧作为最终照片

3. **异步处理**：
   - `takePhoto()` 是异步的，立即返回
   - 通过 `isPhotoCapturing()` 检查是否完成
   - 实际保存操作在流处理线程的 `handlePhotoFrame()` 中完成

### 完整拍照示例

```cpp
bool takePhotoSafely(VideoSystem& video_system, const std::string& filename) {
    // 1. 确保流已启动
    if (!video_system.isStreaming()) {
        VideoError err = video_system.startStream();
        if (err != VideoError::NONE) {
            LOG_ERROR("App", "启动流失败");
            return false;
        }
        // 等待流稳定
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    // 2. 保存当前状态
    VideoMainState old_state = video_system.getMainState();
    
    // 3. 设置主状态为 PHOTO
    VideoError err = video_system.setMainState(VideoMainState::PHOTO);
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "设置主状态失败");
        return false;
    }
    
    // 4. 开始拍照
    err = video_system.takePhoto(filename, true);
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "拍照失败: %d", static_cast<int>(err));
        video_system.setMainState(old_state); // 恢复状态
        return false;
    }
    
    // 5. 等待拍照完成（最多等待 5 秒）
    const int max_wait_ms = 5000;
    const int check_interval_ms = 100;
    int waited_ms = 0;
    
    while (video_system.isPhotoCapturing() && waited_ms < max_wait_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        waited_ms += check_interval_ms;
    }
    
    // 6. 检查是否超时
    if (video_system.isPhotoCapturing()) {
        LOG_ERROR("App", "拍照超时");
        video_system.setMainState(old_state);
        return false;
    }
    
    // 7. 恢复主状态
    video_system.setMainState(old_state);
    
    LOG_INFO("App", "拍照成功: %s", filename.c_str());
    return true;
}
```

---

## 录像功能

### 基本用法

#### 1. 开始录像（手动停止）

```cpp
// 设置主状态为 RECORD（必须）
VideoError err = video_system.setMainState(VideoMainState::RECORD);
if (err != VideoError::NONE) {
    LOG_ERROR("App", "设置主状态失败");
    return;
}

// 开始录像（自动生成文件名，手动停止）
err = video_system.startRecord();
if (err != VideoError::NONE) {
    LOG_ERROR("App", "开始录像失败: %d", static_cast<int>(err));
    return;
}

// ... 录像进行中 ...

// 停止录像
err = video_system.stopRecord();
if (err != VideoError::NONE) {
    LOG_ERROR("App", "停止录像失败");
    return;
}

// 恢复主状态
video_system.setMainState(VideoMainState::NONE);
```

#### 2. 指定文件名录像

```cpp
video_system.setMainState(VideoMainState::RECORD);

// 指定文件名
VideoError err = video_system.startRecord("/root/video/my_video.h264");
if (err != VideoError::NONE) {
    LOG_ERROR("App", "开始录像失败");
    return;
}

// ... 录像进行中 ...

video_system.stopRecord();
video_system.setMainState(VideoMainState::NONE);
```

#### 3. 定时录像（自动停止）

```cpp
video_system.setMainState(VideoMainState::RECORD);

// 录像 10 秒后自动停止
VideoError err = video_system.startRecord("", 10);
if (err != VideoError::NONE) {
    LOG_ERROR("App", "开始录像失败");
    return;
}

// 等待录像完成（或手动检查）
while (video_system.isRecording()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// 恢复状态
video_system.setMainState(VideoMainState::NONE);
```

### 工作原理

1. **文件格式**：
   - 录像文件保存为 H.264 格式（`.h264` 扩展名）
   - 需要播放器支持 H.264 裸流（如 VLC、ffplay）

2. **帧写入**：
   - 流处理线程在 `handleRecordFrame()` 中持续写入视频帧
   - 每写入一帧都会刷新文件缓冲区

3. **时长控制**：
   - 如果 `duration_sec > 0`，系统会在达到时长后自动停止
   - 如果 `duration_sec = 0`，需要手动调用 `stopRecord()` 停止

### 完整录像示例

```cpp
bool recordVideoSafely(VideoSystem& video_system, 
                       const std::string& filename, 
                       int duration_sec = 0) {
    // 1. 确保流已启动
    if (!video_system.isStreaming()) {
        VideoError err = video_system.startStream();
        if (err != VideoError::NONE) {
            LOG_ERROR("App", "启动流失败");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    // 2. 检查是否正在录像
    if (video_system.isRecording()) {
        LOG_WARN("App", "正在录像中，请先停止");
        return false;
    }
    
    // 3. 保存当前状态
    VideoMainState old_state = video_system.getMainState();
    
    // 4. 设置主状态为 RECORD
    VideoError err = video_system.setMainState(VideoMainState::RECORD);
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "设置主状态失败");
        return false;
    }
    
    // 5. 开始录像
    err = video_system.startRecord(filename, duration_sec);
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "开始录像失败: %d", static_cast<int>(err));
        video_system.setMainState(old_state);
        return false;
    }
    
    LOG_INFO("App", "录像已开始: %s", filename.empty() ? "(自动生成)" : filename.c_str());
    
    // 6. 如果设置了时长，等待自动停止；否则返回，由调用者手动停止
    if (duration_sec > 0) {
        while (video_system.isRecording()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        video_system.setMainState(old_state);
        LOG_INFO("App", "录像已自动停止");
    }
    
    return true;
}

// 使用示例：手动停止
void exampleManualStop() {
    VideoSystem video_system;
    video_system.initialize();
    video_system.startStream();
    
    // 开始录像（手动停止）
    recordVideoSafely(video_system, "/root/video/test.h264", 0);
    
    // 录像 5 秒后手动停止
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    video_system.stopRecord();
    video_system.setMainState(VideoMainState::NONE);
}

// 使用示例：自动停止
void exampleAutoStop() {
    VideoSystem video_system;
    video_system.initialize();
    video_system.startStream();
    
    // 录像 10 秒后自动停止
    recordVideoSafely(video_system, "/root/video/test.h264", 10);
    
    // 函数返回时录像已自动停止
}
```

---

## 状态管理

### 状态检查

```cpp
// 检查系统状态
bool is_init = video_system.isInitialized();      // 是否已初始化
bool is_streaming = video_system.isStreaming();   // 流是否运行
bool is_photo = video_system.isPhotoCapturing();  // 是否正在拍照
bool is_record = video_system.isRecording();      // 是否正在录像

// 获取当前主状态
VideoMainState state = video_system.getMainState();
```

### 状态转换最佳实践

1. **拍照前**：
   ```cpp
   VideoMainState old_state = video_system.getMainState();
   video_system.setMainState(VideoMainState::PHOTO);
   video_system.takePhoto(...);
   // 等待完成
   video_system.setMainState(old_state);
   ```

2. **录像前**：
   ```cpp
   VideoMainState old_state = video_system.getMainState();
   video_system.setMainState(VideoMainState::RECORD);
   video_system.startRecord(...);
   // ... 录像进行中 ...
   video_system.stopRecord();
   video_system.setMainState(old_state);
   ```

3. **状态恢复**：
   - 始终在操作完成后恢复原始状态
   - 如果操作失败，也要恢复状态，避免系统卡在错误状态

---

## 错误处理

### 错误码枚举

```cpp
enum class VideoError {
    NONE = 0,              // 成功
    INIT_FAILED,           // 初始化失败
    NOT_INITIALIZED,       // 未初始化
    ALREADY_STARTED,       // 已经启动
    NOT_STARTED,           // 未启动
    INVALID_STATE,         // 无效状态
    FILE_OPEN_FAILED,      // 文件打开失败
    RKMPI_ERROR,          // RKMPI 错误
    // ... 其他错误
};
```

### 错误处理示例

```cpp
VideoError err = video_system.takePhoto();
if (err != VideoError::NONE) {
    switch (err) {
        case VideoError::ALREADY_STARTED:
            LOG_WARN("App", "正在拍照中，请稍候");
            break;
        case VideoError::NOT_INITIALIZED:
            LOG_ERROR("App", "系统未初始化");
            break;
        case VideoError::INVALID_STATE:
            LOG_ERROR("App", "系统状态无效");
            break;
        default:
            LOG_ERROR("App", "未知错误: %d", static_cast<int>(err));
            break;
    }
    return false;
}
```

---

## 示例代码

### 完整示例：拍照和录像

```cpp
#include "app/media/camera/camera.hpp"
#include "app/tool/log/log.hpp"
#include <thread>
#include <chrono>

using namespace app::media::camera;
using namespace app::tool::log;

int main() {
    // 1. 配置
    VideoConfig config;
    config.width  = 1920;
    config.height = 1080;
    config.fps    = 30;
    config.format = EncodeFormat::H264;
    config.photo_path = "/root/picture/";
    config.record_path = "/root/video/";
    
    // 2. 创建系统
    VideoSystem video_system(config);
    
    // 3. 初始化
    VideoError err = video_system.initialize();
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "初始化失败");
        return -1;
    }
    
    // 4. 启动流
    err = video_system.startStream();
    if (err != VideoError::NONE) {
        LOG_ERROR("App", "启动流失败");
        video_system.shutdown();
        return -1;
    }
    
    // 5. 拍照示例
    LOG_INFO("App", "开始拍照...");
    VideoMainState old_state = video_system.getMainState();
    video_system.setMainState(VideoMainState::PHOTO);
    
    err = video_system.takePhoto("/root/picture/test.jpg");
    if (err == VideoError::NONE) {
        while (video_system.isPhotoCapturing()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LOG_INFO("App", "拍照完成");
    }
    
    video_system.setMainState(old_state);
    
    // 6. 录像示例
    LOG_INFO("App", "开始录像...");
    old_state = video_system.getMainState();
    video_system.setMainState(VideoMainState::RECORD);
    
    err = video_system.startRecord("/root/video/test.h264", 5); // 录像 5 秒
    if (err == VideoError::NONE) {
        while (video_system.isRecording()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        LOG_INFO("App", "录像完成");
    }
    
    video_system.setMainState(old_state);
    
    // 7. 清理
    video_system.stopStream();
    video_system.shutdown();
    
    LOG_INFO("App", "程序退出");
    return 0;
}
```

---

## 注意事项

1. **线程安全**：
   - `VideoSystem` 是线程安全的，可以在多线程环境中使用
   - 但建议在同一线程中管理状态转换

2. **资源管理**：
   - 使用 RAII，析构函数会自动清理资源
   - 但建议显式调用 `shutdown()` 确保资源及时释放

3. **状态一致性**：
   - 拍照和录像前必须设置正确的 `VideoMainState`
   - 操作完成后必须恢复原始状态

4. **流处理线程**：
   - 拍照和录像功能依赖流处理线程
   - 必须先调用 `startStream()` 启动流

5. **编码器切换**：
   - 从 H.264 切换到 JPEG 拍照需要时间
   - 拍照完成后记得恢复 H.264 编码器（如果之前切换了）

6. **文件路径**：
   - 确保保存路径的目录存在或系统有权限创建
   - 系统会在初始化时自动创建默认目录

---

## 总结

`VideoSystem` 提供了完整的视频采集、编码、拍照、录像功能。通过合理的状态管理和错误处理，可以安全高效地使用这些功能。关键要点：

- ✅ 初始化顺序：配置 → 创建 → 初始化 → 启动流
- ✅ 拍照流程：设置状态 → 拍照 → 等待完成 → 恢复状态
- ✅ 录像流程：设置状态 → 开始录像 → 停止录像 → 恢复状态
- ✅ 状态管理：始终保存和恢复原始状态
- ✅ 错误处理：检查所有返回值，妥善处理错误情况

