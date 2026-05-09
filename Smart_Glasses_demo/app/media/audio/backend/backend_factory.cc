/* backend_factory.cc - 后端工厂实现 */

#include "backend_factory.hpp"
#include "alsa_capture.hpp"
#include "alsa_playback.hpp"
#include "../../../tool/log/log.hpp"

#include "rkmpi_capture.hpp"
#include "rkmpi_playback.hpp"

/* 实际可用性由 RkMpi 后端内部的 HAS_RKMPI_AI / HAS_RKMPI_AO 决定 */
#define HAS_RKMPI_CAPTURE 1
#define HAS_RKMPI_PLAYBACK 1

namespace app::media::audio
{

    using namespace tool::log;

#define TAG "BackendFactory"

    std::unique_ptr<ICaptureBackend>
    BackendFactory::createCapture(BackendMode mode, const CaptureCfg& cfg, FramePool* pool)
    {
        /* RkMpi 优先 */
        if (mode == BackendMode::RkMpi || mode == BackendMode::Auto)
        {
#if HAS_RKMPI_CAPTURE
            auto rk = std::make_unique<RkMpiCapture>();
            if (rk->init(cfg, pool) == Error::OK)
            {
                LOG_INFO(TAG, "采集后端: RkMpi");
                return rk;
            }
            LOG_WARN(TAG, "RkMpi 采集初始化失败");
            if (mode == BackendMode::RkMpi)
                return nullptr;
#else
            if (mode == BackendMode::RkMpi)
            {
                LOG_ERROR(TAG, "编译时未包含 RkMpi 采集后端");
                return nullptr;
            }
#endif
        }

        /* ALSA 后备 */
        if (mode == BackendMode::Alsa || (mode == BackendMode::Auto && cfg.fallback_to_alsa))
        {
            auto alsa = std::make_unique<AlsaCapture>();
            if (alsa->init(cfg, pool) == Error::OK)
            {
                LOG_INFO(TAG, "采集后端: ALSA");
                return alsa;
            }
            LOG_ERROR(TAG, "ALSA 采集初始化失败");
        }

        return nullptr;
    }

    std::unique_ptr<IPlaybackBackend>
    BackendFactory::createPlayback(BackendMode mode, const PlaybackCfg& cfg, FramePool* pool)
    {
        if (mode == BackendMode::RkMpi || mode == BackendMode::Auto)
        {
#if HAS_RKMPI_PLAYBACK
            auto rk = std::make_unique<RkMpiPlayback>();
            if (rk->init(cfg, pool) == Error::OK)
            {
                LOG_INFO(TAG, "播放后端: RkMpi");
                return rk;
            }
            LOG_WARN(TAG, "RkMpi 播放初始化失败");
            if (mode == BackendMode::RkMpi)
                return nullptr;
#else
            if (mode == BackendMode::RkMpi)
            {
                LOG_ERROR(TAG, "编译时未包含 RkMpi 播放后端");
                return nullptr;
            }
#endif
        }

        if (mode == BackendMode::Alsa || (mode == BackendMode::Auto && cfg.fallback_to_alsa))
        {
            auto alsa = std::make_unique<AlsaPlayback>();
            if (alsa->init(cfg, pool) == Error::OK)
            {
                LOG_INFO(TAG, "播放后端: ALSA");
                return alsa;
            }
            LOG_ERROR(TAG, "ALSA 播放初始化失败");
        }

        return nullptr;
    }

} // namespace app::media::audio
