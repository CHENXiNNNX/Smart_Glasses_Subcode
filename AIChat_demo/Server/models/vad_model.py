import numpy as np
from funasr import AutoModel
import os
from tools.logger import logger

current_dir = os.path.dirname(os.path.abspath(__file__))
_vad_path = os.path.join(current_dir, "./FunAudioLLM/iic/speech_fsmn_vad_zh-cn-16k-common-pytorch")

class VADModel:
    def __init__(self, device="cpu", vad_model_path=_vad_path, frame_duration_ms=200, sample_rate=16000,
                 max_buffer_length_ms=15000, no_speech_timeout_ms=3000, post_speech_buffer_ms=200):
        """
        初始化 VAD 模型

        :param device: 使用的设备 ("cpu" 或 "cuda")
        :param vad_model_path: VAD 模型路径
        :param frame_duration_ms: 每次处理的音频帧时长 (毫秒)
        :param sample_rate: 音频采样率
        :param max_buffer_length_ms: 音频缓冲区的最大长度 (毫秒)
        :param no_speech_timeout_ms: 无语音活动的超时时间 (毫秒)
        :param post_speech_buffer_ms: 语音结束后的缓冲时间 (毫秒)
        """

        self.sample_rate = sample_rate
        self.frame_duration_ms = frame_duration_ms
        self.max_buffer_length_ms = max_buffer_length_ms
        self.no_speech_timeout_ms = no_speech_timeout_ms
        self.post_speech_buffer_ms = post_speech_buffer_ms

        # 加载 VAD 模型
        self.vad_model = AutoModel(
            model=vad_model_path,
            disable_pbar=True,
            max_end_silence_time=self.frame_duration_ms,
            disable_update=True,
            device=device
        )

        # 状态变量
        self.reset()

    def reset(self):
        """重置 VAD 状态"""
        self.audio_buffer = np.array([], dtype=np.int16)  # 清空音频缓冲区
        self.processed_audio_length = 0  # 已处理的音频长度（毫秒）
        self.last_speech_pos = -1  # 最后一次语音活动的位置
        self.is_speaking = False  # 是否正在说话
        self.vad_cache = {}  # VAD 模型的缓存

    def process_audio_frame(self, audio_frame: np.ndarray) -> int:
        """
        流式处理音频数据，进行语音活动检测

        :param audio_frame: 输入的音频片数据 (numpy 数组, 例如 audio_data_array = np.frombuffer(pcm_data, dtype=np.int16))
        :return:
            0 - 正常处理，未检测到语音结束或无语音活动
            1 - 检测到语音结束
            2 - 检测到无语音活动
            3 - 缓冲区已满，停止存储新的音频数据
        """

        # 计算当前缓冲区的总时长（毫秒）
        audio_length = len(self.audio_buffer) * 1000 // self.sample_rate

        # 检查缓冲区是否超过最大长度
        if audio_length > self.max_buffer_length_ms:
            logger.info("Buffer too long, exceeded %d seconds", self.max_buffer_length_ms // 1000)
            return 3  # 缓冲区已满

        # 将输入音频数据追加到缓冲区
        self.audio_buffer = np.concatenate((self.audio_buffer, audio_frame))

        # 每次处理的音频帧长度（样本数）
        chunk_stride = self.sample_rate * self.frame_duration_ms // 1000

        # 如果剩余音频不足一个帧长度，则不处理
        if len(self.audio_buffer) - self.processed_audio_length * self.sample_rate // 1000 < chunk_stride:
            return 0

        # 获取当前帧的音频数据
        beg_frame = self.processed_audio_length * self.sample_rate // 1000
        end_frame = beg_frame + chunk_stride
        speech_chunk = self.audio_buffer[beg_frame:end_frame]

        # 调用 VAD 模型进行检测
        res = self.vad_model.generate(input=speech_chunk, cache=self.vad_cache, is_final=False, chunk_size=self.frame_duration_ms)
        self.processed_audio_length += self.frame_duration_ms  # 更新已处理的音频长度

        # 解析 VAD 结果
        if len(res[0]["value"]):
            for start, end in res[0]["value"]:
                # print("start: ", start, " end: ", end)
                if end != -1:  # 检测到语音结束点
                    self.last_speech_pos = end
                    self.is_speaking = False
                elif start != -1:  # 检测到语音开始点
                    self.is_speaking = True
                    self.last_speech_pos = start

        # 如果正在说话，更新最后的语音活动位置
        if self.is_speaking:
            self.last_speech_pos = audio_length

        # 如果超过等待说话时间（无语音活动超时）
        if not self.is_speaking and (audio_length - self.last_speech_pos) > self.no_speech_timeout_ms:
            logger.info("No speech detected for %d seconds, drop...", self.no_speech_timeout_ms // 1000)
            return 2

        # 如果超过语音结束缓冲时间，返回语音结束
        if self.last_speech_pos > 0 and (audio_length - self.last_speech_pos) > self.post_speech_buffer_ms:
            # 说话时长小于800ms, 则不进行ASR识别（可能是噪声）
            if self.last_speech_pos > 800:
                logger.info("Speech ended, last speech position: %d ms", self.last_speech_pos)
                return 1
            else:
                self.reset()
        return 0
