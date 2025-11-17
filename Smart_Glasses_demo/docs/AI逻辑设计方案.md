1. AI对话状态机（只列举这一个，其他可以维持v2版本的样子）
    STARTING,           // 启动中 --- 处理设备ws连接服务器的操作
    IDLE,               // 空闲
    LISTENING,          // 监听中
    THINKING,           // 思考中
    SPEAKING,           // 说话中
    ERROR               // 错误状态

2. AI流程
- 检测是否联网 → 未联网则进入配网模式(暂留空实现，先只给日志提示没联网) → 成功联网后则进入设备激活检测
- 设备未激活则等待激活(阻塞式) → 激活成功后则先进行ws的ai服务器的连接操作，连接成功后进入待机状态(IDLE)
- 待机状态 → 等待唤醒词(音频数据不上传服务器)
- 检测到唤醒词 → 我这边默认发送“唤醒词到ai服务器识别”并得到ai回复(暂留空实现)
- ai回复后 → 进入监听(开始监听音频，此时音频数据上传到服务器做处理，此时数据不再喂给唤醒词) → STT → LLM → TTS
- TTS结束(进行连续对话需要在ai回复的句子结束后，发送一个listening消息表示需要继续对话，服务器不会给与回复) → 继续监听 → 如果持续没有数据进行ai对话的话服务器会自动进入待机模式，同时关闭连接，这个时候设备端需要处理ws断开连接后的自动重连操作，并回到IDLE状态等待下次唤醒（等待下次唤醒词唤醒）
注意：ai对话在处理的时候，位于thinking状态，在进行ai对话的回复的时候位于speaking状态，要注意处理在服务器持续没有数据进行ai对话的时候的断联操作，通过自动重连恢复连接

3. HELLO消息的配置(关键)

4. 对于服务器下发的opus音频数据，需要跳过包头直接对opus的音频数据进行处理，服务器下发的opus包头的信息不可用，是服务器那边自定义的格式

5. 在进行v3版本的代码重写的时候，主要注意不需要过多的日志消息，只需要重点的日志就可以了，同时使用中文进行描述，并且不要进行过度的异常保护，代码封装以及状态机增加复杂性，但是依旧保持我原有的c++实现风格，同时要注意命名空间的书写，命名空间的书写需要规范:如/app/chatbot/chatbot.cc的代码文件的话，建议的命名空间为目录指引即(app::chatbot)，而/app/protocol/websocket/websocket.cc的代码文件的话，建议的命名空间为目录指引即(app::protocol::websocket)的命名空间形式。最后，我不需要过多的日志统计



以下是官方的ws通信协议示例介绍，可以参考，但是不需要按它配置的音频参数发送，按我当前的v2版本的配置进行发送

通信协议：Websocket 连接
基本信息

- 协议版本: 1
- 传输方式: Websocket
- 音频格式: OPUS
- 音频参数:
  - 采样率: 16000Hz 
  - 通道数: 1
  - 帧长: 60ms

连接建立

1. 客户端连接Websocket服务器时需要携带以下headers:
Authorization: Bearer <access_token>
Protocol-Version: 1
Device-Id: <设备MAC地址>
Client-Id: <设备UUID>
设备MAC地址和UUID都是设备唯一识别码。

2. 连接成功后，客户端发送hello消息:
{
    "type": "hello",
    "version": 1,
    "transport": "websocket",
    "features": {
        "mcp": true
    },
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}

3. 服务端响应hello消息:
{
    "type": "hello",
    "transport": "websocket",
    "audio_params": {
        "format": "opus",
        "sample_rate": 24000,
        "channels": 1,
        "frame_duration": 60
    }
}
Websocket协议不返回 session_id，所以消息中的会话ID可设置为空。

消息类型

1. 语音识别相关消息

开始监听
{
    "session_id": "<会话ID>",
    "type": "listen",
    "state": "start",
    "mode": "<监听模式>"
}
监听模式:
- "auto": 自动停止
- "manual": 手动停止
- "realtime": 持续监听
auto 与 realtime 是服务器端 VAD 的两种工作模式，realtime 需要 AEC 支持。

停止监听
{
    "session_id": "<会话ID>",
    "type": "listen",
    "state": "stop"
}

唤醒词检测
{
    "session_id": "<会话ID>",
    "type": "listen",
    "state": "detect",
    "text": "<唤醒词>"
}

2. 语音合成相关消息

服务端发送的TTS状态消息:
{
    "type": "tts",
    "state": "<状态>",
    "text": "<文本内容>" // 仅在 sentence_start 时携带
}
状态类型:
- "start": 开始播放
- "stop": 停止播放  
- "sentence_start": 新句子开始

3. 中止消息
{
    "session_id": "<会话ID>",
    "type": "abort",
    "reason": "wake_word_detected" // 可选
}

4. MCP 相关消息
客户端 / 服务端:
{
    "session_id": "<会话ID>",
    "type": "mcp",
    "payload": <MCP Paylaod>
}

5. 情感状态消息
服务端发送:
{
    "type": "llm",
    "emotion": "<情感类型>"
}

二进制数据传输

- 音频数据使用二进制帧传输
- 客户端发送OPUS编码的音频数据
- 服务端返回OPUS编码的TTS音频数据

错误处理

当发生网络错误时，客户端会收到错误消息并关闭连接。客户端需要实现重连机制。

会话流程

1. 建立Websocket连接
2. 交换hello消息
3. 开始语音交互:
  - 发送开始监听
  - 发送音频数据
  - 接收识别结果
  - 接收TTS音频
4. 结束会话时关闭连接