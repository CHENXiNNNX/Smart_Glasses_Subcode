import threading
import queue
from service_manager import ServiceManager
from tools.logger import logger

class TTSGenerateThread(threading.Thread):
    def __init__(self, sevice_manager: ServiceManager):
        super().__init__(daemon=True)
        self.sevice_manager = sevice_manager

    def run(self):
        while not self.sevice_manager.stop_event.is_set():  # 检查 stop_event 是否被设置
            try:
                # 从 TTS任务队列中获取文字
                text_chunk = self.sevice_manager.tts_text_queue.get(timeout=1)  # 设置超时时间，避免阻塞
                # 调用 TTS 服务生成语音
                self.sevice_manager.tts_service.tts_speech_stream(text_chunk)
                # 将生成的语音数据放入语音队列
                # self.sevice_manager.audio_queue.put(audio_data)
            except queue.Empty:
                # 如果队列为空，继续检查 stop_event
                continue
            except Exception as e:
                logger.error(f"TTS生成线程发生错误: {e}")
