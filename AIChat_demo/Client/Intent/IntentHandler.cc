// IntentHandler.cc
#include "IntentHandler.h"
#include <iostream>
#include "../Utils/user_log.h"

// 定义静态成员变量
std::unordered_map<std::string, IntentHandler::Callback> IntentHandler::function_map_;

void IntentHandler::RegisterFunction(const std::string& function_name, Callback callback) {
    if (function_map_.find(function_name) != function_map_.end()) {
        USER_LOG_WARN("Function %s is already registered.", function_name.c_str());
    }
    function_map_[function_name] = callback;
}

void IntentHandler::HandleIntent(const Json::Value& intent_data) {
    try {
        // 检查是否包含 "function_call"
        if (intent_data.isMember("function_call")) {
            const Json::Value& function_call = intent_data["function_call"];
            std::string function_name = function_call["name"].asString();
            const Json::Value& arguments = function_call["arguments"];

            // 查找并执行回调函数
            if (function_map_.find(function_name) != function_map_.end()) {
                function_map_[function_name](arguments);
            } else {
                std::cerr << "No callback registered for function: " << function_name << std::endl;
            }
        } else {
            std::cerr << "Invalid intent data: missing 'function_call'" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error handling intent: " << e.what() << std::endl;
    }
}