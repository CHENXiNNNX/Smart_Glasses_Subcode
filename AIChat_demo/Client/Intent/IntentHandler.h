// IntentHandler.h
#ifndef INTENT_HANDLER_H
#define INTENT_HANDLER_H

#include <string>
#include <unordered_map>
#include <functional>
#ifdef __arm__
#include <json/json.h>
#else
#include <jsoncpp/json/json.h>
#endif

class IntentHandler {
public:
    using Callback = std::function<void(const Json::Value& arguments)>;

    // 注册回调函数
    static void RegisterFunction(const std::string& function_name, Callback callback);

    // 处理服务器发送的意图数据
    static void HandleIntent(const Json::Value& intent_data);

private:
    static std::unordered_map<std::string, Callback> function_map_; // 存储 function_name 和回调函数的映射
};

#endif // INTENT_HANDLER_H