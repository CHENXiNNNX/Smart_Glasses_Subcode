#ifndef STATE_CONFIG_H
#define STATE_CONFIG_H

#include "../StateMachine/StateMachine.h"
#include "Application.h"

enum class AppState {
    fault,
    startup,
    stopping,
    idle,
    listening,
    thinking,
    speaking,
};

class StateConfig {
public:
    /*
        * @brief Configure the state machine with states and transitions.
        * 
        * @param state_machine The state machine to configure.
        * @param app Pointer to the Application instance.
        * 
        * This function sets up the state machine by registering states and their corresponding
        * entry and exit actions, as well as defining the transitions between states based on events.
    */
    static void Configure(StateMachine& state_machine, Application* app);
};

#endif // STATE_CONFIG_H