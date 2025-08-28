#include "./AudioProcess.h"
#include "../Utils/user_log.h"
#include <iostream>
#include <fstream>


AudioProcess::AudioProcess(int sample_rate, int channels, int frame_duration_ms) 
    : sample_rate(sample_rate), 
      channels(channels), 
      frame_duration_ms(frame_duration_ms),
      encoder(nullptr), 
      decoder(nullptr), 
      isRecording(false), 
      recordStream(nullptr),
      isPlaying(false),
      playbackStream(nullptr) {
        if (!initializeOpus()) {
            USER_LOG_ERROR("Failed to initialize Opus encoder/decoder.");
        }
}

AudioProcess::~AudioProcess() {
    cleanupOpus();
    clearRecordedAudioQueue();
    clearPlaybackAudioQueue();
    if (isRecording) {
        stopRecording();
    }
    if (isPlaying) {
        stopPlaying();
    }
}

bool AudioProcess::initializeOpus() {
    int error;

    // 初始化 Opus 编码器
    encoder = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        USER_LOG_ERROR("Opus encoder initialization failed: %s", opus_strerror(error));
        return false;
    }

    // 初始化 Opus 解码器
    decoder = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK) {
        USER_LOG_ERROR("Opus decoder initialization failed: %s", opus_strerror(error));
        opus_encoder_destroy(encoder);
        return false;
    }
    return true;
}

void AudioProcess::cleanupOpus() {
    if (encoder) {
        opus_encoder_destroy(encoder);
    }
    if (decoder) {
        opus_decoder_destroy(decoder);
    }
}

bool AudioProcess::startRecording() {

    if (isRecording) {
        USER_LOG_WARN("Already recording. Cannot start again.");
        return false;
    }

    PaError err;

    // 初始化 PortAudio
    err = Pa_Initialize();
    if (err != paNoError) {
        USER_LOG_ERROR("PortAudio error: %s", Pa_GetErrorText(err));
        return false;
    }

    // 配置音频流参数
    PaStreamParameters inputParameters;
    inputParameters.device = Pa_GetDefaultInputDevice();
    if (inputParameters.device == paNoDevice) {
        USER_LOG_ERROR("No default input device found.");
        Pa_Terminate();
        return false;
    }
    inputParameters.channelCount = channels;       // 通道数
    inputParameters.sampleFormat = paInt16;       // 16 位样本
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    // 打开音频流
    err = Pa_OpenStream(&recordStream,
                        &inputParameters,
                        nullptr, // 无输出
                        sample_rate,
                        sample_rate / 1000 * frame_duration_ms, // 每缓冲区的帧数
                        paClipOff, // 不剪裁样本
                        recordCallback,
                        this);
    if (err != paNoError) {
        USER_LOG_ERROR("Error opening recordStream: %s", Pa_GetErrorText(err));
        Pa_Terminate();
        return false;
    }

    // 开始录制
    err = Pa_StartStream(recordStream);
    if (err != paNoError) {
        USER_LOG_ERROR("Error starting recordStream: %s", Pa_GetErrorText(err));
        Pa_CloseStream(recordStream);
        Pa_Terminate();
        return false;
    }

    isRecording = true;
    USER_LOG_INFO("Recording started.");
    return true;
}

bool AudioProcess::stopRecording() {

    if (!isRecording) {
        USER_LOG_WARN("Not recording. Nothing to stop.");
        return false;
    }

    PaError err;

    // 停止录制
    err = Pa_StopStream(recordStream);
    if (err != paNoError) {
        USER_LOG_ERROR("Error stopping recordStream: %s", Pa_GetErrorText(err));
        return false;
    }

    // 关闭音频流
    err = Pa_CloseStream(recordStream);
    if (err != paNoError) {
        USER_LOG_ERROR("Error closing recordStream: %s", Pa_GetErrorText(err));
        return false;
    }

    // 释放 PortAudio 资源
    Pa_Terminate();

    isRecording = false;
    USER_LOG_INFO("Recording stopped.");
    return true;
}

bool AudioProcess::getRecordedAudio(std::vector<int16_t>& recordedData) {
    std::unique_lock<std::mutex> lock(recordedAudioMutex);
    recordedAudioCV.wait(lock, [this] { return !recordedAudioQueue.empty() || !isRecording; });

    if (recordedAudioQueue.empty()) {
        return false; // 队列为空且不再录音
    }

    recordedData.swap(recordedAudioQueue.front());
    recordedAudioQueue.pop();
    return true;
}

void AudioProcess::clearRecordedAudioQueue() {
    std::lock_guard<std::mutex> lock(recordedAudioMutex);
    std::queue<std::vector<int16_t>> empty;
    std::swap(recordedAudioQueue, empty);
}

int AudioProcess::recordCallback(const void *inputBuffer, void *outputBuffer,
                                 unsigned long framesPerBuffer,
                                 const PaStreamCallbackTimeInfo* timeInfo,
                                 PaStreamCallbackFlags statusFlags,
                                 void *userData) {
    (void) outputBuffer; // 未使用输出缓冲区
    (void) timeInfo;     // 未使用时间信息
    (void) statusFlags;  // 未使用状态标志

    AudioProcess* audioProcess = static_cast<AudioProcess*>(userData);
    const int16_t* input = static_cast<const int16_t*>(inputBuffer);

    std::vector<int16_t> frame(framesPerBuffer * audioProcess->channels);
    std::copy(input, input + framesPerBuffer * audioProcess->channels, frame.begin());

    {
        std::lock_guard<std::mutex> lock(audioProcess->recordedAudioMutex);

        // 检查队列长度是否超过 750
        if (audioProcess->recordedAudioQueue.size() >= 750) {
            audioProcess->recordedAudioQueue.pop(); // 移除最旧的帧
        }

        // 添加新的帧
        audioProcess->recordedAudioQueue.push(frame);
    }
    audioProcess->recordedAudioCV.notify_one();

    return paContinue;
}

bool AudioProcess::startPlaying() {
    if (isPlaying) {
        USER_LOG_WARN("Already playing. Cannot start again.");
        return false;
    }

    PaError err;

    // 初始化 PortAudio
    err = Pa_Initialize();
    if (err != paNoError) {
        USER_LOG_ERROR("PortAudio error: %s", Pa_GetErrorText(err));
        return false;
    }

    // 配置音频流参数
    PaStreamParameters outputParameters;
    outputParameters.device = Pa_GetDefaultOutputDevice();
    if (outputParameters.device == paNoDevice) {
        USER_LOG_ERROR("No default output device found.");
        Pa_Terminate();
        return false;
    }
    outputParameters.channelCount = channels;       // 通道数
    outputParameters.sampleFormat = paInt16;       // 16 位样本
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    // 打开音频流
    err = Pa_OpenStream(&playbackStream,
                        nullptr, // 无输入
                        &outputParameters,
                        sample_rate,
                        sample_rate / 1000 * frame_duration_ms, // 每缓冲区的帧数
                        paClipOff, // 不剪裁样本
                        playCallback,
                        this);
    if (err != paNoError) {
        USER_LOG_ERROR("Error opening playbackStream: %s", Pa_GetErrorText(err));
        Pa_Terminate();
        return false;
    }

    // 开始播放
    err = Pa_StartStream(playbackStream);
    if (err != paNoError) {
        USER_LOG_ERROR("Error starting playbackStream: %s", Pa_GetErrorText(err));
        Pa_CloseStream(playbackStream);
        Pa_Terminate();
        return false;
    }

    isPlaying = true;
    USER_LOG_INFO("Playback started.");
    return true;
}

bool AudioProcess::stopPlaying() {
    if (!isPlaying) {
        USER_LOG_WARN("Not playing. Nothing to stop.");
        return false;
    }

    PaError err;

    // 停止播放
    err = Pa_StopStream(playbackStream);
    if (err != paNoError) {
        USER_LOG_ERROR("Error stopping playbackStream: %s", Pa_GetErrorText(err));
        return false;
    }

    // 关闭音频流
    err = Pa_CloseStream(playbackStream);
    if (err != paNoError) {
        USER_LOG_ERROR("Error closing playbackStream: %s", Pa_GetErrorText(err));
        return false;
    }

    // 释放 PortAudio 资源
    Pa_Terminate();

    isPlaying = false;
    USER_LOG_INFO("Playback stopped.");
    return true;
}

int AudioProcess::playCallback(const void *inputBuffer, void *outputBuffer,
                               unsigned long framesPerBuffer,
                               const PaStreamCallbackTimeInfo* timeInfo,
                               PaStreamCallbackFlags statusFlags,
                               void *userData) {
    (void) inputBuffer; // 未使用输入缓冲区
    (void) timeInfo;     // 未使用时间信息
    (void) statusFlags;  // 未使用状态标志

    AudioProcess* audioProcess = static_cast<AudioProcess*>(userData);
    int16_t* output = static_cast<int16_t*>(outputBuffer);

    std::lock_guard<std::mutex> lock(audioProcess->playbackMutex);

    if (audioProcess->playbackQueue.empty()) {
        // 如果队列为空，则填充静音数据
        std::fill(output, output + framesPerBuffer * audioProcess->channels, 0);
        return paContinue;
    }

    // 获取并处理当前帧
    std::vector<int16_t>& currentFrame = audioProcess->playbackQueue.front();
    size_t samplesToCopy = std::min(static_cast<size_t>(framesPerBuffer * audioProcess->channels), currentFrame.size());

    std::copy(currentFrame.begin(), currentFrame.begin() + samplesToCopy, output);

    if (samplesToCopy < framesPerBuffer * audioProcess->channels) {
        // 如果当前帧不足，则用静音填充剩余部分
        std::fill(output + samplesToCopy, output + framesPerBuffer * audioProcess->channels, 0);
    }

    // 移除已播放的数据
    if (samplesToCopy == currentFrame.size()) {
        audioProcess->playbackQueue.pop();
    } else {
        // 更新队列中的第一个元素以删除已播放的部分
        audioProcess->playbackQueue.front().erase(audioProcess->playbackQueue.front().begin(), audioProcess->playbackQueue.front().begin() + samplesToCopy);
    }

    return paContinue;
}

void AudioProcess::clearPlaybackAudioQueue() {
    std::lock_guard<std::mutex> lock(playbackMutex);
    std::queue<std::vector<int16_t>> empty;
    std::swap(playbackQueue, empty);
}

void AudioProcess::addFrameToPlaybackQueue(const std::vector<int16_t>& pcm_frame) {
    std::lock_guard<std::mutex> lock(playbackMutex);
    
    // 计算每帧的样本数量
    int frame_size = sample_rate / 1000 * frame_duration_ms;

    // 如果当前帧大小小于预期的帧大小，则填充静音
    if (pcm_frame.size() < static_cast<size_t>(frame_size)) {
        auto tempFrame = pcm_frame;
        tempFrame.resize(frame_size, 0); // 使用0填充至目标长度
        playbackQueue.push(tempFrame);
    } else {
        playbackQueue.push(pcm_frame);
    }
}

std::queue<std::vector<int16_t>> AudioProcess::loadAudioFromFile(const std::string& filename, int frame_duration_ms) {
    std::ifstream infile(filename, std::ios::binary);
    if (!infile) {
        USER_LOG_ERROR("Failed to open file: %s", filename.c_str());
        return {};
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
        USER_LOG_ERROR("Error reading file: %s", filename.c_str());
        return {};
    }

    // 计算每帧的样本数量
    int frame_size = sample_rate / 1000 * frame_duration_ms;

    // 将音频数据切分成帧
    std::queue<std::vector<int16_t>> audio_frames;
    for (size_t i = 0; i < numSamples; i += frame_size) {
        size_t remaining_samples = numSamples - i;
        size_t current_frame_size = (remaining_samples > frame_size) ? frame_size : remaining_samples;

        std::vector<int16_t> frame(current_frame_size);
        std::copy(audio_data.begin() + i, audio_data.begin() + i + current_frame_size, frame.begin());
        audio_frames.push(frame);
    }

    return audio_frames;
}


void AudioProcess::saveToPCMFile(const std::string& filename, const std::queue<std::vector<int16_t>>& audioQueue) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        USER_LOG_ERROR("Failed to open file: %s", filename.c_str());
        return;
    }

    {
        std::queue<std::vector<int16_t>> tempQueue = audioQueue;
        while (!tempQueue.empty()) {
            const std::vector<int16_t>& frame = tempQueue.front();
            file.write(reinterpret_cast<const char*>(frame.data()), frame.size() * sizeof(int16_t));
            tempQueue.pop();
        }
    }

    file.close();
    USER_LOG_INFO("Saved recording to %s", filename.c_str());
}

void AudioProcess::saveToPCMFile(const std::string& filename) {
    std::unique_lock<std::mutex> lock(recordedAudioMutex);
    saveToPCMFile(filename, recordedAudioQueue);
}

bool AudioProcess::encode(const std::vector<int16_t>& pcm_frame, uint8_t* opus_data, size_t& opus_data_size) {
    if (!encoder) {
        USER_LOG_ERROR("Encoder not initialized");
        return false;
    }

    int frame_size = pcm_frame.size();

    if (frame_size <= 0) {
        USER_LOG_ERROR("Invalid PCM frame size: %d", frame_size);
        return false;
    }

    // 对当前帧进行编码
    int encoded_bytes_size = opus_encode(encoder, pcm_frame.data(), frame_size, opus_data, 2048); // max 2048 bytes

    if (encoded_bytes_size < 0) {
        USER_LOG_ERROR("Encoding failed: %s", opus_strerror(encoded_bytes_size));
        return false;
    }

    opus_data_size = static_cast<size_t>(encoded_bytes_size);
    return true;
}

bool AudioProcess::decode(const uint8_t* opus_data, size_t opus_data_size, std::vector<int16_t>& pcm_frame) {
    if (!decoder) {
        USER_LOG_ERROR("Decoder not initialized");
        return false;
    }

    int frame_size = 960;  // 40ms 帧, 16000Hz 采样率, 理论上应该是 640 个样本，但是 Opus 限制为 960
    pcm_frame.resize(frame_size * channels);

    // 对当前帧进行解码
    int decoded_samples = opus_decode(decoder, opus_data, static_cast<int>(opus_data_size), pcm_frame.data(), frame_size, 0);

    if (decoded_samples < 0) {
        USER_LOG_ERROR("Decoding failed: %s", opus_strerror(decoded_samples));
        return false;
    }

    pcm_frame.resize(decoded_samples * channels);
    return true;
}

BinProtocol* AudioProcess::PackBinFrame(const uint8_t* payload, size_t payload_size, int ws_protocol_version) {
    // Allocate memory for BinaryProtocol + payload
    auto pack = (BinProtocol*)malloc(sizeof(BinProtocol) + payload_size);
    if (!pack) {
        USER_LOG_ERROR("Memory allocation failed");
        return nullptr;
    }

    pack->version = htons(ws_protocol_version);
    pack->type = htons(0);  // Indicate audio data type
    pack->payload_size = htonl(payload_size);
    assert(sizeof(BinProtocol) == 8);

    // Copy payload data
    memcpy(pack->payload, payload, payload_size);

    return pack;
}

bool AudioProcess::UnpackBinFrame(const uint8_t* packed_data, size_t packed_data_size, BinProtocolInfo& protocol_info, std::vector<uint8_t>& opus_data) {
    // 检查输入数据的有效性
    if (packed_data_size < sizeof(uint16_t) * 2 + sizeof(uint32_t)) { // 至少需要2字节版本+2字节类型+4字节负载大小
        USER_LOG_ERROR("Packed data size is too small");
        return false;
    }

    // 解析头部信息
    const uint16_t* version_ptr = reinterpret_cast<const uint16_t*>(packed_data);
    const uint16_t* type_ptr = reinterpret_cast<const uint16_t*>(packed_data + sizeof(uint16_t));
    const uint32_t* payload_size_ptr = reinterpret_cast<const uint32_t*>(packed_data + sizeof(uint16_t) * 2);

    uint16_t version = ntohs(*version_ptr);
    uint16_t type = ntohs(*type_ptr);
    uint32_t payload_size = ntohl(*payload_size_ptr);

    // 确认总数据大小是否匹配
    if (packed_data_size < sizeof(uint16_t) * 2 + sizeof(uint32_t) + payload_size) {
        USER_LOG_ERROR("Packed data size does not match payload size");
        return false;
    }

    // protocol_info
    protocol_info.version = version;
    protocol_info.type = type;

    // 提取并填充opus_data
    opus_data.clear();
    opus_data.insert(opus_data.end(), packed_data + sizeof(uint16_t) * 2 + sizeof(uint32_t), 
                     packed_data + sizeof(uint16_t) * 2 + sizeof(uint32_t) + payload_size);

    return true;
}