from typing import Dict, Any, List, Callable, Type, Optional
from pydantic import BaseModel, create_model, ValidationError

class FunctionRegistry:
    def __init__(self):
        self.registered_functions = {}  # 存储已注册函数信息
        self.real_functions = {}       # 存储真实可调用函数对象

    def register_function(self, function_name: str, description: str, parameters: Dict[str, str], impl: Callable = None):
        """
        注册函数到系统
        :param function_name: 函数名称
        :param description: 功能描述
        :param parameters: 参数结构 {字段名: 描述}
        :param impl: 实际实现的函数对象（可选）
        """
        if not isinstance(parameters, dict):
            raise ValueError("参数结构必须是字典")

        for param_name, param_description in parameters.items():
            if not isinstance(param_description, str):
                raise ValueError(f"参数类型必须是str对象，当前{param_name}描述为{param_description}")

        # 创建 Pydantic 模型用于参数校验
        params_model = create_model(
            function_name + 'Params',
            **{k: (Optional[str], None) for k in parameters.keys()}  # 设置为可选参数，默认值为 None
        )

        self.registered_functions[function_name] = {
            'description': description,
            'arguments': parameters,
            'params_model': params_model
        }
        if impl:
            self.real_functions[function_name] = impl

    def get_registered_tools(self) -> List[Dict]:
        """获取所有已注册工具信息，并将参数类型转为字符串"""
        return [
            {
                "type": "function",
                "function": {
                    "name": name,
                    "description": info["description"],
                    "arguments": {k: v for k, v in info["arguments"].items()}  # 直接返回字符串
                }
            }
            for name, info in self.registered_functions.items()
        ]

    def execute_function(self, function_call: Dict[str, Any]) -> Any:
        """
        执行注册的函数
        :param function_call: 包含函数名和参数的字典
        """
        func_name = function_call["function_call"]['name']
        args = function_call["function_call"].get('arguments', {})  # 直接使用字典
        if not isinstance(args, dict):
            raise ValueError("参数必须是字典类型")
        if func_name not in self.real_functions:
            raise ValueError(f"未找到实际函数实现: {func_name}")
        params_model = self.registered_functions[func_name]['params_model']

        # 将所有参数值转换为字符串
        str_args = {k: str(v) for k, v in args.items()}

        try:
            validated_args = params_model(**str_args)  # 直接传入字典
        except ValidationError as e:
            raise ValueError(f"参数校验失败: {e}")

        result = self.real_functions[func_name](**validated_args.dict())
        return result


global_registry = FunctionRegistry()
