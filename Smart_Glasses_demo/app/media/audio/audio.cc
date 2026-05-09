/* audio.cc - 音频门面 AudioDrv 实现 */

#include "audio.hpp"

#include "core/audio_bus.hpp"
#include "core/frame_pool.hpp"
#include "backend/backend_factory.hpp"
#include "backend/capture_backend.hpp"
#include "backend/playback_backend.hpp"

#include "../../tool/log/log.hpp"
#include "../../tool/time/time.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>

/* 延迟 include — codec / proc / util 模块 */
#include "codec/opus_codec.hpp"
#include "codec/resampler.hpp"
#include "proc/audio_proc.hpp"
#include "util/wav_recorder.hpp"

namespace app::media::audio
{

    using namespace tool::log;
    using namespace tool::time;

#define TAG "AudioDrv"

    static constexpr uint32_t UPLINK_OPUS_RATE    = 16000;
    static constexpr uint32_t UPLINK_OPUS_BITRATE = 32000;
    static constexpr uint32_t CAPTURE_RATE        = 48000;

    /*====================================================================
     * Impl
     *====================================================================*/

    class AudioDrv::Impl
    {
    public:
        AudioCfg cfg_;
        bool     init_    = false;
        bool     running_ = false;

        FramePool   pool_;
        AudioBus    bus_;
        OpusCodec   opus_;
        Resampler   resampler_;
        AudioProc   proc_;
        WavRecorder wav_;

        std::unique_ptr<ICaptureBackend>  capture_;
        std::unique_ptr<IPlaybackBackend> playback_;

        /* 上行 16kHz Opus（独立 OpusCodec） */
        OpusCodec uplink_opus_;

        std::atomic<uint32_t>                 capture_frames_{0};
        std::atomic<uint32_t>                 playback_frames_{0};
        std::chrono::steady_clock::time_point stats_time_;

        ErrorCb    error_cb_;
        std::mutex error_mtx_;

        /* 播放内部订阅句柄 — AudioDrv 自身订阅 SpeakerPcm 驱动后端 */
        SubHandle spk_sub_ = 0;

        /*--------------------------------------------------------------
         * init
         *--------------------------------------------------------------*/
        Error init(const AudioCfg& cfg)
        {
            if (init_)
                return Error::ALREADY_INIT;

            cfg_ = cfg;

            /* 1. 帧内存池 */
            if (pool_.init(cfg.memory) != Error::OK)
                return Error::MEMORY_ERROR;

            /* 2. 采集后端 */
            if (cfg.enable_capture)
            {
                capture_ = BackendFactory::createCapture(cfg.capture.backend, cfg.capture, &pool_);
                if (!capture_)
                {
                    pool_.deinit();
                    return Error::DEVICE_ERROR;
                }
            }

            /* 3. Speex：采集未开 VQE 时 */
            if (cfg.enable_proc && capture_ && !capture_->has_builtin_vqe())
            {
                if (proc_.init(cfg.proc, cfg.capture.rate, cfg.capture.frame_ms) != Error::OK)
                {
                    capture_.reset();
                    pool_.deinit();
                    return Error::CODEC_ERROR;
                }
            }

            /* 4. 播放后端 */
            if (cfg.enable_playback)
            {
                playback_ =
                    BackendFactory::createPlayback(cfg.playback.backend, cfg.playback, &pool_);
                if (!playback_)
                {
                    proc_.deinit();
                    capture_.reset();
                    pool_.deinit();
                    return Error::DEVICE_ERROR;
                }
            }

            /* 5. Opus 编解码 */
            if (cfg.enable_opus)
            {
                if (opus_.init(cfg.opus, &pool_) != Error::OK)
                {
                    playback_.reset();
                    proc_.deinit();
                    capture_.reset();
                    pool_.deinit();
                    return Error::CODEC_ERROR;
                }
            }

            /* 6. 重采样器 */
            if (resampler_.init(cfg.capture.channels, &pool_) != Error::OK)
            {
                opus_.deinit();
                playback_.reset();
                proc_.deinit();
                capture_.reset();
                pool_.deinit();
                return Error::CODEC_ERROR;
            }

            /* 7. 上行 16kHz Opus */
            {
                OpusCfg up_cfg;
                up_cfg.rate     = UPLINK_OPUS_RATE;
                up_cfg.channels = 1;
                up_cfg.bitrate  = UPLINK_OPUS_BITRATE;
                up_cfg.vbr      = true;
                if (uplink_opus_.init(up_cfg, &pool_) == Error::OK)
                    LOG_INFO(TAG, "上行 Opus %uHz 1ch %ukbps", UPLINK_OPUS_RATE,
                             UPLINK_OPUS_BITRATE / 1000);
                else
                    LOG_WARN(TAG, "上行 Opus 初始化失败，上发不可用");
            }

            stats_time_ = std::chrono::steady_clock::now();
            init_       = true;

            LOG_INFO(TAG, "初始化完成 capture=%s playback=%s proc=%s opus=%d",
                     capture_ ? capture_->name() : "none", playback_ ? playback_->name() : "none",
                     proc_.is_init() ? "Speex"
                                     : (capture_ && capture_->has_builtin_vqe() ? "VQE" : "none"),
                     cfg.enable_opus);
            return Error::OK;
        }

        /*--------------------------------------------------------------
         * deinit
         *--------------------------------------------------------------*/
        void deinit()
        {
            if (!init_)
                return;
            stop();

            if (spk_sub_)
            {
                bus_.unsubscribe(spk_sub_);
                spk_sub_ = 0;
            }

            uplink_opus_.deinit();
            resampler_.deinit();
            opus_.deinit();
            playback_.reset();
            capture_.reset();
            proc_.deinit();
            pool_.deinit();

            init_ = false;
            LOG_INFO(TAG, "已释放");
        }

        /*--------------------------------------------------------------
         * start / stop
         *--------------------------------------------------------------*/
        Error start()
        {
            if (running_)
                return Error::OK;

            /* 注册播放内部订阅：SpeakerPcm → 后端 push */
            if (playback_)
            {
                spk_sub_ = bus_.subscribe(StreamId::SpeakerPcm,
                                          [this](const FramePtr& f)
                                          {
                                              playback_->push(f);
                                              playback_frames_++;
                                          });

                if (playback_->start() != Error::OK)
                    return Error::DEVICE_ERROR;
            }

            /* 启动采集 */
            if (capture_)
            {
                auto err = capture_->start(
                    [this](FramePtr frame)
                    {
                        capture_frames_++;

                        /* MicRaw */
                        bus_.publish(StreamId::MicRaw, frame);

                        if (proc_.is_init())
                            proc_.process(frame->get<int16_t>(), frame->samples);

                        bus_.publish(StreamId::MicProcessed, frame);
                    });

                if (err != Error::OK)
                {
                    if (playback_)
                    {
                        playback_->stop();
                        bus_.unsubscribe(spk_sub_);
                        spk_sub_ = 0;
                    }
                    return Error::DEVICE_ERROR;
                }
            }

            running_ = true;
            LOG_INFO(TAG, "启动");
            return Error::OK;
        }

        void stop()
        {
            if (!running_)
                return;
            wav_.stop();
            if (capture_)
                capture_->stop();
            if (playback_)
                playback_->stop();
            if (spk_sub_)
            {
                bus_.unsubscribe(spk_sub_);
                spk_sub_ = 0;
            }
            running_ = false;
            LOG_INFO(TAG, "停止");
        }

        /*--------------------------------------------------------------
         * 播放
         *--------------------------------------------------------------*/
        Error pushPlaybackPcm(const FramePtr& pcm)
        {
            if (!pcm)
                return Error::INVALID_PARAM;
            bus_.publish(StreamId::SpeakerPcm, pcm);
            return Error::OK;
        }

        Error pushPlaybackPcm(const int16_t* data, size_t samples)
        {
            if (!data || samples == 0)
                return Error::INVALID_PARAM;
            if (!playback_)
                return Error::NOT_INIT;

            size_t   bytes = samples * cfg_.playback.channels * sizeof(int16_t);
            FramePtr f     = pool_.alloc(bytes);
            if (!f)
                return Error::MEMORY_ERROR;
            std::memcpy(f->data, data, bytes);
            f->samples  = static_cast<uint32_t>(samples);
            f->size     = bytes;
            f->rate     = cfg_.playback.rate;
            f->channels = cfg_.playback.channels;
            return pushPlaybackPcm(f);
        }

        Error pushPlaybackOpus(const uint8_t* data, size_t len)
        {
            if (!opus_.is_init())
                return Error::NOT_INIT;
            FramePtr pcm = opus_.decode(data, len);
            if (!pcm)
                return Error::CODEC_ERROR;
            return pushPlaybackPcm(pcm);
        }

        /*--------------------------------------------------------------
         * 上行编码
         *--------------------------------------------------------------*/
        FramePtr encodeCaptureUplink(const FramePtr& pcm_48k)
        {
            if (!pcm_48k || !pcm_48k->data || pcm_48k->samples == 0)
                return nullptr;
            if (!uplink_opus_.is_init() || !resampler_.is_init())
                return nullptr;

            FramePtr pcm_16k = resampler_.resample(pcm_48k->get<int16_t>(), pcm_48k->samples,
                                                   CAPTURE_RATE, UPLINK_OPUS_RATE);
            if (!pcm_16k || pcm_16k->samples == 0)
                return nullptr;

            return uplink_opus_.encode(pcm_16k->get<int16_t>(), pcm_16k->samples);
        }

        /*--------------------------------------------------------------
         * 统计
         *--------------------------------------------------------------*/
        Stats stats() const
        {
            Stats s{};
            auto  now = std::chrono::steady_clock::now();
            auto  elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - stats_time_).count();

            if (elapsed > 0)
            {
                s.capture_fps  = capture_frames_.load() * 1000.0f / static_cast<float>(elapsed);
                s.playback_fps = playback_frames_.load() * 1000.0f / static_cast<float>(elapsed);
            }

            s.capture_frames   = capture_frames_.load();
            s.capture_drops    = capture_ ? capture_->drops() : 0;
            s.playback_frames  = playback_frames_.load();
            s.playback_drops   = playback_ ? playback_->drops() : 0;
            s.encode_cnt       = opus_.encode_cnt();
            s.decode_cnt       = opus_.decode_cnt();
            s.wav_frames       = wav_.frames();
            s.wav_sec          = wav_.duration_sec();
            s.mem_used         = pool_.used();
            s.mem_total        = pool_.total();
            s.capture_backend  = capture_ ? capture_->name() : "none";
            s.playback_backend = playback_ ? playback_->name() : "none";
            return s;
        }

        void reset_stats()
        {
            capture_frames_  = 0;
            playback_frames_ = 0;
            stats_time_      = std::chrono::steady_clock::now();
        }
    };

    /*====================================================================
     * AudioDrv 薄包装
     *====================================================================*/

    AudioDrv::AudioDrv() : impl_(std::make_unique<Impl>()) {}
    AudioDrv::~AudioDrv()
    {
        deinit();
    }

    Error AudioDrv::init(const AudioCfg& cfg)
    {
        return impl_->init(cfg);
    }
    void AudioDrv::deinit()
    {
        impl_->deinit();
    }
    bool AudioDrv::is_init() const
    {
        return impl_->init_;
    }

    Error AudioDrv::start()
    {
        return impl_->start();
    }
    Error AudioDrv::stop()
    {
        impl_->stop();
        return Error::OK;
    }
    bool AudioDrv::is_running() const
    {
        return impl_->running_;
    }

    SubHandle AudioDrv::subscribe(StreamId id, FrameCb cb)
    {
        return impl_->bus_.subscribe(id, std::move(cb));
    }
    void AudioDrv::unsubscribe(SubHandle handle)
    {
        impl_->bus_.unsubscribe(handle);
    }

    Error AudioDrv::pushPlaybackPcm(const FramePtr& pcm)
    {
        return impl_->pushPlaybackPcm(pcm);
    }
    Error AudioDrv::pushPlaybackPcm(const int16_t* data, size_t samples)
    {
        return impl_->pushPlaybackPcm(data, samples);
    }
    Error AudioDrv::pushPlaybackOpus(const uint8_t* data, size_t len)
    {
        return impl_->pushPlaybackOpus(data, len);
    }

    OpusCodec& AudioDrv::opus()
    {
        return impl_->opus_;
    }
    Resampler& AudioDrv::resampler()
    {
        return impl_->resampler_;
    }
    WavRecorder& AudioDrv::wav()
    {
        return impl_->wav_;
    }
    FramePool& AudioDrv::pool()
    {
        return impl_->pool_;
    }

    FramePtr AudioDrv::encodeCaptureUplink(const FramePtr& f)
    {
        return impl_->encodeCaptureUplink(f);
    }

    void AudioDrv::setVolume(uint8_t vol)
    {
        if (impl_->playback_)
            impl_->playback_->set_volume(vol);
    }
    uint8_t AudioDrv::volume() const
    {
        return impl_->playback_ ? impl_->playback_->volume() : 0;
    }

    Stats AudioDrv::stats() const
    {
        return impl_->stats();
    }
    void AudioDrv::reset_stats()
    {
        impl_->reset_stats();
    }

    const AudioCfg& AudioDrv::cfg() const
    {
        return impl_->cfg_;
    }

} // namespace app::media::audio
