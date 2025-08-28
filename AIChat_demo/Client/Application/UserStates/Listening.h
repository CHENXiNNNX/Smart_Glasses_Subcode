#ifndef LISTENING_STATE_H
#define LISTENING_STATE_H

#include "../../Application/Application.h"

class ListeningState {
public:
    static void Enter(Application* app);
    static void Run(Application* app);
    static void Exit(Application* app);
private:
    static std::atomic<bool> state_running_;       // 静态成员变量：运行状态标志
    static std::thread state_running_thread_;      // 静态成员变量：运行线程
};

#endif // LISTENING_STATE_H