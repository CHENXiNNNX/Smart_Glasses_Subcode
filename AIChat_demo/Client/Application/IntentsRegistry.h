#ifndef INTENTS_REGISTRY_H
#define INTENTS_REGISTRY_H

#include "../Intent/IntentHandler.h"
#ifdef __arm__
#include <json/json.h>
#else
#include <jsoncpp/json/json.h>
#endif
#include <unordered_map>
#include <string>

class IntentsRegistry {
public:
    // 汇总注册所有函数到 IntentHandler
    static void RegisterAllFunctions(IntentHandler& intent_handler);

    // 生成注册消息的 JSON
    static Json::Value GenerateRegisterMessage();
};

#endif // INTENTS_REGISTRY_H