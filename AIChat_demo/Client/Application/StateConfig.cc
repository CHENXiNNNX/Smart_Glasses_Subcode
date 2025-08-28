#include "StateConfig.h"
#include "../Events/AppEvents.h"
#include "./UserStates/Fault.h"
#include "./UserStates/Startup.h"
#include "./UserStates/Stop.h"
#include "./UserStates/Idle.h"
#include "./UserStates/Listening.h"
#include "./UserStates/Thinking.h"
#include "./UserStates/Speaking.h"

void StateConfig::Configure(StateMachine& state_machine, Application* app) {
    // 注册状态
    state_machine.RegisterState(static_cast<int>(AppState::fault), 
        [app]() { FaultState::Enter(app); }, 
        [app]() { FaultState::Exit(app); });
    
    state_machine.RegisterState(static_cast<int>(AppState::startup), 
        [app]() { StartupState::Enter(app); }, 
        [app]() { StartupState::Exit(app); });
    
    state_machine.RegisterState(static_cast<int>(AppState::stopping),
        [app]() { StopState::Enter(app); }, 
        [app]() { StopState::Exit(app); });

    state_machine.RegisterState(static_cast<int>(AppState::idle), 
        [app]() { IdleState::Enter(app); }, 
        [app]() { IdleState::Exit(app); });

    state_machine.RegisterState(static_cast<int>(AppState::listening), 
        [app]() { ListeningState::Enter(app); }, 
        [app]() { ListeningState::Exit(app); });

    state_machine.RegisterState(static_cast<int>(AppState::thinking), 
        [app]() { ThinkingState::Enter(app); }, 
        [app]() { ThinkingState::Exit(app); });

    state_machine.RegisterState(static_cast<int>(AppState::speaking), 
        [app]() { SpeakingState::Enter(app); }, 
        [app]() { SpeakingState::Exit(app); });

    // 添加状态切换
    state_machine.RegisterTransition(static_cast<int>(AppState::startup), static_cast<int>(AppEvent::startup_done), static_cast<int>(AppState::idle));
    state_machine.RegisterTransition(static_cast<int>(AppState::idle), static_cast<int>(AppEvent::wake_detected), static_cast<int>(AppState::speaking));
    state_machine.RegisterTransition(static_cast<int>(AppState::listening), static_cast<int>(AppEvent::vad_no_speech), static_cast<int>(AppState::idle));
    state_machine.RegisterTransition(static_cast<int>(AppState::listening), static_cast<int>(AppEvent::asr_received), static_cast<int>(AppState::thinking));
    state_machine.RegisterTransition(static_cast<int>(AppState::thinking), static_cast<int>(AppEvent::speaking_msg_received), static_cast<int>(AppState::speaking)); 
    state_machine.RegisterTransition(static_cast<int>(AppState::speaking), static_cast<int>(AppEvent::speaking_end), static_cast<int>(AppState::listening));
    state_machine.RegisterTransition(static_cast<int>(AppState::speaking), static_cast<int>(AppEvent::dialogue_end), static_cast<int>(AppState::idle));
    state_machine.RegisterTransition(-1, static_cast<int>(AppEvent::fault_happen), static_cast<int>(AppState::fault));
    state_machine.RegisterTransition(-1, static_cast<int>(AppEvent::to_stop), static_cast<int>(AppState::stopping));
    state_machine.RegisterTransition(static_cast<int>(AppState::fault), static_cast<int>(AppEvent::fault_solved), static_cast<int>(AppState::idle));
    // 初始化
    state_machine.Initialize();
}