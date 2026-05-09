/*
 * audio_service_impl.hpp - 音频服务实现（基于 AudioDrv 订阅模型）
 */

#pragma once

#include "../interfaces/iaudio_service.hpp"
#include <cstdint>
#include <memory>

namespace app::media::audio
{
    class AudioDrv;
}

namespace app
{

    class AudioServiceImpl : public IAudioService
    {
    public:
        explicit AudioServiceImpl(media::audio::AudioDrv* drv);
        ~AudioServiceImpl() override;

        bool decodeAndPlay(const uint8_t* opus_data, size_t opus_len) override;
        bool startPlayback() override;
        void stopPlayback() override;
        bool isPlaybackRunning() const override;

        bool startCapture() override;
        void stopCapture() override;
        bool isCaptureRunning() const override;

        void setWakewordCallback(WakewordCb cb) override;
        void setCaptureCallback(CaptureCb cb) override;

        void    setVolume(uint8_t vol) override;
        uint8_t getVolume() const override;

    private:
        void rebuildSubscription();

        media::audio::AudioDrv* drv_;
        WakewordCb              wakeword_cb_;
        CaptureCb               capture_cb_;
        std::uint64_t           mic_sub_{0};
    };

} // namespace app
