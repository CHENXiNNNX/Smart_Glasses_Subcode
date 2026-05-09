/* rkmpi_playback.hpp - RK MPI AO 播放后端 */

#pragma once

#include "playback_backend.hpp"
#include <memory>

namespace app::media::audio
{

    class RkMpiPlayback : public IPlaybackBackend
    {
    public:
        RkMpiPlayback();
        ~RkMpiPlayback() override;

        Error init(const PlaybackCfg& cfg, FramePool* pool) override;
        void  deinit() override;
        Error start() override;
        Error stop() override;
        bool  is_running() const override;

        Error push(const FramePtr& frame) override;
        Error push(const int16_t* data, size_t samples) override;
        void  clear() override;

        void        set_volume(uint8_t vol) override;
        uint8_t     volume() const override;
        const char* name() const override
        {
            return "RkMpi";
        }
        size_t   queue_size() const override;
        uint32_t drops() const override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
