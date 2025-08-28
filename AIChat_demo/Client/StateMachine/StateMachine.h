#ifndef STATE_MACHINE_H_
#define STATE_MACHINE_H_

#include <functional>
#include <unordered_map>
#include <string>

using EnterFunc_t = std::function<void()>;
using ExitFunc_t = std::function<void()>;

// StateMachine class declaration
class StateMachine {
public:
    StateMachine(int initialState);
    ~StateMachine();

    /**
     * @brief 初始化状态机。
     * 
     */
    void Initialize();

    /**
     * @brief 注册状态。
     * 
     * @param state 状态
     * @param on_enter 进入状态时的回调函数
     * @param on_exit 退出状态时的回调函数
     */
    void RegisterState(int state, EnterFunc_t on_enter, ExitFunc_t on_exit);

    /**
     * @brief 注册状态转换。
     * 
     * @param from 起始状态，-1表示任意状态
     * @param event 事件
     * @param to 目标状态
     */
    void RegisterTransition(int from, int event, int to);

    /**
     * @brief 处理事件。
     * 
     * @param event 事件
     * @return true 处理成功
     * @return false 处理失败
     */
    bool HandleEvent(int event);

    /**
     * @brief 获取当前状态。
     * 
     * @return int 当前状态
     */
    int GetCurrentState() const;

private:
    void ChangeState(int newState);

    int currentState_;
    std::unordered_map<int, std::pair<EnterFunc_t, ExitFunc_t>> stateActions_;
    std::unordered_map<int, std::unordered_map<int, int>> transitions_;
};

#endif  // STATE_MACHINE_H_