#ifndef STARTUP_STATE_H
#define STARTUP_STATE_H

#include "../../Application/Application.h"

class StartupState {
public:
    // 状态进入时的逻辑
    static void Enter(Application* app);

    // 状态退出时的逻辑
    static void Exit(Application* app);
};

#endif // STARTUP_STATE_H