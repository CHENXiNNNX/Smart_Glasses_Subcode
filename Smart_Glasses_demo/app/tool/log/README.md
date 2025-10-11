# 异步日志管理系统

## 📖 简介

这是一个专为智能眼镜项目设计的**高性能异步日志系统**，具有以下特点：

- ✅ **异步写入** - 使用环形缓冲区和后台线程，不阻塞业务逻辑
- ✅ **线程安全** - 基于原子操作的无锁设计
- ✅ **多日志级别** - DEBUG/INFO/WARN/ERROR，支持运行时级别过滤
- ✅ **彩色输出** - 终端彩色日志，便于快速定位问题
- ✅ **文件轮转** - 双文件策略，自动轮转，总大小控制在10MB以内
- ✅ **格式化输出** - 支持 printf 风格的格式化
- ✅ **模块标签** - 支持 [WebSocket]、[AIManager] 等标签，便于区分
- ✅ **零依赖** - 纯C++实现，不依赖第三方日志库
- ✅ **简单易用** - 单行宏接口，无需手动初始化

---

## 🚀 快速开始

### 1. 包含头文件

```cpp
#include "log.h"

using namespace glasses::tool::logger;
```

### 2. 初始化日志系统

```cpp
int main() {
    // 配置日志系统（可选，使用默认配置）
    LogConfig config;
    config.enable_console = true;           // 启用控制台输出
    config.enable_file = true;              // 启用文件输出
    config.enable_color = true;             // 启用彩色输出
    config.log_file_path = "./log/smart_glasses.log";  // 日志文件路径
    config.max_file_size = 5 * 1024 * 1024; // 5MB
    config.min_level = LogLevel::DEBUG;     // 最小日志级别
    
    // 初始化
    Logger::getInstance().initialize(config);
    
    // 你的代码...
    
    // 关闭（可选，析构函数会自动调用）
    Logger::getInstance().shutdown();
    
    return 0;
}
```

### 3. 使用日志宏

#### 方式一：USER_LOG 宏（按需求设计）

```cpp
USER_LOG("INFO", "WebSocket", "Connected to %s", url.c_str());
USER_LOG("DEBUG", "AIManager", "Processing audio frame: %d bytes", size);
USER_LOG("WARN", "Audio", "Buffer is %d%% full", percentage);
USER_LOG("ERROR", "Network", "Connection failed: %s", error_msg.c_str());
```

#### 方式二：快捷宏（推荐）

```cpp
LOG_DEBUG("WebSocket", "Sending message #%d", count);
LOG_INFO("AIManager", "AI Manager initialized");
LOG_WARN("Audio", "High latency detected: %d ms", latency);
LOG_ERROR("Network", "Failed to connect: %s", strerror(errno));
```

---

## 📊 日志格式

### 控制台输出（彩色）

**默认格式（不显示线程ID）：**
```
[2025-01-11 15:30:45.123456] [DEBUG] [WebSocket] Connecting to server...
[2025-01-11 15:30:45.234567] [INFO ] [AIManager] Device-Id: 00:0c:29:bd:43:05
[2025-01-11 15:30:45.345678] [WARN ] [Audio] Buffer is 85% full
[2025-01-11 15:30:45.456789] [ERROR] [Network] Connection timeout
```

**开启线程ID后：**
```
[2025-01-11 15:30:45.123456] [DEBUG] [WebSocket] [T1234] Connecting to server...
[2025-01-11 15:30:45.234567] [INFO ] [AIManager] [T1234] Device-Id: 00:0c:29:bd:43:05
[2025-01-11 15:30:45.345678] [WARN ] [Audio] [T5678] Buffer is 85% full
[2025-01-11 15:30:45.456789] [ERROR] [Network] [T9012] Connection timeout
```

### 文件输出（无颜色）

```
[2025-01-11 15:30:45.123456] [DEBUG] [WebSocket] Connecting to server...
[2025-01-11 15:30:45.234567] [INFO ] [AIManager] Device-Id: 00:0c:29:bd:43:05
```

**格式说明：**
- `[时间戳]` - 精确到微秒（可选，默认显示）
- `[级别]` - DEBUG/INFO/WARN/ERROR（带彩色）
- `[模块标签]` - 模块名称（带彩色）
- `[线程ID]` - 线程ID的hash值（可选，默认隐藏）
- 日志内容

**关闭时间戳后（适合系统时间不准确的情况）：**
```
[DEBUG] [WebSocket] Connecting to server...
[INFO ] [AIManager] Device-Id: 00:0c:29:bd:43:05
[WARN ] [Audio] Buffer is 85% full
[ERROR] [Network] Connection timeout
```

---

## 🎨 日志级别和颜色

| 级别 | 用途 | 终端颜色 | 示例 |
|------|------|---------|------|
| **DEBUG** | 调试信息 | 🔵 蓝色 | `LOG_DEBUG("Module", "Debug info")` |
| **INFO** | 普通信息 | ⚪ 白色 | `LOG_INFO("Module", "Operation completed")` |
| **WARN** | 警告信息 | 🟡 黄色 | `LOG_WARN("Module", "Buffer nearly full")` |
| **ERROR** | 错误信息 | 🔴 红色 | `LOG_ERROR("Module", "Connection failed")` |

---

## 📁 文件轮转策略

### 双文件轮转

系统使用双文件轮转策略，始终保持总大小在 **10MB** 以内：

```
log/
├── smart_glasses.log       # 当前日志文件（0-5MB）
└── smart_glasses.log.old   # 备份日志文件（0-5MB）
```

**注意**：日志系统会自动创建 `log` 目录（如果不存在）。

### 轮转流程

当 `smart_glasses.log` 达到 **5MB** 时：

1. 删除旧的 `smart_glasses.log.old`（如果存在）
2. 将 `smart_glasses.log` 重命名为 `smart_glasses.log.old`
3. 创建新的 `smart_glasses.log` 继续写入

### 文件头示例

```
========================================
  Smart Glasses Log File
  Start Time: 2025-01-11 15:30:45
========================================
```

---

## ⚙️ 配置选项

### LogConfig 结构体

```cpp
struct LogConfig {
    bool enable_console;        // 是否输出到控制台（默认：true）
    bool enable_file;           // 是否输出到文件（默认：true）
    bool enable_color;          // 是否启用彩色输出（默认：true）
    bool enable_timestamp;      // 是否显示时间戳（默认：true）
    bool enable_thread_id;      // 是否显示线程ID（默认：false）
    std::string log_file_path;  // 日志文件路径（默认："./log/smart_glasses.log"）
    size_t max_file_size;       // 单文件最大大小（默认：5MB）
    size_t buffer_size;         // 环形缓冲区大小（默认：8192条消息）
    LogLevel min_level;         // 最小日志级别（默认：DEBUG）
};
```

### 运行时配置

```cpp
Logger& logger = Logger::getInstance();

// 设置最小日志级别（低于此级别的日志将被过滤）
logger.setMinLevel(LogLevel::INFO);

// 禁用控制台输出（只写文件）
logger.setConsoleEnabled(false);

// 禁用文件输出（只打印到控制台）
logger.setFileEnabled(false);

// 禁用彩色输出（适合非终端环境）
logger.setColorEnabled(false);

// 启用/禁用时间戳显示
logger.setTimestampEnabled(true);   // 显示时间戳（默认）
logger.setTimestampEnabled(false);  // 隐藏时间戳

// 启用/禁用线程ID显示
logger.setThreadIdEnabled(true);   // 显示线程ID
logger.setThreadIdEnabled(false);  // 隐藏线程ID（默认）

// 手动刷新缓冲区
logger.flush();
```

---

## 🔧 集成到项目

### 1. 更新 CMakeLists.txt

在主 CMakeLists.txt 中添加：

```cmake
# 添加 log 子目录
add_subdirectory(app/tool/log)

# 链接到主程序
target_link_libraries(main log)
```

### 2. 替换现有的 std::cout

#### 原代码：

```cpp
std::cout << "[WebSocket] Connection opened" << std::endl;
std::cerr << "[AIManager] ✗ Failed to initialize" << std::endl;
```

#### 新代码：

```cpp
LOG_INFO("WebSocket", "Connection opened");
LOG_ERROR("AIManager", "Failed to initialize");
```

### 3. 在各个模块中使用

#### WebSocket 模块：

```cpp
#include "log.h"

LOG_INFO("WebSocket", "Connecting to %s", url.c_str());
LOG_DEBUG("WebSocket", "Sending binary data: %zu bytes", size);
LOG_WARN("WebSocket", "Connection timeout, reconnecting...");
LOG_ERROR("WebSocket", "Failed to send: %s", error.c_str());
```

#### AIManager 模块：

```cpp
#include "log.h"

LOG_INFO("AIManager", "Initializing AI Manager...");
LOG_DEBUG("AIManager", "Device-Id: %s", device_id.c_str());
LOG_INFO("AIManager", "AI Manager initialized successfully!");
```

---

## 🎯 使用场景示例

### 1. WebSocket 连接日志

```cpp
LOG_INFO("WebSocket", "Connecting to wss://api.tenclass.net/xiaozhi/v1/");
LOG_DEBUG("WebSocket", "Setting headers: Device-Id=%s", device_id.c_str());

if (connect_failed) {
    LOG_ERROR("WebSocket", "Connection failed: %s", error_msg.c_str());
    LOG_WARN("WebSocket", "Retrying in %d ms", retry_interval);
} else {
    LOG_INFO("WebSocket", "Connection established");
    LOG_DEBUG("WebSocket", "Sending hello message");
}
```

### 2. 音频处理日志

```cpp
LOG_INFO("Audio", "Initializing audio system...");
LOG_DEBUG("Audio", "Sample rate: %d Hz, Channels: %d", 48000, 1);

if (init_result == AUDIO_ERROR_NONE) {
    LOG_INFO("Audio", "Audio system initialized");
} else {
    LOG_ERROR("Audio", "Failed to initialize: error code %d", init_result);
}

LOG_DEBUG("Audio", "Processing frame: %d samples", frame_size);
LOG_WARN("Audio", "Buffer usage: %d%%", buffer_percentage);
```

### 3. AI 状态机日志

```cpp
LOG_DEBUG("StateMachine", "State: %s → %s", 
          old_state_str.c_str(), new_state_str.c_str());

LOG_INFO("StateMachine", "Wakeword detected, entering LISTENING state");
LOG_DEBUG("StateMachine", "Audio upload: %s", enabled ? "enabled" : "disabled");
```

### 4. 多线程日志

```cpp
// 线程1
LOG_INFO("Thread1", "Worker thread started");

// 线程2
LOG_INFO("Thread2", "Processing task #%d", task_id);

// 线程3
LOG_WARN("Thread3", "Queue is getting full: %d/%d", current, max);
```

日志系统会自动添加线程ID，便于追踪多线程问题。

---

## 🛠️ 性能特性

### 1. 异步写入

- 日志消息先写入**环形缓冲区**（无锁操作）
- 后台线程批量写入文件和控制台
- **不会阻塞**业务线程

### 2. 环形缓冲区

- 默认大小：**8192** 条消息
- 使用原子操作实现无锁队列
- 缓冲区满时自动丢弃（输出警告）

### 3. 批量写入

- 后台线程每 **100ms** 批量处理消息
- 减少系统调用次数
- 提高文件写入效率

### 4. 零拷贝优化

- 环形缓冲区直接存储 `LogMessage` 对象
- 避免不必要的字符串拷贝

---

## 🐛 调试技巧

### 1. 临时提高日志级别

```cpp
// 临时启用 DEBUG 级别日志
Logger::getInstance().setMinLevel(LogLevel::DEBUG);

// 执行需要调试的代码
someFunction();

// 恢复 INFO 级别
Logger::getInstance().setMinLevel(LogLevel::INFO);
```

### 2. 只输出到文件（减少控制台干扰）

```cpp
Logger::getInstance().setConsoleEnabled(false);
```

### 3. 禁用彩色输出（适合重定向）

```cpp
Logger::getInstance().setColorEnabled(false);
```

### 4. 手动刷新缓冲区

```cpp
// 确保关键日志立即写入文件
LOG_ERROR("Critical", "Fatal error occurred!");
Logger::getInstance().flush();
```

---

## 📦 编译和运行示例

### 编译

```bash
cd /home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo
mkdir -p build && cd build
cmake .. -DBUILD_LOG_EXAMPLE=ON
make log_example
```

### 运行

```bash
cd ../bin
./log_example
```

### 查看日志文件

```bash
cat log/smart_glasses.log
tail -f log/smart_glasses.log  # 实时查看
```

---

## 📝 注意事项

### 1. 初始化时机

- 在 `main()` 函数开始时初始化
- 确保在任何日志输出之前调用 `initialize()`

### 2. 线程安全

- 日志系统**完全线程安全**
- 可以在任意线程中调用日志宏
- 无需额外加锁

### 3. 缓冲区满处理

- 缓冲区满时会**丢弃新消息**
- 每100条丢弃输出一次警告
- 如果频繁丢弃，考虑增大 `buffer_size`

### 4. 文件权限

- 确保日志文件路径可写
- 如果无法创建文件，会输出错误到 `stderr`

### 5. 性能影响

- 异步设计，对业务线程影响极小
- 单条日志调用耗时 < **1微秒**
- 适合高频日志场景（如音频帧处理）

---

## 🔍 常见问题

### Q1: 日志没有输出？

**A:** 检查以下几点：
1. 是否调用了 `Logger::getInstance().initialize()`？
2. 日志级别是否低于 `min_level`？
3. 文件路径是否有写权限？

### Q2: 日志输出延迟？

**A:** 这是正常的异步行为。可以：
1. 调用 `Logger::getInstance().flush()` 强制刷新
2. 降低后台线程的 sleep 时间（修改源码）

### Q3: 如何禁用 DEBUG 日志？

**A:** 
```cpp
Logger::getInstance().setMinLevel(LogLevel::INFO);
```

### Q4: 如何查看历史日志？

**A:** 查看 `log/smart_glasses.log.old` 文件

### Q5: 日志文件太大？

**A:** 调整 `max_file_size` 配置：
```cpp
config.max_file_size = 2 * 1024 * 1024;  // 改为 2MB
```

---

## 📚 API 参考

### 宏定义

| 宏 | 说明 | 示例 |
|----|------|------|
| `USER_LOG(level, tag, format, ...)` | 通用日志宏 | `USER_LOG("INFO", "Tag", "msg")` |
| `LOG_DEBUG(tag, format, ...)` | DEBUG 日志 | `LOG_DEBUG("Tag", "msg")` |
| `LOG_INFO(tag, format, ...)` | INFO 日志 | `LOG_INFO("Tag", "msg")` |
| `LOG_WARN(tag, format, ...)` | WARN 日志 | `LOG_WARN("Tag", "msg")` |
| `LOG_ERROR(tag, format, ...)` | ERROR 日志 | `LOG_ERROR("Tag", "msg")` |

### Logger 类方法

| 方法 | 说明 |
|------|------|
| `getInstance()` | 获取单例 |
| `initialize(config)` | 初始化日志系统 |
| `shutdown()` | 关闭日志系统 |
| `log(level, tag, format, ...)` | 写入格式化日志 |
| `setMinLevel(level)` | 设置最小日志级别 |
| `setConsoleEnabled(bool)` | 启用/禁用控制台输出 |
| `setFileEnabled(bool)` | 启用/禁用文件输出 |
| `setColorEnabled(bool)` | 启用/禁用彩色输出 |
| `flush()` | 刷新缓冲区 |
| `isRunning()` | 检查是否运行中 |

---

## 📄 许可证

本代码为智能眼镜项目内部使用，遵循项目许可证。

---

## 👥 联系方式

如有问题或建议，请联系 Smart_Glasses Team。

