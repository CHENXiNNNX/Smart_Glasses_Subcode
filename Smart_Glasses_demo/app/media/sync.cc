#include "sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>
#include <time.h>

// 初始化时间同步
int sync_init(sync_context_t *sync_ctx) {
    if (!sync_ctx) {
        return -1;
    }
    
    // 获取当前时间作为基准时间
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    sync_ctx->base_time_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    
    sync_ctx->last_audio_ts = 0;
    sync_ctx->last_video_ts = 0;
    sync_ctx->is_initialized = true;
    sync_ctx->audio_drift = 0.0;
    sync_ctx->video_drift = 0.0;
    
    printf("[SYNC] Time synchronization initialized, base time: %" PRIu64 " us\n", sync_ctx->base_time_us);
    return 0;
}

// 释放时间同步资源
int sync_deinit(sync_context_t *sync_ctx) {
    if (!sync_ctx) {
        return -1;
    }
    
    memset(sync_ctx, 0, sizeof(sync_context_t));
    printf("[SYNC] Time synchronization deinitialized\n");
    return 0;
}

// 获取同步后的时间戳（微秒）
uint64_t sync_get_timestamp(sync_context_t *sync_ctx, uint64_t raw_timestamp, bool is_audio) {
    if (!sync_ctx || !sync_ctx->is_initialized) {
        return raw_timestamp;
    }
    
    // 如果是第一个时间戳，直接使用
    if ((is_audio && sync_ctx->last_audio_ts == 0) || 
        (!is_audio && sync_ctx->last_video_ts == 0)) {
        if (is_audio) {
            sync_ctx->last_audio_ts = raw_timestamp;
        } else {
            sync_ctx->last_video_ts = raw_timestamp;
        }
        return raw_timestamp;
    }
    
    // 计算时间差
    uint64_t last_ts = is_audio ? sync_ctx->last_audio_ts : sync_ctx->last_video_ts;
    uint64_t diff = raw_timestamp > last_ts ? raw_timestamp - last_ts : 0;
    
    // 应用漂移补偿
    double drift_compensation = is_audio ? sync_ctx->audio_drift : sync_ctx->video_drift;
    if (drift_compensation != 0.0) {
        diff = (uint64_t)((double)diff * (1.0 + drift_compensation));
    }
    
    // 更新最后时间戳
    uint64_t synced_timestamp = last_ts + diff;
    if (is_audio) {
        sync_ctx->last_audio_ts = synced_timestamp;
    } else {
        sync_ctx->last_video_ts = synced_timestamp;
    }
    
    return synced_timestamp;
}

// 更新音频时间戳
int sync_update_audio_ts(sync_context_t *sync_ctx, uint64_t audio_ts) {
    if (!sync_ctx || !sync_ctx->is_initialized) {
        return -1;
    }
    
    sync_ctx->last_audio_ts = audio_ts;
    return 0;
}

// 更新视频时间戳
int sync_update_video_ts(sync_context_t *sync_ctx, uint64_t video_ts) {
    if (!sync_ctx || !sync_ctx->is_initialized) {
        return -1;
    }
    
    sync_ctx->last_video_ts = video_ts;
    return 0;
}

// 计算音视频时间差（微秒）
int64_t sync_calculate_diff(sync_context_t *sync_ctx) {
    if (!sync_ctx || !sync_ctx->is_initialized) {
        return 0;
    }
    
    // 如果任一时间戳未初始化，返回0
    if (sync_ctx->last_audio_ts == 0 || sync_ctx->last_video_ts == 0) {
        return 0;
    }
    
    // 返回音频时间戳与视频时间戳的差值
    return (int64_t)sync_ctx->last_audio_ts - (int64_t)sync_ctx->last_video_ts;
}

// 重置时间同步
int sync_reset(sync_context_t *sync_ctx) {
    if (!sync_ctx) {
        return -1;
    }
    
    // 重新获取基准时间
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    sync_ctx->base_time_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    
    sync_ctx->last_audio_ts = 0;
    sync_ctx->last_video_ts = 0;
    sync_ctx->audio_drift = 0.0;
    sync_ctx->video_drift = 0.0;
    
    printf("[SYNC] Time synchronization reset, new base time: %" PRIu64 " us\n", sync_ctx->base_time_us);
    return 0;
}
