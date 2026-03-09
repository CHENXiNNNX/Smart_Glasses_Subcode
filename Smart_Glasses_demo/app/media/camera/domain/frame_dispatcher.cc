/*
 * frame_dispatcher.cc - 帧分发器
 */

#include "frame_dispatcher.hpp"
#include "tool/memory/memory.hpp"
#include "video_ring_buffer.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace app::media::camera::domain
{

    static constexpr size_t H264_SLOT_SIZE = 256 * 1024;
    static constexpr size_t H264_QUEUE_CAP = 4;

    struct FrameDispatcher::Impl
    {
        std::vector<H264SinkRaw> h264_sinks_;
        std::vector<JpegSink>    jpeg_sinks_;
        std::mutex               mtx_;

        std::unique_ptr<tool::memory::MemoryPool>       h264_pool_;
        std::unique_ptr<tool::memory::VideoRingBuffer>  h264_buffer_;
        std::thread                                     consumer_thread_;
        std::atomic<bool>                               consumer_stop_{false};

        void start_consumer()
        {
            if (consumer_thread_.joinable())
                return;
            consumer_stop_ = false;
            consumer_thread_ = std::thread(&Impl::consumer_loop, this);
        }

        void stop_consumer()
        {
            consumer_stop_ = true;
            if (consumer_thread_.joinable())
                consumer_thread_.join();
        }

        void consumer_loop()
        {
            while (!consumer_stop_ && h264_buffer_)
            {
                const uint8_t* data     = nullptr;
                size_t         size     = 0;
                uint64_t       pts      = 0;
                bool           keyframe = false;
                if (h264_buffer_->dequeue(data, size, pts, keyframe) && size > 0)
                {
                    std::vector<H264SinkRaw> copy;
                    {
                        std::lock_guard<std::mutex> lk(mtx_);
                        copy = h264_sinks_;
                    }
                    for (auto& sink : copy)
                    {
                        if (sink)
                            sink(data, size, pts, keyframe);
                    }
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
        }

        void ensure_h264_buffer()
        {
            if (h264_buffer_)
                return;
            tool::memory::VideoRingBufferConfig cfg;
            cfg.slot_size   = H264_SLOT_SIZE;
            cfg.capacity    = H264_QUEUE_CAP;
            cfg.drop_policy = tool::memory::DropPolicy::DROP_OLDEST_P;
            size_t pool_sz  = cfg.slot_size * cfg.capacity + 1024;
            h264_pool_   = std::make_unique<tool::memory::MemoryPool>(pool_sz);
            h264_buffer_ = std::make_unique<tool::memory::VideoRingBuffer>(*h264_pool_, cfg);
        }
    };

    FrameDispatcher::FrameDispatcher() : impl_(std::make_unique<Impl>()) {}

    FrameDispatcher::~FrameDispatcher()
    {
        impl_->stop_consumer();
    }

    void FrameDispatcher::add_h264_sink(H264SinkRaw sink)
    {
        std::lock_guard<std::mutex> lk(impl_->mtx_);
        impl_->ensure_h264_buffer();
        impl_->h264_sinks_.push_back(std::move(sink));
        impl_->start_consumer();
    }

    void FrameDispatcher::add_jpeg_sink(JpegSink sink)
    {
        std::lock_guard<std::mutex> lk(impl_->mtx_);
        impl_->jpeg_sinks_.push_back(std::move(sink));
    }

    void FrameDispatcher::remove_all()
    {
        impl_->stop_consumer();
        std::lock_guard<std::mutex> lk(impl_->mtx_);
        impl_->h264_sinks_.clear();
        impl_->jpeg_sinks_.clear();
    }

    void FrameDispatcher::dispatch_h264(const FramePtr& frame)
    {
        if (!frame || !frame->data || frame->size == 0)
            return;
        impl_->ensure_h264_buffer();
        if (!impl_->h264_buffer_->enqueue(frame->data, frame->size, frame->pts, frame->keyframe))
            return;
    }

    void FrameDispatcher::dispatch_jpeg(const FramePtr& frame)
    {
        std::vector<JpegSink> copy;
        {
            std::lock_guard<std::mutex> lk(impl_->mtx_);
            copy = impl_->jpeg_sinks_;
        }
        for (auto& sink : copy)
        {
            if (sink)
                sink(frame);
        }
    }

} // namespace app::media::camera::domain
