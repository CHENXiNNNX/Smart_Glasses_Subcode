from services.tts_service import TTSService
import pyaudio
from datetime import datetime
from tools.logger import logger

def get_current_time():
    """返回当前时间的字符串表示"""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")

class Callback():
    _player = None
    _stream = None

    def on_open(self):
        current_time = get_current_time()
        print(f"{current_time} - websocket is open.")
        self._player = pyaudio.PyAudio()
        self._stream = self._player.open(
            format=pyaudio.paInt16, channels=1, rate=16000, output=True
        )

    def on_complete(self):
        print("speech synthesis task complete successfully.")

    def on_error(self, message: str):
        print(f"speech synthesis task failed, {message}")

    def on_close(self):
        print("websocket is closed.")
        # 停止播放器
        self._stream.stop_stream()
        self._stream.close()
        self._player.terminate()

    def on_event(self, message):
        print(f"recv speech synthsis message {message}")

    def on_data(self, data: bytes) -> None:
        current_time = get_current_time()
        print(f"{current_time} - Audio result length: {len(data)}")
        self._stream.write(data)

synthesizer_callback = Callback()
tts_service = TTSService()

logger.info("TTS服务已启动")
tts_service.tts_set(on_open=synthesizer_callback.on_open,
                     on_complete=synthesizer_callback.on_complete,
                     on_error=synthesizer_callback.on_error,
                     on_close=synthesizer_callback.on_close,
                     on_data=synthesizer_callback.on_data)
tts_service.tts_speech_stream('你好，我是一个语音合成测试程序。')
tts_service.tts_close()
