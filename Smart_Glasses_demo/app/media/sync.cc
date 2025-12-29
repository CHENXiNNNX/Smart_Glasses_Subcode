#include "sync.hpp"
#include "../tool/log/log.hpp"
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace
{
    constexpr const char* LOG_TAG                     = "SYNC";
    constexpr uint64_t    MICROSECONDS_PER_SECOND     = 1000000ULL;
    constexpr uint64_t    NANOSECONDS_PER_MICROSECOND = 1000ULL;
} // namespace

// 初始化时间同步
int sync_init(sync_context_t* sync_ctx)
{
    if (!sync_ctx)
    {
        return -1;
    }

    // 获取当前时间作为基准时间
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    sync_ctx->base_time_us = static_cast<uint64_t>(ts.tv_sec) * MICROSECONDS_PER_SECOND +
                             static_cast<uint64_t>(ts.tv_nsec) / NANOSECONDS_PER_MICROSECOND;

    sync_ctx->last_audio_ts  = 0;
    sync_ctx->last_video_ts  = 0;
    sync_ctx->is_initialized = true;
    sync_ctx->audio_drift    = 0.0;
    sync_ctx->video_drift    = 0.0;

    LOG_INFO(LOG_TAG, "时间同步已初始化，基准时间: %" PRIu64 " us", sync_ctx->base_time_us);
    return 0;
}

// 释放时间同步资源
int sync_deinit(sync_context_t* sync_ctx)
{
    if (!sync_ctx)
    {
        return -1;
    }

    std::memset(sync_ctx, 0, sizeof(sync_context_t));
    LOG_INFO(LOG_TAG, "时间同步已去初始化");
    return 0;
}

// 获取同步后的时间戳（微秒）
uint64_t sync_get_timestamp(sync_context_t* sync_ctx, uint64_t raw_timestamp, bool is_audio)
{
    if (!sync_ctx || !sync_ctx->is_initialized)
    {
        return raw_timestamp;
    }

    // 如果是第一个时间戳，直接使用
    if ((is_audio && sync_ctx->last_audio_ts == 0) || (!is_audio && sync_ctx->last_video_ts == 0))
    {
        if (is_audio)
        {
            sync_ctx->last_audio_ts = raw_timestamp;
        }
        else
        {
            sync_ctx->last_video_ts = raw_timestamp;
        }
        return raw_timestamp;
    }

    // 计算时间差
    uint64_t last_ts = is_audio ? sync_ctx->last_audio_ts : sync_ctx->last_video_ts;
    uint64_t diff    = raw_timestamp > last_ts ? raw_timestamp - last_ts : 0;

    // 应用漂移补偿
    double drift_compensation = is_audio ? sync_ctx->audio_drift : sync_ctx->video_drift;
    if (drift_compensation != 0.0)
    {
        diff = static_cast<uint64_t>(static_cast<double>(diff) * (1.0 + drift_compensation));
    }

    // 更新最后时间戳
    uint64_t synced_timestamp = last_ts + diff;
    if (is_audio)
    {
        sync_ctx->last_audio_ts = synced_timestamp;
    }
    else
    {
        sync_ctx->last_video_ts = synced_timestamp;
    }

    return synced_timestamp;
}

// 更新音频时间戳
int sync_update_audio_ts(sync_context_t* sync_ctx, uint64_t audio_ts)
{
    if (!sync_ctx || !sync_ctx->is_initialized)
    {
        return -1;
    }

    sync_ctx->last_audio_ts = audio_ts;
    return 0;
}

// 更新视频时间戳
int sync_update_video_ts(sync_context_t* sync_ctx, uint64_t video_ts)
{
    if (!sync_ctx || !sync_ctx->is_initialized)
    {
        return -1;
    }

    sync_ctx->last_video_ts = video_ts;
    return 0;
}

// 计算音视频时间差（微秒）
int64_t sync_calculate_diff(sync_context_t* sync_ctx)
{
    if (!sync_ctx || !sync_ctx->is_initialized)
    {
        return 0;
    }

    // 如果任一时间戳未初始化，返回0
    if (sync_ctx->last_audio_ts == 0 || sync_ctx->last_video_ts == 0)
    {
        return 0;
    }

    // 返回音频时间戳与视频时间戳的差值
    return static_cast<int64_t>(sync_ctx->last_audio_ts) -
           static_cast<int64_t>(sync_ctx->last_video_ts);
}

// 重置时间同步
int sync_reset(sync_context_t* sync_ctx)
{
    if (!sync_ctx)
    {
        return -1;
    }

    // 重新获取基准时间
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    sync_ctx->base_time_us = static_cast<uint64_t>(ts.tv_sec) * MICROSECONDS_PER_SECOND +
                             static_cast<uint64_t>(ts.tv_nsec) / NANOSECONDS_PER_MICROSECOND;

    sync_ctx->last_audio_ts = 0;
    sync_ctx->last_video_ts = 0;
    sync_ctx->audio_drift   = 0.0;
    sync_ctx->video_drift   = 0.0;

    LOG_INFO(LOG_TAG, "时间同步已重置，新基准时间: %" PRIu64 " us", sync_ctx->base_time_us);
    return 0;
}
