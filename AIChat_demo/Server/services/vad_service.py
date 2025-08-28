from config.settings import global_settings
from models.vad_model import VADModel

class VADService:
    def __init__(self):
        self.vad_model = VADModel()

    def reset(self):
        """重置 VAD 状态"""
        self.vad_model.reset()

    def process_audio_frame(self, audio_frame):
        """
        流式处理音频数据，进行语音活动检测

        :param audio_frame: 输入的音频片数据 (numpy 数组, 例如 audio_data_array = np.frombuffer(pcm_data, dtype=np.int16))
        :return:
            0 - 正常处理，未检测到语音结束或无语音活动
            1 - 检测到语音结束
            2 - 检测到无语音活动
            3 - 缓冲区已满
        """
        res = self.vad_model.process_audio_frame(audio_frame)
        return res
