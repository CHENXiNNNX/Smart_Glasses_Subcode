import numpy as np
import os
from services.vad_service import VADService

def load_pcm_file(file_path: str, dtype=np.int16) -> np.ndarray:
    """
    加载 PCM 文件并转换为 NumPy 数组
    :param file_path: PCM 文件路径
    :param dtype: 数据类型（默认 np.int16）
    :return: PCM 格式的 NumPy 数组
    """
    with open(file_path, "rb") as f:
        pcm_data = f.read()
    return np.frombuffer(pcm_data, dtype=dtype)

def slice_audio(audio_data: np.ndarray, frame_duration_ms: int, sample_rate: int) -> list:
    """
    将音频数据切片为固定时长的帧
    :param audio_data: PCM 格式的音频数据
    :param frame_duration_ms: 每帧的时长（毫秒）
    :param sample_rate: 音频采样率
    :return: 切片后的音频帧列表
    """
    frame_size = sample_rate * frame_duration_ms // 1000  # 每帧的样本数
    return [audio_data[i:i + frame_size] for i in range(0, len(audio_data), frame_size)]

def test_vad_service(vad_ser: VADService, audio_frames: list):
    """
    测试 VAD 模型
    :param vad_ser: vad_service 实例
    :param audio_frames: 切片后的音频帧列表
    """
    print("开始测试 VAD 模型...")
    for i, frame in enumerate(audio_frames):
        if len(frame) == 0:
            continue
        result = vad_ser.process_audio_frame(frame)
        if result:
            break
    print("VAD 测试完成！")


if __name__ == "__main__":
    # 设置 PCM 文件路径
    pcm_file_path = "./test/vad_test1.pcm"
    if not os.path.exists(pcm_file_path):
        print(f"PCM 文件 {pcm_file_path} 不存在，请检查路径！")
        exit(1)

    # 加载 PCM 文件为 NumPy 数组
    print(f"加载 PCM 文件: {pcm_file_path}")
    pcm_data = load_pcm_file(pcm_file_path)

    # 初始化 VAD
    vad_ser = VADService()

    # 将音频数据切片为 40ms 的帧
    frame_duration_ms = 40  # 每帧 40ms
    sample_rate = 16000  # 采样率 16kHz
    audio_frames = slice_audio(pcm_data, frame_duration_ms, sample_rate)

    # 测试 VAD 模型
    test_vad_service(vad_ser, audio_frames)
