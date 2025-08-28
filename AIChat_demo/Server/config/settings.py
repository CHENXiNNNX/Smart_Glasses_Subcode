import dashscope

class Settings:
    protocol_version = 2
    # LLM API 密钥
    dashscope.api_key = "sk-d8e4dc07bc01425fa83e851bc1d66b7f"

    # 模型选择
    INTENT_MODEL = "qwen-turbo"       # 专门用于意图识别
    CHAT_MODEL = "qwen-turbo"         # 用于常规对话

    # device
    ASR_DEVICE = "cpu"            # ASR 模型使用的设备
    # ASR_DEVICE = "cuda"            # ASR 模型使用的设备
    VAD_DEVICE = "cpu"            # ASR 模型使用的设备

    # 超时设置
    API_TIMEOUT = 10  # 秒

    def Set_API_Key(self, aliyun_api_key):
        dashscope.api_key = aliyun_api_key

global_settings = Settings()
