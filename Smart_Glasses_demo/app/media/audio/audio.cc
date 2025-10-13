// #include "audio.h"
// #include <iostream>
// #include <fstream>
// #include <cstring>
// #include <algorithm>

// // 录音回调函数
// static int recordCallback(const void *inputBuffer, void *outputBuffer, 
//                          unsigned long framesPerBuffer, 
//                          const PaStreamCallbackTimeInfo* timeInfo, 
//                          PaStreamCallbackFlags statusFlags, 
//                          void *userData) {
//     (void) outputBuffer; // 未使用输出缓冲区
//     (void) timeInfo;     // 未使用时间信息
//     (void) statusFlags;  // 未使用状态标志

//     audio_system_t* audio_system = static_cast<audio_system_t*>(userData);
//     const int16_t* input = static_cast<const int16_t*>(inputBuffer);

//     std::vector<int16_t> frame;
//     frame.reserve(framesPerBuffer * audio_system->channels);
//     frame.assign(input, input + framesPerBuffer * audio_system->channels);

//     // 应用3A算法处理
//     if (audio_system->a3_state) {
//         process_audio_3a(audio_system, frame);
//     }
    
//     // 唤醒词检测（仅在非AI流式传输状态下）
//     static bool logged_wakeword_callback_status = false;
//     if (!logged_wakeword_callback_status) {
//         if (audio_system->wakeword_audio_callback && audio_system->ai_manager) {
//             std::cout << "[Audio] ✓ Wakeword callback is set and will be called" << std::endl;
//         } else {
//             std::cout << "[Audio] ✗ Wakeword callback NOT set: callback=" 
//                       << (audio_system->wakeword_audio_callback ? "YES" : "NO")
//                       << ", ai_manager=" << (audio_system->ai_manager ? "YES" : "NO") << std::endl;
//         }
//         logged_wakeword_callback_status = true;
//     }
    
//     // 只在非AI流式传输状态下处理唤醒词检测（避免与AI音频上传冲突）
//     if (!audio_system->is_ai_streaming && 
//         audio_system->wakeword_audio_callback && 
//         audio_system->ai_manager) {
//         audio_system->wakeword_audio_callback(audio_system->ai_manager, frame.data(), frame.size());
//     }

//     {
//         std::lock_guard<std::mutex> lock(audio_system->recordedAudioMutex);

//         // 检查队列长度是否超过 750
//         if (audio_system->recordedAudioQueue.size() >= 750) {
//             audio_system->recordedAudioQueue.pop(); // 移除最旧的帧
//         }

//     // 添加新的帧
//     audio_system->recordedAudioQueue.push(frame);
//     }
//     audio_system->recordedAudioCV.notify_one();

// #if USE_WEBRTC
//     // WebRTC音频数据发送（仅在WebRTC模式下）
//     if (audio_system->current_mode == AUDIO_MODE_WEBRTC && 
//         audio_system->is_webrtc_streaming && 
//         audio_system->webrtc_audio_callback) {
//         // 编码为Opus
//         uint8_t opus_buffer[2048];
//         size_t opus_size = 2048;
        
//         if (encode_opus(audio_system, 
//                     reinterpret_cast<uint8_t*>(frame.data()),
//                     frame.size() * sizeof(int16_t),
//                     opus_buffer, &opus_size) == AUDIO_ERROR_NONE) {
//             // 获取同步后的时间戳
//              uint64_t synced_timestamp = audio_system->sync_ctx ? sync_get_timestamp(audio_system->sync_ctx, get_nowus(), true) : get_nowus();
//             // 调用WebRTC回调发送Opus数据
//             audio_system->webrtc_audio_callback(opus_buffer, opus_size, synced_timestamp);
//         }
//     }
// #endif

//     // xiaozhi AI音频数据发送（仅在AI模式下）
//     if (audio_system->current_mode == AUDIO_MODE_AI &&
//         audio_system->is_ai_streaming && 
//         audio_system->ai_audio_callback) {
//         // xiaozhi服务器期望16kHz的音频，需要重采样
        
//         // 初始化AI专用编码器和重采样器（如果还没初始化）
//         if (!audio_system->ai_encoder) {
//             // 创建16kHz Opus编码器
//             int error;
//             audio_system->ai_encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
//             if (error != OPUS_OK) {
//                 std::cerr << "[Audio] ✗ Failed to create AI Opus encoder" << std::endl;
//                 return paContinue;
//             }
            
//             // 配置AI编码器
//             opus_encoder_ctl(audio_system->ai_encoder, OPUS_SET_BITRATE(32000));
//             opus_encoder_ctl(audio_system->ai_encoder, OPUS_SET_VBR(1));
//             opus_encoder_ctl(audio_system->ai_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
            
//             // 创建AI专用重采样器 48kHz → 16kHz
//             audio_system->ai_resampler = src_new(SRC_SINC_BEST_QUALITY, 1, &error);
//             if (!audio_system->ai_resampler) {
//                 std::cerr << "[Audio] ✗ Failed to create AI resampler: " << src_strerror(error) << std::endl;
//                 opus_encoder_destroy(audio_system->ai_encoder);
//                 audio_system->ai_encoder = nullptr;
//                 return paContinue;
//             }
            
//             // 清空重采样缓冲区
//             audio_system->ai_resample_buffer.clear();
            
//             std::cout << "[Audio] ✓ AI audio pipeline: 48kHz → Resample → 16kHz → Opus (independent resampler)" << std::endl;
//         }
        
//         // 1. 重采样 48kHz → 16kHz (使用独立的AI resampler)
//         double src_ratio = 16000.0 / 48000.0;  // 1/3
//         SRC_DATA src_data;
//         std::vector<float> input_float(frame.size());
//         std::vector<float> output_float(frame.size());  // 预留足够空间
        
//         // int16 → float
//         src_short_to_float_array(frame.data(), input_float.data(), frame.size());
        
//         // 重采样
//         src_data.data_in = input_float.data();
//         src_data.input_frames = frame.size();
//         src_data.data_out = output_float.data();
//         src_data.output_frames = output_float.size();
//         src_data.src_ratio = src_ratio;
//         src_data.end_of_input = 0;
        
//         int resample_error = src_process(audio_system->ai_resampler, &src_data);
//         if (resample_error) {
//             std::cerr << "[Audio] ✗ AI resample error: " << src_strerror(resample_error) << std::endl;
//             return paContinue;
//         }
        
//         // float → int16，添加到累积缓冲区
//         for (long i = 0; i < src_data.output_frames_gen; i++) {
//             float sample = output_float[i];
//             sample = std::max(-1.0f, std::min(1.0f, sample));
//             audio_system->ai_resample_buffer.push_back(static_cast<int16_t>(sample * 32767.0f));
//         }
        
//         // 2. 当累积到320样本（16kHz 20ms）时，进行Opus编码
//         const int TARGET_FRAME_SIZE = 320;  // 16kHz * 0.02s = 320 samples
//         while (audio_system->ai_resample_buffer.size() >= TARGET_FRAME_SIZE) {
//             // 取出320样本
//             std::vector<int16_t> encode_frame(audio_system->ai_resample_buffer.begin(),
//                                               audio_system->ai_resample_buffer.begin() + TARGET_FRAME_SIZE);
//             audio_system->ai_resample_buffer.erase(audio_system->ai_resample_buffer.begin(),
//                                                    audio_system->ai_resample_buffer.begin() + TARGET_FRAME_SIZE);
            
//             // 使用AI专用编码器编码
//             uint8_t opus_buffer[2048];
//             int encoded_bytes = opus_encode(audio_system->ai_encoder,
//                                            encode_frame.data(),
//                                            TARGET_FRAME_SIZE,
//                                            opus_buffer,
//                                            sizeof(opus_buffer));
            
//             if (encoded_bytes < 0) {
//                 std::cerr << "[Audio] ✗ AI Opus encode failed: " << opus_strerror(encoded_bytes) << std::endl;
//                 continue;
//             }
            
//             // 3. 通过回调发送编码后的数据
//             audio_system->ai_audio_callback(opus_buffer, encoded_bytes, get_nowus());
//         }
//     }

//     return paContinue;
// }

// // 播放回调函数
// static int playCallback(const void *inputBuffer, void *outputBuffer, 
//                         unsigned long framesPerBuffer, 
//                         const PaStreamCallbackTimeInfo* timeInfo, 
//                         PaStreamCallbackFlags statusFlags, 
//                         void *userData) {
//     (void) inputBuffer; // 未使用输入缓冲区
//     (void) timeInfo;     // 未使用时间信息
//     (void) statusFlags;  // 未使用状态标志

//     audio_system_t* audio_system = static_cast<audio_system_t*>(userData);
//     int16_t* output = static_cast<int16_t*>(outputBuffer);

//     std::lock_guard<std::mutex> lock(audio_system->playbackMutex);

//     if (audio_system->playbackQueue.empty()) {
//         // 如果队列为空，则填充静音数据
//         std::fill(output, output + framesPerBuffer * audio_system->channels, 0);
//         return paContinue;
//     }

//     // 获取并处理当前帧
//     std::vector<int16_t>& currentFrame = audio_system->playbackQueue.front();
//     size_t samplesToCopy = std::min(static_cast<size_t>(framesPerBuffer * audio_system->channels), currentFrame.size());

//     // 使用memcpy优化大数据拷贝性能
//     std::memcpy(output, currentFrame.data(), samplesToCopy * sizeof(int16_t));

//     if (samplesToCopy < framesPerBuffer * audio_system->channels) {
//         // 如果当前帧不足，则用静音填充剩余部分
//         std::fill(output + samplesToCopy, output + framesPerBuffer * audio_system->channels, 0);
//     }

//     // 移除已播放的数据
//     if (samplesToCopy == currentFrame.size()) {
//         audio_system->playbackQueue.pop();
//     } else {
//         // 更新队列中的第一个元素以删除已播放的部分
//         audio_system->playbackQueue.front().erase(audio_system->playbackQueue.front().begin(), 
//                                                  audio_system->playbackQueue.front().begin() + samplesToCopy);
//     }

//     return paContinue;
// }

// audio_error_t audio_system_init(audio_system_t *audio_system, sync_context_t *sync_ctx) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 初始化默认参数
//     audio_system->sample_rate = AUDIO_SAMPLE_RATE;
//     audio_system->channels = AUDIO_CHANNELS;
//     audio_system->frame_duration_ms = AUDIO_FRAME_DURATION_MS;
//     audio_system->encoder = nullptr;
//     audio_system->decoder = nullptr;
//     audio_system->ai_encoder = nullptr;
//     audio_system->recordStream = nullptr;
//     audio_system->playbackStream = nullptr;
//     audio_system->isRecording = false;
//     audio_system->isPlaying = false;
//     audio_system->current_mode = AUDIO_MODE_NONE;
//     audio_system->sync_ctx = sync_ctx;  // 设置时间同步上下文
    
//     // 初始化重采样配置
//     audio_system->resample_config.input_sample_rate = 0;
//     audio_system->resample_config.output_sample_rate = 0;
//     audio_system->resample_config.channels = 0;
//     audio_system->resample_config.converter_type = 0;
//     audio_system->resample_config.src_state = nullptr;
//     audio_system->resample_config.is_initialized = false;
    
//     // 初始化3A算法配置参数
//     audio_system->a3_config.denoise_enabled = AUDIO_DENOISE_ENABLED;
//     audio_system->a3_config.agc_enabled = AUDIO_AGC_ENABLED;
//     audio_system->a3_config.vad_enabled = AUDIO_VAD_ENABLED;
//     audio_system->a3_config.dereverb_enabled = AUDIO_DEREVERB_ENABLED;
//     audio_system->a3_config.agc_level = AUDIO_AGC_LEVEL;  // 目标电平
//     audio_system->a3_config.noise_suppress_level = AUDIO_NOISE_SUPPRESS_LEVEL;  // 噪声抑制级别 (dB)
//     audio_system->a3_config.echo_suppress_level = AUDIO_ECHO_SUPPRESS_LEVEL;   // 回声抑制级别 (dB)
//     audio_system->a3_config.agc_increment = AUDIO_AGC_INCREMENT;   // AGC增益增加速度 (dB/秒)
//     audio_system->a3_config.agc_decrement = AUDIO_AGC_DECREMENT;  // AGC增益减少速度 (dB/秒)
//     audio_system->a3_config.agc_max_gain = AUDIO_AGC_MAX_GAIN;    // AGC最大增益 (dB)
//     audio_system->a3_state = nullptr;
    
//     #if USE_WEBRTC
//     // WebRTC相关初始化
//     audio_system->webrtc_manager = nullptr;
//     audio_system->is_webrtc_streaming = false;
//     audio_system->webrtc_audio_callback = nullptr;
//     #endif
    
//     // xiaozhi AI相关初始化
//     audio_system->ai_manager = nullptr;
//     audio_system->is_ai_streaming = false;
//     audio_system->ai_audio_callback = nullptr;
//     audio_system->wakeword_audio_callback = nullptr;
//     audio_system->ai_resampler = nullptr;
//     audio_system->ai_resample_buffer.clear();
    
//     // 初始化PortAudio
//     PaError err = Pa_Initialize();
//     if (err != paNoError) {
//         std::cerr << "PortAudio initialization failed: " << Pa_GetErrorText(err) << std::endl;
//         return AUDIO_ERROR_INITIALIZE_FAILED;
//     }

//     // 初始化Opus编解码器
//     if (init_opus_codec(audio_system) != AUDIO_ERROR_NONE) {
//         Pa_Terminate();
//         return AUDIO_ERROR_INITIALIZE_FAILED;
//     }

//     return AUDIO_ERROR_NONE;
// }

// audio_error_t audio_system_deinit(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 停止录音和播放
//     if (audio_system->isRecording) {
//         stop_recording(audio_system);
//     }
//     if (audio_system->isPlaying) {
//         stop_playback(audio_system);
//     }

//     // 清空队列
//     clear_recording_queue(audio_system);
//     clear_playback_queue(audio_system);

//     // 释放重采样资源
//     release_audio_resample(audio_system);

//     // 释放Opus编解码器
//     release_opus_codec(audio_system);

//     // 释放3A算法资源
//     release_audio_3a(audio_system);

//     // 终止PortAudio
//     PaError err = Pa_Terminate();
//     if (err != paNoError) {
//         std::cerr << "PortAudio termination failed: " << Pa_GetErrorText(err) << std::endl;
//         return AUDIO_ERROR_INITIALIZE_FAILED;
//     }

//     return AUDIO_ERROR_NONE;
// }

// audio_error_t set_audio_mode(audio_system_t *audio_system, audio_mode_t mode) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 如果当前模式与目标模式相同，直接返回成功
//     if (audio_system->current_mode == mode) {
//         return AUDIO_ERROR_NONE;
//     }

//     // 如果当前正在录音或播放，先停止
//     if (audio_system->isRecording) {
//         stop_recording(audio_system);
//     }
//     if (audio_system->isPlaying) {
//         stop_playback(audio_system);
//     }

//     // 清空队列
//     clear_recording_queue(audio_system);
//     clear_playback_queue(audio_system);

//     // 设置新的模式
//     audio_system->current_mode = mode;
//     std::cout << "[Audio] Mode switched to: " << mode << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_mode_t get_current_mode(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_MODE_NONE;
//     }
//     return audio_system->current_mode;
// }

// audio_error_t start_recording(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (audio_system->isRecording) {
//         std::cerr << "Already recording. Cannot start again." << std::endl;
//         return AUDIO_ERROR_MODE_CONFLICT;
//     }

//     // 检查是否没有设置音频模式
//     if (audio_system->current_mode == AUDIO_MODE_NONE) {
//         std::cerr << "No audio mode set. Cannot start recording." << std::endl;
//         return AUDIO_ERROR_MODE_CONFLICT;
//     }

//     // 初始化3A算法
//     audio_error_t a3_err = init_audio_3a(audio_system);
//     if (a3_err != AUDIO_ERROR_NONE) {
//         std::cerr << "Failed to initialize 3A algorithms" << std::endl;
//         return a3_err;
//     }

//     PaError err;

//     // 配置音频流参数
//     PaStreamParameters inputParameters;
//     inputParameters.device = Pa_GetDefaultInputDevice();
//     if (inputParameters.device == paNoDevice) {
//         std::cerr << "No default input device found." << std::endl;
//         release_audio_3a(audio_system);
//         return AUDIO_ERROR_DEVICE_NOT_FOUND;
//     }
//     inputParameters.channelCount = audio_system->channels;       // 通道数
//     inputParameters.sampleFormat = paInt16;       // 16 位样本
//     inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
//     inputParameters.hostApiSpecificStreamInfo = nullptr;

//     // 打开音频流
//     err = Pa_OpenStream(&audio_system->recordStream, 
//                         &inputParameters, 
//                         nullptr, // 无输出
//                         audio_system->sample_rate, 
//                         audio_system->sample_rate / 1000 * audio_system->frame_duration_ms, // 每缓冲区的帧数
//                         paClipOff, // 不剪裁样本
//                         recordCallback, 
//                         audio_system);
//     if (err != paNoError) {
//         std::cerr << "Error opening recordStream: " << Pa_GetErrorText(err) << std::endl;
//         release_audio_3a(audio_system);
//         return AUDIO_ERROR_STREAM_OPEN_FAILED;
//     }

//     // 开始录制
//     err = Pa_StartStream(audio_system->recordStream);
//     if (err != paNoError) {
//         std::cerr << "Error starting recordStream: " << Pa_GetErrorText(err) << std::endl;
//         Pa_CloseStream(audio_system->recordStream);
//         audio_system->recordStream = nullptr;
//         release_audio_3a(audio_system);
//         return AUDIO_ERROR_STREAM_START_FAILED;
//     }

//     audio_system->isRecording = true;
//     std::cout << "Recording started." << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t stop_recording(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (!audio_system->isRecording) {
//         std::cerr << "Not recording. Nothing to stop." << std::endl;
//         return AUDIO_ERROR_MODE_CONFLICT;
//     }

//     PaError err;

//     // 停止录制
//     err = Pa_StopStream(audio_system->recordStream);
//     if (err != paNoError) {
//         std::cerr << "Error stopping recordStream: " << Pa_GetErrorText(err) << std::endl;
//         return AUDIO_ERROR_STREAM_START_FAILED;
//     }

//     // 关闭音频流
//     err = Pa_CloseStream(audio_system->recordStream);
//     if (err != paNoError) {
//         std::cerr << "Error closing recordStream: " << Pa_GetErrorText(err) << std::endl;
//         return AUDIO_ERROR_STREAM_OPEN_FAILED;
//     }

//     // 释放3A算法资源
//     release_audio_3a(audio_system);

//     audio_system->recordStream = nullptr;
//     audio_system->isRecording = false;
//     std::cout << "Recording stopped." << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t clear_recording_queue(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     std::lock_guard<std::mutex> lock(audio_system->recordedAudioMutex);
//     std::queue<std::vector<int16_t>> empty;
//     std::swap(audio_system->recordedAudioQueue, empty);
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t start_playback(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (audio_system->isPlaying) {
//         std::cerr << "Already playing. Cannot start again." << std::endl;
//         return AUDIO_ERROR_MODE_CONFLICT;
//     }

//     // 检查是否没有设置音频模式
//     if (audio_system->current_mode == AUDIO_MODE_NONE) {
//         std::cerr << "No audio mode set. Cannot start playback." << std::endl;
//         return AUDIO_ERROR_MODE_CONFLICT;
//     }

//     PaError err;

//     // 配置音频流参数
//     PaStreamParameters outputParameters;
//     outputParameters.device = Pa_GetDefaultOutputDevice();
//     if (outputParameters.device == paNoDevice) {
//         std::cerr << "No default output device found." << std::endl;
//         return AUDIO_ERROR_DEVICE_NOT_FOUND;
//     }
//     outputParameters.channelCount = audio_system->channels;       // 通道数
//     outputParameters.sampleFormat = paInt16;       // 16 位样本
//     outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
//     outputParameters.hostApiSpecificStreamInfo = nullptr;

//     // 打开音频流
//     err = Pa_OpenStream(&audio_system->playbackStream, 
//                         nullptr, // 无输入
//                         &outputParameters, 
//                         audio_system->sample_rate, 
//                         audio_system->sample_rate / 1000 * audio_system->frame_duration_ms, // 每缓冲区的帧数
//                         paClipOff, // 不剪裁样本
//                         playCallback, 
//                         audio_system);
//     if (err != paNoError) {
//         std::cerr << "Error opening playbackStream: " << Pa_GetErrorText(err) << std::endl;
//         return AUDIO_ERROR_STREAM_OPEN_FAILED;
//     }

//     // 开始播放
//     err = Pa_StartStream(audio_system->playbackStream);
//     if (err != paNoError) {
//         std::cerr << "Error starting playbackStream: " << Pa_GetErrorText(err) << std::endl;
//         Pa_CloseStream(audio_system->playbackStream);
//         audio_system->playbackStream = nullptr;
//         return AUDIO_ERROR_STREAM_START_FAILED;
//     }

//     audio_system->isPlaying = true;
//     std::cout << "Playback started." << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t stop_playback(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (!audio_system->isPlaying) {
//         std::cerr << "Not playing. Nothing to stop." << std::endl;
//         return AUDIO_ERROR_MODE_CONFLICT;
//     }

//     PaError err;

//     // 停止播放
//     err = Pa_StopStream(audio_system->playbackStream);
//     if (err != paNoError) {
//         std::cerr << "Error stopping playbackStream: " << Pa_GetErrorText(err) << std::endl;
//         return AUDIO_ERROR_STREAM_START_FAILED;
//     }

//     // 关闭音频流
//     err = Pa_CloseStream(audio_system->playbackStream);
//     if (err != paNoError) {
//         std::cerr << "Error closing playbackStream: " << Pa_GetErrorText(err) << std::endl;
//         return AUDIO_ERROR_STREAM_OPEN_FAILED;
//     }

//     audio_system->playbackStream = nullptr;
//     audio_system->isPlaying = false;
//     std::cout << "Playback stopped." << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t clear_playback_queue(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     std::lock_guard<std::mutex> lock(audio_system->playbackMutex);
//     std::queue<std::vector<int16_t>> empty;
//     std::swap(audio_system->playbackQueue, empty);
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t init_opus_codec(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     int error;

//     // 初始化 Opus 编码器
//     audio_system->encoder = opus_encoder_create(audio_system->sample_rate, audio_system->channels, OPUS_APPLICATION_VOIP, &error);
//     if (error != OPUS_OK) {
//         std::cerr << "Opus encoder initialization failed: " << opus_strerror(error) << std::endl;
//         return AUDIO_ERROR_INITIALIZE_FAILED;
//     }

//     // 初始化 Opus 解码器
//     audio_system->decoder = opus_decoder_create(audio_system->sample_rate, audio_system->channels, &error);
//     if (error != OPUS_OK) {
//         std::cerr << "Opus decoder initialization failed: " << opus_strerror(error) << std::endl;
//         opus_encoder_destroy(audio_system->encoder);
//         audio_system->encoder = nullptr;
//         return AUDIO_ERROR_INITIALIZE_FAILED;
//     }
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t encode_opus(audio_system_t *audio_system, uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size) {
//     if (!audio_system || !input || !output || !output_size) {
//         std::cerr << "Invalid parameters for encode_opus" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (!audio_system->encoder) {
//         std::cerr << "Encoder not initialized" << std::endl;
//         return AUDIO_ERROR_ENCODE_FAILED;
//     }

//     // 计算样本数量
//     size_t frame_size = input_size / sizeof(int16_t) / audio_system->channels;

//     if (frame_size <= 0) {
//         std::cerr << "Invalid PCM frame size: " << frame_size << std::endl;
//         return AUDIO_ERROR_ENCODE_FAILED;
//     }

//     // 使用固定的2048字节作为最大编码缓冲区大小
//     const int MAX_ENCODE_BUFFER_SIZE = 2048;
//     if (*output_size < MAX_ENCODE_BUFFER_SIZE) {
//         std::cerr << "Output buffer too small for encoding. Need at least " << MAX_ENCODE_BUFFER_SIZE << " bytes" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 对当前帧进行编码
//     int encoded_bytes_size = opus_encode(audio_system->encoder, 
//                                         reinterpret_cast<const int16_t*>(input), 
//                                         static_cast<int>(frame_size), 
//                                         output, 
//                                         MAX_ENCODE_BUFFER_SIZE);

//     if (encoded_bytes_size < 0) {
//         std::cerr << "Encoding failed: " << opus_strerror(encoded_bytes_size) << std::endl;
//         return AUDIO_ERROR_ENCODE_FAILED;
//     }

//     *output_size = static_cast<size_t>(encoded_bytes_size);
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t decode_opus(audio_system_t *audio_system, uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size) {
//     if (!audio_system || !input || !output || !output_size) {
//         std::cerr << "Invalid parameters for decode_opus" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (!audio_system->decoder) {
//         std::cerr << "Decoder not initialized" << std::endl;
//         return AUDIO_ERROR_DECODE_FAILED;
//     }

//     // 根据配置计算样本数
//     const int frame_size = audio_system->sample_rate / 1000 * audio_system->frame_duration_ms;
//     size_t max_output_size = frame_size * audio_system->channels * sizeof(int16_t);

//     if (max_output_size > *output_size) {
//         std::cerr << "Output buffer too small for decoding. Need at least " << max_output_size << " bytes" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 对当前帧进行解码
//     int decoded_samples = opus_decode(audio_system->decoder, 
//                                      input, 
//                                      static_cast<int>(input_size), 
//                                      reinterpret_cast<int16_t*>(output), 
//                                      frame_size, 
//                                      0);

//     if (decoded_samples < 0) {
//         std::cerr << "Decoding failed: " << opus_strerror(decoded_samples) << std::endl;
//         return AUDIO_ERROR_DECODE_FAILED;
//     }

//     *output_size = static_cast<size_t>(decoded_samples) * audio_system->channels * sizeof(int16_t);
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t release_opus_codec(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (audio_system->encoder) {
//         opus_encoder_destroy(audio_system->encoder);
//         audio_system->encoder = nullptr;
//     }
//     if (audio_system->decoder) {
//         opus_decoder_destroy(audio_system->decoder);
//         audio_system->decoder = nullptr;
//     }
//     if (audio_system->ai_encoder) {
//         opus_encoder_destroy(audio_system->ai_encoder);
//         audio_system->ai_encoder = nullptr;
//     }
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t save_audio(audio_system_t *audio_system, const char *file_path) {
//     if (!audio_system || !file_path) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     std::ofstream file(file_path, std::ios::binary);
//     if (!file) {
//         std::cerr << "Failed to open file: " << file_path << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     {
//         std::unique_lock<std::mutex> lock(audio_system->recordedAudioMutex);
//         std::queue<std::vector<int16_t>> tempQueue = audio_system->recordedAudioQueue;
//         while (!tempQueue.empty()) {
//             const std::vector<int16_t>& frame = tempQueue.front();
//             file.write(reinterpret_cast<const char*>(frame.data()), frame.size() * sizeof(int16_t));
//             tempQueue.pop();
//         }
//     }

//     file.close();
//     std::cout << "Saved recording to " << file_path << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// bool get_recorded_audio(audio_system_t *audio_system, std::vector<int16_t>& recordedData) {
//     if (!audio_system) {
//         return false;
//     }

//     std::unique_lock<std::mutex> lock(audio_system->recordedAudioMutex);
    
//     // 如果队列不为空，直接返回数据
//     if (!audio_system->recordedAudioQueue.empty()) {
//         recordedData.swap(audio_system->recordedAudioQueue.front());
//         audio_system->recordedAudioQueue.pop();
//         return true;
//     }
    
//     // 如果队列为空且不再录音，返回false
//     if (!audio_system->isRecording) {
//         return false;
//     }
    
//     // 等待直到队列不为空或录音停止
//     audio_system->recordedAudioCV.wait(lock, [audio_system] { 
//         return !audio_system->recordedAudioQueue.empty() || !audio_system->isRecording; 
//     });

//     // 再次检查队列是否为空
//     if (audio_system->recordedAudioQueue.empty()) {
//         return false; // 队列为空且不再录音
//     }

//     recordedData.swap(audio_system->recordedAudioQueue.front());
//     audio_system->recordedAudioQueue.pop();
//     return true;
// }

// void add_frame_to_playback_queue(audio_system_t *audio_system, const std::vector<int16_t>& pcm_frame) {
//     if (!audio_system) {
//         return;
//     }

//     std::lock_guard<std::mutex> lock(audio_system->playbackMutex);
    
//     // 计算每帧的样本数量
//     int frame_size = (audio_system->sample_rate / 1000 * audio_system->frame_duration_ms) * audio_system->channels;

//     // 如果当前帧大小小于预期的帧大小，则填充静音
//     if (pcm_frame.size() < static_cast<size_t>(frame_size)) {
//         auto tempFrame = pcm_frame;
//         tempFrame.resize(frame_size, 0); // 使用0填充至目标长度
//         audio_system->playbackQueue.push(tempFrame);
//     } else {
//         audio_system->playbackQueue.push(pcm_frame);
//     }
// }

// std::queue<std::vector<int16_t>> load_audio_from_file(audio_system_t *audio_system, const std::string& filename, int frame_duration_ms) {
//     std::queue<std::vector<int16_t>> audio_frames;
    
//     if (!audio_system) {
//         return audio_frames;
//     }

//     std::ifstream infile(filename, std::ios::binary);
//     if (!infile) {
//         std::cerr << "Failed to open file: " << filename << std::endl;
//         return audio_frames;
//     }

//     // 获取文件大小
//     infile.seekg(0, std::ios::end);
//     std::streampos fileSize = infile.tellg();
//     infile.seekg(0, std::ios::beg);

//     // 计算样本数量
//     size_t numSamples = static_cast<size_t>(fileSize) / sizeof(int16_t);

//     // 读取音频数据
//     std::vector<int16_t> audio_data(numSamples);
//     infile.read(reinterpret_cast<char*>(audio_data.data()), fileSize);

//     if (!infile) {
//         std::cerr << "Error reading file: " << filename << std::endl;
//         return audio_frames;
//     }

//     // 计算每帧的样本数量
//     size_t frame_size = static_cast<size_t>(audio_system->sample_rate / 1000 * frame_duration_ms);

//     // 将音频数据切分成帧
//     for (size_t i = 0; i < numSamples; i += frame_size) {
//         size_t remaining_samples = numSamples - i;
//         size_t current_frame_size = (remaining_samples > frame_size) ? frame_size : remaining_samples;

//         std::vector<int16_t> frame(current_frame_size);
//         std::copy(audio_data.begin() + i, audio_data.begin() + i + current_frame_size, frame.begin());
//         audio_frames.push(frame);
//     }

//     return audio_frames;
// }

// // ================== 重采样功能实现 ==================
// audio_error_t init_audio_resample(audio_system_t *audio_system, int input_rate, int output_rate, int channels, int converter_type) {
//     if (!audio_system) {
//         std::cerr << "Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 如果已经初始化，先释放
//     if (audio_system->resample_config.is_initialized) {
//         release_audio_resample(audio_system);
//     }

//     // 验证参数
//     if (input_rate <= 0 || output_rate <= 0 || channels <= 0) {
//         std::cerr << "Invalid resample parameters: input_rate=" << input_rate 
//                   << ", output_rate=" << output_rate << ", channels=" << channels << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 验证转换器类型
//     if (converter_type < SRC_SINC_BEST_QUALITY || converter_type > SRC_LINEAR) {
//         std::cerr << "Invalid converter type: " << converter_type << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 设置重采样参数
//     audio_system->resample_config.input_sample_rate = input_rate;
//     audio_system->resample_config.output_sample_rate = output_rate;
//     audio_system->resample_config.channels = channels;
//     audio_system->resample_config.converter_type = converter_type;
//     audio_system->resample_config.src_state = nullptr;
//     audio_system->resample_config.is_initialized = false;

//     // 初始化libsamplerate状态
//     int error;
//     audio_system->resample_config.src_state = src_new(converter_type, channels, &error);
//     if (!audio_system->resample_config.src_state) {
//         std::cerr << "Failed to initialize libsamplerate: " << src_strerror(error) << std::endl;
//         return AUDIO_ERROR_INITIALIZE_FAILED;
//     }

//     audio_system->resample_config.is_initialized = true;
//     std::cout << "Audio resample initialized: " << input_rate << "Hz -> " << output_rate 
//               << "Hz, channels=" << channels << ", converter=" << converter_type << std::endl;
    
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t process_audio_resample(audio_system_t *audio_system, const std::vector<int16_t>& input_data, std::vector<int16_t>& output_data) {
//     if (!audio_system) {
//         std::cerr << "Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (!audio_system->resample_config.is_initialized) {
//         std::cerr << "Resample not initialized" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (input_data.empty()) {
//         output_data.clear();
//         return AUDIO_ERROR_NONE;
//     }

//     // 计算输入样本数（按通道）
//     int input_frames = input_data.size() / audio_system->resample_config.channels;
//     if (input_frames <= 0) {
//         std::cerr << "Invalid input data size: " << input_data.size() 
//                   << ", channels: " << audio_system->resample_config.channels << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     // 计算输出缓冲区大小（按比例估算，通常需要1.5-2倍大小）
//     double ratio = static_cast<double>(audio_system->resample_config.output_sample_rate) / 
//                    static_cast<double>(audio_system->resample_config.input_sample_rate);
//     int estimated_output_frames = static_cast<int>(input_frames * ratio * 1.5) + 100; // 额外100帧缓冲
    
//     // 准备输入数据（转换为float）
//     std::vector<float> input_float(input_data.size());
//     for (size_t i = 0; i < input_data.size(); i++) {
//         input_float[i] = static_cast<float>(input_data[i]) / 32768.0f; // 转换为-1.0到1.0范围
//     }

//     // 准备输出缓冲区
//     std::vector<float> output_float(estimated_output_frames * audio_system->resample_config.channels);
    
//     // 设置重采样数据
//     SRC_DATA src_data;
//     src_data.data_in = input_float.data();
//     src_data.data_out = output_float.data();
//     src_data.input_frames = input_frames;
//     src_data.output_frames = estimated_output_frames;
//     src_data.src_ratio = ratio;
//     src_data.end_of_input = 0; // 0表示还有更多数据，1表示这是最后的数据

//     // 执行重采样
//     int error = src_process(audio_system->resample_config.src_state, &src_data);
//     if (error != 0) {
//         std::cerr << "Resample process failed: " << src_strerror(error) << std::endl;
//         return AUDIO_ERROR_ENCODE_FAILED; // 使用编码错误作为通用处理错误
//     }

//     // 转换回int16_t并填充输出
//     int actual_output_samples = src_data.output_frames_gen * audio_system->resample_config.channels;
//     output_data.resize(actual_output_samples);
    
//     for (int i = 0; i < actual_output_samples; i++) {
//         // 限制范围并转换回int16_t
//         float sample = output_float[i];
//         sample = std::max(-1.0f, std::min(1.0f, sample)); // 限制在-1.0到1.0范围
//         output_data[i] = static_cast<int16_t>(sample * 32767.0f);
//     }

//     return AUDIO_ERROR_NONE;
// }

// audio_error_t release_audio_resample(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }

//     if (audio_system->resample_config.src_state) {
//         src_delete(audio_system->resample_config.src_state);
//         audio_system->resample_config.src_state = nullptr;
//     }

//     audio_system->resample_config.is_initialized = false;
//     audio_system->resample_config.input_sample_rate = 0;
//     audio_system->resample_config.output_sample_rate = 0;
//     audio_system->resample_config.channels = 0;
//     audio_system->resample_config.converter_type = 0;

//     std::cout << "Audio resample released" << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// bool is_resample_initialized(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return false;
//     }
//     return audio_system->resample_config.is_initialized;
// }

// #if USE_WEBRTC
// audio_error_t start_webrtc_audio_stream(audio_system_t *audio_system) {
//     if (!audio_system) {
//         std::cerr << "[AUDIO] Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (!audio_system->webrtc_audio_callback) {
//         std::cerr << "[AUDIO] WebRTC audio callback not set" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (audio_system->is_webrtc_streaming) {
//         std::cout << "[AUDIO] WebRTC audio stream already started" << std::endl;
//         return AUDIO_ERROR_NONE;
//     }
    
//     // 设置音频模式为WebRTC
//     if (set_audio_mode(audio_system, AUDIO_MODE_WEBRTC) != AUDIO_ERROR_NONE) {
//         std::cerr << "[AUDIO] Failed to set audio mode to WebRTC" << std::endl;
//         return AUDIO_ERROR_MODE_CONFLICT;
//     }
    
//     // 开始录音
//     if (start_recording(audio_system) != AUDIO_ERROR_NONE) {
//         std::cerr << "[AUDIO] Failed to start recording for WebRTC" << std::endl;
//         return AUDIO_ERROR_STREAM_START_FAILED;
//     }
    
//     audio_system->is_webrtc_streaming = true;
//     std::cout << "[AUDIO] WebRTC audio stream started" << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t stop_webrtc_audio_stream(audio_system_t *audio_system) {
//     if (!audio_system) {
//         std::cerr << "[AUDIO] Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (!audio_system->is_webrtc_streaming) {
//         std::cout << "[AUDIO] WebRTC audio stream already stopped" << std::endl;
//         return AUDIO_ERROR_NONE;
//     }
    
//     // 停止录音
//     if (stop_recording(audio_system) != AUDIO_ERROR_NONE) {
//         std::cerr << "[AUDIO] Failed to stop recording for WebRTC" << std::endl;
//         return AUDIO_ERROR_STREAM_START_FAILED;
//     }
    
//     audio_system->is_webrtc_streaming = false;
//     std::cout << "[AUDIO] WebRTC audio stream stopped" << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t set_webrtc_audio_callback(audio_system_t *audio_system, void *webrtc_manager, void (*audio_callback)(void *data, int len, uint64_t timestamp)) {
//     if (!audio_system) {
//         std::cerr << "[AUDIO] Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (!audio_callback) {
//         std::cerr << "[AUDIO] Invalid audio callback" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     audio_system->webrtc_manager = webrtc_manager;
//     audio_system->webrtc_audio_callback = audio_callback;
    
//     std::cout << "[AUDIO] WebRTC audio callback set successfully" << std::endl;
//     return AUDIO_ERROR_NONE;
// }
// #endif

// audio_error_t start_ai_audio_stream(audio_system_t *audio_system) {
//     if (!audio_system) {
//         std::cerr << "[AUDIO] Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (!audio_system->ai_audio_callback) {
//         std::cerr << "[AUDIO] AI audio callback not set" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (audio_system->is_ai_streaming) {
//         std::cout << "[AUDIO] AI audio stream already started" << std::endl;
//         return AUDIO_ERROR_NONE;
//     }
    
//     // 标记为AI流式传输状态（开始上传音频到服务器）
//     audio_system->is_ai_streaming = true;
//     std::cout << "[AUDIO] AI audio streaming enabled (uploading to server)" << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t stop_ai_audio_stream(audio_system_t *audio_system) {
//     if (!audio_system) {
//         std::cerr << "[AUDIO] Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (!audio_system->is_ai_streaming) {
//         std::cout << "[AUDIO] AI audio stream already stopped" << std::endl;
//         return AUDIO_ERROR_NONE;
//     }
    
//     // 释放AI编码器
//     if (audio_system->ai_encoder) {
//         opus_encoder_destroy(audio_system->ai_encoder);
//         audio_system->ai_encoder = nullptr;
//         std::cout << "[AUDIO] AI encoder destroyed" << std::endl;
//     }
    
//     // 释放AI专用重采样器
//     if (audio_system->ai_resampler) {
//         src_delete(audio_system->ai_resampler);
//         audio_system->ai_resampler = nullptr;
//         std::cout << "[AUDIO] AI resampler released" << std::endl;
//     }
    
//     // 清空重采样缓冲区
//     audio_system->ai_resample_buffer.clear();
    
//     // 停止上传音频到服务器，但保持录音继续运行（用于唤醒词检测）
//     audio_system->is_ai_streaming = false;
//     std::cout << "[AUDIO] AI audio streaming disabled (stopped uploading, recording continues for wakeword)" << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// audio_error_t set_ai_audio_callback(audio_system_t *audio_system, void *ai_manager, void (*audio_callback)(void *data, int len, uint64_t timestamp)) {
//     if (!audio_system) {
//         std::cerr << "[AUDIO] Audio system is null" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (!audio_callback) {
//         std::cerr << "[AUDIO] Invalid audio callback" << std::endl;
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     audio_system->ai_manager = ai_manager;
//     audio_system->ai_audio_callback = audio_callback;
    
//     std::cout << "[AUDIO] AI audio callback set successfully" << std::endl;
//     return AUDIO_ERROR_NONE;
// }

// // 3A算法初始化函数
// audio_error_t init_audio_3a(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     // 计算每帧样本数
//     int frame_size = audio_system->sample_rate * audio_system->frame_duration_ms / 1000;
    
//     // 初始化Speex预处理状态
//     audio_system->a3_state = speex_preprocess_state_init(frame_size, audio_system->sample_rate);
//     if (!audio_system->a3_state) {
//         std::cerr << "Failed to initialize Speex preprocessor" << std::endl;
//         return AUDIO_ERROR_INITIALIZE_FAILED;
//     }
    
//     // 配置Speex预处理参数
//     configure_audio_3a(audio_system, &audio_system->a3_config);
    
//     return AUDIO_ERROR_NONE;
// }

// // 3A算法释放函数
// audio_error_t release_audio_3a(audio_system_t *audio_system) {
//     if (!audio_system) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     if (audio_system->a3_state) {
//         speex_preprocess_state_destroy(audio_system->a3_state);
//         audio_system->a3_state = nullptr;
//     }
    
//     return AUDIO_ERROR_NONE;
// }

// // 3A算法处理函数
// audio_error_t process_audio_3a(audio_system_t *audio_system, std::vector<int16_t>& audio_frame) {
//     if (!audio_system || !audio_system->a3_state || audio_frame.empty()) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     // 使用Speex预处理算法处理音频帧
//     speex_preprocess_run(audio_system->a3_state, audio_frame.data());
    
//     return AUDIO_ERROR_NONE;
// }

// // 3A算法配置函数
// audio_error_t configure_audio_3a(audio_system_t *audio_system, const audio_3a_config_t* config) {
//     if (!audio_system || !audio_system->a3_state || !config) {
//         return AUDIO_ERROR_INVALID_PARAM;
//     }
    
//     // 配置降噪
//     int denoise_enabled = config->denoise_enabled ? 1 : 0;
//     speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_DENOISE, &denoise_enabled);
    
//     if (config->denoise_enabled) {
//         int noise_suppress_level = config->noise_suppress_level;
//         speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, 
//                            &noise_suppress_level);
//     }
    
//     // 配置AGC
//     int agc_enabled = config->agc_enabled ? 1 : 0;
//     speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_AGC, &agc_enabled);
    
//     if (config->agc_enabled) {
//         // 设置AGC参数
//         float agc_level = config->agc_level;
//         speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_AGC_LEVEL, 
//                            &agc_level);
        
//         int agc_increment = config->agc_increment;
//         speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_AGC_INCREMENT, 
//                            &agc_increment);
        
//         int agc_decrement = config->agc_decrement;
//         speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_AGC_DECREMENT, 
//                            &agc_decrement);
        
//         int agc_max_gain = config->agc_max_gain;
//         speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, 
//                            &agc_max_gain);
//     }
    
//     // 配置VAD
//     int vad_enabled = config->vad_enabled ? 1 : 0;
//     speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_VAD, &vad_enabled);
    
//     // 配置去混响
//     int dereverb_enabled = config->dereverb_enabled ? 1 : 0;
//     speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_DEREVERB, &dereverb_enabled);
    
//     if (config->dereverb_enabled) {
//         int echo_suppress_level = config->echo_suppress_level;
//         speex_preprocess_ctl(audio_system->a3_state, SPEEX_PREPROCESS_SET_DEREVERB_LEVEL, 
//                            &echo_suppress_level);
//     }
    
//     return AUDIO_ERROR_NONE;
// }
