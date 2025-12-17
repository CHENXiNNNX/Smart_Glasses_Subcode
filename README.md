## Smart Glasses Demo

智能眼镜完整演示项目 - 集成音视频采集、AI对话、WebRTC通信的嵌入式应用。

### 📁 项目结构

```
Smart_Glasses_demo/
├── main.cpp                    # 程序入口
├── CMakeLists.txt              # CMake构建配置
├── toolchain.cmake             # 交叉编译工具链
│
├── app/                        # 应用核心模块
│   ├── app.cc/hpp              # 应用主逻辑
│   │
│   ├── chatbot/                # AI聊天机器人
│   │   ├── chatbot.cc/hpp      # 聊天主逻辑
│   │   ├── activation/         # 设备激活
│   │   ├── mcp/                # MCP协议服务器
│   │   ├── protocol_handle/    # 协议消息处理
│   │   ├── statemachine/       # AI对话状态机
│   │   └── wakeword/           # 唤醒词检测
│   │
│   ├── media/                  # 媒体处理
│   │   ├── audio/              # 音频系统
│   │   ├── camera/             # 视频系统
│   │   ├── sync.cc/hpp         # 音视频时间同步
│   │   └── media_config.hpp    # 媒体配置
│   │
│   ├── protocol/               # 通信协议
│   │   ├── webrtc/             # WebRTC实现
│   │   ├── websocket/          # WebSocket客户端
│   │   ├── http/               # HTTP客户端
│   │   ├── mqtt/               # MQTT协议
│   │   └── rtsp/               # RTSP协议
│   │
│   ├── network/                # 网络管理
│   │   ├── wifi/               # WiFi
│   │   ├── bluetooth/          # 蓝牙
│   │   └── lte/                # 4G/LTE
│   │
│   ├── tool/                   # 工具库
│   │   ├── log/                # 日志系统
│   │   ├── memory/             # 内存池
│   │   ├── mac/                # MAC地址
│   │   └── uuid/               # UUID生成
│   │
│   └── battery/                # 电池管理
│
├── test/                       # 单元测试
├── signaling_server.js         # WebRTC信令服务器
└── webrtc_server.js            # WebRTC服务器
```

### 🎯 核心功能

| 模块 | 功能 | 技术栈 |
|------|------|--------|
| **音频** | 采集、播放、3A算法、Opus编解码 | PortAudio + Opus + Speex |
| **视频** | 采集、H264/H265编码、ISP控制、拍照录像 | RKMPI + RK AIQ |
| **AI对话** | 唤醒词检测、ASR、LLM、TTS | Porcupine + 云端API |
| **通信** | WebRTC实时音视频、WebSocket信令 | libdatachannel |
| **网络** | WiFi/蓝牙/LTE连接管理 | wpa_supplicant + BlueZ |

### 🔧 编译

```bash
# 交叉编译
mkdir build && cd build
cmake ..
make -j$(nproc)

```
