# 设备端A  ，  服务器B  ,  APP端C
1. 初始时均默认已连接服务器

2. 音视频传输配置
(1)音频收发
-opus格式
-48000hz
-实时的双向音频通信
(2)视频发送
-h264格式
-实时的设备端到APP端的单向发送视频

3.功能:
(1)WebRTC角色固定：无论谁先加入房间或谁先发送连接请求，设备端始终作为WebRTC的offerer(发起方)，APP端始终作为answerer(应答方)
   - 设备端创建并发送SDP offer
   - APP端接收offer后创建并发送SDP answer
   - 双方交换ICE候选信息
(2)服务器会根据房间信息下发规则，进行房间信息下发，下发规则如下:
   - 某一房间内的人员变动之后会下发一次对应房间的房间信息给该房间内的所有客户端
   - 有连接服务器的用户向服务器进行申请查询房间信息的时候，会将信息下发给请求的用户
   - 待定

# 完整的连接流程
1. 设备端A ---> APP端C
(1)设备端A发送连接请求给服务器B,连接请求的格式如下:
{
  "type": "call_request",
  "from": {
    "device_id": "xxx",        // mac地址
    "client_id": "xxx"         // uuid
  },
  "to": {
    "device_id": "xxx",        // mac地址
    "client_id": "xxx"         // uuid
  },
  "data": {                 // 这个data只需要设备端接收到处理就行了，APP端如果接收到则不需要处理，只是视为连接请求
    "message": true/false,  // 当为true，则通知设备端打开消息通道，进行消息互通发送，反之则不开
    "audio": true/false,    // 当为true，则通知设备端打开音频通道，进行音频互通发送，反之则不开
    "video": true/false     // 当为true，则通知设备端打开视频通道，进行视频互通发送，反之则不开(这里的视频数据发送只是做设备端到APP端的发送)
  },
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

(2)服务器B接收到连接请求之后，会根据连接请求中的信息，即“to“字段对应的信息，然后将连接请求消息转发给APP端C

(3)APP端C接收处理服务器转发来的连接请求后，会进行处理，此时可以选择接受或者拒绝请求，并将回答的结果告知服务器B，由服务器B中转给设备端A,
连接请求回应的消息格式如下:
{
  "type": "call_respond",
  "from": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "respond": "accept/refuse",   // 接受:accept 拒绝:refuse
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

(4)服务器B在得知APP端C的连接请求回应的消息之后，会根据回应的结果执行不同的逻辑。如果APP端C接受了设备端A的连接请求的话，则服务器B首先会去进行建立房间的
操作，服务器B会生成的随机且唯一的房间id作为"房间号"，同时还会随机生成一个这个房间对应的一个"6位数"的房间密码，一起作为房间的信息，在服务器B完成了建立
房间的操作之后，随后服务器就会开始下一步，将之前的APP端C回应的连接请求回应消息转发给设备端A，随后就是服务器下发房间信息给双方(设备端A和APP端C)，告知双方
可以进入房间建立连接了;如果APP端C拒绝了设备端A的连接请求的话，则服务器B直接将APP端C回应的连接请求回应消息转发给设备端A即可，而不进行其它操作。房间信息的
消息格式如下:
{
  "type": "room_info",
  "from": "server",          // 服务器下发的，则固定死为server
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "data": {
    "room_id": "xxx",        // xxx为房间id
    "room_password": "xxx",  // xxx为房间密码
    "num": "xxx"               // xxx为房间内人数
  },
  "time": ISO 8601 格式的时间戳字符串（UTC）
} 

(5)设备端A在收到APP端C回应的连接请求的回应消息之后，则会进行相应的处理。如果APP端C接收了连接请求，则设备端A则会解析服务器下发的房间信息加入对应的房间，与此同时
的APP端C也会根据服务器B下发的房间信息加入对应的房间；如果APP端C拒绝了连接请求，则不进行后续流程。

(6)服务器B在检测到双方都进入房间之后，则会下发消息给设备端A，告知对端已就绪，已经可以开始进行连接的操作了。注意：由于设备端始终作为WebRTC的offerer（发起方），因此只需要通知设备端开始创建并发送offer；APP端作为answerer（应答方）只需等待接收offer即可，无需接收此消息。告知已就绪的消息格式如下:
{
  "type": "start_connect",
  "from": "server",             // 服务器下发的，则固定死为server
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

(7)后续就是交换SDP和ICE，最终完成连接
{
  "type": "offer",
  "from": {
    "device_id": "xxx"             // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx"             // mac地址
    "client_id": "xxx"             // uuid
  }
  "data": {},
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

{
  "type": "answer",
  "from": {
    "device_id": "xxx"             // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx"             // mac地址
    "client_id": "xxx"             // uuid
  }
  "data": {},
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

{
  "type": "ice",
  "from": {
    "device_id": "xxx"             // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx"             // mac地址
    "client_id": "xxx"             // uuid
  }
  "data": {},
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

2. APP端C ---> 设备端A
(1)APP端C发送连接请求给服务器B,连接请求的格式如下:
{
  "type": "call_request",
  "from": {
    "device_id": "xxx",        // mac地址
    "client_id": "xxx"         // uuid
  },
  "to": {
    "device_id": "xxx",        // mac地址
    "client_id": "xxx"         // uuid
  },
  "data": {                 // 这个data只需要设备端接收到处理就行了，APP端如果接收到则不需要处理，只是视为连接请求
    "message": true/false,  // 当为true，则通知设备端打开消息通道，进行消息互通发送，反之则不开
    "audio": true/false,    // 当为true，则通知设备端打开音频通道，进行音频互通发送，反之则不开
    "video": true/false     // 当为true，则通知设备端打开视频通道，进行视频互通发送，反之则不开(这里的视频数据发送只是做设备端到APP端的发送)
  },
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

(2)服务器B接收到连接请求之后，会根据连接请求中的信息，即“to“字段对应的信息，然后将连接请求消息转发给设备端A

(3)设备端A接收处理服务器转发来的连接请求后，会进行处理，此时可以选择接受或者拒绝请求，并将回答的结果告知服务器B，由服务器B中转给APP端C,
连接请求回应的消息格式如下:
{
  "type": "call_respond",
  "from": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "respond": "accept/refuse",  // 接受:accept 拒绝:refuse
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

(4)服务器B在得知设备端A的连接请求回应的消息之后，会根据回应的结果执行不同的逻辑。如果设备端A接受了APP端C的连接请求的话，则服务器B首先会去进行建立房间的
操作，服务器B会生成的随机且唯一的房间id作为"房间号"，同时还会随机生成一个这个房间对应的一个"6位数"的房间密码，一起作为房间的信息，在服务器B完成了建立
房间的操作之后，随后服务器就会开始下一步，将之前的设备端A回应的连接请求回应消息转发给APP端C，随后就是服务器下发房间信息给双方(设备端A和APP端C)，告知双方
可以进入房间建立连接了;如果设备端A拒绝了APP端C的连接请求的话，则服务器B直接将设备端A回应的连接请求回应消息转发给APP端C即可，而不进行其它操作。房间信息的
消息格式如下:
{
  "type": "room_info",
  "from": "server",             // 服务器下发的，则固定死为server
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "data": {
    "room_id": "xxx",           // xxx为房间id
    "room_password": "xxx",     // xxx为房间密码
    "num": "xxx"                  // xxx为房间内人数
  },
  "time": ISO 8601 格式的时间戳字符串（UTC）
} 

(5)APP端C在收到设备端A回应的连接请求的回应消息之后，则会进行相应的处理。如果设备端A接收了连接请求，则APP端C则会解析服务器B下发的房间信息加入对应的房间，与此同时
的设备端A也会根据服务器B下发的房间信息加入对应的房间；如果设备端A拒绝了连接请求，则不进行后续流程。

(6)服务器B在检测到双方都进入房间之后，则会下发消息给设备端A，告知对端已就绪，已经可以开始进行连接的操作了。注意：由于设备端始终作为WebRTC的offerer（发起方），因此只需要通知设备端开始创建并发送offer；APP端作为answerer（应答方）只需等待接收offer即可，无需接收此消息。告知已就绪的消息格式如下:
{
  "type": "start_connect",
  "from": "server",             // 服务器下发的，则固定死为server
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

(7)后续就是交换SDP和ICE，最终完成连接
{
  "type": "offer",
  "from": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "data": {},
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

{
  "type": "answer",
  "from": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "data": {},
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

{
  "type": "ice",
  "from": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "data": {},
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

# 补充的消息格式
1. 请求房间信息
{
  "type": "get_room_info",
  "from": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "to": "server",
  "time": ISO 8601 格式的时间戳字符串（UTC）
}

2. 错误信息
{
  "type": "error",
  "from": "server",
  "to": {
    "device_id": "xxx",            // mac地址
    "client_id": "xxx"             // uuid
  },
  "data": {
    "error_code": 错误码,
    "error_message": "错误描述信息"
  },
  "time": ISO 8601 格式的时间戳字符串（UTC）
}