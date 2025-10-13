# AR 眼镜 AI 识图系统技术文档

## 文档信息
- **模块名称**: AI 视觉识别系统 (Vision Recognition System)
- **相机驱动**: ESP32 Camera GMF (Graphics Media Framework)
- **支持设备**: ESP32-S3 系列开发板
- **最后更新**: 2025-01-11

---

## 目录
1. [系统概述](#1-系统概述)
2. [架构设计](#2-架构设计)
3. [拍照流程](#3-拍照流程)
4. [AI 分析流程](#4-ai-分析流程)
5. [Vision API 集成](#5-vision-api-集成)
6. [性能优化](#6-性能优化)
7. [错误处理](#7-错误处理)
8. [使用示例](#8-使用示例)
9. [配置参数](#9-配置参数)

---

## 1. 系统概述

### 1.1 功能描述

AI 识图系统是 AR 眼镜的核心视觉功能，允许用户通过语音指令拍照并获取 AI 对图像内容的理解和描述。

**核心能力:**
- 📸 高质量图像捕获
- 🤖 AI 物体识别
- 🎨 场景理解
- 💬 自然语言描述
- 🌐 云端 Vision API 集成

### 1.2 技术栈

| 组件 | 技术 | 说明 |
|------|------|------|
| **相机驱动** | ESP32 Camera + GMF | 硬件加速编解码 |
| **图像格式** | JPEG | 高压缩比，适合传输 |
| **通信协议** | HTTP/HTTPS | multipart/form-data |
| **AI 引擎** | 云端 Vision API | 物体检测、场景识别 |
| **接口协议** | MCP (Model Context Protocol) | 统一的工具调用接口 |

### 1.3 工作流程概览

```
用户语音指令
    ↓
"看看这是什么？"
    ↓
LLM 语义理解
    ↓
调用 MCP 工具: self.camera.take_photo
    ↓
┌─────────────────────────────────────┐
│  1. 相机预热 (600ms)                 │
│  2. 拍照捕获 (300ms)                 │
│  3. 图像上传 (500-1000ms)            │
│  4. AI 推理 (1-2s)                   │
└─────────────────────────────────────┘
    ↓
返回 JSON 结果
    ↓
LLM 生成回复
    ↓
TTS 语音播报
```

**总耗时**: 约 **2.4-4 秒**

---

## 2. 架构设计

### 2.1 系统架构图

```
┌────────────────────────────────────────────────────────────┐
│                      应用层                                 │
│  ┌──────────────────────────────────────────────────────┐  │
│  │              MCP Server                              │  │
│  │  - 工具注册: self.camera.take_photo                  │  │
│  │  - 参数解析: question                                │  │
│  │  - 结果返回: JSON                                    │  │
│  └──────────────────┬───────────────────────────────────┘  │
└────────────────────┼────────────────────────────────────────┘
                     │
                     ▼
┌────────────────────────────────────────────────────────────┐
│                  相机抽象层 (Camera)                        │
│  ┌──────────────────────────────────────────────────────┐  │
│  │           Esp32CameraGMF                             │  │
│  │  - Capture(): 拍照                                   │  │
│  │  - Explain(): AI 分析                                │  │
│  │  - SetExplainUrl(): 配置 Vision API                 │  │
│  └──────────────────┬───────────────────────────────────┘  │
└────────────────────┼────────────────────────────────────────┘
                     │
         ┌───────────┼───────────┐
         ▼           ▼           ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ ESP32 Camera │ │     GMF      │ │     HTTP     │
│   Driver     │ │  硬件编码     │ │   Client     │
└──────────────┘ └──────────────┘ └──────────────┘
         │
         ▼
┌──────────────────────────────────────────────────────────┐
│                   硬件层                                  │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐         │
│  │ OV2640     │  │   PSRAM    │  │   WiFi     │         │
│  │ 相机模组    │  │  图像缓冲   │  │   模块     │         │
│  └────────────┘  └────────────┘  └────────────┘         │
└──────────────────────────────────────────────────────────┘
```

### 2.2 类设计

#### Esp32CameraGMF 类

```cpp
class Esp32CameraGMF : public Camera {
private:
    // 帧缓冲
    camera_fb_t* fb_;
    
    // Vision API 配置
    std::string explain_url_;
    std::string explain_token_;
    
    // GMF 硬件加速
    esp_gmf_element_handle_t video_enc_handle_;
    esp_gmf_pipeline_handle_t video_pipeline_;
    
    // 同步对象
    SemaphoreHandle_t camera_mutex_;
    QueueHandle_t frame_queue_;

public:
    // 核心方法
    bool Capture();                              // 拍照
    std::string Explain(const std::string& q);   // AI 分析
    void SetExplainUrl(const std::string& url, 
                       const std::string& token); // 配置 API
};
```

### 2.3 数据流图

```
┌─────────────┐
│   用户语音   │ "看看这是什么？"
└──────┬──────┘
       │
       ▼
┌─────────────┐
│     LLM     │ 语义理解 → 工具调用
└──────┬──────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  MCP Server: tools/call                     │
│  {                                          │
│    "name": "self.camera.take_photo",        │
│    "arguments": {                           │
│      "question": "这是什么？"                │
│    }                                        │
│  }                                          │
└──────┬──────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  Capture() - 拍照                           │
│  ┌───────────────────────────────────────┐  │
│  │ 预热帧 1 → 丢弃                        │  │
│  │ 预热帧 2 → 丢弃                        │  │
│  │ 预热帧 3 → 丢弃                        │  │
│  │ ─────────────────────────────────     │  │
│  │ 捕获帧 1 → 丢弃                        │  │
│  │ 捕获帧 2 → 丢弃                        │  │
│  │ 捕获帧 3 → 保留 ✓                     │  │
│  └───────────────────────────────────────┘  │
│  结果: JPEG 图像 (10-50KB)                   │
└──────┬──────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  Explain() - AI 分析                        │
│  ┌───────────────────────────────────────┐  │
│  │ HTTP POST                             │  │
│  │ multipart/form-data                   │  │
│  │ ┌──────────────────────────────────┐  │  │
│  │ │ question: "这是什么？"            │  │  │
│  │ │ file: [JPEG 数据]                │  │  │
│  │ └──────────────────────────────────┘  │  │
│  └───────────────────────────────────────┘  │
└──────┬──────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  Vision API 服务器                          │
│  ┌───────────────────────────────────────┐  │
│  │ 图像预处理                             │  │
│  │ AI 模型推理                            │  │
│  │ 物体检测 + 场景识别                    │  │
│  │ 生成描述                               │  │
│  └───────────────────────────────────────┘  │
└──────┬──────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────┐
│  JSON 响应                                  │
│  {                                          │
│    "success": true,                         │
│    "objects": ["笔记本电脑", "鼠标"],       │
│    "description": "这是一台银色笔记本...",  │
│    "scene": "办公场景",                     │
│    "confidence": 0.95                       │
│  }                                          │
└──────┬──────────────────────────────────────┘
       │
       ▼
┌─────────────┐
│     LLM     │ 生成自然语言回复
└──────┬──────┘
       │
       ▼
┌─────────────┐
│     TTS     │ "我看到了一台银色的笔记本电脑..."
└─────────────┘
```

---

## 3. 拍照流程

### 3.1 Capture() 方法详解

#### 3.1.1 流程图

```
开始
  ↓
🔒 获取相机互斥锁 (1000ms 超时)
  ↓
  ├─ 成功
  │   ↓
  │   ┌─────────────────────────────────────┐
  │   │  阶段 1: 相机预热                    │
  │   │  ┌───────────────────────────────┐  │
  │   │  │ for (i = 0; i < 3; i++)       │  │
  │   │  │   获取帧 → 立即释放             │  │
  │   │  │   延迟 200ms                   │  │
  │   │  └───────────────────────────────┘  │
  │   │  目的: 让 AEC/AGC 参数稳定          │
  │   │  耗时: ~600ms                        │
  │   └─────────────────────────────────────┘
  │   ↓
  │   ┌─────────────────────────────────────┐
  │   │  阶段 2: 高质量捕获                  │
  │   │  ┌───────────────────────────────┐  │
  │   │  │ for (i = 0; i < 3; i++)       │  │
  │   │  │   释放旧帧                      │  │
  │   │  │   延迟 150ms (i > 0)           │  │
  │   │  │   获取新帧                      │  │
  │   │  │   if (最后一帧)                 │  │
  │   │  │     质量检查 (>3KB)             │  │
  │   │  └───────────────────────────────┘  │
  │   │  目的: 获取最稳定的图像              │
  │   │  耗时: ~300ms                        │
  │   └─────────────────────────────────────┘
  │   ↓
  │   🔓 释放相机互斥锁
  │   ↓
  │   返回 true
  │
  └─ 失败
      ↓
      记录错误日志
      ↓
      返回 false
```

#### 3.1.2 代码实现要点

```cpp
bool Esp32CameraGMF::Capture() {
    // 1. 线程安全 - 获取互斥锁
    if (xSemaphoreTake(camera_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire camera mutex");
        return false;
    }

    // 2. 相机预热阶段
    ESP_LOGI(TAG, "开始相机预热，让传感器参数稳定...");
    for (int warmup = 0; warmup < 3; warmup++) {
        camera_fb_t* warmup_fb = esp_camera_fb_get();
        if (warmup_fb != nullptr) {
            esp_camera_fb_return(warmup_fb);  // 立即释放
            ESP_LOGI(TAG, "预热帧 %d/3 完成", warmup + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(200));  // 200ms 间隔
    }
    ESP_LOGI(TAG, "相机预热完成");

    // 3. 高质量捕获阶段
    int frames_to_get = 3;
    ESP_LOGI(TAG, "开始超高质量拍照，丢弃前%d帧", frames_to_get - 1);
    
    for (int i = 0; i < frames_to_get; i++) {
        // 释放旧帧
        if (fb_ != nullptr) {
            esp_camera_fb_return(fb_);
        }
        
        // 等待传感器调整
        if (i > 0) {
            vTaskDelay(pdMS_TO_TICKS(150));  // 150ms 延迟
        }
        
        // 获取新帧
        fb_ = esp_camera_fb_get();
        if (fb_ == nullptr) {
            ESP_LOGE(TAG, "Camera capture failed at frame %d", i);
            xSemaphoreGive(camera_mutex_);
            return false;
        }
        
        ESP_LOGI(TAG, "获取第%d帧: %dx%d, 大小: %d字节", 
                 i + 1, fb_->width, fb_->height, fb_->len);
        
        // 最后一帧质量检查
        if (i == frames_to_get - 1) {
            if (fb_->len < 3000) {  // 最小 3KB
                ESP_LOGW(TAG, "最后一帧图像可能有问题，大小: %d字节", fb_->len);
            } else {
                ESP_LOGI(TAG, "成功获取超高质量图像，大小: %d字节", fb_->len);
            }
        }
    }

    // 4. 释放锁
    xSemaphoreGive(camera_mutex_);
    return true;
}
```

### 3.2 图像质量优化

#### 3.2.1 传感器参数调优

```cpp
// 初始化时设置传感器参数
sensor_t *s = esp_camera_sensor_get();

// 镜像和翻转
s->set_hmirror(s, 1);     // 水平镜像
s->set_vflip(s, 0);       // 垂直翻转

// 曝光控制
s->set_aec2(s, 1);        // 启用自动曝光控制 2
s->set_ae_level(s, 3);    // 曝光等级 +3 (增加亮度)
s->set_aec_value(s, 200); // AEC 值 200 (增加曝光时间)

// 增益控制
s->set_gain_ctrl(s, 1);   // 启用自动增益控制
s->set_agc_gain(s, 15);   // AGC 增益 15 (最大增益)
```

**参数说明:**

| 参数 | 范围 | 推荐值 | 说明 |
|------|------|--------|------|
| `ae_level` | -2 ~ +2 | +3 | 曝光补偿，正值增加亮度 |
| `aec_value` | 0 ~ 1200 | 200 | 曝光时间，越大越亮 |
| `agc_gain` | 0 ~ 30 | 15 | 增益值，越大越亮（噪点也增加） |

#### 3.2.2 多帧捕获策略

**为什么需要多帧？**

1. **传感器稳定**: 相机从待机到工作需要时间让 AEC/AGC 稳定
2. **参数调整**: 前几帧的曝光和增益参数可能不准确
3. **质量保证**: 通过丢弃不稳定的帧，确保最终图像质量

**帧数选择:**

| 帧类型 | 帧数 | 延迟 | 目的 |
|--------|------|------|------|
| 预热帧 | 3 | 200ms | 唤醒传感器，稳定参数 |
| 丢弃帧 | 2 | 150ms | 等待参数完全稳定 |
| 保留帧 | 1 | - | 最终高质量图像 |

**总耗时计算:**
```
预热时间 = 3 × 200ms = 600ms
捕获时间 = 2 × 150ms = 300ms
总时间 = 900ms
```

### 3.3 性能统计

#### 典型拍照数据

| 指标 | 数值 |
|------|------|
| **分辨率** | QVGA (320×240) |
| **格式** | JPEG |
| **压缩质量** | 12 (0-63, 越小越好) |
| **图像大小** | 10-50 KB |
| **预热时间** | 600 ms |
| **捕获时间** | 300 ms |
| **总耗时** | ~900 ms |

#### 内存占用

```
PSRAM 使用:
- 帧缓冲: ~20 KB (JPEG 压缩后)
- 预留空间: 1 MB (防止碎片)

Heap 使用:
- 临时变量: <1 KB
```

---

## 4. AI 分析流程

### 4.1 Explain() 方法详解

#### 4.1.1 完整流程

```cpp
std::string Esp32CameraGMF::Explain(const std::string& question) {
    // ===== 阶段 1: 配置检查 =====
    if (explain_url_.empty()) {
        return "{\"success\": false, \"message\": \"Vision API not configured\"}";
    }
    
    // ===== 阶段 2: 获取互斥锁 =====
    if (xSemaphoreTake(camera_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return "{\"success\": false, \"message\": \"Camera busy\"}";
    }
    
    // ===== 阶段 3: 构造 multipart/form-data =====
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";
    
    // 3.1 question 字段
    std::string question_field = 
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"question\"\r\n" +
        "\r\n" +
        question + "\r\n";
    
    // 3.2 file 字段头部
    std::string file_header = 
        "--" + boundary + "\r\n" +
        "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n" +
        "Content-Type: image/jpeg\r\n" +
        "\r\n";
    
    // 3.3 结束边界
    std::string multipart_footer = 
        "\r\n--" + boundary + "--\r\n";
    
    // ===== 阶段 4: 配置 HTTP 客户端 =====
    auto http = Board::GetInstance().CreateHttp();
    
    // 设备标识
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid());
    
    // 认证令牌
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    
    // 内容类型
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    
    // 分块传输（避免大内存分配）
    http->SetHeader("Transfer-Encoding", "chunked");
    
    // ===== 阶段 5: 发送 HTTP 请求 =====
    if (!http->Open("POST", explain_url_)) {
        xSemaphoreGive(camera_mutex_);
        return "{\"success\": false, \"message\": \"Connection failed\"}";
    }
    
    // 发送 question 字段
    http->Write(question_field.c_str(), question_field.size());
    
    // 发送 file 字段头部
    http->Write(file_header.c_str(), file_header.size());
    
    // 发送 JPEG 图像数据
    http->Write((const char*)fb_->buf, fb_->len);
    
    // 发送结束边界
    http->Write(multipart_footer.c_str(), multipart_footer.size());
    
    // 结束分块传输
    http->Write("", 0);
    
    // ===== 阶段 6: 检查响应 =====
    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", http->GetStatusCode());
        xSemaphoreGive(camera_mutex_);
        return "{\"success\": false, \"message\": \"Upload failed\"}";
    }
    
    // ===== 阶段 7: 读取结果 =====
    std::string result = http->ReadAll();
    http->Close();
    
    // 日志记录
    ESP_LOGI(TAG, "Image: %dx%d, size=%d, question=%s\nResult: %s",
        fb_->width, fb_->height, fb_->len, question.c_str(), result.c_str());
    
    // ===== 阶段 8: 释放锁 =====
    xSemaphoreGive(camera_mutex_);
    
    return result;
}
```

### 4.2 HTTP 请求详解

#### 4.2.1 完整 HTTP 请求格式

```http
POST /analyze HTTP/1.1
Host: vision-api.example.com
Device-Id: 94:A9:90:27:3D:50
Client-Id: 550e8400-e29b-41d4-a716-446655440000
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
Content-Type: multipart/form-data; boundary=----ESP32_CAMERA_BOUNDARY
Transfer-Encoding: chunked

------ESP32_CAMERA_BOUNDARY
Content-Disposition: form-data; name="question"

这是什么？
------ESP32_CAMERA_BOUNDARY
Content-Disposition: form-data; name="file"; filename="camera.jpg"
Content-Type: image/jpeg

[JPEG 二进制数据, 约 10-50KB]
------ESP32_CAMERA_BOUNDARY--
```

#### 4.2.2 请求头说明

| 请求头 | 值 | 说明 |
|--------|----|----|
| `Device-Id` | MAC 地址 | 设备唯一硬件标识 |
| `Client-Id` | UUID | 客户端实例标识 |
| `Authorization` | Bearer Token | Vision API 认证令牌 |
| `Content-Type` | multipart/form-data | 表单数据格式 |
| `Transfer-Encoding` | chunked | 分块传输编码 |

#### 4.2.3 multipart/form-data 结构

```
┌────────────────────────────────────────────┐
│  Boundary: ----ESP32_CAMERA_BOUNDARY       │
└────────────────────────────────────────────┘
         │
         ├─ Part 1: question 字段
         │  ├─ Content-Disposition: form-data; name="question"
         │  └─ 内容: "这是什么？"
         │
         ├─ Part 2: file 字段
         │  ├─ Content-Disposition: form-data; name="file"; filename="camera.jpg"
         │  ├─ Content-Type: image/jpeg
         │  └─ 内容: [JPEG 二进制数据]
         │
         └─ 结束边界: ----ESP32_CAMERA_BOUNDARY--
```

### 4.3 Vision API 响应

#### 4.3.1 成功响应示例

```json
{
  "success": true,
  "objects": [
    {
      "name": "笔记本电脑",
      "confidence": 0.98,
      "bbox": [120, 80, 200, 160]
    },
    {
      "name": "鼠标",
      "confidence": 0.95,
      "bbox": [50, 150, 30, 40]
    },
    {
      "name": "键盘",
      "confidence": 0.92,
      "bbox": [100, 180, 120, 40]
    }
  ],
  "description": "这是一台银色的笔记本电脑放在桌面上，旁边有一个黑色的鼠标和白色的键盘。整体看起来是一个典型的办公场景。",
  "scene": "办公场景",
  "colors": [
    {"name": "银色", "percentage": 45},
    {"name": "黑色", "percentage": 30},
    {"name": "白色", "percentage": 25}
  ],
  "metadata": {
    "model": "vision-v2.0",
    "processing_time_ms": 1250,
    "image_resolution": "320x240"
  }
}
```

#### 4.3.2 错误响应示例

```json
{
  "success": false,
  "error": {
    "code": "INVALID_IMAGE",
    "message": "图像格式不支持或损坏"
  }
}
```

```json
{
  "success": false,
  "error": {
    "code": "RATE_LIMIT_EXCEEDED",
    "message": "API 调用频率超限，请稍后再试"
  }
}
```

### 4.4 性能指标

| 阶段 | 典型耗时 | 说明 |
|------|----------|------|
| 配置检查 | <1 ms | 内存操作 |
| 互斥锁获取 | <10 ms | FreeRTOS 信号量 |
| 构造请求体 | <5 ms | 字符串拼接 |
| 建立连接 | 100-300 ms | TCP + TLS 握手 |
| 上传图像 | 200-500 ms | 取决于网速 |
| AI 推理 | 1-2 s | 服务器处理 |
| 下载结果 | <50 ms | JSON 响应较小 |
| **总耗时** | **1.5-3 s** | - |

---

## 5. Vision API 集成

### 5.1 MCP Initialize 配置

#### 5.1.1 配置流程

```
服务器                              设备
  │                                  │
  ├─── MCP: initialize ─────────────→│
  │   {                              │
  │     "method": "initialize",      │
  │     "params": {                  │
  │       "capabilities": {          │
  │         "vision": {              │
  │           "url": "https://...",  │
  │           "token": "Bearer ..."  │
  │         }                        │
  │       }                          │
  │     }                            │
  │   }                              │
  │                                  ├─ 解析 capabilities
  │                                  │  └─ SetExplainUrl(url, token)
  │                                  │
  │←─── initialize result ───────────┤
  │   {                              │
  │     "result": {                  │
  │       "serverInfo": {...}        │
  │     }                            │
  │   }                              │
  │                                  │
```

#### 5.1.2 代码实现

```cpp
// MCP Server 解析 capabilities
void McpServer::ParseCapabilities(const cJSON *capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                
                // 动态配置 Vision API
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

// Camera 保存配置
void Esp32CameraGMF::SetExplainUrl(const std::string& url, 
                                   const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
    ESP_LOGI(TAG, "Vision API configured: %s", url.c_str());
}
```

### 5.2 MCP 工具调用

#### 5.2.1 工具定义

```cpp
// MCP 工具注册
AddTool("self.camera.take_photo",
    "Take a photo and explain it. Use this tool after the user asks you to see something.\n"
    "Args:\n"
    "  `question`: The question that you want to ask about the photo.\n"
    "Return:\n"
    "  A JSON object that provides the photo information.",
    PropertyList({Property("question", kPropertyTypeString)}),
    [](const PropertyList &properties) -> ReturnValue {
        auto camera = Board::GetInstance().GetCamera();
        
        // 1. 拍照
        if (!camera->Capture()) {
            return "{\"success\": false, \"message\": \"Failed to capture photo\"}";
        }
        
        // 2. AI 分析
        auto question = properties["question"].value<std::string>();
        return camera->Explain(question);
    }
);
```

#### 5.2.2 调用示例

**请求:**
```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "tools/call",
  "params": {
    "name": "self.camera.take_photo",
    "arguments": {
      "question": "这是什么？"
    }
  }
}
```

**响应:**
```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"success\": true, \"objects\": [\"笔记本电脑\"], \"description\": \"这是一台银色笔记本电脑...\"}"
      }
    ],
    "isError": false
  }
}
```

### 5.3 Vision API 服务器要求

#### 5.3.1 接口规范

**请求格式:**
- **方法**: POST
- **Content-Type**: `multipart/form-data`
- **字段**:
  - `question` (string): 用户的问题
  - `file` (binary): JPEG 图像文件

**响应格式:**
- **Content-Type**: `application/json`
- **状态码**: 200 (成功)
- **响应体**: JSON 对象

#### 5.3.2 认证方式

```http
Authorization: Bearer <token>
```

**Token 来源:**
- 通过 MCP `initialize` 方法传递
- 存储在设备的 `explain_token_` 成员变量中
- 每次请求自动添加到 HTTP 请求头

#### 5.3.3 服务器端实现参考（伪代码）

```python
from flask import Flask, request, jsonify
import cv2
import numpy as np
from your_vision_model import analyze_image

app = Flask(__name__)

@app.route('/analyze', methods=['POST'])
def analyze():
    # 1. 验证 Token
    auth_header = request.headers.get('Authorization')
    if not auth_header or not validate_token(auth_header):
        return jsonify({'success': False, 'error': 'Unauthorized'}), 401
    
    # 2. 获取参数
    question = request.form.get('question')
    image_file = request.files.get('file')
    
    if not image_file:
        return jsonify({'success': False, 'error': 'No image provided'}), 400
    
    # 3. 读取图像
    image_data = image_file.read()
    nparr = np.frombuffer(image_data, np.uint8)
    image = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
    
    # 4. AI 分析
    result = analyze_image(image, question)
    
    # 5. 返回结果
    return jsonify({
        'success': True,
        'objects': result['objects'],
        'description': result['description'],
        'scene': result['scene'],
        'confidence': result['confidence']
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080, ssl_context='adhoc')
```

---

## 6. 性能优化

### 6.1 图像质量优化

#### 6.1.1 传感器参数调优表

| 参数 | 低光环境 | 正常光 | 强光环境 |
|------|----------|--------|----------|
| `ae_level` | +3 | +1 | -1 |
| `aec_value` | 300 | 200 | 100 |
| `agc_gain` | 20 | 15 | 10 |

#### 6.1.2 分辨率与质量权衡

| 分辨率 | JPEG 大小 | 上传时间 | AI 准确度 | 推荐场景 |
|--------|-----------|----------|-----------|----------|
| QQVGA (160×120) | 3-5 KB | ~100 ms | 低 | 快速识别 |
| QVGA (320×240) | 10-20 KB | ~200 ms | 中 | **推荐** |
| VGA (640×480) | 40-80 KB | ~500 ms | 高 | 细节识别 |
| SVGA (800×600) | 60-120 KB | ~800 ms | 最高 | 高精度场景 |

**当前配置:** QVGA (320×240)，JPEG 质量 12

### 6.2 内存优化

#### 6.2.1 内存使用分析

```
总 PSRAM: 8 MB
├─ 系统预留: 2 MB
├─ 视频缓冲: 3 MB (视频模式)
├─ AI 处理: 1 MB
└─ 拍照缓冲: 100 KB
    ├─ 帧缓冲 (fb_): 20-50 KB (JPEG)
    ├─ HTTP 缓冲: 50 KB
    └─ 临时变量: <10 KB
```

#### 6.2.2 低内存模式

```cpp
// 检测可用内存
size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

if (psram_free < 1024 * 1024) {  // < 1 MB
    ESP_LOGW(TAG, "Low memory mode enabled");
    
    // 降低分辨率
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QQVGA);  // 160×120
    
    // 降低 JPEG 质量
    s->set_quality(s, 20);  // 0-63，越大压缩率越高
}
```

### 6.3 网络优化

#### 6.3.1 分块传输

**优势:**
- 避免一次性分配大缓冲区
- 降低内存峰值
- 支持流式上传

```cpp
// 使用 Transfer-Encoding: chunked
http->SetHeader("Transfer-Encoding", "chunked");

// 分块发送
http->Write(part1, size1);  // 第一块
http->Write(part2, size2);  // 第二块
http->Write("", 0);         // 结束标记
```

#### 6.3.2 超时配置

```cpp
// HTTP 超时设置
http->SetConnectTimeout(5000);   // 连接超时 5 秒
http->SetReadTimeout(30000);     // 读取超时 30 秒
```

#### 6.3.3 重试机制

```cpp
const int MAX_RETRIES = 3;
int retry_count = 0;

while (retry_count < MAX_RETRIES) {
    if (http->Open("POST", explain_url_)) {
        // 上传成功
        break;
    }
    
    retry_count++;
    ESP_LOGW(TAG, "Upload failed, retry %d/%d", retry_count, MAX_RETRIES);
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待 1 秒
}
```

### 6.4 并发控制

#### 6.4.1 互斥锁保护

```cpp
// 防止拍照和视频推流冲突
SemaphoreHandle_t camera_mutex_;

// 使用互斥锁
if (xSemaphoreTake(camera_mutex_, pdMS_TO_TICKS(1000)) == pdTRUE) {
    // 临界区：拍照或视频处理
    xSemaphoreGive(camera_mutex_);
} else {
    // 超时处理
    return "{\"success\": false, \"message\": \"Camera busy\"}";
}
```

#### 6.4.2 任务优先级

| 任务 | 优先级 | 说明 |
|------|--------|------|
| 音频处理 | 8 | 最高优先级 |
| 视频推流 | 6 | 次高优先级 |
| AI 拍照 | 5 | 中等优先级 |
| 后台任务 | 3 | 低优先级 |

---

## 7. 错误处理

### 7.1 错误分类

#### 7.1.1 配置错误

| 错误代码 | 错误消息 | 原因 | 解决方法 |
|---------|---------|------|---------|
| `CONFIG_NOT_SET` | Vision API not configured | 未通过 MCP initialize 配置 | 检查服务器配置 |
| `INVALID_URL` | Invalid Vision API URL | URL 格式错误 | 修正 URL 格式 |

```cpp
if (explain_url_.empty()) {
    return "{\"success\": false, \"error\": {\"code\": \"CONFIG_NOT_SET\", \"message\": \"Vision API not configured\"}}";
}
```

#### 7.1.2 硬件错误

| 错误代码 | 错误消息 | 原因 | 解决方法 |
|---------|---------|------|---------|
| `CAMERA_BUSY` | Camera busy | 相机正在被其他任务使用 | 稍后重试 |
| `CAPTURE_FAILED` | Failed to capture photo | 相机硬件故障 | 检查硬件连接 |
| `LOW_MEMORY` | Insufficient memory | 内存不足 | 释放其他资源 |

```cpp
if (xSemaphoreTake(camera_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return "{\"success\": false, \"error\": {\"code\": \"CAMERA_BUSY\", \"message\": \"Camera busy\"}}";
}
```

#### 7.1.3 网络错误

| 错误代码 | 错误消息 | 原因 | 解决方法 |
|---------|---------|------|---------|
| `CONNECTION_FAILED` | Failed to connect to Vision API | 网络不可达 | 检查网络连接 |
| `TIMEOUT` | Request timeout | 服务器响应超时 | 增加超时时间或重试 |
| `HTTP_ERROR` | HTTP error: XXX | HTTP 状态码错误 | 查看具体状态码 |

```cpp
if (!http->Open("POST", explain_url_)) {
    return "{\"success\": false, \"error\": {\"code\": \"CONNECTION_FAILED\", \"message\": \"Failed to connect\"}}";
}

if (http->GetStatusCode() != 200) {
    return "{\"success\": false, \"error\": {\"code\": \"HTTP_ERROR\", \"message\": \"HTTP " + std::to_string(http->GetStatusCode()) + "\"}}";
}
```

#### 7.1.4 API 错误

| 错误代码 | 错误消息 | 原因 | 解决方法 |
|---------|---------|------|---------|
| `UNAUTHORIZED` | Authentication failed | Token 无效或过期 | 更新 Token |
| `RATE_LIMIT` | Rate limit exceeded | API 调用频率超限 | 降低调用频率 |
| `INVALID_IMAGE` | Invalid image format | 图像格式不支持 | 检查图像格式 |

### 7.2 错误恢复策略

#### 7.2.1 自动重试

```cpp
bool RetryableError(const std::string& error_code) {
    return error_code == "TIMEOUT" || 
           error_code == "CONNECTION_FAILED" ||
           error_code == "HTTP_ERROR";
}

std::string ExplainWithRetry(const std::string& question, int max_retries = 3) {
    for (int i = 0; i < max_retries; i++) {
        std::string result = Explain(question);
        
        cJSON* json = cJSON_Parse(result.c_str());
        if (json) {
            auto success = cJSON_GetObjectItem(json, "success");
            if (cJSON_IsTrue(success)) {
                cJSON_Delete(json);
                return result;  // 成功
            }
            
            auto error = cJSON_GetObjectItem(json, "error");
            if (error) {
                auto code = cJSON_GetObjectItem(error, "code");
                if (code && !RetryableError(code->valuestring)) {
                    cJSON_Delete(json);
                    return result;  // 不可重试的错误
                }
            }
            cJSON_Delete(json);
        }
        
        ESP_LOGW(TAG, "Retry %d/%d after error", i + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000 * (i + 1)));  // 指数退避
    }
    
    return "{\"success\": false, \"error\": {\"code\": \"MAX_RETRIES\", \"message\": \"Max retries exceeded\"}}";
}
```

#### 7.2.2 降级策略

```cpp
// 网络不可用时，返回本地结果
if (!IsNetworkAvailable()) {
    return "{\"success\": true, \"description\": \"网络不可用，无法进行 AI 分析\", \"local\": true}";
}

// 内存不足时，降低图像质量
if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < 512 * 1024) {
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QQVGA);
    s->set_quality(s, 30);
}
```

### 7.3 日志记录

#### 7.3.1 日志级别

| 级别 | 使用场景 | 示例 |
|------|---------|------|
| `ERROR` | 关键错误 | `ESP_LOGE(TAG, "Camera init failed")` |
| `WARN` | 警告信息 | `ESP_LOGW(TAG, "Low memory detected")` |
| `INFO` | 重要信息 | `ESP_LOGI(TAG, "Photo captured: 20KB")` |
| `DEBUG` | 调试信息 | `ESP_LOGD(TAG, "Frame %d processed", i)` |

#### 7.3.2 关键日志点

```cpp
// 1. 拍照开始
ESP_LOGI(TAG, "开始相机预热，让传感器参数稳定...");

// 2. 每帧捕获
ESP_LOGI(TAG, "获取第%d帧: %dx%d, 大小: %d字节", i+1, fb_->width, fb_->height, fb_->len);

// 3. 质量检查
if (fb_->len < 3000) {
    ESP_LOGW(TAG, "图像可能有问题，大小: %d字节", fb_->len);
}

// 4. 上传开始
ESP_LOGI(TAG, "上传图像到 Vision API: %s", explain_url_.c_str());

// 5. 上传结果
ESP_LOGI(TAG, "AI 分析结果: %s", result.c_str());

// 6. 错误日志
ESP_LOGE(TAG, "HTTP error: %d", http->GetStatusCode());
```

---

## 8. 使用示例

### 8.1 用户交互场景

#### 场景 1: 基础识图

**用户:** "看看这是什么？"

**系统流程:**
```
1. 语音识别 → "看看这是什么？"
2. LLM 理解 → 需要调用 camera.take_photo
3. 拍照 (900ms)
4. 上传 + AI 分析 (2s)
5. LLM 生成回复 → "我看到了一台笔记本电脑..."
6. TTS 播报
```

**总耗时:** ~3.5秒

#### 场景 2: 多轮对话

**用户:** "看看我的桌面"  
**设备:** "我看到了一台笔记本电脑、鼠标和键盘"

**用户:** "电脑是什么颜色的？"  
**设备:** "电脑是银色的"

> 注意：第二次提问不需要重新拍照，LLM 使用第一次的分析结果

#### 场景 3: 特定问题

**用户:** "这个水果新鲜吗？"

**系统流程:**
```
1. 拍照
2. question = "这个水果新鲜吗？"
3. Vision API 分析水果的颜色、表面状态
4. LLM 基于分析结果回答
```

### 8.2 集成示例代码

#### 8.2.1 初始化配置

```cpp
// main/application.cc

void Application::Start() {
    // 1. 获取相机实例
    auto camera = Board::GetInstance().GetCamera();
    if (!camera) {
        ESP_LOGE(TAG, "Camera not available");
        return;
    }
    
    // 2. 通过 MCP 接收 Vision API 配置
    // 在 MCP initialize 时自动配置，无需手动调用
    
    // 3. 注册 MCP 工具
    McpServer::GetInstance().AddCommonTools();
}
```

#### 8.2.2 手动调用（测试）

```cpp
// 测试代码
void TestCameraExplain() {
    auto camera = Board::GetInstance().GetCamera();
    
    // 1. 拍照
    if (!camera->Capture()) {
        ESP_LOGE(TAG, "Capture failed");
        return;
    }
    
    // 2. AI 分析
    std::string result = camera->Explain("这是什么？");
    
    // 3. 解析结果
    cJSON* json = cJSON_Parse(result.c_str());
    if (json) {
        auto success = cJSON_GetObjectItem(json, "success");
        if (cJSON_IsTrue(success)) {
            auto description = cJSON_GetObjectItem(json, "description");
            if (cJSON_IsString(description)) {
                ESP_LOGI(TAG, "AI 分析: %s", description->valuestring);
            }
        }
        cJSON_Delete(json);
    }
}
```

#### 8.2.3 自定义 Vision API

```cpp
// 如果不使用 MCP，可以手动配置
void SetupCustomVisionAPI() {
    auto camera = Board::GetInstance().GetCamera();
    
    camera->SetExplainUrl(
        "https://my-vision-api.com/analyze",
        "Bearer my_custom_token"
    );
}
```

---

## 9. 配置参数

### 9.1 相机配置

#### 9.1.1 config.h 配置

```cpp
// main/boards/atk-dnesp32s3/config.h

/* 相机引脚配置 */
#define CAM_PIN_PWDN    GPIO_NUM_NC
#define CAM_PIN_RESET   GPIO_NUM_NC
#define CAM_PIN_VSYNC   GPIO_NUM_47
#define CAM_PIN_HREF    GPIO_NUM_48
#define CAM_PIN_PCLK    GPIO_NUM_45
#define CAM_PIN_XCLK    GPIO_NUM_NC
#define CAM_PIN_SIOD    GPIO_NUM_39  // I2C SDA
#define CAM_PIN_SIOC    GPIO_NUM_38  // I2C SCL

// 数据引脚
#define CAM_PIN_D0      GPIO_NUM_4
#define CAM_PIN_D1      GPIO_NUM_5
#define CAM_PIN_D2      GPIO_NUM_6
#define CAM_PIN_D3      GPIO_NUM_7
#define CAM_PIN_D4      GPIO_NUM_15
#define CAM_PIN_D5      GPIO_NUM_16
#define CAM_PIN_D6      GPIO_NUM_17
#define CAM_PIN_D7      GPIO_NUM_18

// XL9555 GPIO 扩展器控制
#define OV_PWDN_IO      4  // Power Down
#define OV_RESET_IO     5  // Reset
```

#### 9.1.2 相机初始化配置

```cpp
// main/boards/atk-dnesp32s3/atk_dnesp32s3.cc

camera_config_t config = {};

// 引脚配置
config.pin_pwdn = CAM_PIN_PWDN;
config.pin_reset = CAM_PIN_RESET;
config.pin_xclk = CAM_PIN_XCLK;
config.pin_sccb_sda = CAM_PIN_SIOD;
config.pin_sccb_scl = CAM_PIN_SIOC;
config.pin_d7 = CAM_PIN_D7;
config.pin_d6 = CAM_PIN_D6;
config.pin_d5 = CAM_PIN_D5;
config.pin_d4 = CAM_PIN_D4;
config.pin_d3 = CAM_PIN_D3;
config.pin_d2 = CAM_PIN_D2;
config.pin_d1 = CAM_PIN_D1;
config.pin_d0 = CAM_PIN_D0;
config.pin_vsync = CAM_PIN_VSYNC;
config.pin_href = CAM_PIN_HREF;
config.pin_pclk = CAM_PIN_PCLK;

// 时钟配置
config.xclk_freq_hz = 24000000;  // 24 MHz
config.ledc_timer = LEDC_TIMER_0;
config.ledc_channel = LEDC_CHANNEL_0;

// 图像配置
config.pixel_format = PIXFORMAT_JPEG;
config.frame_size = FRAMESIZE_QVGA;    // 320×240
config.jpeg_quality = 12;              // 0-63，越小质量越高
config.fb_count = 2;                   // 帧缓冲数量
config.fb_location = CAMERA_FB_IN_PSRAM;
config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
```

### 9.2 性能调优参数

#### 9.2.1 可调参数表

| 参数 | 位置 | 默认值 | 调优建议 |
|------|------|--------|----------|
| 预热帧数 | `Capture()` | 3 | 低端硬件可减少到 2 |
| 预热延迟 | `Capture()` | 200 ms | 可根据传感器调整 |
| 捕获帧数 | `Capture()` | 3 | 快速模式可减少到 2 |
| 捕获延迟 | `Capture()` | 150 ms | 稳定优先可增加到 200 ms |
| 质量阈值 | `Capture()` | 3000 bytes | 根据分辨率调整 |
| HTTP 超时 | `Explain()` | 30 s | 网络慢时可增加 |
| 重试次数 | - | 3 | 可根据可靠性要求调整 |

#### 9.2.2 场景预设

**快速模式:**
```cpp
const int WARMUP_FRAMES = 2;       // 预热帧数
const int WARMUP_DELAY = 150;      // 预热延迟 (ms)
const int CAPTURE_FRAMES = 2;      // 捕获帧数
const int CAPTURE_DELAY = 100;     // 捕获延迟 (ms)
const int MIN_SIZE = 2000;         // 最小图像大小 (bytes)
// 总耗时: ~600ms
```

**平衡模式 (默认):**
```cpp
const int WARMUP_FRAMES = 3;       // 预热帧数
const int WARMUP_DELAY = 200;      // 预热延迟 (ms)
const int CAPTURE_FRAMES = 3;      // 捕获帧数
const int CAPTURE_DELAY = 150;     // 捕获延迟 (ms)
const int MIN_SIZE = 3000;         // 最小图像大小 (bytes)
// 总耗时: ~900ms
```

**高质量模式:**
```cpp
const int WARMUP_FRAMES = 5;       // 预热帧数
const int WARMUP_DELAY = 300;      // 预热延迟 (ms)
const int CAPTURE_FRAMES = 5;      // 捕获帧数
const int CAPTURE_DELAY = 200;     // 捕获延迟 (ms)
const int MIN_SIZE = 5000;         // 最小图像大小 (bytes)
// 总耗时: ~2200ms
```

### 9.3 Vision API 配置

#### 9.3.1 通过 MCP 配置（推荐）

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "capabilities": {
      "vision": {
        "url": "https://vision-api.example.com/analyze",
        "token": "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
      }
    }
  }
}
```

#### 9.3.2 手动配置

```cpp
// 在代码中直接配置
auto camera = Board::GetInstance().GetCamera();
camera->SetExplainUrl(
    "https://my-vision-api.com/analyze",
    "Bearer my_token_here"
);
```

#### 9.3.3 环境变量配置（服务器端）

```bash
# .env 文件
VISION_API_URL=https://vision-api.example.com/analyze
VISION_API_TOKEN=your_token_here
```

---

## 10. 附录

### 10.1 支持的图像格式

| 格式 | 支持 | 说明 |
|------|------|------|
| JPEG | ✅ | 默认，推荐使用 |
| RGB565 | ❌ | 需要转换为 JPEG |
| YUV422 | ❌ | 需要转换为 JPEG |
| GRAYSCALE | ❌ | 需要转换为 JPEG |

### 10.2 支持的相机模组

| 型号 | 分辨率 | 帧率 | 支持 |
|------|--------|------|------|
| OV2640 | 最大 UXGA | 15 fps | ✅ |
| OV3660 | 最大 QXGA | 15 fps | ✅ |
| OV5640 | 最大 QSXGA | 15 fps | ✅ |

### 10.3 硬件要求

| 组件 | 最低要求 | 推荐配置 |
|------|----------|----------|
| MCU | ESP32-S3 | ESP32-S3 (8MB PSRAM) |
| PSRAM | 2 MB | 8 MB |
| Flash | 4 MB | 16 MB |
| 相机 | OV2640 | OV3660 或更高 |

### 10.4 常见问题

#### Q1: 图像太暗怎么办？

**A:** 调整传感器参数：
```cpp
s->set_ae_level(s, 3);      // 增加到 +3
s->set_aec_value(s, 300);   // 增加曝光时间
s->set_agc_gain(s, 20);     // 增加增益
```

#### Q2: 拍照速度慢怎么优化？

**A:** 使用快速模式：
- 减少预热帧数到 2
- 减少捕获帧数到 2
- 减少延迟到 100ms

#### Q3: Vision API 调用失败？

**A:** 检查：
1. 网络连接是否正常
2. Vision API URL 是否正确
3. Token 是否有效
4. 查看设备日志的详细错误信息

#### Q4: 图像质量不稳定？

**A:** 增加预热和捕获帧数：
- 预热帧数增加到 5
- 捕获帧数增加到 5
- 延迟增加到 200ms

#### Q5: 内存不足错误？

**A:** 降低图像分辨率：
```cpp
config.frame_size = FRAMESIZE_QQVGA;  // 160×120
config.jpeg_quality = 20;             // 增加压缩率
```

### 10.5 相关文档

- [ESP32 Camera Driver 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/camera_driver.html)
- [MCP 协议规范](https://modelcontextprotocol.io/specification/2024-11-05)
- [HTTP multipart/form-data 规范](https://www.w3.org/TR/html401/interact/forms.html#h-17.13.4)

---

## 11. 更新日志

| 版本 | 日期 | 变更内容 |
|------|------|---------|
| 1.0.0 | 2025-01-11 | 初始版本发布 |

---

**文档版本**: 1.0.0  
**最后更新**: 2025-01-11  
**维护者**: AR Glasses Team

**联系方式:**  
如有问题或建议，请通过 GitHub Issues 反馈。

