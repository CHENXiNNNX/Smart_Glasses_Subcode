# 嵌入式 C/C++ 代码命名规范

## 文档说明

本文档定义了嵌入式 C/C++ 项目的代码命名规范，旨在提高代码可读性、可维护性和团队协作效率。

**版本**: 1.0  
**最后更新**: 2024年  
**适用范围**: 所有嵌入式 C/C++ 项目代码

---

## 目录

1. [基本原则](#基本原则)
2. [变量命名规范](#变量命名规范)
3. [函数命名规范](#函数命名规范)
4. [常量命名规范](#常量命名规范)
5. [宏定义命名规范](#宏定义命名规范)
6. [类型定义命名规范](#类型定义命名规范)
7. [全局变量命名规范](#全局变量命名规范)
8. [静态变量/函数命名规范](#静态变量函数命名规范)
9. [类与结构体命名规范](#类与结构体命名规范)
10. [命名空间命名规范](#命名空间命名规范)
11. [文件命名规范](#文件命名规范)
12. [命名示例](#命名示例)
13. [常见错误与避免](#常见错误与避免)

---

## 基本原则

### 1.1 命名清晰性原则
- **明确含义**：变量名和函数名应清晰表达其用途，避免使用缩写（除非是广泛接受的缩写）
- **避免歧义**：命名不应产生歧义，应能准确反映变量或函数的实际功能
- **长度适中**：命名长度应在可读性和简洁性之间取得平衡

### 1.2 命名一致性原则
- **统一风格**：整个项目应保持统一的命名风格
- **遵循约定**：严格遵守本文档定义的命名规范
- **团队协作**：确保团队成员理解并遵循相同的命名规范

### 1.3 可读性优先原则
- **自解释代码**：好的命名可以减少注释需求
- **避免魔法数字**：使用命名常量替代硬编码数字
- **语义明确**：命名应能清晰表达代码意图

---

## 变量命名规范

### 2.1 局部变量
**规范**: 使用**下划线命名法**（snake_case）

```c
// ✅ 正确示例
int current_temperature;
uint8_t sensor_value;
bool is_connected;
char buffer_data[256];
size_t message_count;

// ❌ 错误示例
int currentTemperature;  // 驼峰命名（不符合规范）
int temp;                 // 缩写不清晰
int t;                    // 单字母变量
```

### 2.2 变量命名要求
- **使用下划线分隔单词**：`sensor_value` 而非 `sensorValue`
- **避免单字母变量**：除循环计数器（`i`, `j`, `k`）外，避免使用单字母变量
- **避免缩写**：除非是广泛接受的缩写（如 `buf`, `idx`, `len`, `max`, `min`）
- **布尔变量前缀**：建议使用 `is_`, `has_`, `should_`, `can_` 等前缀
- **数组/指针后缀**：可添加 `_array`, `_ptr`, `_buf` 等后缀以明确类型

```c
// ✅ 布尔变量示例
bool is_initialized;
bool has_data;
bool should_retry;
bool can_send;

// ✅ 数组/指针示例
uint8_t data_buffer[1024];
char* message_ptr;
int* value_array;
```

### 2.3 循环计数器
**规范**: 允许使用单字母变量（`i`, `j`, `k`）作为循环计数器

```c
// ✅ 正确示例
for (int i = 0; i < array_size; i++) {
    process_data(array[i]);
}

for (int j = 0; j < row_count; j++) {
    for (int k = 0; k < col_count; k++) {
        matrix[j][k] = 0;
    }
}

// ✅ 更清晰的循环变量（推荐）
for (int index = 0; index < array_size; index++) {
    process_data(array[index]);
}
```

---

## 函数命名规范

### 3.1 函数命名
**规范**: 使用**驼峰命名法**（camelCase）

```c
// ✅ 正确示例
void initializeSystem(void);
bool checkConnection(void);
int readSensorData(void);
void setTemperature(int temp);
uint32_t calculateChecksum(const uint8_t* data, size_t len);

// ❌ 错误示例
void initialize_system(void);  // 下划线命名（不符合规范）
void init(void);                // 缩写不清晰
void f(void);                   // 无意义命名
```

### 3.2 函数命名要求
- **动词开头**：函数名应以动词开头，表达动作
- **驼峰命名**：使用驼峰命名法，首字母小写
- **模块前缀**：对外接口函数建议添加模块前缀（如 `audio_`, `camera_`）
- **静态函数**：模块内静态函数可保持简洁，或添加 `s_` 前缀

```c
// ✅ 模块前缀示例
void audioInitialize(void);
void audioStartPlayback(void);
void cameraCaptureFrame(void);
void cameraSetResolution(int width, int height);

// ✅ 静态函数示例
static void processData(void);
static bool validateInput(int value);
// 或使用 s_ 前缀
static void s_processData(void);
```

### 3.3 函数命名模式
- **获取函数**：`get` + 名词，如 `getTemperature()`, `getStatus()`
- **设置函数**：`set` + 名词，如 `setMode()`, `setConfig()`
- **检查函数**：`is` + 形容词，如 `isReady()`, `isValid()`
- **判断函数**：`check` + 名词，如 `checkConnection()`, `checkError()`
- **初始化函数**：`initialize` 或 `init`，如 `initializeSystem()`
- **清理函数**：`cleanup` 或 `shutdown`，如 `cleanupResources()`

```c
// ✅ 函数命名模式示例
int getCurrentTemperature(void);
void setOperationMode(Mode mode);
bool isSystemReady(void);
bool checkDataIntegrity(const uint8_t* data, size_t len);
void initializeHardware(void);
void cleanupResources(void);
```

---

## 常量命名规范

### 4.1 常量命名
**规范**: 使用**下划线命名法 + 全大写**（UPPER_SNAKE_CASE）

```c
// ✅ 正确示例
#define MAX_BUFFER_SIZE     1024
#define DEFAULT_TIMEOUT_MS  1000
#define SENSOR_COUNT        8
#define PI                  3.14159265359

// ❌ 错误示例
#define maxBufferSize       1024  // 驼峰命名（不符合规范）
#define MAXBUFFERSIZE       1024  // 无下划线分隔
#define max_buffer_size     1024  // 小写（不符合规范）
```

### 4.2 常量定义方式
**推荐使用 `const` 变量而非 `#define`**（除非用于条件编译或宏函数）

```c
// ✅ 推荐方式（C++ 或 C99+）
const int MAX_BUFFER_SIZE = 1024;
const uint32_t DEFAULT_TIMEOUT_MS = 1000;
const float PI = 3.14159265359f;

// ✅ 使用 #define（条件编译或宏函数）
#define ENABLE_DEBUG        1
#define MAX(a, b)           (((a) > (b)) ? (a) : (b))

// ❌ 不推荐（魔法数字）
if (buffer_size > 1024) {  // 应使用命名常量
    // ...
}
```

### 4.3 枚举常量
**规范**: 枚举名使用驼峰命名，枚举值使用全大写+下划线

```c
// ✅ 正确示例
typedef enum {
    SENSOR_STATUS_IDLE = 0,
    SENSOR_STATUS_ACTIVE,
    SENSOR_STATUS_ERROR
} SensorStatus;

// ✅ 或使用枚举类（C++）
enum class LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR
};
```

---

## 宏定义命名规范

### 4.1 宏定义命名
**规范**: 使用**下划线命名法 + 全大写**（UPPER_SNAKE_CASE）

```c
// ✅ 正确示例
#define ENABLE_DEBUG            1
#define MAX_RETRY_COUNT         3
#define BUFFER_SIZE             256
#define API_VERSION             "1.0.0"

// ❌ 错误示例
#define enableDebug             1   // 驼峰命名
#define MAXRETRYCOUNT           3   // 无下划线分隔
```

### 4.2 宏函数命名
**规范**: 宏函数参数必须加括号，防止副作用

```c
// ✅ 正确示例
#define MAX(a, b)           (((a) > (b)) ? (a) : (b))
#define MIN(a, b)           (((a) < (b)) ? (a) : (b))
#define ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#define IS_POWER_OF_TWO(n)  (((n) != 0) && (((n) & ((n) - 1)) == 0))

// ❌ 错误示例
#define MAX(a, b)   (a > b ? a : b)  // 参数未加括号
```

### 4.3 条件编译宏
**规范**: 条件编译宏使用全大写+下划线

```c
// ✅ 正确示例
#ifdef ENABLE_FEATURE_X
    // 功能代码
#endif

#if defined(PLATFORM_ARM) && defined(OS_LINUX)
    // 平台特定代码
#endif
```

---

## 类型定义命名规范

### 5.1 类型定义命名
**规范**: 使用**下划线命名法 + "_t" 后缀**

```c
// ✅ 正确示例
typedef uint8_t  sensor_status_t;
typedef uint32_t timestamp_t;
typedef int      error_code_t;

// ✅ 结构体类型定义
typedef struct {
    uint8_t  id;
    uint16_t value;
    bool     is_valid;
} SensorData_t;

// ❌ 错误示例
typedef uint8_t SensorStatus;      // 驼峰命名
typedef uint8_t sensor_status;     // 无 _t 后缀
```

### 5.2 结构体/联合体命名
**规范**: 结构体标签和类型名应清晰命名

```c
// ✅ 正确示例
typedef struct {
    uint32_t id;
    char     name[32];
    bool     active;
} DeviceConfig_t;

// ✅ 或使用结构体标签
typedef struct DeviceConfig {
    uint32_t id;
    char     name[32];
    bool     active;
} DeviceConfig_t;

// ✅ 联合体示例
typedef union {
    uint32_t word;
    struct {
        uint8_t byte0;
        uint8_t byte1;
        uint8_t byte2;
        uint8_t byte3;
    } bytes;
} DataUnion_t;
```

### 5.3 指针类型定义
**规范**: 指针类型定义应明确表达含义

```c
// ✅ 正确示例
typedef struct SensorData* SensorDataPtr_t;
typedef void (*CallbackFunc_t)(int event_id, void* user_data);

// ✅ 函数指针类型
typedef int (*ReadFunc_t)(void* buffer, size_t size);
typedef void (*ErrorHandler_t)(int error_code);
```

---

## 全局变量命名规范

### 6.1 全局变量命名
**规范**: 使用**下划线命名法 + "g_" 前缀**（尽量避免使用全局变量）

```c
// ✅ 正确示例
int g_system_state;
uint32_t g_error_count;
bool g_is_initialized;
char g_device_name[64];

// ❌ 错误示例
int systemState;          // 驼峰命名
int system_state;          // 无 g_ 前缀
```

### 6.2 全局变量使用原则
- **尽量避免使用全局变量**：优先使用函数参数、返回值或封装在结构体中
- **模块前缀**：若必须使用，应添加模块前缀，如 `audio_`, `camera_`
- **线程安全**：多线程环境下必须考虑线程安全性

```c
// ✅ 模块前缀示例
int g_audio_sample_rate;
bool g_camera_is_recording;
uint32_t g_network_packet_count;

// ✅ 更好的方式：封装在结构体中
typedef struct {
    int sample_rate;
    bool is_recording;
    uint32_t packet_count;
} SystemState_t;

extern SystemState_t g_system_state;
```

---

## 静态变量/函数命名规范

### 7.1 静态变量命名
**规范**: 使用**下划线命名法 + "s_" 前缀**

```c
// ✅ 正确示例
static uint8_t s_buffer[64];
static int s_initialized = 0;
static bool s_is_running = false;
static uint32_t s_error_count = 0;

// ❌ 错误示例
static uint8_t buffer[64];        // 无 s_ 前缀
static int Buffer[64];              // 驼峰命名
```

### 7.2 静态函数命名
**规范**: 静态函数可使用 `s_` 前缀，或保持简洁命名

```c
// ✅ 方式一：使用 s_ 前缀
static void s_processData(void);
static bool s_validateInput(int value);

// ✅ 方式二：保持简洁（推荐）
static void processData(void);
static bool validateInput(int value);
```

### 7.3 文件作用域变量
**规范**: 文件作用域静态变量必须使用 `s_` 前缀

```c
// ✅ 正确示例
static int s_module_initialized = 0;
static uint32_t s_total_requests = 0;
static Mutex_t s_mutex;
```

---

## 类与结构体命名规范

### 8.1 类命名（C++）
**规范**: 使用**驼峰命名法，首字母大写**（PascalCase）

```cpp
// ✅ 正确示例
class AudioSystem {
public:
    void initialize();
    void startPlayback();
};

class VideoSystem {
public:
    bool captureFrame();
    void setResolution(int width, int height);
};

// ❌ 错误示例
class audio_system { };     // 下划线命名
class Audio_System { };     // 混合命名
```

### 8.2 结构体命名（C）
**规范**: 使用**驼峰命名法，首字母大写**，或使用 `_t` 后缀

```c
// ✅ 方式一：PascalCase（推荐）
typedef struct {
    uint32_t id;
    char     name[32];
} DeviceConfig;

// ✅ 方式二：snake_case + _t 后缀
typedef struct {
    uint32_t id;
    char     name[32];
} device_config_t;
```

### 8.3 成员变量命名
**规范**: 类成员变量使用下划线后缀（`member_`），或使用 `m_` 前缀

```cpp
// ✅ 方式一：下划线后缀（推荐）
class Logger {
private:
    bool initialized_;
    size_t buffer_size_;
    FILE* log_file_;
};

// ✅ 方式二：m_ 前缀
class Logger {
private:
    bool m_initialized;
    size_t m_bufferSize;
    FILE* m_logFile;
};
```

---

## 命名空间命名规范

### 9.1 命名空间命名（C++）
**规范**: 使用**小写字母 + 下划线**（snake_case）

```cpp
// ✅ 正确示例
namespace app {
namespace tool {
namespace log {
    // 代码
}
}
}

// ✅ 或使用嵌套命名空间（C++17+）
namespace app::tool::log {
    // 代码
}

// ❌ 错误示例
namespace App { }           // 首字母大写
namespace appTool { }       // 驼峰命名
```

---

## 文件命名规范

### 10.1 源文件命名
**规范**: 使用**小写字母 + 下划线**（snake_case）

```c
// ✅ 正确示例
log.cc          // C++ 源文件
log.c           // C 源文件
audio_system.cc
camera_manager.c

// ❌ 错误示例
Log.cc          // 首字母大写
logSystem.cc    // 驼峰命名
```

### 10.2 头文件命名
**规范**: 使用**小写字母 + 下划线**（snake_case）

```c
// ✅ 正确示例
log.hpp         // C++ 头文件
log.h           // C 头文件
audio_system.hpp
camera_manager.h

// ❌ 错误示例
Log.h           // 首字母大写
logSystem.h     // 驼峰命名
```

### 10.3 头文件保护宏
**规范**: 使用**全大写 + 下划线**，格式为 `文件名_H` 或 `文件名_HPP`

```c
// ✅ 正确示例
#ifndef LOG_HPP
#define LOG_HPP
// 头文件内容
#endif // LOG_HPP

#ifndef AUDIO_SYSTEM_HPP
#define AUDIO_SYSTEM_HPP
// 头文件内容
#endif // AUDIO_SYSTEM_HPP
```

---

## 命名示例

### 11.1 完整示例

```c
// ============================================================================
// 常量定义
// ============================================================================
#define MAX_BUFFER_SIZE         1024
#define DEFAULT_TIMEOUT_MS       1000
#define SENSOR_COUNT            8

// ============================================================================
// 类型定义
// ============================================================================
typedef enum {
    SENSOR_STATUS_IDLE = 0,
    SENSOR_STATUS_ACTIVE,
    SENSOR_STATUS_ERROR
} SensorStatus_t;

typedef struct {
    uint8_t  id;
    uint16_t value;
    bool     is_valid;
    SensorStatus_t status;
} SensorData_t;

// ============================================================================
// 全局变量（尽量避免使用）
// ============================================================================
int g_system_state = 0;
bool g_is_initialized = false;

// ============================================================================
// 函数声明
// ============================================================================
void initializeSystem(void);
bool checkConnection(void);
int readSensorData(SensorData_t* data);
void setSensorStatus(SensorStatus_t status);

// ============================================================================
// 函数实现
// ============================================================================
static int s_sensor_count = 0;  // 静态变量

void initializeSystem(void) {
    int retry_count = 0;
    bool is_success = false;
    
    // 初始化代码
    g_is_initialized = true;
}

bool checkConnection(void) {
    bool is_connected = false;
    uint32_t timeout_ms = DEFAULT_TIMEOUT_MS;
    
    // 检查连接代码
    return is_connected;
}

int readSensorData(SensorData_t* data) {
    if (data == NULL) {
        return -1;
    }
    
    uint8_t raw_value = 0;
    bool is_valid = false;
    
    // 读取传感器数据
    data->value = raw_value;
    data->is_valid = is_valid;
    
    return 0;
}
```

### 11.2 C++ 示例

```cpp
// ============================================================================
// 命名空间
// ============================================================================
namespace app {
namespace tool {
namespace log {

// ============================================================================
// 枚举类
// ============================================================================
enum class LogLevel {
    DEBUG = 0,
    INFO,
    WARN,
    ERROR
};

// ============================================================================
// 类定义
// ============================================================================
class Logger {
public:
    static Logger& getInstance();
    bool initialize(const LogConfig& config);
    void log(LogLevel level, const char* tag, const char* format, ...);
    
private:
    Logger();
    ~Logger();
    
    void workerThread();
    void writeToFile(const LogMessage& msg);
    
    // 成员变量（下划线后缀）
    bool initialized_;
    size_t buffer_size_;
    FILE* log_file_;
};

} // namespace log
} // namespace tool
} // namespace app
```

---

## 常见错误与避免

### 12.1 命名不一致
**错误**: 混用驼峰和下划线命名
```c
// ❌ 错误
int sensorValue;
bool is_connected;
void InitializeSystem(void);
```

**正确**: 统一使用规范命名
```c
// ✅ 正确
int sensor_value;
bool is_connected;
void initializeSystem(void);
```

### 12.2 缩写过度
**错误**: 使用不清晰的缩写
```c
// ❌ 错误
int tmp;
bool flg;
void proc();
```

**正确**: 使用完整且有意义的命名
```c
// ✅ 正确
int temperature;
bool is_enabled;
void processData();
```

### 12.3 单字母变量滥用
**错误**: 除循环计数器外使用单字母变量
```c
// ❌ 错误
int a = 10;
int b = 20;
int c = a + b;
```

**正确**: 使用有意义的变量名
```c
// ✅ 正确
int first_value = 10;
int second_value = 20;
int sum = first_value + second_value;
```

### 12.4 魔法数字
**错误**: 使用未命名的常量
```c
// ❌ 错误
if (buffer_size > 1024) {
    // ...
}
delay_ms(1000);
```

**正确**: 使用命名常量
```c
// ✅ 正确
#define MAX_BUFFER_SIZE  1024
#define DEFAULT_DELAY_MS 1000

if (buffer_size > MAX_BUFFER_SIZE) {
    // ...
}
delay_ms(DEFAULT_DELAY_MS);
```

### 12.5 命名过长或过短
**错误**: 命名长度不当
```c
// ❌ 错误
int a;                                    // 过短
int temperature_sensor_value_in_celsius;  // 过长
```

**正确**: 长度适中，清晰表达含义
```c
// ✅ 正确
int temperature;
int sensor_value;
```

---

## 总结

### 核心原则
1. **变量名**：下划线命名法（snake_case）
2. **函数名**：驼峰命名法（camelCase）
3. **常量名**：全大写+下划线（UPPER_SNAKE_CASE）
4. **宏定义**：全大写+下划线（UPPER_SNAKE_CASE）
5. **类型定义**：下划线命名法+"_t"后缀
6. **全局变量**：下划线命名法+"g_"前缀（尽量避免）
7. **静态变量**：下划线命名法+"s_"前缀
8. **类名**：驼峰命名法，首字母大写（PascalCase）
9. **文件名**：小写字母+下划线（snake_case）

### 检查清单
- [ ] 所有变量使用下划线命名法
- [ ] 所有函数使用驼峰命名法
- [ ] 所有常量使用全大写+下划线
- [ ] 所有宏定义使用全大写+下划线
- [ ] 全局变量添加 `g_` 前缀
- [ ] 静态变量添加 `s_` 前缀
- [ ] 类型定义添加 `_t` 后缀
- [ ] 避免使用单字母变量（除循环计数器）
- [ ] 避免使用魔法数字
- [ ] 命名清晰表达含义

---

## 参考资源

- MISRA C:2012 Guidelines
- Google C++ Style Guide
- Linux Kernel Coding Style
- Embedded C Coding Standard (Barr Group)

---

**文档维护**: 如有疑问或建议，请联系项目维护者。

