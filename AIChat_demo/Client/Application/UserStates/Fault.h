#ifndef FAULT_STATE_H
#define FAULT_STATE_H

#include "../../Application/Application.h"

class FaultState {
public:
    // 状态进入时的逻辑
    static void Enter(Application* app);

    // 状态退出时的逻辑
    static void Exit(Application* app);
};

#endif // FAULT_STATE_H