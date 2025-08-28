#ifndef STOP_STATE_H
#define STOP_STATE_H

#include "../../Application/Application.h"
#include <vector>
#include <thread>
#include <atomic>

class StopState {
public:
    static void Enter(Application* app);
    static void Exit(Application* app);
};

#endif // STOP_STATE_H