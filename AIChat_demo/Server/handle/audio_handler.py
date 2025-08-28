import numpy as np
from service_manager import ServiceManager
import json
from tools.logger import logger
from config.settings import global_settings


class AudioHandler:
    def __init__(self, service_manager: ServiceManager):
        self.service_manager = service_manager
        self.service_manager.is_vad = False # 防止VAD发生后还语音加入

    async def handle_audio_message(self, msg):
        """
        处理音频消息
        :param msg: 收到的按照格式打包的 包含 PCM 音频的数据
        :return: 响应消息
        """
        # 解码音频数据
        bin_protocol = self.service_manager.audio_processor.unpack_bin_frame(msg)
        if bin_protocol:
            protocol_version, type, payload = bin_protocol
            if type == 0 and protocol_version == global_settings.protocol_version and self.service_manager.is_vad == False:
                # 处理音频数据
                pcm_data = self.service_manager.audio_processor.decode_audio(payload)
                audio_data_np_array = np.frombuffer(pcm_data, dtype=np.int16)

                # 使用 VAD 检测语音活动
                vad_result = self.service_manager.vad_service.process_audio_frame(audio_data_np_array)
                # 正常处理，未检测到语音结束或无语音活动
                if vad_result == 0: # 继续处理音频数据
                    # 存入音频到 ASR 服务的缓冲区
                    self.service_manager.asr_service.asr_add_audio_buffer(pcm_data)
                # 检测到语音结束
                elif vad_result == 1:
                    self.service_manager.is_vad = True
                    asr_res = self.service_manager.asr_service.asr_generate_text()
                    # asr识别到，然后开一个任务进行对话
                    self.service_manager.task_manager.submit_task(self.service_manager.chat_start_task, asr_res)
                    # 发送asr识别结果
                    res =  {
                            "type": "asr",
                            "text": asr_res
                    }
                    logger.info(f"asr result: {asr_res}")
                    # send asr result to client
                    self.service_manager.ws_send_queue.put(json.dumps(res))
                # 无语音活动
                elif vad_result == 2:
                    self.service_manager.is_vad = True
                    res = {
                    "type": "vad",
                    "state": "no_speech"
                    }
                    logger.warning("vad result: no_speech")
                    self.service_manager.ws_send_queue.put(json.dumps(res))
                # 缓冲区已满
                elif vad_result == 3:
                    self.service_manager.is_vad = True
                    asr_res = self.service_manager.asr_service.asr_generate_text()
                    # asr识别到，然后开一个任务进行对话
                    self.service_manager.task_manager.submit_task(self.service_manager.chat_start_task, asr_res)
                    # 发送asr识别结果
                    res =  {
                            "type": "asr",
                            "text": asr_res
                    }
                    logger.warning(f"vad result: buffer_full, asr result: {asr_res}")
                    # send asr result to client
                    self.service_manager.ws_send_queue.put(json.dumps(res))


