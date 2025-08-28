// RobotMove.cc
#include "RobotMove.h"
#include <iostream>
#include "../../Utils/user_log.h"

namespace RobotMove {
    void Move(const Json::Value& arguments) {
        // 从 arguments 中获取 "direction" 参数
        if (arguments.isMember("direction") && arguments["direction"].isString()) {
            std::string direction = arguments["direction"].asString();
            USER_LOG_INFO("RobotMove::Move direction: %s", direction.c_str());
        } else {
            USER_LOG_INFO("Invalid or missing 'direction' argument in RobotMove::Move");
        }
    }
}