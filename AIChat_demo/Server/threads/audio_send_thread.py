import threading
import queue
import asyncio
from tools.logger import logger
from service_manager import ServiceManager
from config.settings import global_settings

class AudioSendThread(threading.Thread):
    def __init__(self, sevice_manager: ServiceManager):
        super().__init__(daemon=True)
        self.sevice_manager = sevice_manager

    def run(self):
        remain_data = b''
        while not self.sevice_manager.stop_event.is_set():  # 检查 stop_event 是否被设置
            try:
                # 从语音队列中获取语音数据
                audio_data = self.sevice_manager.audio_queue.get(timeout=1)  # 设置超时时间，避免阻塞
                # 调用发送回调函数，将语音数据发送给客户端
                # 最开始的数据，需要大于一定值，才开始发送出去，防止断续
                # if len(audio_data) < 1000:
                #     remain_data += audio_data
                #     continue
                # 二进制数据: PCM-16bit 音频数据
                if isinstance(audio_data, bytes):
                    samples_per_frame = int(self.sevice_manager.audio_processor.frame_duration_ms * self.sevice_manager.audio_processor.sample_rate / 1000)*2
                    audio_data = remain_data + audio_data
                    # 切片, 编码, 打包, 发送
                    for i in range(0, len(audio_data), samples_per_frame):
                        frame_slice = audio_data[i:i + samples_per_frame]
                        if len(frame_slice) == samples_per_frame:
                            # 编码当前帧并发送
                            opus_data = self.sevice_manager.audio_processor.encode_audio(frame_slice)
                            bin_data = self.sevice_manager.audio_processor.pack_bin_frame(type=0, version=global_settings.protocol_version, payload=opus_data)
                            self.sevice_manager.ws_send_queue.put(bin_data)
                        else:
                            # 最后一帧不足时, 保留
                            remain_data = frame_slice
                            pass
            except queue.Empty:
                # 如果队列为空，继续检查 stop_event
                continue
            except Exception as e:
                logger.error(f"TTS发送线程发生错误: {e}")
