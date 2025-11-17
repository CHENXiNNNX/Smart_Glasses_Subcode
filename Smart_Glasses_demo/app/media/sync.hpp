#ifndef _SYNC_HPP
#define _SYNC_HPP

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // 时间同步状态结构体
    typedef struct
    {
        uint64_t base_time_us;   // 基准时间（微秒）
        uint64_t last_audio_ts;  // 最后音频时间戳
        uint64_t last_video_ts;  // 最后视频时间戳
        bool     is_initialized; // 是否已初始化
        double   audio_drift;    // 音频漂移补偿
        double   video_drift;    // 视频漂移补偿
    } sync_context_t;

    // 初始化时间同步
    int sync_init(sync_context_t* sync_ctx);

    // 释放时间同步资源
    int sync_deinit(sync_context_t* sync_ctx);

    // 获取同步后的时间戳（微秒）
    uint64_t sync_get_timestamp(sync_context_t* sync_ctx, uint64_t raw_timestamp, bool is_audio);

    // 更新音频时间戳
    int sync_update_audio_ts(sync_context_t* sync_ctx, uint64_t audio_ts);

    // 更新视频时间戳
    int sync_update_video_ts(sync_context_t* sync_ctx, uint64_t video_ts);

    // 计算时间差（微秒）
    int64_t sync_calculate_diff(sync_context_t* sync_ctx);

    // 重置时间同步
    int sync_reset(sync_context_t* sync_ctx);

#ifdef __cplusplus
}
#endif

#endif // _SYNC_HPP
