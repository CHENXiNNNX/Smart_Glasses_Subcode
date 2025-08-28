from funasr import AutoModel
import dashscope
from tools.logger import logger

class LLMModel:
    def __init__(self, model_name: str = "qwen-turbo"):
        self.model_name = model_name
        # 对话初始历史记录
        # 默认的是这个content
        self.messages = [
            {"role": "system", "content": "你是一个桌面机器人, 名为Echo, 快速地回复我."}
        ]

    def set_model_sys_content(self, content: str):
        """
        设置模型的系统描述内容

        :param content: 系统内容
        """
        self.messages[0]["content"] = content

    def add_message(self, role: str, content: str):
        """
        添加对话历史记录

        :param role: 消息角色 ("user" 或 "assistant")
        :param content: 消息内容
        """
        self.messages.append({"role": role, "content": content})

    def clear_messages(self):
        """
        清空对话历史记录
        """
        # 默认的是这个content
        self.messages = [
            {"role": "system", "content": "你是一个桌面机器人, 名为Echo, 全程请快速地回复我. 同时你还有函数执行的功能, 可以根据函数来回复我. "}
        ]

    def get_LLM_response(self, question: str) -> str:
        """
        非流式生成回答

        :param question: 用户问题
        :return: 完整的回答
        """
        if question:
            self.add_message("user", question)

        try:
            response = dashscope.Generation.call(
                api_key=dashscope.api_key,
                model=self.model_name,
                messages=self.messages,
                result_format='message',
                stream=False,
                incremental_output=False,
                enable_search=True
            )

            if response["status_code"] == 200:
                content = response["output"]["choices"][0]["message"]["content"]
                self.add_message("assistant", content)
                return content
            else:
                logger.error(f"非流式生成失败，状态码: {response['status_code']}")
                return "抱歉，我暂时无法处理您的请求。"
        except Exception as e:
            logger.error(f"非流式生成异常: {str(e)}")
            return "抱歉，我暂时无法处理您的请求。"


    def get_LLM_response_stream(self, question):
        """获取百炼对话回答\r\n
        注意: 此处是yield生成器, 需要在外部循环中调用"""
        # 如果在此添加用户问题
        if(question):
            self.add_message('user', question)
        messages = self.messages

        try:
            responses = dashscope.Generation.call(
                api_key=dashscope.api_key,
                model=self.model_name, # 模型列表：https://help.aliyun.com/zh/model-studio/getting-started/models
                messages=messages,
                result_format='message',
                stream=True,
                incremental_output=True,
                enable_search=True
            )
            full_text = ''
            for response in responses:
                if response["status_code"] == 200:
                    for choice in response["output"]["choices"]:
                        full_text += choice["message"]["content"]
                        yield choice["message"]["content"]
                else:
                    logger.error(f"Failed to get LLM response with status code {response['status_code']}")
                    yield -1
            # 最后记录回复的信息
            self.add_message('assistant', full_text)
        except Exception as e:
            logger.error(f"An exception occurred: {str(e)}")
            yield -1
