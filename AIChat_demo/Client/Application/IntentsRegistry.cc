#include "IntentsRegistry.h"
#include "UserIntents/RobotMove.h"

void IntentsRegistry::RegisterAllFunctions(IntentHandler& intent_handler) {
    // 注册机器人移动相关的函数
    intent_handler.RegisterFunction("robot_move", RobotMove::Move);

    // 如果有其他功能模块，可以在这里继续注册
    // intent_handler.RegisterFunction("audio_play", AudioControl::Play);
}

Json::Value IntentsRegistry::GenerateRegisterMessage() {
    Json::Value message;
    message["type"] = "functions_register";

    // 添加 robot_move 的元信息
    Json::Value robot_move;
    robot_move["name"] = "robot_move";
    robot_move["description"] = "让机器人运动";

    // 添加多个 arguments
    Json::Value arguments;
    arguments["direction"] = "字符数据,分别有forward,backward,left和right";
    arguments["speed"] = "整数数据,表示运动速度";
    arguments["duration"] = "浮点数,表示运动持续时间（秒）";
    robot_move["arguments"] = arguments;

    // 将 robot_move 添加到 functions 数组中
    message["functions"].append(robot_move);

    // 如果有其他功能模块，可以在这里继续添加元信息
    // Json::Value audio_play;
    // audio_play["name"] = "audio_play";
    // audio_play["description"] = "播放音频文件";
    // Json::Value audio_arguments;
    // audio_arguments["file"] = "字符串,表示音频文件路径";
    // audio_arguments["volume"] = "整数数据,表示音量大小";
    // audio_play["arguments"] = audio_arguments;
    // message["functions"].append(audio_play);

    return message;
}