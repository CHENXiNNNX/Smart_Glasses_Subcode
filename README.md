### 📒 Overview

详见Smart_Glasses_demo文件夹，这个文件夹里面的app为工程的主要代码内容，组成了Smart_Glasses项目, 详细如何编译和使用见[Smart_Glasses_demo](./Smart_Glasses_demo/README.md).

### 📑 智能眼镜整体框架：

实现的功能如下：

```

项目的文件目录如下:

```
Smart_Glasses_demo/
├── main.cpp                  # 主程序入口
├── CMakeLists.txt            # 构建配置文件
├── app/                      # 应用核心模块
│   ├── app.c/app.h           # 应用主逻辑
│   ├── chatbot/              # AI聊天机器人模块
│   ├── media/                # 媒体处理模块
│   │   ├── camera/           # 相机处理子模块
│   │   └── audio/            # 音频处理子模块
|   |
|———|—— devices/              #此分支新增
|   |   ├── icp20100/         #气压温度处理模块   
|   |   |—— mpr121/           #按键触摸处理模块
|   |   |—— qmi8658/          #六轴模块处理模块
|   |   |__ tg28_powerkey/    #电源按键处理模块
|   |
|   |
│   └── protocol/             # 通信协议模块
│       ├── rtsp/             # RTSP协议实现
│       ├── webrtc/           # WebRTC协议实现
│       └── websocket/        # WebSocket协议实现
├── 3rdparty/                 # 第三方库
├── utils/                    # 工具模块
├── rkmpi/                    # 瑞芯微MPI框架
├── libdatachannel/           # WebRTC网络通信库
└── conf/                     # 配置文件
```
