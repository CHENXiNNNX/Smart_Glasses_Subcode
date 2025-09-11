#include "audio.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>

// 录音回调函数
static int recordCallback(const void *inputBuffer, void *outputBuffer, 
                         unsigned long framesPerBuffer, 
                         const PaStreamCallbackTimeInfo* timeInfo, 
                         PaStreamCallbackFlags statusFlags, 
                         void *userData) {
    (void) outputBuffer; // 未使用输出缓冲区
    (void) timeInfo;     // 未使用时间信息
    (void) statusFlags;  // 未使用状态标志

    audio_system_t* audio_system = static_cast<audio_system_t*>(userData);
    const int16_t* input = static_cast<const int16_t*>(inputBuffer);

    std::vector<int16_t> frame(framesPerBuffer * audio_system->channels);
    std::copy(input, input + framesPerBuffer * audio_system->channels, frame.begin());

    {
        std::lock_guard<std::mutex> lock(audio_system->recordedAudioMutex);

        // 检查队列长度是否超过 750
        if (audio_system->recordedAudioQueue.size() >= 750) {
            audio_system->recordedAudioQueue.pop(); // 移除最旧的帧
        }

        // 添加新的帧
        audio_system->recordedAudioQueue.push(frame);
    }
    audio_system->recordedAudioCV.notify_one();

    return paContinue;
}

// 播放回调函数
static int playCallback(const void *inputBuffer, void *outputBuffer, 
                        unsigned long framesPerBuffer, 
                        const PaStreamCallbackTimeInfo* timeInfo, 
                        PaStreamCallbackFlags statusFlags, 
                        void *userData) {
    (void) inputBuffer; // 未使用输入缓冲区
    (void) timeInfo;     // 未使用时间信息
    (void) statusFlags;  // 未使用状态标志

    audio_system_t* audio_system = static_cast<audio_system_t*>(userData);
    int16_t* output = static_cast<int16_t*>(outputBuffer);

    std::lock_guard<std::mutex> lock(audio_system->playbackMutex);

    if (audio_system->playbackQueue.empty()) {
        // 如果队列为空，则填充静音数据
        std::fill(output, output + framesPerBuffer * audio_system->channels, 0);
        return paContinue;
    }

    // 获取并处理当前帧
    std::vector<int16_t>& currentFrame = audio_system->playbackQueue.front();
    size_t samplesToCopy = std::min(static_cast<size_t>(framesPerBuffer * audio_system->channels), currentFrame.size());

    std::copy(currentFrame.begin(), currentFrame.begin() + samplesToCopy, output);

    if (samplesToCopy < framesPerBuffer * audio_system->channels) {
        // 如果当前帧不足，则用静音填充剩余部分
        std::fill(output + samplesToCopy, output + framesPerBuffer * audio_system->channels, 0);
    }

    // 移除已播放的数据
    if (samplesToCopy == currentFrame.size()) {
        audio_system->playbackQueue.pop();
    } else {
        // 更新队列中的第一个元素以删除已播放的部分
        audio_system->playbackQueue.front().erase(audio_system->playbackQueue.front().begin(), 
                                                 audio_system->playbackQueue.front().begin() + samplesToCopy);
    }

    return paContinue;
}

audio_error_t audio_system_init(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    // 初始化默认参数
    audio_system->sample_rate = 16000;
    audio_system->channels = 1;
    audio_system->frame_duration_ms = 40;
    audio_system->encoder = nullptr;
    audio_system->decoder = nullptr;
    audio_system->recordStream = nullptr;
    audio_system->playbackStream = nullptr;
    audio_system->isRecording = false;
    audio_system->isPlaying = false;
    audio_system->current_mode = AUDIO_MODE_NONE;

    // 初始化PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_INITIALIZE_FAILED;
    }

    // 初始化Opus编解码器
    if (init_opus_codec(audio_system) != AUDIO_ERROR_NONE) {
        Pa_Terminate();
        return AUDIO_ERROR_INITIALIZE_FAILED;
    }

    return AUDIO_ERROR_NONE;
}

audio_error_t audio_system_deinit(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    // 停止录音和播放
    if (audio_system->isRecording) {
        stop_recording(audio_system);
    }
    if (audio_system->isPlaying) {
        stop_playback(audio_system);
    }

    // 清空队列
    clear_recording_queue(audio_system);
    clear_playback_queue(audio_system);

    // 释放Opus编解码器
    release_opus_codec(audio_system);

    // 终止PortAudio
    PaError err = Pa_Terminate();
    if (err != paNoError) {
        std::cerr << "PortAudio termination failed: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_INITIALIZE_FAILED;
    }

    return AUDIO_ERROR_NONE;
}

audio_error_t set_audio_mode(audio_system_t *audio_system, audio_mode_t mode) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    // 如果当前模式与目标模式相同，直接返回成功
    if (audio_system->current_mode == mode) {
        return AUDIO_ERROR_NONE;
    }

    // 如果当前正在录音或播放，先停止
    if (audio_system->isRecording) {
        stop_recording(audio_system);
    }
    if (audio_system->isPlaying) {
        stop_playback(audio_system);
    }

    // 清空队列
    clear_recording_queue(audio_system);
    clear_playback_queue(audio_system);

    // 设置新的模式
    audio_system->current_mode = mode;
    return AUDIO_ERROR_NONE;
}

audio_mode_t get_current_mode(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_MODE_NONE;
    }
    return audio_system->current_mode;
}

audio_error_t start_recording(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    if (audio_system->isRecording) {
        std::cerr << "Already recording. Cannot start again." << std::endl;
        return AUDIO_ERROR_MODE_CONFLICT;
    }

    // 检查是否没有设置音频模式
    if (audio_system->current_mode == AUDIO_MODE_NONE) {
        std::cerr << "No audio mode set. Cannot start recording." << std::endl;
        return AUDIO_ERROR_MODE_CONFLICT;
    }

    PaError err;

    // 配置音频流参数
    PaStreamParameters inputParameters;
    inputParameters.device = Pa_GetDefaultInputDevice();
    if (inputParameters.device == paNoDevice) {
        std::cerr << "No default input device found." << std::endl;
        return AUDIO_ERROR_DEVICE_NOT_FOUND;
    }
    inputParameters.channelCount = audio_system->channels;       // 通道数
    inputParameters.sampleFormat = paInt16;       // 16 位样本
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    // 打开音频流
    err = Pa_OpenStream(&audio_system->recordStream, 
                        &inputParameters, 
                        nullptr, // 无输出
                        audio_system->sample_rate, 
                        audio_system->sample_rate / 1000 * audio_system->frame_duration_ms, // 每缓冲区的帧数
                        paClipOff, // 不剪裁样本
                        recordCallback, 
                        audio_system);
    if (err != paNoError) {
        std::cerr << "Error opening recordStream: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_STREAM_OPEN_FAILED;
    }

    // 开始录制
    err = Pa_StartStream(audio_system->recordStream);
    if (err != paNoError) {
        std::cerr << "Error starting recordStream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(audio_system->recordStream);
        audio_system->recordStream = nullptr;
        return AUDIO_ERROR_STREAM_START_FAILED;
    }

    audio_system->isRecording = true;
    std::cout << "Recording started." << std::endl;
    return AUDIO_ERROR_NONE;
}

audio_error_t stop_recording(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    if (!audio_system->isRecording) {
        std::cerr << "Not recording. Nothing to stop." << std::endl;
        return AUDIO_ERROR_MODE_CONFLICT;
    }

    PaError err;

    // 停止录制
    err = Pa_StopStream(audio_system->recordStream);
    if (err != paNoError) {
        std::cerr << "Error stopping recordStream: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_STREAM_START_FAILED;
    }

    // 关闭音频流
    err = Pa_CloseStream(audio_system->recordStream);
    if (err != paNoError) {
        std::cerr << "Error closing recordStream: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_STREAM_OPEN_FAILED;
    }

    audio_system->recordStream = nullptr;
    audio_system->isRecording = false;
    std::cout << "Recording stopped." << std::endl;
    return AUDIO_ERROR_NONE;
}

audio_error_t clear_recording_queue(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    std::lock_guard<std::mutex> lock(audio_system->recordedAudioMutex);
    std::queue<std::vector<int16_t>> empty;
    std::swap(audio_system->recordedAudioQueue, empty);
    return AUDIO_ERROR_NONE;
}

audio_error_t start_playback(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    if (audio_system->isPlaying) {
        std::cerr << "Already playing. Cannot start again." << std::endl;
        return AUDIO_ERROR_MODE_CONFLICT;
    }

    // 检查是否没有设置音频模式
    if (audio_system->current_mode == AUDIO_MODE_NONE) {
        std::cerr << "No audio mode set. Cannot start playback." << std::endl;
        return AUDIO_ERROR_MODE_CONFLICT;
    }

    PaError err;

    // 配置音频流参数
    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice();
    if (outputParameters.device == paNoDevice) {
        std::cerr << "No default output device found." << std::endl;
        return AUDIO_ERROR_DEVICE_NOT_FOUND;
    }
    outputParameters.channelCount = audio_system->channels;       // 通道数
    outputParameters.sampleFormat = paInt16;       // 16 位样本
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    // 打开音频流
    err = Pa_OpenStream(&audio_system->playbackStream, 
                        nullptr, // 无输入
                        &outputParameters, 
                        audio_system->sample_rate, 
                        audio_system->sample_rate / 1000 * audio_system->frame_duration_ms, // 每缓冲区的帧数
                        paClipOff, // 不剪裁样本
                        playCallback, 
                        audio_system);
    if (err != paNoError) {
        std::cerr << "Error opening playbackStream: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_STREAM_OPEN_FAILED;
    }

    // 开始播放
    err = Pa_StartStream(audio_system->playbackStream);
    if (err != paNoError) {
        std::cerr << "Error starting playbackStream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(audio_system->playbackStream);
        audio_system->playbackStream = nullptr;
        return AUDIO_ERROR_STREAM_START_FAILED;
    }

    audio_system->isPlaying = true;
    std::cout << "Playback started." << std::endl;
    return AUDIO_ERROR_NONE;
}

audio_error_t stop_playback(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    if (!audio_system->isPlaying) {
        std::cerr << "Not playing. Nothing to stop." << std::endl;
        return AUDIO_ERROR_MODE_CONFLICT;
    }

    PaError err;

    // 停止播放
    err = Pa_StopStream(audio_system->playbackStream);
    if (err != paNoError) {
        std::cerr << "Error stopping playbackStream: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_STREAM_START_FAILED;
    }

    // 关闭音频流
    err = Pa_CloseStream(audio_system->playbackStream);
    if (err != paNoError) {
        std::cerr << "Error closing playbackStream: " << Pa_GetErrorText(err) << std::endl;
        return AUDIO_ERROR_STREAM_OPEN_FAILED;
    }

    audio_system->playbackStream = nullptr;
    audio_system->isPlaying = false;
    std::cout << "Playback stopped." << std::endl;
    return AUDIO_ERROR_NONE;
}

audio_error_t clear_playback_queue(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    std::lock_guard<std::mutex> lock(audio_system->playbackMutex);
    std::queue<std::vector<int16_t>> empty;
    std::swap(audio_system->playbackQueue, empty);
    return AUDIO_ERROR_NONE;
}

audio_error_t init_opus_codec(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    int error;

    // 初始化 Opus 编码器
    audio_system->encoder = opus_encoder_create(audio_system->sample_rate, audio_system->channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        std::cerr << "Opus encoder initialization failed: " << opus_strerror(error) << std::endl;
        return AUDIO_ERROR_INITIALIZE_FAILED;
    }

    // 初始化 Opus 解码器
    audio_system->decoder = opus_decoder_create(audio_system->sample_rate, audio_system->channels, &error);
    if (error != OPUS_OK) {
        std::cerr << "Opus decoder initialization failed: " << opus_strerror(error) << std::endl;
        opus_encoder_destroy(audio_system->encoder);
        audio_system->encoder = nullptr;
        return AUDIO_ERROR_INITIALIZE_FAILED;
    }
    return AUDIO_ERROR_NONE;
}

audio_error_t encode_opus(audio_system_t *audio_system, uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size) {
    if (!audio_system || !input || !output || !output_size) {
        std::cerr << "Invalid parameters for encode_opus" << std::endl;
        return AUDIO_ERROR_INVALID_PARAM;
    }

    if (!audio_system->encoder) {
        std::cerr << "Encoder not initialized" << std::endl;
        return AUDIO_ERROR_ENCODE_FAILED;
    }

    // 计算样本数量
    size_t frame_size = input_size / sizeof(int16_t);

    if (frame_size <= 0) {
        std::cerr << "Invalid PCM frame size: " << frame_size << std::endl;
        return AUDIO_ERROR_ENCODE_FAILED;
    }

    // 使用固定的2048字节作为最大编码缓冲区大小，与官方实现保持一致
    const int MAX_ENCODE_BUFFER_SIZE = 2048;
    if (*output_size < MAX_ENCODE_BUFFER_SIZE) {
        std::cerr << "Output buffer too small for encoding. Need at least " << MAX_ENCODE_BUFFER_SIZE << " bytes" << std::endl;
        return AUDIO_ERROR_INVALID_PARAM;
    }

    // 对当前帧进行编码
    int encoded_bytes_size = opus_encode(audio_system->encoder, 
                                        reinterpret_cast<const int16_t*>(input), 
                                        static_cast<int>(frame_size), 
                                        output, 
                                        MAX_ENCODE_BUFFER_SIZE);

    if (encoded_bytes_size < 0) {
        std::cerr << "Encoding failed: " << opus_strerror(encoded_bytes_size) << std::endl;
        return AUDIO_ERROR_ENCODE_FAILED;
    }

    *output_size = static_cast<size_t>(encoded_bytes_size);
    return AUDIO_ERROR_NONE;
}

audio_error_t decode_opus(audio_system_t *audio_system, uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size) {
    if (!audio_system || !input || !output || !output_size) {
        std::cerr << "Invalid parameters for decode_opus" << std::endl;
        return AUDIO_ERROR_INVALID_PARAM;
    }

    if (!audio_system->decoder) {
        std::cerr << "Decoder not initialized" << std::endl;
        return AUDIO_ERROR_DECODE_FAILED;
    }

    // 对于16kHz采样率，使用固定的960样本帧大小，与官方实现保持一致
    const int FRAME_SIZE = 960;
    size_t max_output_size = FRAME_SIZE * audio_system->channels * sizeof(int16_t);

    if (max_output_size > *output_size) {
        std::cerr << "Output buffer too small for decoding. Need at least " << max_output_size << " bytes" << std::endl;
        return AUDIO_ERROR_INVALID_PARAM;
    }

    // 对当前帧进行解码
    int decoded_samples = opus_decode(audio_system->decoder, 
                                     input, 
                                     static_cast<int>(input_size), 
                                     reinterpret_cast<int16_t*>(output), 
                                     FRAME_SIZE, 
                                     0);

    if (decoded_samples < 0) {
        std::cerr << "Decoding failed: " << opus_strerror(decoded_samples) << std::endl;
        return AUDIO_ERROR_DECODE_FAILED;
    }

    *output_size = static_cast<size_t>(decoded_samples) * audio_system->channels * sizeof(int16_t);
    return AUDIO_ERROR_NONE;
}

audio_error_t release_opus_codec(audio_system_t *audio_system) {
    if (!audio_system) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    if (audio_system->encoder) {
        opus_encoder_destroy(audio_system->encoder);
        audio_system->encoder = nullptr;
    }
    if (audio_system->decoder) {
        opus_decoder_destroy(audio_system->decoder);
        audio_system->decoder = nullptr;
    }
    return AUDIO_ERROR_NONE;
}

audio_error_t save_audio(audio_system_t *audio_system, const char *file_path) {
    if (!audio_system || !file_path) {
        return AUDIO_ERROR_INVALID_PARAM;
    }

    std::ofstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return AUDIO_ERROR_INVALID_PARAM;
    }

    {
        std::unique_lock<std::mutex> lock(audio_system->recordedAudioMutex);
        std::queue<std::vector<int16_t>> tempQueue = audio_system->recordedAudioQueue;
        while (!tempQueue.empty()) {
            const std::vector<int16_t>& frame = tempQueue.front();
            file.write(reinterpret_cast<const char*>(frame.data()), frame.size() * sizeof(int16_t));
            tempQueue.pop();
        }
    }

    file.close();
    std::cout << "Saved recording to " << file_path << std::endl;
    return AUDIO_ERROR_NONE;
}

bool get_recorded_audio(audio_system_t *audio_system, std::vector<int16_t>& recordedData) {
    if (!audio_system) {
        return false;
    }

    std::unique_lock<std::mutex> lock(audio_system->recordedAudioMutex);
    
    // 如果队列不为空，直接返回数据
    if (!audio_system->recordedAudioQueue.empty()) {
        recordedData.swap(audio_system->recordedAudioQueue.front());
        audio_system->recordedAudioQueue.pop();
        return true;
    }
    
    // 如果队列为空且不再录音，返回false
    if (!audio_system->isRecording) {
        return false;
    }
    
    // 等待直到队列不为空或录音停止
    audio_system->recordedAudioCV.wait(lock, [audio_system] { 
        return !audio_system->recordedAudioQueue.empty() || !audio_system->isRecording; 
    });

    // 再次检查队列是否为空
    if (audio_system->recordedAudioQueue.empty()) {
        return false; // 队列为空且不再录音
    }

    recordedData.swap(audio_system->recordedAudioQueue.front());
    audio_system->recordedAudioQueue.pop();
    return true;
}

void add_frame_to_playback_queue(audio_system_t *audio_system, const std::vector<int16_t>& pcm_frame) {
    if (!audio_system) {
        return;
    }

    std::lock_guard<std::mutex> lock(audio_system->playbackMutex);
    
    // 计算每帧的样本数量
    int frame_size = audio_system->sample_rate / 1000 * audio_system->frame_duration_ms;

    // 如果当前帧大小小于预期的帧大小，则填充静音
    if (pcm_frame.size() < static_cast<size_t>(frame_size)) {
        auto tempFrame = pcm_frame;
        tempFrame.resize(frame_size, 0); // 使用0填充至目标长度
        audio_system->playbackQueue.push(tempFrame);
    } else {
        audio_system->playbackQueue.push(pcm_frame);
    }
}

std::queue<std::vector<int16_t>> load_audio_from_file(audio_system_t *audio_system, const std::string& filename, int frame_duration_ms) {
    std::queue<std::vector<int16_t>> audio_frames;
    
    if (!audio_system) {
        return audio_frames;
    }

    std::ifstream infile(filename, std::ios::binary);
    if (!infile) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return audio_frames;
    }

    // 获取文件大小
    infile.seekg(0, std::ios::end);
    std::streampos fileSize = infile.tellg();
    infile.seekg(0, std::ios::beg);

    // 计算样本数量
    size_t numSamples = static_cast<size_t>(fileSize) / sizeof(int16_t);

    // 读取音频数据
    std::vector<int16_t> audio_data(numSamples);
    infile.read(reinterpret_cast<char*>(audio_data.data()), fileSize);

    if (!infile) {
        std::cerr << "Error reading file: " << filename << std::endl;
        return audio_frames;
    }

    // 计算每帧的样本数量
    size_t frame_size = static_cast<size_t>(audio_system->sample_rate / 1000 * frame_duration_ms);

    // 将音频数据切分成帧
    for (size_t i = 0; i < numSamples; i += frame_size) {
        size_t remaining_samples = numSamples - i;
        size_t current_frame_size = (remaining_samples > frame_size) ? frame_size : remaining_samples;

        std::vector<int16_t> frame(current_frame_size);
        std::copy(audio_data.begin() + i, audio_data.begin() + i + current_frame_size, frame.begin());
        audio_frames.push(frame);
    }

    return audio_frames;
}

BinProtocol* pack_bin_frame(audio_system_t *audio_system, const uint8_t* payload, size_t payload_size, int ws_protocol_version) {
    (void) audio_system; // 未使用
    
    // 分配内存用于 BinaryProtocol + payload
    auto pack = static_cast<BinProtocol*>(malloc(sizeof(BinProtocol) + payload_size));
    if (!pack) {
        std::cerr << "Memory allocation failed" << std::endl;
        return nullptr;
    }

    // 与官方实现保持一致的协议打包格式
    pack->version = htons(static_cast<uint16_t>(ws_protocol_version));
    pack->type = htons(0);  // 表示音频数据类型
    pack->payload_size = htonl(static_cast<uint32_t>(payload_size));
    assert(sizeof(BinProtocol) == 8);
    
    // 复制payload数据
    memcpy(pack->payload, payload, payload_size);

    return pack;
}

bool unpack_bin_frame(audio_system_t *audio_system, const uint8_t* packed_data, size_t packed_data_size, BinProtocolInfo& protocol_info, std::vector<uint8_t>& opus_data) {
    (void) audio_system; // 未使用
    
    // 检查输入数据的有效性
    if (packed_data_size < sizeof(uint16_t) * 2 + sizeof(uint32_t)) { // 至少需要2字节版本+2字节类型+4字节负载大小
        std::cerr << "Packed data size is too small" << std::endl;
        return false;
    }

    // 解析头部信息 - 与官方实现保持一致的解析逻辑
    const uint16_t* version_ptr = reinterpret_cast<const uint16_t*>(packed_data);
    const uint16_t* type_ptr = reinterpret_cast<const uint16_t*>(packed_data + sizeof(uint16_t));
    const uint32_t* payload_size_ptr = reinterpret_cast<const uint32_t*>(packed_data + sizeof(uint16_t) * 2);

    uint16_t version = ntohs(*version_ptr);
    uint16_t type = ntohs(*type_ptr);
    uint32_t payload_size = ntohl(*payload_size_ptr);

    // 确认总数据大小是否匹配
    if (packed_data_size < sizeof(uint16_t) * 2 + sizeof(uint32_t) + payload_size) {
        std::cerr << "Packed data size does not match payload size" << std::endl;
        return false;
    }

    // protocol_info
    protocol_info.version = version;
    protocol_info.type = type;

    // 提取并填充opus_data
    opus_data.clear();
    opus_data.insert(opus_data.end(), 
                     packed_data + sizeof(uint16_t) * 2 + sizeof(uint32_t), 
                     packed_data + sizeof(uint16_t) * 2 + sizeof(uint32_t) + payload_size);

    return true;
}