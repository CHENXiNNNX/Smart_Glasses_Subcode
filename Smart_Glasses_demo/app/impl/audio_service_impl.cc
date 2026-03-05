/*
 * audio_service_impl.cc - 音频服务
 */

#include "audio_service_impl.hpp"
#include "../media/audio/audio.hpp"

namespace app
{

    AudioServiceImpl::AudioServiceImpl(media::audio::AudioDrv* drv) : drv_(drv) {}

    AudioServiceImpl::~AudioServiceImpl() = default;

    bool AudioServiceImpl::decodeAndPlay(const uint8_t* opus_data, size_t opus_len)
    {
        if (!drv_ || !drv_->opus().is_init())
            return false;

        auto frame = drv_->opus().decode(opus_data, opus_len);
        if (!frame)
            return false;

        auto err = drv_->playback().push(frame);
        if (err != media::audio::Error::OK)
            return false;

        if (!drv_->playback().is_running())
        {
            err = drv_->playback().start();
            if (err != media::audio::Error::OK)
                return false;
        }
        return true;
    }

    bool AudioServiceImpl::startPlayback()
    {
        if (!drv_)
            return false;
        return drv_->playback().start() == media::audio::Error::OK;
    }

    void AudioServiceImpl::stopPlayback()
    {
        if (drv_)
            drv_->playback().stop();
    }

    bool AudioServiceImpl::isPlaybackRunning() const
    {
        return drv_ && drv_->playback().is_running();
    }

    bool AudioServiceImpl::startCapture()
    {
        if (!drv_)
            return false;
        return drv_->capture().start() == media::audio::Error::OK;
    }

    void AudioServiceImpl::stopCapture()
    {
        if (drv_)
            drv_->capture().stop();
    }

    bool AudioServiceImpl::isCaptureRunning() const
    {
        return drv_ && drv_->capture().is_running();
    }

    void AudioServiceImpl::setWakewordCallback(WakewordCb cb)
    {
        wakeword_cb_ = std::move(cb);
        updateCaptureCallback();
    }

    void AudioServiceImpl::setCaptureCallback(CaptureCb cb)
    {
        capture_cb_ = std::move(cb);
        updateCaptureCallback();
    }

    void AudioServiceImpl::updateCaptureCallback()
    {
        if (!drv_)
            return;

        if (!wakeword_cb_ && !capture_cb_)
        {
            drv_->set_capture_cb(nullptr);
            return;
        }

        drv_->set_capture_cb(
            [this](const media::audio::FramePtr& frame)
            {
                if (!frame || !frame->data)
                    return;

                const int16_t* pcm     = frame->get<int16_t>();
                size_t         samples = frame->samples;

                if (wakeword_cb_ && pcm && samples > 0)
                    wakeword_cb_(pcm, samples);

                if (capture_cb_)
                {
                    /* 48k→16k Opus 上发 */
                    auto opus_frame = drv_->encodeCaptureForAI(frame);
                    if (opus_frame && opus_frame->data && opus_frame->size > 0)
                    {
                        AudioFrameView view;
                        view.data    = opus_frame->data;
                        view.size    = opus_frame->size;
                        view.samples = opus_frame->samples;
                        view.rate    = opus_frame->rate;
                        capture_cb_(view);
                    }
                }
            });
    }

    void AudioServiceImpl::setVolume(uint8_t vol)
    {
        if (drv_)
            drv_->playback().set_volume(vol);
    }

    uint8_t AudioServiceImpl::getVolume() const
    {
        return drv_ ? drv_->playback().volume() : 0;
    }

} // namespace app
