/*
 * frame_dispatcher.hpp - 帧分发器（H264 统一流控，JPEG 同步）
 */

#pragma once

#include "../types.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace tool::memory
{
    class MemoryPool;
    class VideoRingBuffer;
    struct VideoRingBufferConfig;
}

namespace app::media::camera::domain
{

    using H264SinkRaw = std::function<void(const uint8_t* data, size_t size, uint64_t pts, bool keyframe)>;
    using JpegSink   = std::function<void(const FramePtr&)>;

    /* H264: 编码器→VideoRingBuffer→消费线程→sink；JPEG: 同步分发 */
    class FrameDispatcher
    {
    public:
        FrameDispatcher();
        ~FrameDispatcher();

        FrameDispatcher(const FrameDispatcher&)            = delete;
        FrameDispatcher& operator=(const FrameDispatcher&) = delete;

        void add_h264_sink(H264SinkRaw sink);
        void add_jpeg_sink(JpegSink sink);
        void remove_all();

        void dispatch_h264(const FramePtr& frame);
        void dispatch_jpeg(const FramePtr& frame);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera::domain
