// #include "wakeword.h"
// #include <iostream>
// #include <cstring>

// // Snowboy C接口
// extern "C" {
//     #include "../../../3rdparty/snowboy/include/snowboy-detect-c-wrapper.h"
// }

// namespace glasses {
// namespace chatbot {
// namespace wakeword {

// WakewordDetector::WakewordDetector()
//     : detector_(nullptr)
//     , enabled_(false)
//     , initialized_(false) {
// }

// WakewordDetector::~WakewordDetector() {
//     if (detector_) {
//         SnowboyDetectDestructor(detector_);
//         detector_ = nullptr;
//     }
// }

// bool WakewordDetector::initialize(const std::string& resource_file,
//                                   const std::string& model_file,
//                                   float sensitivity,
//                                   float audio_gain) {
//     if (initialized_) {
//         std::cerr << "[Wakeword] Already initialized" << std::endl;
//         return false;
//     }
    
//     std::cout << "[Wakeword] Initializing..." << std::endl;
//     std::cout << "[Wakeword]   Resource: " << resource_file << std::endl;
//     std::cout << "[Wakeword]   Model: " << model_file << std::endl;
//     std::cout << "[Wakeword]   Sensitivity: " << sensitivity << std::endl;
//     std::cout << "[Wakeword]   Audio Gain: " << audio_gain << std::endl;
    
//     // 创建Snowboy检测器
//     detector_ = SnowboyDetectConstructor(resource_file.c_str(), model_file.c_str());
    
//     if (!detector_) {
//         std::cerr << "[Wakeword] ✗ Failed to create detector" << std::endl;
//         std::cerr << "[Wakeword]   Please check if resource and model files exist" << std::endl;
//         return false;
//     }
    
//     // 设置灵敏度
//     char sensitivity_str[32];
//     snprintf(sensitivity_str, sizeof(sensitivity_str), "%.2f", sensitivity);
//     SnowboyDetectSetSensitivity(detector_, sensitivity_str);
    
//     // 设置音频增益
//     SnowboyDetectSetAudioGain(detector_, audio_gain);
    
//     // 不使用前端处理（我们已经有3A算法了）
//     SnowboyDetectApplyFrontend(detector_, false);
    
//     // 打印检测器信息
//     int sample_rate = SnowboyDetectSampleRate(detector_);
//     int num_channels = SnowboyDetectNumChannels(detector_);
//     int bits_per_sample = SnowboyDetectBitsPerSample(detector_);
//     int num_hotwords = SnowboyDetectNumHotwords(detector_);
    
//     std::cout << "[Wakeword] ✓ Detector created" << std::endl;
//     std::cout << "[Wakeword]   Sample Rate: " << sample_rate << " Hz" << std::endl;
//     std::cout << "[Wakeword]   Channels: " << num_channels << std::endl;
//     std::cout << "[Wakeword]   Bits/Sample: " << bits_per_sample << std::endl;
//     std::cout << "[Wakeword]   Num Hotwords: " << num_hotwords << std::endl;
    
//     initialized_ = true;
//     enabled_ = true;
    
//     return true;
// }

// int WakewordDetector::processAudioFrame(const int16_t* data, int length) {
//     if (!initialized_ || !enabled_ || !detector_) {
//         return 0;
//     }
    
//     // 运行检测
//     // 返回值：0-无检测，-1-静音，-2-错误，>0-检测到第N个关键词
//     int result = SnowboyDetectRunDetection(detector_, data, length, false);
    
//     if (result > 0) {
//         std::cout << "[Wakeword] ✓ Hotword " << result << " detected!" << std::endl;
        
//         // 触发回调
//         if (callback_) {
//             callback_(result);
//         }
//     }
    
//     return result;
// }

// void WakewordDetector::setCallback(std::function<void(int)> callback) {
//     callback_ = callback;
// }

// void WakewordDetector::setSensitivity(float sensitivity) {
//     if (!detector_) {
//         return;
//     }
    
//     char sensitivity_str[32];
//     snprintf(sensitivity_str, sizeof(sensitivity_str), "%.2f", sensitivity);
//     SnowboyDetectSetSensitivity(detector_, sensitivity_str);
    
//     std::cout << "[Wakeword] Sensitivity set to " << sensitivity << std::endl;
// }

// void WakewordDetector::setAudioGain(float gain) {
//     if (!detector_) {
//         return;
//     }
    
//     SnowboyDetectSetAudioGain(detector_, gain);
//     std::cout << "[Wakeword] Audio gain set to " << gain << std::endl;
// }

// void WakewordDetector::setEnabled(bool enabled) {
//     enabled_ = enabled;
//     std::cout << "[Wakeword] " << (enabled ? "Enabled" : "Disabled") << std::endl;
// }

// bool WakewordDetector::isEnabled() const {
//     return enabled_;
// }

// bool WakewordDetector::isInitialized() const {
//     return initialized_;
// }

// int WakewordDetector::getSampleRate() const {
//     if (!detector_) {
//         return 0;
//     }
//     return SnowboyDetectSampleRate(detector_);
// }

// int WakewordDetector::getNumChannels() const {
//     if (!detector_) {
//         return 0;
//     }
//     return SnowboyDetectNumChannels(detector_);
// }

// int WakewordDetector::getBitsPerSample() const {
//     if (!detector_) {
//         return 0;
//     }
//     return SnowboyDetectBitsPerSample(detector_);
// }

// void WakewordDetector::reset() {
//     if (!detector_) {
//         return;
//     }
    
//     SnowboyDetectReset(detector_);
//     std::cout << "[Wakeword] Reset" << std::endl;
// }

// } // namespace wakeword
// } // namespace chatbot
// } // namespace glasses


