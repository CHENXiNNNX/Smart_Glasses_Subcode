#ifndef ROBOT_MOVE_H
#define ROBOT_MOVE_H

#ifdef __arm__
#include <json/json.h>
#else
#include <jsoncpp/json/json.h>
#endif

namespace RobotMove {
    void Move(const Json::Value& arguments);
}

#endif // ROBOT_MOVE_H