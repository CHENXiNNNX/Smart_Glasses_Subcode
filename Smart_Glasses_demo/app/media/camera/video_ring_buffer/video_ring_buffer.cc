/*
 * video_ring_buffer.cc - 视频帧环形缓冲
 */

#include "video_ring_buffer.hpp"

#include <cstring>

namespace app::tool::memory
{

    namespace
    {
        constexpr size_t slot_meta_size()
        {
            return sizeof(size_t) + sizeof(uint64_t) + sizeof(bool);
        }
    } // namespace

    VideoRingBuffer::VideoRingBuffer(MemoryPool& pool, const VideoRingBufferConfig& config)
        : pool_(pool), config_(config), slot_stride_(config.slot_size + slot_meta_size())
    {
        size_t total = slot_stride_ * config.capacity;
        slots_       = static_cast<uint8_t*>(pool_.allocate(total));
    }

    VideoRingBuffer::~VideoRingBuffer()
    {
        if (slots_)
        {
            pool_.deallocate(slots_);
            slots_ = nullptr;
        }
    }

    uint8_t* VideoRingBuffer::slot_ptr(size_t index) const noexcept
    {
        return slots_ + (index % config_.capacity) * slot_stride_;
    }

    bool VideoRingBuffer::try_drop_oldest_p()
    {
        if (count_ == 0)
            return false;
        /* 从 head 起找首个 P 帧，丢弃后 head 前移 */
        for (size_t i = 0; i < count_; ++i)
        {
            size_t   idx = (head_ + i) % config_.capacity;
            uint8_t* s   = slot_ptr(idx);
            bool     kf;
            std::memcpy(&kf, s + config_.slot_size + OFFSET_KEYFRAME, sizeof(bool));
            if (!kf)
            {
                head_ = (head_ + 1) % config_.capacity;
                count_--;
                return true;
            }
        }
        return false;
    }

    bool VideoRingBuffer::enqueue(const uint8_t* data, size_t size, uint64_t pts, bool keyframe)
    {
        if (!slots_ || size > config_.slot_size)
            return false;

        std::lock_guard<std::mutex> lock(mutex_);

        if (count_ >= config_.capacity)
        {
            if (config_.drop_policy == DropPolicy::DROP_NEWEST)
                return false;
            if (config_.drop_policy == DropPolicy::DROP_OLDEST_P)
            {
                if (!keyframe || !try_drop_oldest_p())
                    return false;
            }
        }

        uint8_t* slot = slot_ptr(tail_);
        std::memcpy(slot, data, size);
        std::memcpy(slot + config_.slot_size + OFFSET_SIZE, &size, sizeof(size_t));
        std::memcpy(slot + config_.slot_size + OFFSET_PTS, &pts, sizeof(uint64_t));
        std::memcpy(slot + config_.slot_size + OFFSET_KEYFRAME, &keyframe, sizeof(bool));

        tail_ = (tail_ + 1) % config_.capacity;
        count_++;
        return true;
    }

    bool VideoRingBuffer::dequeue(const uint8_t*& out_data, size_t& out_size, uint64_t& out_pts,
                                  bool& out_keyframe)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0)
            return false;

        uint8_t* slot = slot_ptr(head_);
        out_data      = slot;
        std::memcpy(&out_size, slot + config_.slot_size + OFFSET_SIZE, sizeof(size_t));
        std::memcpy(&out_pts, slot + config_.slot_size + OFFSET_PTS, sizeof(uint64_t));
        std::memcpy(&out_keyframe, slot + config_.slot_size + OFFSET_KEYFRAME, sizeof(bool));

        head_ = (head_ + 1) % config_.capacity;
        count_--;
        return true;
    }

    bool VideoRingBuffer::empty() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    bool VideoRingBuffer::full() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ >= config_.capacity;
    }

    size_t VideoRingBuffer::size() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

} // namespace app::tool::memory
