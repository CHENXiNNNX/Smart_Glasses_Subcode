/*
 * audio_service_impl.cc - 音频服务（基于 AudioDrv 订阅模型）
 */

#include "audio_service_impl.hpp"
#include "../media/audio/audio.hpp"

namespace app
{

    AudioServiceImpl::AudioServiceImpl(media::audio::AudioDrv* drv) : drv_(drv) {}

    AudioServiceImpl::~AudioServiceImpl()
    {
        if (drv_ && mic_sub_)
        {
            drv_->unsubscribe(mic_sub_);
        }
    }

    bool AudioServiceImpl::decodeAndPlay(const uint8_t* opus_data, size_t opus_len)
    {
        if (!drv_)
        {
            return false;
        }
        return drv_->pushPlaybackOpus(opus_data, opus_len) == media::audio::Error::OK;
    }

    bool AudioServiceImpl::startPlayback()
    {
        return drv_ && drv_->is_running();
    }

    void AudioServiceImpl::stopPlayback()
    {
        /* 播放由 AudioDrv 统一管理，无需单独停止 */
    }

    bool AudioServiceImpl::isPlaybackRunning() const
    {
        return drv_ && drv_->is_running();
    }

    bool AudioServiceImpl::startCapture()
    {
        return drv_ && drv_->is_running();
    }

    void AudioServiceImpl::stopCapture()
    {
        /* 采集由 AudioDrv 统一管理 */
    }

    bool AudioServiceImpl::isCaptureRunning() const
    {
        return drv_ && drv_->is_running();
    }

    void AudioServiceImpl::setWakewordCallback(WakewordCb cb)
    {
        wakeword_cb_ = std::move(cb);
        rebuildSubscription();
    }

    void AudioServiceImpl::setCaptureCallback(CaptureCb cb)
    {
        capture_cb_ = std::move(cb);
        rebuildSubscription();
    }

    void AudioServiceImpl::rebuildSubscription()
    {
        if (!drv_)
        {
            return;
        }

        if (mic_sub_)
        {
            drv_->unsubscribe(mic_sub_);
            mic_sub_ = 0;
        }

        if (!wakeword_cb_ && !capture_cb_)
        {
            return;
        }

        mic_sub_ =
            drv_->subscribe(media::audio::StreamId::MicProcessed,
                            [this](const media::audio::FramePtr& frame)
                            {
                                if (!frame || !frame->data)
                                {
                                    return;
                                }

                                const int16_t* pcm     = frame->get<int16_t>();
                                size_t         samples = frame->samples;

                                if (wakeword_cb_ && pcm && samples > 0)
                                {
                                    wakeword_cb_(pcm, samples);
                                }

                                if (capture_cb_)
                                {
                                    auto opus_frame = drv_->encodeCaptureUplink(frame);
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
        {
            drv_->setVolume(vol);
        }
    }

    uint8_t AudioServiceImpl::getVolume() const
    {
        return drv_ ? drv_->volume() : 0;
    }

} // namespace app
