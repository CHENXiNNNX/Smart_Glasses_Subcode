from tools.audio_processor import AudioProcessor
from services.asr_service import ASRService

audio_processor = AudioProcessor()
asr_serv = ASRService()

load_audio_frames = audio_processor.load_audio_from_file("./test/waked.pcm")
for i in range(len(load_audio_frames)):
    # 将音频数据添加到缓冲区
    asr_serv.asr_add_audio_buffer(load_audio_frames[i])
res = asr_serv.asr_generate_text()
print(res)
