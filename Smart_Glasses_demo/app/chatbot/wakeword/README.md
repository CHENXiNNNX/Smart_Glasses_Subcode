# 唤醒词检测模块

## 功能说明

本模块使用 Snowboy 库实现唤醒词检测功能，支持自定义唤醒词。

## 已有唤醒词模型

在 `3rdparty/snowboy/resources/models/` 目录下有以下预训练模型：

| 文件名 | 唤醒词 | 类型 |
|--------|--------|------|
| `snowboy.umdl` | "snowboy" | 通用模型 |
| `echo.pmdl` | "echo" / "回声" | 个人模型 |
| `computer.umdl` | "computer" | 通用模型 |
| `jarvis.umdl` | "jarvis" | 通用模型 |
| `smart_mirror.umdl` | "smart mirror" | 通用模型 |
| `view_glass.umdl` | "view glass" | 通用模型 |
| `neoya.umdl` | "neoya" | 通用模型 |
| `hey_extreme.umdl` | "hey extreme" | 通用模型 |

## API 使用

### 基本用法

```cpp
#include "app/chatbot/wakeword/wakeword.h"

using namespace glasses::chatbot::wakeword;

// 1. 创建检测器
WakewordDetector wakeword;

// 2. 初始化
std::string resource = "./third_party/snowboy/resources/common.res";
std::string model = "./third_party/snowboy/resources/models/snowboy.umdl";
wakeword.initialize(resource, model, 0.5f, 1.0f);

// 3. 设置回调
wakeword.setCallback([](int hotword_index) {
    std::cout << "唤醒词检测到！" << std::endl;
});

// 4. 处理音频帧（在音频回调中）
int result = wakeword.processAudioFrame(audio_data, num_samples);
if (result > 0) {
    // 检测到唤醒词
}
```

### 参数说明

**sensitivity（灵敏度）**：
- 范围：0.0 - 1.0
- 默认：0.5
- 说明：
  - 值越大，越容易触发（误触发率高）
  - 值越小，越难触发（漏检率高）
  - 建议：0.4 - 0.6

**audio_gain（音频增益）**：
- 范围：0.0 - 10.0
- 默认：1.0
- 说明：
  - 用于放大或缩小输入音频
  - 如果麦克风声音太小，可以增大
  - 如果环境噪音大，可以减小

## 测试程序

```bash
cd build
make

# 运行唤醒词测试
./bin/test_wakeword
```

## 训练自定义唤醒词

### 方法1：使用Snowboy在线训练（推荐）

1. 访问 https://snowboy.kitt.ai/
2. 注册账号并登录
3. 点击"Create Hotword"
4. 录制3次你的唤醒词（如"小智小智"）
5. 下载生成的 `.pmdl` 文件
6. 将文件放到 `3rdparty/snowboy/resources/models/` 目录
7. 在代码中使用新模型

### 方法2：使用多个样本训练

1. 收集多个人说唤醒词的录音（WAV格式，16kHz，单声道）
2. 使用Snowboy提供的训练工具：
```bash
cd 3rdparty/snowboy
python training_service.py
```

## 集成到AIManager

```cpp
// 在 AIManager 中添加唤醒词检测器
class AIManager::Impl {
    WakewordDetector* wakeword_detector_;
    
    void setupWakeword() {
        wakeword_detector_ = new WakewordDetector();
        wakeword_detector_->initialize(resource, model, 0.5f, 1.0f);
        
        // 检测到唤醒词后自动开始监听
        wakeword_detector_->setCallback([this](int index) {
            std::cout << "[AIManager] Wakeword detected, starting listening..." << std::endl;
            this->startListening();
        });
    }
};

// 在音频回调中处理
void recordCallback(...) {
    // ...3A算法处理后
    
    // 如果未在监听状态，进行唤醒词检测
    if (!is_listening && wakeword_detector_->isEnabled()) {
        wakeword_detector_->processAudioFrame(frame.data(), frame.size());
    }
}
```

## 性能要求

- **CPU占用**：约1-3%（单核）
- **内存占用**：约10-20MB
- **延迟**：< 50ms
- **采样率**：16kHz（Snowboy要求）
- **声道数**：1（单声道）

## 注意事项

1. **采样率转换**：如果你的音频系统是48kHz，需要重采样到16kHz
2. **音频格式**：Snowboy要求int16格式的PCM数据
3. **帧长度**：建议每次处理160-480个样本（10-30ms）
4. **误触发**：在嘈杂环境下可能误触发，建议调整sensitivity
5. **多唤醒词**：可以同时加载多个模型，用逗号分隔

## 故障排查

### Q: 检测器初始化失败
**A**: 检查文件路径是否正确：
```bash
ls -la ./third_party/snowboy/resources/common.res
ls -la ./third_party/snowboy/resources/models/snowboy.umdl
```

### Q: 唤醒词检测不灵敏
**A**: 
1. 增大sensitivity值（0.5 → 0.7）
2. 增大audio_gain值（1.0 → 1.5）
3. 靠近麦克风说话
4. 确认麦克风工作正常

### Q: 误触发率高
**A**:
1. 减小sensitivity值（0.5 → 0.3）
2. 重新训练模型（增加负样本）
3. 确认环境噪音水平

### Q: 采样率不匹配
**A**: Snowboy要求16kHz，如果你的系统是48kHz：
```cpp
// 在recordCallback中重采样
if (wakeword_enabled) {
    std::vector<int16_t> resampled;
    resample_48k_to_16k(frame, resampled);
    wakeword->processAudioFrame(resampled.data(), resampled.size());
}
```

## 参考资料

- Snowboy官网：https://snowboy.kitt.ai/
- Snowboy GitHub：https://github.com/Kitt-AI/snowboy
- 文档：https://github.com/Kitt-AI/snowboy#pretrained-universal-models

---

**最后更新**: 2025-10-10  
**版本**: v1.0


