
## AI语言助手demo(Client端)

<p align="center">
    <img border="1px" width="100%" src="../assets/AIchat_states.png">
</p>

### 库安装

使用编译之前请安装CmakeList涉及到的库(如果不在电脑本地跑测试可以忽略):

1. jsoncpp

```sh
sudo apt-get install libjsoncpp-dev
```

2. opus

```sh
sudo apt install libopus-dev
```

3. portaudio(依赖ALSA)

```sh
sudo apt-get install libasound-dev
sudo apt-get -y install libportaudio2
```

4. websocketpp(依赖boost)

```sh
sudo apt-get install libboost-dev

git clone https://github.com/zaphoyd/websocketpp.git
cd websocketpp #进入目录
cmake CMakeList.txt #执行cmake
sudo make
sudo make install
```

安装好websocketpp之后，查看是否系统有头文件了

```sh
ls /usr/local/include/websocketpp
```

### 如何编译&运行：

**注意：** Client运行之前，确保先把Server服务器跑起来，才能正常工作

#### 1. X86 (电脑端测试)： 
```sh
mkdir build && cd build
cmake ..
make
```
然后运行即可, 这里的地址参数为Server端的IP地址, 按照你自己的设置改即可：
```sh
./build/AIChatClient 192.168.211.1 8000 123456
```

#### 2. arm:

注意, 需要先修改toolchain.cmake中的SDK路径

这个也是一样，需要确保SDK的buildroot已经设置好库并编译好了, 因为会用到sysroot里面的lib和inc(涉及到的库)

```sh
mkdir build && cd build
cmake -DTARGET_ARM=ON ..
make
make install
```

最后生成的demo文件夹发送到开发板执行即可

```sh
cd ./rv1106_AIChatClient_demo
chmod +x ./bin/AIChatClient
./bin/AIChatClient 172.32.0.100 8000 123456
```

#### 3. 清除:

```sh
# in dir: build
make clean-all
```

### Websockets协议定义：

以下是Client端会向Server端发送的信息:

1. 鉴权信息：

   ```
   Authorization: "Bearer " + access_token
   Device-Id: MAC address
   Protocol-Version: 定义的协议版本
   ```

2. 发送参数

   ```json
   {
       "type": "hello",
       "audio_params": {
           "format": "opus",
           "sample_rate": "16000",
           "channels": "1",
           "frame_duration": "40"
       }
   }
   ```

3. 发送状态改变

   ```json
   {
       "type": "state", 
       "state": "idle" 
   }
   ```
    "state"还包括listening等，详见代码

4. 打包发送的音频数据

   ```cpp
   struct BinProtocol {
       uint16_t version;       //协议版本
       uint16_t type;          //0为音频数据
       uint32_t payload_size;  //音频数据长度
       uint8_t payload[];      //opus音频数据
   } __attribute__((packed));
   ```

5. 函数注册

```json
{
    "type": "reg_func",
    "functions": [
        {
            "name": "robot_move",
            "description": "让机器人运动",
            "arguments": {
                "direction": "字符数据,分别有forward,backward,left和right"
            }
        },
        {
            "name": "xxx",
            "description": "描述xxxxx",
            "arguments": {
                "arg1": "描述xxx",
                "arg2": "描述xxx"
            }
        }
    ]
}
```

6. 可能接收到的意图

```json
{
    "function_call": {
        "name": "robot_move",
        "arguments": {
            "direction": "forward",
            "speed": "1"
        }
    }
}
```