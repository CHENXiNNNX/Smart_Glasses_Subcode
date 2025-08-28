import wave
import os
import subprocess
from pydub import AudioSegment

# 明确指定 FFmpeg 的路径
ffmpeg_path = "E:/environment/ffmpeg/bin/ffmpeg.exe"  # 使用正斜杠或双反斜杠
if not os.path.isfile(ffmpeg_path):
    raise FileNotFoundError(f"FFmpeg executable not found at {ffmpeg_path}")

# 添加 FFmpeg 路径到系统的 PATH 环境变量
os.environ["PATH"] += os.pathsep + os.path.dirname(ffmpeg_path)

# 设置 pydub 的转换器路径
AudioSegment.converter = ffmpeg_path

def wav_to_pcm(wav_file, pcm_file):
    """
    将 WAV 文件转换为 PCM 格式。

    :param wav_file: 输入的 WAV 文件路径
    :param pcm_file: 输出的 PCM 文件路径
    """
    try:
        # 打开WAV文件
        with wave.open(wav_file, 'rb') as wf:
            # 获取参数
            nchannels, sampwidth, framerate, nframes, comptype, compname = wf.getparams()

            # 读取所有帧
            frames = wf.readframes(nframes)

            # 写入PCM文件
            with open(pcm_file, 'wb') as f:
                f.write(frames)

            print(f"成功将 {wav_file} 转换为 {pcm_file}")
    except Exception as e:
        print(f"转换过程中发生错误: {e}")

def mp3_to_wav(mp3_file, wav_file):
    # 使用 pydub 将 MP3 转换为 WAV
    audio = AudioSegment.from_mp3(mp3_file)

    # 设置输出参数以确保符合要求
    # - 格式：WAV
    # - 编码：PCM (无压缩)
    # - 采样率：16kHz（或根据需要调整）
    # - 位深度：16 bit
    # - 通道数：2 (立体声) 或 1 (单声道)
    audio = audio.set_frame_rate(16000).set_channels(1).set_sample_width(2)

    # 导出为 WAV 文件
    audio.export(wav_file, format="wav")

# 使用示例
wav_file = 'vad_test2.wav'  # 输入的WAV文件路径
pcm_file = 'vad_test2.pcm'  # 输出的PCM文件路径
wav_to_pcm(wav_file, pcm_file)

# mp3_file = "vad_test2.mp3"  # 输入 MP3 文件路径
# wav_file = "vad_test2.wav"  # 输出 WAV 文件路径
# mp3_to_wav(mp3_file, wav_file)
