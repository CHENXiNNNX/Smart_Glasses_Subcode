import numpy as np
import sounddevice as sd
import threading

# 读取 PCM 文件
with open("E:\projects\programs\python_codes\Chat_Server\\test\\vad_test.pcm", "rb") as f:
    audio_data = np.frombuffer(f.read(), dtype=np.int16)

# 获取音频时长
num_samples = len(audio_data)  # 音频样本总数
sampling_rate = 16000  # 采样率

# 计算时长（秒）
duration_seconds = num_samples / sampling_rate

print(f"音频时长: {duration_seconds:.2f} 秒")

# 标志变量，用于控制播放是否应该停止
stop_playback = False

def play_audio():
    global stop_playback
    try:
        # 播放音频
        sd.play(audio_data, samplerate=sampling_rate)
        while sd.get_stream().active and not stop_playback:
            sd.sleep(100)  # 等待一段时间，检查是否需要停止
    except Exception as e:
        print(f"播放过程中发生错误: {e}")
    finally:
        sd.stop()  # 停止播放
        print("播放已停止")

# 创建一个线程来播放音频
play_thread = threading.Thread(target=play_audio)
play_thread.start()

# 主线程等待用户输入以中断播放
input("按 Enter 键停止播放...\n")
stop_playback = True

# 等待播放线程结束
play_thread.join()
