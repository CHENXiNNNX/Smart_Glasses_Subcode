# xiaozhi AI集成使用手册

## 快速开始

### 1. 编译项目

```bash
cd /home/irex/WorkSpace/Smart_Glasses/Demo/Smart_Glasses_demo
mkdir -p build
cd build
cmake ..
make
```

### 2. 运行AI聊天机器人

```bash
./bin/main
```

### 3. 基本操作

```
s - 开始监听（按一次即可，后续自动连续对话）
x - 停止监听
i - 显示状态信息
q - 退出程序
```

---

## API使用指南

### 初始化AI管理器

```cpp
#include "app/chatbot/chatbot.h"
#include "app/media/audio/audio.h"

using namespace glasses::chatbot;

// 1. 初始化音频系统
audio_system_t audio_system;
sync_context_t sync_ctx;
sync_init(&sync_ctx);
audio_system_init(&audio_system, &sync_ctx);

// 2. 创建AI配置
AIConfig config;
// device_id和client_id会自动生成

// 3. 创建AI管理器
AIManager ai_manager(config);

// 4. 设置回调（可选）
ai_manager.onSTTText([](const std::string& text, bool is_final) {
    std::cout << "识别: " << text << std::endl;
});

ai_manager.onLLMText([](const std::string& text, bool is_final) {
    std::cout << "AI: " << text << std::endl;
});

// 5. 初始化
ai_manager.initialize(&audio_system);

// 6. 启动服务
ai_manager.start();

// 7. 开始监听
ai_manager.startListening("auto");

// 8. 清理
ai_manager.stop();
ai_manager.shutdown();
audio_system_deinit(&audio_system);
```

---

## 高级功能

### 注册MCP设备

```cpp
#include "app/chatbot/mcp/mcp.h"

using namespace glasses::chatbot::mcp;
using namespace glasses::chatbot::protocol;

// 1. 创建设备描述符
IoTDescriptor led = createSimpleDescriptor(
    "smart_led", 
    "Smart LED light"
);

// 2. 添加属性
addProperty(led, "power", "电源状态", "string");
addProperty(led, "brightness", "亮度", "number");

// 3. 添加方法
addMethod(led, "turn_on", "打开LED");
addMethod(led, "turn_off", "关闭LED");
addMethod(led, "set_brightness", "设置亮度");
addMethodParameter(led, "set_brightness", "level", "亮度值(0-100)", "number");

// 4. 实现处理函数
bool ledHandler(const std::string& device, 
                const std::string& method,
                const std::map<std::string, std::string>& params) {
    if (method == "turn_on") {
        // 执行打开LED的逻辑
        return true;
    }
    // ...
    return false;
}

// 5. 实现状态获取
std::map<std::string, std::string> ledGetter(const std::string& device) {
    return {
        {"power", "on"},
        {"brightness", "80"}
    };
}

// 6. 注册设备
ai_manager.registerDevice(led, ledHandler, ledGetter);
```

现在可以对AI说："帮我打开LED" 或 "把LED亮度设为50"！

---

## 配置调整

### 音量调整

修改 `app/media/media_config.h`:

```cpp
#define AUDIO_MASTER_VOLUME 0.5f  // 播放音量 (0.0-1.0)
```

### 音频参数调整

```cpp
#define AUDIO_SAMPLE_RATE 48000       // 采集采样率
#define AUDIO_CHANNELS 1              // 声道数
#define AUDIO_FRAME_DURATION_MS 20    // 帧时长
#define AUDIO_BIT_RATE 32000          // Opus比特率
```

### 3A算法调整

```cpp
#define AUDIO_DENOISE_ENABLED true           // 降噪
#define AUDIO_AGC_ENABLED true               // 自动增益
#define AUDIO_VAD_ENABLED true               // 语音检测
#define AUDIO_AGC_LEVEL 8000.0f              // AGC目标电平
#define AUDIO_NOISE_SUPPRESS_LEVEL -45       // 降噪强度
```

---

## 常见问题

### Q: 为什么第一次能识别，后续不行？

**A**: 编码器状态残留。确保`stop_ai_audio_stream()`中释放了编码器：
```cpp
opus_encoder_destroy(audio_system->ai_encoder);
release_audio_resample(audio_system);
```

### Q: 如何调整延迟？

**A**: 修改`chatbot.cc`中的延迟参数：
```cpp
state_machine->onTTS_stop(2000);  // TTS后延迟2秒
std::this_thread::sleep_for(std::chrono::milliseconds(2500));  // listen重发延迟2.5秒
```

### Q: 识别率不高怎么办？

**A**: 
1. 调整AGC增益：`AUDIO_AGC_LEVEL`提高到16000
2. 靠近麦克风说话
3. 降低环境噪音
4. 调整降噪强度：`AUDIO_NOISE_SUPPRESS_LEVEL`改为-30

---

## 代码结构

```
Smart_Glasses/Demo/Smart_Glasses_demo/
├── app/
│   ├── chatbot/
│   │   ├── chatbot.h              ← AI主控制器
│   │   ├── chatbot.cc
│   │   ├── protocol_handle/       ← 协议处理
│   │   ├── statemachine/          ← AI状态机
│   │   ├── mcp/                   ← MCP工具
│   │   └── uuid/                  ← UUID管理
│   ├── protocol/
│   │   ├── websocket/             ← WebSocket客户端
│   │   └── udp/                   ← UDP IPC
│   ├── media/
│   │   └── audio/                 ← 音频系统
│   └── tool/
│       ├── mac/                   ← MAC地址工具
│       └── memory/                ← 内存池
├── test/
│   └── test_ai_chatbot.cpp        ← 测试程序
└── docs/
    ├── xiaozhi_integration_issues.md  ← 问题记录
    └── xiaozhi_user_guide.md          ← 本文档
```

---

**祝使用愉快！** 🎉

