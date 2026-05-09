/* audio.hpp - 音频模块唯一对外门面（与 camera/camera.hpp 中 CameraDrv 命名风格一致） */

#pragma once

#include "core/types.hpp"
#include <memory>

namespace app::media::audio
{

    class FramePool;
    class AudioBus;
    class OpusCodec;
    class Resampler;
    class AudioProc;
    class WavRecorder;

    class AudioDrv
    {
    public:
        AudioDrv();
        ~AudioDrv();

        AudioDrv(const AudioDrv&)            = delete;
        AudioDrv& operator=(const AudioDrv&) = delete;

        /*----------------------------------------------------------------
         * 生命周期
         *----------------------------------------------------------------*/
        Error init(const AudioCfg& cfg);
        void  deinit();
        bool  is_init() const;

        Error start();
        Error stop();
        bool  is_running() const;

        /*----------------------------------------------------------------
         * 订阅 / 取消订阅 — 核心注册接口
         *----------------------------------------------------------------*/
        SubHandle subscribe(StreamId id, FrameCb cb);
        void      unsubscribe(SubHandle handle);

        /*----------------------------------------------------------------
         * 播放入口（生产者侧）
         *----------------------------------------------------------------*/
        Error pushPlaybackPcm(const FramePtr& pcm);
        Error pushPlaybackPcm(const int16_t* data, size_t samples);
        Error pushPlaybackOpus(const uint8_t* data, size_t len);

        /*----------------------------------------------------------------
         * 工具模块访问
         *----------------------------------------------------------------*/
        OpusCodec&   opus();
        Resampler&   resampler();
        WavRecorder& wav();
        FramePool&   pool();

        /*----------------------------------------------------------------
         * 上行编码（48k PCM → 16k Opus）
         *----------------------------------------------------------------*/
        FramePtr encodeCaptureUplink(const FramePtr& pcm_48k);

        /*----------------------------------------------------------------
         * 运行时控制
         *----------------------------------------------------------------*/
        void    setVolume(uint8_t vol);
        uint8_t volume() const;

        /*----------------------------------------------------------------
         * 统计
         *----------------------------------------------------------------*/
        Stats stats() const;
        void  reset_stats();

        /*----------------------------------------------------------------
         * 配置只读访问
         *----------------------------------------------------------------*/
        const AudioCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
