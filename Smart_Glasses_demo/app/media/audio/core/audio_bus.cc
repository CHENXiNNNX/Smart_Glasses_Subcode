/* audio_bus.cc - 发布-订阅音频总线实现 */

#include "audio_bus.hpp"

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace app::media::audio
{

    static constexpr size_t STREAM_COUNT = static_cast<size_t>(StreamId::COUNT_);

    struct Subscriber
    {
        SubHandle handle;
        FrameCb   cb;
    };

    class AudioBus::Impl
    {
    public:
        std::atomic<uint64_t>     next_handle_{1};
        mutable std::shared_mutex mtx_;
        std::vector<Subscriber>   subs_[STREAM_COUNT];

        /* handle → StreamId 的反向映射，用于 unsubscribe */
        std::unordered_map<SubHandle, StreamId> handle_map_;

        SubHandle subscribe(StreamId id, FrameCb cb)
        {
            SubHandle        h = next_handle_.fetch_add(1, std::memory_order_relaxed);
            std::unique_lock lk(mtx_);
            subs_[static_cast<size_t>(id)].push_back({h, std::move(cb)});
            handle_map_[h] = id;
            return h;
        }

        void unsubscribe(SubHandle handle)
        {
            std::unique_lock lk(mtx_);
            auto             it = handle_map_.find(handle);
            if (it == handle_map_.end())
                return;
            auto& vec = subs_[static_cast<size_t>(it->second)];
            for (auto vi = vec.begin(); vi != vec.end(); ++vi)
            {
                if (vi->handle == handle)
                {
                    vec.erase(vi);
                    break;
                }
            }
            handle_map_.erase(it);
        }

        void publish(StreamId id, const FramePtr& frame)
        {
            std::shared_lock lk(mtx_);
            for (auto& sub : subs_[static_cast<size_t>(id)])
            {
                if (sub.cb)
                    sub.cb(frame);
            }
        }

        size_t subscriber_count(StreamId id) const
        {
            std::shared_lock lk(mtx_);
            return subs_[static_cast<size_t>(id)].size();
        }
    };

    AudioBus::AudioBus() : impl_(std::make_unique<Impl>()) {}
    AudioBus::~AudioBus() = default;

    SubHandle AudioBus::subscribe(StreamId id, FrameCb cb)
    {
        return impl_->subscribe(id, std::move(cb));
    }

    void AudioBus::unsubscribe(SubHandle handle)
    {
        impl_->unsubscribe(handle);
    }

    void AudioBus::publish(StreamId id, const FramePtr& frame)
    {
        impl_->publish(id, frame);
    }

    size_t AudioBus::subscriber_count(StreamId id) const
    {
        return impl_->subscriber_count(id);
    }

} // namespace app::media::audio
