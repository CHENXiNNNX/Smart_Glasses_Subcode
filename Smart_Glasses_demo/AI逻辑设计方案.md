### AI状态机设计
1. 状态设计
enum class ChatbotState {
    fault,      // 故障状态
    startup,    // 启动状态
    stopping,   // 停止状态
    idle,       // 空闲状态
    listening,  // 聆听状态
    thinking,   // 思考状态
    speaking,   // 说话状态
};
2. 状态变换流程
程序启动(startup) --> 开始建立ws连接(startup) --> 建立成功(idle) --> 开始唤醒词检测(idle) --> 唤醒词检测成功 --> 进入聆听状态(listening) --> 检测到语音 --> 思考对话(thinking) --> 开始回答(speaking)
                                            --> 建立失败(startup) --> 进行重连(startup) --> 回到开始建立ws连接步骤 

