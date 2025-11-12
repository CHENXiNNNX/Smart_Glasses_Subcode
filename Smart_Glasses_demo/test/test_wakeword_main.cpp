/**
 * @file test_wakeword_main.cpp
 * @brief 唤醒词检测测试程序
 * @details 测试内容：
 *          1. 唤醒词检测器初始化
 *          2. 麦克风录音并实时检测唤醒词
 *          3. 检测灵敏度调整测试
 *          4. 音频增益调整测试
 *          5. 唤醒词检测统计
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <time.h>
#include <cstdint>
#include <signal.h>
#include "../app/chatbot/wakeword/wakeword.h"
#include "../app/media/audio/audio.h"
#include "../app/tool/log/log.h"

using namespace app::chatbot::wakeword;
using namespace app::media::audio;
using namespace app::tool::log;

// 时间函数
inline uint64_t get_nowus(void) {
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000 + (uint64_t)time.tv_nsec / 1000;
}

// 全局变量
std::atomic<bool> g_running{true};
std::atomic<int> g_wakeword_count{0};
std::atomic<uint64_t> g_last_detection_time{0};

// 信号处理
void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n收到中断信号，正在停止..." << std::endl;
        g_running.store(false);
    }
}

// 打印分隔线
void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

// 打印检测结果
void printDetectionResult(WakewordResult result, int hotword_index) {
    switch (result) {
        case WakewordResult::HOTWORD_1:
        case WakewordResult::HOTWORD_2:
        case WakewordResult::HOTWORD_3:
            std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
            std::cout << "║  🎙️  检测到唤醒词！热词 " << hotword_index << "           ║" << std::endl;
            std::cout << "╚════════════════════════════════════════╝" << std::endl;
            break;
        case WakewordResult::SILENCE:
            // 静音，不打印
            break;
        case WakewordResult::ERROR:
            std::cerr << "✗ 检测错误" << std::endl;
            break;
        default:
            break;
    }
}

// 测试1：基本唤醒词检测
void testBasicWakeword() {
    printSeparator("测试1: 基本唤醒词检测");
    
    // 配置唤醒词检测器
    WakewordConfig ww_config;
    ww_config.resource_file = "./third_party/snowboy/resources/common.res";
    ww_config.model_file = "./third_party/snowboy/resources/models/echo.pmdl";
    ww_config.sensitivity = 0.5f;
    ww_config.audio_gain = 1.0f;
    ww_config.apply_frontend = false;
    
    std::cout << "\n唤醒词配置：" << std::endl;
    std::cout << "  资源文件: " << ww_config.resource_file << std::endl;
    std::cout << "  模型文件: " << ww_config.model_file << std::endl;
    std::cout << "  灵敏度: " << ww_config.sensitivity << std::endl;
    std::cout << "  音频增益: " << ww_config.audio_gain << std::endl;
    
    // 创建唤醒词检测器
    std::cout << "\n正在创建唤醒词检测器..." << std::endl;
    WakewordDetector detector(ww_config);
    
    // 初始化
    WakewordError err = detector.initialize();
    if (err != WakewordError::NONE) {
        std::cerr << "✗ 唤醒词检测器初始化失败！" << std::endl;
        std::cerr << "  请确保资源文件和模型文件路径正确" << std::endl;
        return;
    }
    
    std::cout << "✓ 唤醒词检测器初始化成功" << std::endl;
    std::cout << "  采样率: " << detector.getSampleRate() << " Hz" << std::endl;
    std::cout << "  声道数: " << detector.getNumChannels() << std::endl;
    std::cout << "  每样本位数: " << detector.getBitsPerSample() << std::endl;
    std::cout << "  唤醒词数量: " << detector.getNumHotwords() << std::endl;
    
    // 设置回调
    detector.setWakewordCallback([](WakewordResult result, int hotword_index) {
        printDetectionResult(result, hotword_index);
        g_wakeword_count.fetch_add(1);
        g_last_detection_time.store(get_nowus());
    });
    
    detector.setErrorCallback([](WakewordError /*error*/, const std::string& message) {
        std::cerr << "\n[错误] " << message << std::endl;
    });
    
    // 启用检测
    detector.setEnabled(true);
    std::cout << "✓ 唤醒词检测已启用" << std::endl;
    
    // 配置音频系统
    AudioConfig audio_config;
    audio_config.sample_rate = 48000;  // 硬件采样率
    audio_config.channels = 1;
    audio_config.frame_duration_ms = 10;  // 10ms帧时长
    
    std::cout << "\n正在初始化音频系统..." << std::endl;
    AudioSystem audio_system(audio_config);
    
    AudioError audio_err = audio_system.initialize();
    if (audio_err != AudioError::NONE) {
        std::cerr << "✗ 音频系统初始化失败" << std::endl;
        return;
    }
    
    std::cout << "✓ 音频系统初始化成功" << std::endl;
    
    // 设置唤醒词音频回调
    audio_system.setWakewordCallback([&detector](const int16_t* data, size_t length) {
        // 将音频数据送给唤醒词检测器
        detector.processAudioFrame(data, static_cast<int>(length));
    });
    
    // 开始录音
    audio_err = audio_system.startRecord();
    if (audio_err != AudioError::NONE) {
        std::cerr << "✗ 开始录音失败" << std::endl;
        return;
    }
    
    std::cout << "✓ 录音已开始" << std::endl;
    
    printSeparator("唤醒词检测运行中");
    std::cout << "\n请说出唤醒词..." << std::endl;
    std::cout << "按 Ctrl+C 停止测试\n" << std::endl;
    
    // 运行检测循环
    int seconds = 0;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        seconds++;
        
        // 每10秒显示一次状态
        if (seconds % 10 == 0) {
            std::cout << "\r已运行 " << seconds << " 秒，检测到 " 
                      << g_wakeword_count.load() << " 次唤醒词...       " << std::flush;
        }
    }
    
    std::cout << std::endl;
    
    // 停止录音
    audio_system.stopRecord();
    std::cout << "\n✓ 录音已停止" << std::endl;
    
    // 打印统计
    printSeparator("检测统计");
    std::cout << "\n总检测次数: " << g_wakeword_count.load() << std::endl;
    std::cout << "运行时间: " << seconds << " 秒" << std::endl;
    if (seconds > 0) {
        std::cout << "检测频率: " << (double)g_wakeword_count.load() / seconds << " 次/秒" << std::endl;
    }
}

// 测试2：灵敏度调整测试
void testSensitivityAdjustment() {
    printSeparator("测试2: 灵敏度调整测试");
    
    WakewordConfig ww_config;
    ww_config.resource_file = "./third_party/snowboy/resources/common.res";
    ww_config.model_file = "./third_party/snowboy/resources/models/echo.pmdl";
    ww_config.sensitivity = 0.3f;  // 初始灵敏度较低
    
    WakewordDetector detector(ww_config);
    
    WakewordError err = detector.initialize();
    if (err != WakewordError::NONE) {
        std::cerr << "✗ 初始化失败" << std::endl;
        return;
    }
    
    std::cout << "✓ 检测器初始化成功" << std::endl;
    
    // 设置回调
    detector.setWakewordCallback([](WakewordResult result, int hotword_index) {
        printDetectionResult(result, hotword_index);
        g_wakeword_count.fetch_add(1);
    });
    
    detector.setEnabled(true);
    
    // 配置音频
    AudioConfig audio_config;
    audio_config.sample_rate = 48000;
    audio_config.channels = 1;
    audio_config.frame_duration_ms = 10;
    
    AudioSystem audio_system(audio_config);
    audio_system.initialize();
    
    audio_system.setWakewordCallback([&detector](const int16_t* data, size_t length) {
        detector.processAudioFrame(data, static_cast<int>(length));
    });
    
    audio_system.startRecord();
    
    // 测试不同灵敏度
    std::vector<float> sensitivities = {0.3f, 0.5f, 0.7f};
    
    for (float sens : sensitivities) {
        if (!g_running.load()) break;
        
        std::cout << "\n设置灵敏度为: " << sens << std::endl;
        detector.setSensitivity(sens);
        
        g_wakeword_count.store(0);
        std::cout << "测试 10 秒，请说唤醒词..." << std::endl;
        
        for (int i = 0; i < 10 && g_running.load(); i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "." << std::flush;
        }
        
        std::cout << "\n灵敏度 " << sens << " 检测到 " << g_wakeword_count.load() << " 次" << std::endl;
    }
    
    audio_system.stopRecord();
    
    printSeparator("测试完成");
    std::cout << "\n建议灵敏度: 0.5 (平衡误报和漏报)" << std::endl;
}

// 测试3：音频增益测试
void testAudioGain() {
    printSeparator("测试3: 音频增益测试");
    
    WakewordConfig ww_config;
    ww_config.resource_file = "./third_party/snowboy/resources/common.res";
    ww_config.model_file = "./third_party/snowboy/resources/models/echo.pmdl";
    ww_config.sensitivity = 0.5f;
    ww_config.audio_gain = 1.0f;
    
    WakewordDetector detector(ww_config);
    
    if (detector.initialize() != WakewordError::NONE) {
        std::cerr << "✗ 初始化失败" << std::endl;
        return;
    }
    
    detector.setWakewordCallback([](WakewordResult result, int hotword_index) {
        printDetectionResult(result, hotword_index);
        g_wakeword_count.fetch_add(1);
    });
    
    detector.setEnabled(true);
    
    AudioConfig audio_config;
    audio_config.sample_rate = 48000;
    audio_config.channels = 1;
    
    AudioSystem audio_system(audio_config);
    audio_system.initialize();
    
    audio_system.setWakewordCallback([&detector](const int16_t* data, size_t length) {
        detector.processAudioFrame(data, static_cast<int>(length));
    });
    
    audio_system.startRecord();
    
    // 测试不同增益
    std::vector<float> gains = {0.5f, 1.0f, 2.0f};
    
    for (float gain : gains) {
        if (!g_running.load()) break;
        
        std::cout << "\n设置音频增益为: " << gain << std::endl;
        detector.setAudioGain(gain);
        
        g_wakeword_count.store(0);
        std::cout << "测试 10 秒，请说唤醒词..." << std::endl;
        
        for (int i = 0; i < 10 && g_running.load(); i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "." << std::flush;
        }
        
        std::cout << "\n增益 " << gain << " 检测到 " << g_wakeword_count.load() << " 次" << std::endl;
    }
    
    audio_system.stopRecord();
    
    printSeparator("测试完成");
    std::cout << "\n建议增益: 1.0 (正常水平)" << std::endl;
}

// 测试4：长时间稳定性测试
void testLongRunning() {
    printSeparator("测试4: 长时间稳定性测试");
    
    WakewordConfig ww_config;
    ww_config.resource_file = "./third_party/snowboy/resources/common.res";
    ww_config.model_file = "./third_party/snowboy/resources/models/echo.pmdl";
    ww_config.sensitivity = 0.5f;
    
    WakewordDetector detector(ww_config);
    
    if (detector.initialize() != WakewordError::NONE) {
        std::cerr << "✗ 初始化失败" << std::endl;
        return;
    }
    
    std::atomic<int> error_count{0};
    
    detector.setWakewordCallback([](WakewordResult result, int hotword_index) {
        printDetectionResult(result, hotword_index);
        g_wakeword_count.fetch_add(1);
    });
    
    detector.setErrorCallback([&error_count](WakewordError /*error*/, const std::string& message) {
        std::cerr << "\n[错误] " << message << std::endl;
        error_count.fetch_add(1);
    });
    
    detector.setEnabled(true);
    
    AudioConfig audio_config;
    audio_config.sample_rate = 48000;
    audio_config.channels = 1;
    
    AudioSystem audio_system(audio_config);
    audio_system.initialize();
    
    audio_system.setWakewordCallback([&detector](const int16_t* data, size_t length) {
        detector.processAudioFrame(data, static_cast<int>(length));
    });
    
    audio_system.startRecord();
    
    std::cout << "\n开始长时间稳定性测试（60秒）..." << std::endl;
    std::cout << "按 Ctrl+C 提前停止\n" << std::endl;
    
    uint64_t start_time = get_nowus();
    
    for (int i = 0; i < 60 && g_running.load(); i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        int current_count = g_wakeword_count.load();
        
        // 每10秒更新一次状态
        if ((i + 1) % 10 == 0) {
            std::cout << "\r已运行 " << (i + 1) << " 秒，检测到 " 
                      << current_count << " 次唤醒词，错误 " 
                      << error_count.load() << " 次      " << std::flush;
        }
    }
    
    std::cout << std::endl;
    
    audio_system.stopRecord();
    
    uint64_t total_time_ms = (get_nowus() - start_time) / 1000;
    
    printSeparator("稳定性测试结果");
    std::cout << "\n运行时间: " << total_time_ms / 1000.0 << " 秒" << std::endl;
    std::cout << "检测次数: " << g_wakeword_count.load() << std::endl;
    std::cout << "错误次数: " << error_count.load() << std::endl;
    std::cout << "检测率: " << (double)g_wakeword_count.load() / (total_time_ms / 1000.0) << " 次/秒" << std::endl;
    
    if (error_count.load() == 0) {
        std::cout << "\n✓ 稳定性测试通过，无错误发生" << std::endl;
    } else {
        std::cout << "\n⚠ 检测到 " << error_count.load() << " 个错误" << std::endl;
    }
}

// 主函数
int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   唤醒词检测测试程序                    ║" << std::endl;
    std::cout << "║   Wakeword Detection Test Suite       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    
    // 初始化日志系统
    LogConfig log_config;
    log_config.enable_console = true;
    log_config.enable_color = true;
    log_config.min_level = LogLevel::INFO;
    Logger::getInstance().initialize(log_config);
    
    try {
        int test_num = 1;
        
        if (argc > 1) {
            test_num = std::stoi(argv[1]);
        }
        
        // 重置计数器
        g_wakeword_count.store(0);
        g_running.store(true);
        
        switch (test_num) {
            case 1:
                testBasicWakeword();
                break;
            case 2:
                testSensitivityAdjustment();
                break;
            case 3:
                testAudioGain();
                break;
            case 4:
                testLongRunning();
                break;
            default:
                std::cerr << "无效的测试编号: " << test_num << std::endl;
                std::cerr << "用法: " << argv[0] << " [1|2|3|4]" << std::endl;
                std::cerr << "  1: 基本唤醒词检测（默认）" << std::endl;
                std::cerr << "  2: 灵敏度调整测试" << std::endl;
                std::cerr << "  3: 音频增益测试" << std::endl;
                std::cerr << "  4: 长时间稳定性测试" << std::endl;
                return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\n========================================\n" << std::endl;
    
    return 0;
}

