# 设备端A ， 服务器B

1. 设备端通过 WebSocket 连接服务器，建立长连接

2. 媒体传输配置
(1)图片发送
-jpeg格式
-640x480
-每5秒上传一张
(2)音频发送
-opus格式
-48000hz
-单声道
-20ms帧
-32kbps码率
-实时上传

3. 功能
(1)设备端作为 WebSocket 客户端，主动连接服务器
(2)服务器接收连接后，按消息类型分别保存图片和音频
(3)支持断线重连

# 消息格式

所有消息均为二进制，统一格式如下:
[1字节类型][4字节长度大端][payload数据]

类型定义:
- 0x01: JPEG图片
- 0x02: Opus音频帧

示例(伪代码):
```
JPEG消息: [0x01][length_be_4bytes][jpeg_binary_data]
Opus消息: [0x02][length_be_4bytes][opus_frame_data]
```

# 完整连接流程

1. 设备端A ---> 服务器B
(1)设备端A启动后，根据配置的服务器地址发起 WebSocket 连接请求，默认地址为 ws://{host}:8000

(2)服务器B监听端口(默认8000)，接收到连接请求后建立 WebSocket 连接

(3)连接建立后，设备端A同时启动相机和音频模块:
   - 相机以 JPEG 流模式运行，在 set_jpeg_cb 回调中按5秒间隔上传图片
   - 音频以采集+Opus编码模式运行，在 set_capture_cb 回调中实时上传 Opus 帧

(4)设备端A发送图片消息时，先检查 WebSocket 连接状态，若已连接则构建消息并 sendBinary 发送。消息格式如下:
```
字节0: 0x01 (JPEG类型)
字节1-4: 图片数据长度，大端序
字节5起: JPEG 二进制数据
```

(5)设备端A发送音频消息时，同样检查连接状态后构建消息并 sendBinary 发送。消息格式如下:
```
字节0: 0x02 (Opus类型)
字节1-4: Opus帧数据长度，大端序
字节5起: Opus 帧二进制数据
```

(6)服务器B收到二进制消息后，解析类型和长度，根据类型执行不同逻辑:
   - 类型0x01: 将 payload 写入 data/images/ 目录，文件名 capture_{sessionId}_{timestamp}.jpg
   - 类型0x02: 将 payload 作为 Opus 帧追加写入 Ogg 容器，保存至 data/audio/ 目录，文件名 audio_{sessionId}.ogg

(7)连接断开时，设备端A会定期(每10秒)尝试重连; 服务器B在连接关闭时完成当前会话的 Ogg 文件写入

# 服务器存储说明

1. 图片
- 目录: data/images/
- 命名: capture_{sessionId}_{timestamp}.jpg
- 格式: 直接写入 JPEG 二进制

2. 音频
- 目录: data/audio/
- 命名: audio_{sessionId}.ogg
- 格式: 将接收到的 Opus 帧按序封装为 Ogg Opus 文件(RFC 7845)，可用 ffplay 等播放器播放
