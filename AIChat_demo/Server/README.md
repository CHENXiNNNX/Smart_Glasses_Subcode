## AI语言助手demo(Server端)

### 环境搭建

这里默认大家都是用的自己的电脑搭建服务，默认同学们都没有GPU（有就更好）

首先创建虚拟环境, 不然容易污染你的系统环境, 作者使用的python3.10。环境名字就起名`AIChatServerEnv`好了，环境名可自定义。

``` sh
cd ./your-path
conda create --prefix ./AIChatServerEnv python=3.10
```

然后启动虚拟环境，并安装所需要的包，如果下载不了需要科学上网

``` sh
conda activate ./AIChatServerEnv
pip install -r ./requirments.txt
```

搭建完毕，直接运行即可了, access_token是Client端匹配的密码，aliyun_api_key是阿里云的API key，用于访问通义千问

``` sh
python ./main.py --access_token="123456" --aliyun_api_key="sk-your-api-key"
```

### 文件目录介绍

```sh
Server/
├── config/                # 全局设置
├── handle/                # ws接收内容的处理
|   ├── audio_handle.py    # 音频数据处理
|   ├── auth_handle.py     # 鉴权
│   └── text_handle.py     # 文本数据处理
├── models/                # 
├── services/              # 
├── test/                  # 单功能测试
├── threads/               # 多线程相关
├── tools/                 # 工具
|   ├── audio_processor.py # 音频处理
|   ├── logger.py          # log
│   └── registry.py        # 意图注册
├── ws_server.py           # websocket server 业务
├── service_manager.py     # services 全局管理
└── main.py
```

### WebSockets协议说明

以下是Server端会向Client端发送的信息:

1. 鉴权信息：

   ```json
   {
      "type": "auth",
      "message": "Authentication failed" 
   }
   ```
   "message"还包括: "Client authenticated"

2. VAD检测到说话的活跃状态

   ```json
   {
      "type": "vad",
      "state": "no_speech" 
   }
   ```
   "state"还包括: "end", "too_long"

3. ASR识别到说话的文字

   ```json
   {
       "type": "asr",
       "text": "speech的内容"
   }
   ```

4. tts生成语音完毕

   ```json
   {
      "type": "tts",
      "state": "end"
   }
   ```
   "state"还包括: "continue"

5. 对话结束

   ```json
   {
      "type": "chat",
      "dialogue": "end"
   }
   ```
   "state"还包括: "continue"


6. 打包发送的音频数据

   ```python
    version: 协议版本 (2 字节)
    type: 消息类型 (2 字节)
    payload: opus格式消息负载 (字节)
   ```