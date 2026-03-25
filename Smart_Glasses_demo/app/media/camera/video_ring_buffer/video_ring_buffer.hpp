/*
 * video_ring_buffer.hpp - 视频帧环形缓冲（MemoryPool 分配）
 */

#pragma once

#include "tool/memory/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace app::tool::memory
{

    enum class DropPolicy
    {
        DROP_NEWEST,   /* 满时丢新帧 */
        DROP_OLDEST_P, /* 满时优先丢最老 P 帧，保留 I 帧 */
    };

    struct VideoRingBufferConfig
    {
        size_t     slot_size;   /* 每槽字节数 */
        size_t     capacity;    /* 槽位数 */
        DropPolicy drop_policy; /* 满时丢帧策略 */
    };

    /* 槽位布局: data | size | pts | keyframe */
    class VideoRingBuffer
    {
    public:
        explicit VideoRingBuffer(MemoryPool& pool, const VideoRingBufferConfig& config);
        ~VideoRingBuffer();

        VideoRingBuffer(const VideoRingBuffer&)            = delete;
        VideoRingBuffer& operator=(const VideoRingBuffer&) = delete;

        /* @return true 入队成功，false 满或 size>slot_size */
        bool enqueue(const uint8_t* data, size_t size, uint64_t pts, bool keyframe);

        /* @return true 出队成功，out_data 指向内部槽位，下次 enqueue/dequeue 前有效 */
        bool dequeue(const uint8_t*& out_data, size_t& out_size, uint64_t& out_pts,
                     bool& out_keyframe);

        [[nodiscard]] bool   empty() const noexcept;
        [[nodiscard]] bool   full() const noexcept;
        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] size_t get_slot_size() const noexcept
        {
            return config_.slot_size;
        }
        [[nodiscard]] size_t get_capacity() const noexcept
        {
            return config_.capacity;
        }

    private:
        MemoryPool&           pool_;
        VideoRingBufferConfig config_;
        uint8_t*              slots_ = nullptr;
        size_t                slot_stride_;
        size_t                head_  = 0;
        size_t                tail_  = 0;
        size_t                count_ = 0;
        mutable std::mutex    mutex_;

        static constexpr size_t OFFSET_SIZE     = 0;
        static constexpr size_t OFFSET_PTS      = sizeof(size_t);
        static constexpr size_t OFFSET_KEYFRAME = OFFSET_PTS + sizeof(uint64_t);

        uint8_t* slot_ptr(size_t index) const noexcept;
        bool     try_drop_oldest_p();
    };

} // namespace app::tool::memory
