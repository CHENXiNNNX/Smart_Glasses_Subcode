
from tools.logger import logger
from tools.registry import global_registry
from typing import Dict, Optional
from services.chat_service import ChatService
from services.intent_service import IntentService

chat_service = ChatService()
intent_service = IntentService(global_registry)

def register_builtin_functions():
    """注册基础功能函数"""
    def motor_move(**kwargs):
        """控制机器人运动"""
        direction = kwargs.get("direction", "unknown")
        speed = kwargs.get("speed", 50)  # 默认速度为 50%
        duration = kwargs.get("duration", 1.0)  # 默认持续时间为 1 秒

        print(f"[Motor Move] Direction: {direction}, Speed: {speed}%, Duration: {duration}s")
        return f"Robot moved {direction} for {duration} seconds"

    def get_current_time():
        """获取当前时间"""
        from datetime import datetime
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"[Current Time] {now}")
        return now

    def continue_chat():
        return "继续聊天..."

    def handle_exit_intent():
        return "再见！"

    # 注册到系统
    global_registry.register_function("robot_move", "让机器人运动", {"direction": "字符数据,分别有forward,backward,left和right", "speed": "默认为1,数字", "duration": "持续时间s,默认为1"}, motor_move)
    global_registry.register_function("get_current_time", "获取当前时间", {}, get_current_time)
    global_registry.register_function("continue_chat", "继续聊天意图", {}, continue_chat)
    global_registry.register_function("exit_chat", "结束对话意图", {}, handle_exit_intent)

def get_print_llm_res(user_input: str, history: Optional[Dict] = None, is_stream: bool = False):
    print("llm回复: ")
    final_response = chat_service.generate_chat_response(user_input, history=history, is_stream=is_stream)
    for text_chunk in final_response:
        print(text_chunk, end="", flush=True)

def main():
    register_builtin_functions()
    print("开始交互（输入exit退出）")
    while True:
        user_input = input("\n用户: ").strip()
        if user_input.lower() == 'exit':
            break
        logger.info(f"[用户输入]: {user_input}")
        # 1. 意图识别阶段
        function_calls = intent_service.detect_intent(user_input)
        history = []
        # 2. 执行函数调用（如果有）
        for function_call in function_calls:
            if "function_call" in function_call and "name" in function_call["function_call"]:
                logger.info(f"[准备调用] {function_call}")
                if function_call["function_call"]["name"] == "continue_chat":
                    # 继续聊天意图，直接返回聊天引擎的回复
                    pass
                elif function_call["function_call"]["name"] == "exit_chat":
                    # 结束对话意图，直接返回聊天引擎的回复
                    get_print_llm_res(user_input, history, is_stream=True)
                    return
                # 其他函数调用
                else:
                    try:
                        result = global_registry.execute_function(function_call)
                        logger.info(f"[函数响应] {result}")
                        history.append([
                            {"role": "user", "content": f"函数调用: {function_call}"},
                            {"role": "assistant", "content": f"函数调用成功, 函数输出为: {result}"}
                        ])
                    except Exception as e:
                        logger.error(f"[函数调用错误] {str(e)}")
        # 聊天引擎回复
        get_print_llm_res(user_input, history, is_stream=True)

if __name__ == "__main__":
    main()
