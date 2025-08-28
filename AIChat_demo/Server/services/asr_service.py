import numpy as np
from config.settings import global_settings
from models.asr_model import ASRModel


class ASRService:
    def __init__(self):
        self.asr_model = ASRModel(device=global_settings.ASR_DEVICE)
        self.asr_model.clear_audio_buffer()

    def reset(self):
        """重置 ASR 状态"""
        self.asr_model.clear_audio_buffer()

    def asr_add_audio_buffer(self, audio_data):
        """
        添加PCM音频数据到缓冲区
        """
        # 将音频数据转换为numpy数组并添加到缓冲区
        self.asr_model.add_audio_buffer(audio_data)

    def asr_generate_text(self):
        """
        使用 ASR 模型进行语音识别，生成文本, 然后清空音频缓冲区

        :return: 识别结果文本（字符串）。
                - 如果识别成功，返回转录后的文本。
                - 如果识别失败或没有检测到语音，返回 None。
        """
        res = self.asr_model.ASR_generate_text(self.asr_model.audio_buffer.astype(np.float32))
        self.asr_model.clear_audio_buffer()  # 清空音频缓冲区
        return res
