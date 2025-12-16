## Smart_Glasses Demo

智能眼镜演示程序 - 完整的音视频采集、AI对话、WebRTC通信系统

### 项目结构

```
Smart_Glasses_demo/
├── main.cpp                    # 程序主入口
├── app/                        # 应用程序核心模块
│   ├── app.cc                  # 应用程序主逻辑实现
│   ├── app.hpp                 # 应用程序接口定义
│   │
│   ├── chatbot/                # 聊天机器人核心模块
│   │   ├── chatbot.cc          # 聊天机器人主实现
│   │   ├── chatbot.hpp         # 聊天机器人接口定义
│   │   ├── activation/         # 设备激活模块
│   │   ├── mcp/                # MCP协议服务器
│   │   │   └── mcp_tool/       # MCP工具注册管理
│   │   ├── protocol_handle/    # 协议消息处理
│   │   ├── statemachine/       # AI对话状态机
│   │   └── wakeword/           # 唤醒词检测
│   │
│   ├── media/                  # 媒体处理模块
│   │   ├── audio/              # 音频处理子模块（采集、播放、编解码）
│   │   ├── camera/             # 相机处理子模块（采集、编码、ISP）
│   │   ├── sync.cc             # 音视频时间同步
│   │   ├── sync.hpp
│   │   └── media_config.hpp    # 媒体配置文件
│   │
│   ├── protocol/               # 通信协议模块
│   │   ├── webrtc/             # WebRTC协议实现
│   │   ├── websocket/          # WebSocket协议实现
│   │   ├── http/               # HTTP客户端
│   │   ├── mqtt/               # MQTT协议实现
│   │   ├── rtsp/               # RTSP协议实现
│   │   └── protocol_config.hpp # 协议配置文件
│   │
│   ├── network/                # 网络模块
│   │   ├── wifi/               # WiFi管理
│   │   ├── bluetooth/          # 蓝牙管理
│   │   ├── lte/                # LTE管理
│   │   ├── network.cc          # 网络接口
│   │   └── network.hpp
│   │
│   ├── tool/                   # 工具模块
│   │   ├── log/                # 日志系统
│   │   ├── mac/                # MAC地址获取
│   │   ├── uuid/               # UUID生成和管理
│   │   └── memory/             # 内存池管理
│   │
│   └── battery/                # 电池管理模块
│
├── test/                       # 测试程序
│   ├── test_camera_main.cpp    # 相机测试
│   ├── test_audio_main.cpp     # 音频测试
│   ├── test_webrtc_main.cpp    # WebRTC测试
│   ├── test_websocket_main.cpp # WebSocket测试
│   ├── test_wifi_main.cpp      # WiFi测试
│   ├── test_ai_chatbot.cpp     # AI聊天机器人测试
│   ├── test_mac_main.cpp       # MAC地址测试
│   ├── test_uuid_main.cpp      # UUID测试
│   ├── test_mempool_main.cpp   # 内存池测试
│   ├── test_opencv_main.cpp    # OpenCV测试
│   └── test_v4l2_main.cpp      # V4L2测试
│
├── common/                     # 通用代码
├── docs/                       # 文档
├── utils/                      # 工具脚本
├── third_party/                # 第三方依赖
├── build/                      # 编译输出目录
├── bin/                        # 可执行文件目录
├── CMakeLists.txt              # CMake构建配置
├── toolchain.cmake             # 交叉编译工具链配置
├── package.json                # Node.js依赖配置
├── signaling_server.js         # 信令服务器
└── webrtc_server.js            # WebRTC服务器
```

### 核心功能模块

- **媒体处理**：音视频采集、编码、播放、时间同步
- **AI对话**：唤醒词检测、语音识别、LLM对话、TTS合成
- **网络通信**：WiFi连接、WebRTC实时音视频传输、WebSocket信令
- **协议支持**：HTTP、WebSocket、WebRTC、MQTT、RTSP
- **设备管理**：MAC地址、UUID、电池管理

### 编译和运行

详见各模块文档或构建脚本说明。