from funasr import AutoModel
from funasr.utils.postprocess_utils import rich_transcription_postprocess
import numpy as np

_asr_model_path = "./models/FunAudioLLM/iic/SenseVoiceSmall"
_remote_code = "./models/FunAudioLLM/SenseVoice/model.py"

class ASRModel:
    def __init__(self,device="cpu"):
        # 初始化 ASR 模型
        self.asr_model = AutoModel(
            model=_asr_model_path,
            remote_code=_remote_code,
            trust_remote_code=True,
            device=device,
            disable_update=True
        )
        # 使用numpy数组作为音频缓冲区
        self.audio_buffer = np.array([], dtype=np.int16)  # 初始化为空的numpy数组, 用于存储client发送来的音频数据

    def clear_audio_buffer(self):
        """
        清空音频缓冲区
        """
        self.audio_buffer = np.array([], dtype=np.int16)

    def add_audio_buffer(self, pcm_data):
        """
        添加音频数据到缓冲区

        :param pcm_data: pcm格式的音频数据
        """
        # 将音频数据转换为numpy数组并添加到缓冲区
        audio_data_array = np.frombuffer(pcm_data, dtype=np.int16)
        self.audio_buffer = np.append(self.audio_buffer, audio_data_array)

    def get_audio_buffer_lenth(self):
        """
        获取音频缓冲区的长度, 物理时长=lenth/sample_rate

        :return: 音频缓冲区的长度
        """
        return len(self.audio_buffer)

    def ASR_generate_text(self, audio_buffer):
        """
        使用 ASR 模型进行语音识别，生成文本。

        :param audio_buffer: 输入的音频数据，格式为 NumPy 数组，数据类型为 np.float32。
                            - 数据应为单声道（Mono）PCM 格式。
                            - 采样率应与模型的要求一致（通常为 16kHz）。
                            - 数据范围为 [-1.0, 1.0] 或原始 PCM 整数值（如 np.int16）。
                            - 如果是 np.int16 类型，模型会自动进行归一化处理。

        :return: 识别结果文本（字符串）。
                - 如果识别成功，返回转录后的文本。
                - 如果识别失败或没有检测到语音，返回 None。
        """
        res = self.asr_model.generate(input=audio_buffer, cache={}, language='auto', use_itn=True)
        # res是有情感等信息的, 只取text
        if(res[0]['text']):
            return rich_transcription_postprocess(res[0]['text'])
        return None
