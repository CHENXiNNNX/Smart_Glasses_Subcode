#ifndef THINKING_STATE_H
#define THINKING_STATE_H

#include "../../Application/Application.h"
#include <vector>
#include <thread>
#include <atomic>

class ThinkingState {
public:
    static void Enter(Application* app);
    static void Exit(Application* app);
};

#endif // THINKING_STATE_H