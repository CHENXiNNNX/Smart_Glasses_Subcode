#include "app/media/audio/audio.hpp"
#include "app/tool/log/log.hpp"
#include "app/tool/file/file.hpp"
#include "app/media/sync.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>

using namespace app::media::audio;
using namespace app::tool::log;
using namespace app::tool::file;

namespace
{
    constexpr const char* LOG_TAG = "MAIN";
}

int main()
{
    LogConfig log_config;
    log_config.min_level      = LogLevel::INFO;
    log_config.enable_console = true;
    log_config.enable_file    = false;

    Logger& logger = Logger::getInstance();
    if (!logger.initialize(log_config))
    {
        std::cerr << "日志系统初始化失败" << std::endl;
        return -1;
    }

    AudioConfig config;
    config.sample_rate       = 48000;
    config.channels          = 1;
    config.frame_duration_ms = 20;
    config.record_path       = "/root/audio/";

    AudioSystem audio_system(config);

    // 创建同步上下文
    auto sync_ctx = std::make_shared<sync_context_t>();
    if (sync_init(sync_ctx.get()) != 0)
    {
        LOG_ERROR(LOG_TAG, "同步上下文初始化失败");
        return -1;
    }

    if (audio_system.initialize(sync_ctx) != AudioError::NONE)
    {
        LOG_ERROR(LOG_TAG, "音频系统初始化失败");
        sync_deinit(sync_ctx.get());
        return -1;
    }

    if (audio_system.startStream(StreamDirection::INPUT) != AudioError::NONE)
    {
        LOG_ERROR(LOG_TAG, "启动录音流失败");
        audio_system.shutdown();
        return -1;
    }

    if (audio_system.startRecord("", 5) != AudioError::NONE)
    {
        LOG_ERROR(LOG_TAG, "启动录音失败");
        audio_system.stopStream(StreamDirection::INPUT);
        audio_system.shutdown();
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(6));

    if (audio_system.isRecording())
    {
        audio_system.stopRecord();
    }

    audio_system.stopStream(StreamDirection::INPUT);

    std::string record_file = config.record_path + "record_0.wav";
    if (!exists(record_file))
    {
        LOG_ERROR(LOG_TAG, "录音文件不存在: %s", record_file.c_str());
        audio_system.shutdown();
        return -1;
    }

    std::vector<uint8_t> wav_data{};
    if (!readAll(record_file, wav_data))
    {
        LOG_ERROR(LOG_TAG, "读取录音文件失败");
        audio_system.shutdown();
        return -1;
    }

    constexpr size_t WAV_HEADER_SIZE = 44;
    if (wav_data.size() < WAV_HEADER_SIZE)
    {
        LOG_ERROR(LOG_TAG, "WAV文件格式错误");
        audio_system.shutdown();
        return -1;
    }

    std::vector<uint8_t> pcm_data(wav_data.begin() + WAV_HEADER_SIZE, wav_data.end());

    int frame_size_samples = config.sample_rate * config.frame_duration_ms / 1000;
    size_t frame_size_bytes = frame_size_samples * config.channels * sizeof(int16_t);

    if (audio_system.startStream(StreamDirection::OUTPUT) != AudioError::NONE)
    {
        LOG_ERROR(LOG_TAG, "启动播放流失败");
        audio_system.shutdown();
        return -1;
    }

    AudioMemoryPool temp_pool(config.mem_pool_config);
    size_t total_frames = 0;
    size_t offset = 0;

    while (offset < pcm_data.size())
    {
        const size_t remaining = pcm_data.size() - offset;
        const size_t current_frame_size = (remaining < frame_size_bytes) ? remaining : frame_size_bytes;

        auto frame = temp_pool.allocate(current_frame_size);
        if (!frame)
        {
            LOG_ERROR(LOG_TAG, "分配播放帧失败");
            break;
        }

        std::memcpy(frame->data, pcm_data.data() + offset, current_frame_size);
        frame->size = current_frame_size;
        frame->timestamp = 0;

        audio_system.pushPlaybackFrame(frame);
        total_frames++;
        offset += current_frame_size;

        std::this_thread::sleep_for(std::chrono::milliseconds(config.frame_duration_ms));
    }

    double estimated_duration = (double)total_frames * config.frame_duration_ms / 1000.0;
    int wait_seconds = static_cast<int>(estimated_duration) + 1;
    std::this_thread::sleep_for(std::chrono::seconds(wait_seconds));

    audio_system.stopStream(StreamDirection::OUTPUT);
    audio_system.shutdown();
    sync_deinit(sync_ctx.get());

    return 0;
}

