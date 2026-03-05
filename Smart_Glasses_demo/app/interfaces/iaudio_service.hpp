/*
 * iaudio_service.hpp - 音频服务接口
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

namespace app
{

    struct AudioFrameView
    {
        const void* data;
        size_t      size;
        uint32_t    samples;
        uint32_t    rate;
    };

    class IAudioService
    {
    public:
        virtual ~IAudioService() = default;

        // TTS 播放
        virtual bool decodeAndPlay(const uint8_t* opus_data, size_t opus_len) = 0;
        virtual bool startPlayback()                                          = 0;
        virtual void stopPlayback()                                           = 0;
        virtual bool isPlaybackRunning() const                                = 0;

        // 采集（AI 对话）
        virtual bool startCapture()           = 0;
        virtual void stopCapture()            = 0;
        virtual bool isCaptureRunning() const = 0;

        // 唤醒词回调
        using WakewordCb = std::function<void(const int16_t* pcm, size_t samples)>;
        virtual void setWakewordCallback(WakewordCb cb) = 0;

        // AI 流回调（发送到 WebSocket）
        using CaptureCb = std::function<void(const AudioFrameView& frame)>;
        virtual void setCaptureCallback(CaptureCb cb) = 0;

        // 音量
        virtual void    setVolume(uint8_t vol) = 0;
        virtual uint8_t getVolume() const      = 0;
    };

} // namespace app
