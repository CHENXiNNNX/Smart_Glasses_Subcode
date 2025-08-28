#include "./StateMachine.h"
#include "../Utils/user_log.h"
#include <iostream>

StateMachine::StateMachine(int initialState)
    : currentState_(initialState) {
        USER_LOG_INFO("State machine created.");
    }

StateMachine::~StateMachine() {
    USER_LOG_INFO("State machine destroyed.");
}

void StateMachine::Initialize() {
        // 调用初始状态的 enter 回调
        if (stateActions_.find(currentState_) != stateActions_.end()) {
            stateActions_[currentState_].first();
        }
    }

void StateMachine::RegisterState(int state, EnterFunc_t on_enter, ExitFunc_t on_exit) {
    stateActions_[state] = std::make_pair(on_enter, on_exit);
}

void StateMachine::RegisterTransition(int from, int event, int to) {
    // If from is -1, it means the transition is from any state
    if(from == -1) {
        for(auto& state : stateActions_) {
            transitions_[state.first][event] = to;
        }
    } 
    else {
        transitions_[from][event] = to;
    }
}

bool StateMachine::HandleEvent(int event) {
    auto& possibleTransitions = transitions_[currentState_];
    if (possibleTransitions.find(event) == possibleTransitions.end()) {
        USER_LOG_WARN("Event: %d is not handled in current state.", event);
        return false;
    }

    int nextState = possibleTransitions[event];
    ChangeState(nextState);
    return true;
}

int StateMachine::GetCurrentState() const {
    return currentState_;
}

void StateMachine::ChangeState(int newState) {
    // Call exit function for the current state if it exists
    if (stateActions_.find(currentState_) != stateActions_.end()) {
        stateActions_[currentState_].second();
    }

    // Update the current state
    currentState_ = newState;

    // Call enter function for the new state if it exists
    if (stateActions_.find(currentState_) != stateActions_.end()) {
        stateActions_[currentState_].first();
    }
}
