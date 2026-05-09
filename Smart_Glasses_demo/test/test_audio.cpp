/* test_audio.cpp - 音频回声自测（AudioDrv + MicProcessed 订阅 + Opus 闭环）
 *
 * 流程：MicProcessed → Opus 编码 → pushPlaybackOpus（内部解码）→ 扬声器。
 *       Ctrl+C 结束。
 */

#include "app/media/audio/audio.hpp"
#include "app/media/audio/codec/opus_codec.hpp"
#include "app/media/media_config.hpp"
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
    constexpr const char* TAG = "ECHO";

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
               static_cast<unsigned>(s.mem_used / 1024));
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
    cfg.capture.rate           = AUDIO_SAMPLE_RATE;
    cfg.capture.channels       = AUDIO_CHANNELS;
    cfg.capture.frame_ms       = AUDIO_FRAME_DURATION_MS;
    cfg.playback.rate          = AUDIO_SAMPLE_RATE;
    cfg.playback.channels      = AUDIO_CHANNELS;
    cfg.playback.frame_ms      = AUDIO_FRAME_DURATION_MS;
    cfg.playback.volume        = 85;
    cfg.opus.rate              = AUDIO_SAMPLE_RATE;
    cfg.opus.channels          = AUDIO_CHANNELS;
    cfg.opus.bitrate           = AUDIO_BIT_RATE;
    cfg.opus.vbr               = true;
    cfg.proc.denoise           = AUDIO_DENOISE_ENABLED;
    cfg.proc.agc               = AUDIO_AGC_ENABLED;
    cfg.proc.vad               = AUDIO_VAD_ENABLED;
    cfg.proc.dereverb          = AUDIO_DEREVERB_ENABLED;
    cfg.proc.agc_level         = AUDIO_AGC_LEVEL;
    cfg.proc.agc_increment     = AUDIO_AGC_INCREMENT;
    cfg.proc.agc_decrement     = AUDIO_AGC_DECREMENT;
    cfg.proc.agc_max_gain_db   = AUDIO_AGC_MAX_GAIN;
    cfg.proc.noise_suppress_db = AUDIO_NOISE_SUPPRESS_LEVEL;
    cfg.proc.echo_suppress_db  = AUDIO_ECHO_SUPPRESS_LEVEL;
    cfg.enable_capture         = true;
    cfg.enable_playback        = true;
    cfg.enable_opus            = true;
    cfg.enable_proc            = true;

    cfg.capture.rk.enable_vqe = (AUDIO_RK_VQE_ENABLED != 0);
    if (AUDIO_RK_VQE_ENABLED)
    {
        cfg.capture.rk.vqe_config_file   = AUDIO_RK_VQE_CONFIG_PATH;
        cfg.capture.rk.ao_dev_id_for_vqe = AUDIO_RK_VQE_AO_DEV_ID;
        cfg.capture.rk.ao_chn_for_vqe    = AUDIO_RK_VQE_AO_CHN_ID;
        cfg.capture.rk.vqe_gap_ms        = AUDIO_RK_VQE_GAP_MS;
    }

    AudioDrv drv;
    if (drv.init(cfg) != Error::OK)
    {
        LOG_ERROR(TAG, "音频初始化失败");
        return 1;
    }
    LOG_INFO(TAG, "回声 %uHz %uch %ums Opus %ubps 音量%u%%",
             static_cast<unsigned>(AUDIO_SAMPLE_RATE), static_cast<unsigned>(AUDIO_CHANNELS),
             static_cast<unsigned>(AUDIO_FRAME_DURATION_MS), static_cast<unsigned>(AUDIO_BIT_RATE),
             static_cast<unsigned>(cfg.playback.volume));

    std::atomic<uint32_t> drops{0};

    SubHandle sub = drv.subscribe(StreamId::MicProcessed,
                                  [&drv, &drops](const FramePtr& pcm)
                                  {
                                      if (!pcm || !pcm->data || pcm->samples == 0)
                                          return;

                                      auto opus = drv.opus().encode(pcm);
                                      if (!opus)
                                          return; /* Opus 侧 PCM 拼接未满 20ms，非错误 */
                                      if (opus->size == 0)
                                      {
                                          drops.fetch_add(1, std::memory_order_relaxed);
                                          return;
                                      }
                                      /* 与业务一致：上行/对端 Opus 进播放，由 AudioDrv 内解码再送
                                       * AO/ALSA */
                                      if (drv.pushPlaybackOpus(opus->data, opus->size) != Error::OK)
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
        std::this_thread::sleep_for(std::chrono::seconds(1));
        printStats(drv, ++elapsed, drops.load(std::memory_order_relaxed));
    }

    std::cout << std::endl;
    drv.unsubscribe(sub);
    drv.stop();

    auto           s        = drv.stats();
    const uint32_t drop_cnt = drops.load(std::memory_order_relaxed);
    LOG_INFO(TAG, "统计 %ds 采集%u 编码%u 解码%u 播放%u 丢%u 内存%uKB", elapsed, s.capture_frames,
             s.encode_cnt, s.decode_cnt, s.playback_frames, drop_cnt,
             static_cast<unsigned>(s.mem_used / 1024));
    if (s.capture_frames > 0)
        LOG_INFO(TAG, "丢帧率%.1f%%", drop_cnt * 100.0 / s.capture_frames);

    drv.deinit();
    return 0;
}
