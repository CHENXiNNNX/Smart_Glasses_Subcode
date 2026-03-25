/* test_audio_main.cpp - 音频回声测试 */

#include "app/media/audio/audio.hpp"
#include "app/tool/log/log.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace app::media::audio;
using namespace app::tool::log;

namespace
{
    constexpr const char*     TAG          = "ECHO";
    static constexpr uint32_t RATE         = 48000;
    static constexpr uint8_t  CH           = 1;
    static constexpr uint8_t  FRAME_MS     = 20;
    static constexpr uint8_t  VOLUME       = 85;
    static constexpr uint32_t OPUS_BITRATE = 32000;
    static constexpr float    AGC_LEVEL    = 8000.0f;
    static constexpr int      NOISE_DB     = -45;
    static constexpr int      ECHO_DB      = -90;

    std::atomic<bool> g_running{true};

    static void onSignal(int)
    {
        g_running.store(false, std::memory_order_relaxed);
    }

    static void printStats(const AudioDrv& drv, int sec, uint32_t drops)
    {
        auto s = drv.stats();
        printf("\r[%3ds] 采集:%4u 编码:%4u 解码:%4u 播放:%4u 丢:%3u 内存:%uKB  ", sec,
               s.capture_frames, s.encode_cnt, s.decode_cnt, s.playback_frames, drops,
               (unsigned)(s.mem_used / 1024));
        fflush(stdout);
    }
} // namespace

int main()
{
    LogConfig log_cfg;
    log_cfg.min_level    = LogLevel::INFO;
    log_cfg.enable_color = true;
    Logger::inst().init(log_cfg);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    AudioCfg cfg;
    cfg.capture.rate           = RATE;
    cfg.capture.channels       = CH;
    cfg.capture.frame_ms       = FRAME_MS;
    cfg.playback.rate          = RATE;
    cfg.playback.channels      = CH;
    cfg.playback.frame_ms      = FRAME_MS;
    cfg.playback.volume        = VOLUME;
    cfg.opus.rate              = RATE;
    cfg.opus.channels          = CH;
    cfg.opus.bitrate           = OPUS_BITRATE;
    cfg.opus.vbr               = true;
    cfg.proc.denoise           = true;
    cfg.proc.agc               = true;
    cfg.proc.vad               = true;
    cfg.proc.dereverb          = true;
    cfg.proc.agc_level         = AGC_LEVEL;
    cfg.proc.agc_increment     = 12;
    cfg.proc.agc_decrement     = -40;
    cfg.proc.agc_max_gain_db   = 10;
    cfg.proc.noise_suppress_db = NOISE_DB;
    cfg.proc.echo_suppress_db  = ECHO_DB;
    cfg.enable_capture         = true;
    cfg.enable_playback        = true;
    cfg.enable_opus            = true;
    cfg.enable_proc            = true; // 开启 Speex 3A

    // 初始化
    AudioDrv drv;
    if (drv.init(cfg) != Error::OK)
    {
        LOG_ERROR(TAG, "音频初始化失败");
        return 1;
    }
    LOG_INFO(TAG, "回声 %uHz %uch %ums Opus %ubps 3A 音量%d%%", RATE, static_cast<unsigned>(CH),
             static_cast<unsigned>(FRAME_MS), OPUS_BITRATE, static_cast<unsigned>(VOLUME));

    std::atomic<uint32_t> drops{0};

    drv.set_capture_cb(
        [&](const FramePtr& pcm)
        {
            if (!pcm || !pcm->data || pcm->samples == 0)
                return;

            auto opus = drv.opus().encode(pcm);
            if (!opus)
            {
                drops.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            auto decoded = drv.opus().decode(opus->data, opus->size);
            if (!decoded)
            {
                drops.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (drv.playback().push(decoded) != Error::OK)
                drops.fetch_add(1, std::memory_order_relaxed);
        });

    if (drv.start() != Error::OK)
    {
        LOG_ERROR(TAG, "音频启动失败");
        drv.deinit();
        return 1;
    }
    LOG_INFO(TAG, "运行中 Ctrl+C 停止");

    int elapsed = 0;
    while (g_running.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        printStats(drv, ++elapsed, drops.load(std::memory_order_relaxed));
    }

    std::cout << std::endl;
    drv.stop();

    auto s = drv.stats();
    LOG_INFO(TAG, "统计 %ds 采集%u 编码%u 解码%u 播放%u 丢%u 内存%uKB", elapsed, s.capture_frames,
             s.encode_cnt, s.decode_cnt, s.playback_frames, drops.load(),
             static_cast<unsigned>(s.mem_used / 1024));
    if (s.capture_frames > 0)
    {
        LOG_INFO(TAG, "丢帧率%.1f%%", drops.load() * 100.0 / s.capture_frames);
    }

    drv.deinit();
    return 0;
}
