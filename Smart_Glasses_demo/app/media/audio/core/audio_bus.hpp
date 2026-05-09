/* audio_bus.hpp - 发布-订阅音频总线 */

#pragma once

#include "types.hpp"
#include <memory>

namespace app::media::audio
{

    class AudioBus
    {
    public:
        AudioBus();
        ~AudioBus();

        AudioBus(const AudioBus&)            = delete;
        AudioBus& operator=(const AudioBus&) = delete;

        SubHandle subscribe(StreamId id, FrameCb cb);
        void      unsubscribe(SubHandle handle);

        void publish(StreamId id, const FramePtr& frame);

        size_t subscriber_count(StreamId id) const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
