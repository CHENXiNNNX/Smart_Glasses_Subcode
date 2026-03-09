/*
 * types.hpp - 摄像头公共类型
 */

#pragma once

#include "../sync.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace app::media::camera
{

    /* 错误码 */
    enum class Error
    {
        OK = 0,
        NOT_INIT,
        ALREADY_INIT,
        INVALID_PARAM,
        DEVICE_ERROR,
        ENCODER_ERROR,
        MEMORY_ERROR,
        TIMEOUT,
        BUSY,
        NOT_SUPPORTED,
    };

    /* 编码格式 */
    enum class Codec
    {
        H264,
        H265,
    };

    /* 帧数据 */
    struct Frame
    {
        uint8_t* data      = nullptr;
        size_t   size      = 0;
        uint16_t width     = 0;
        uint16_t height    = 0;
        uint64_t pts       = 0;
        uint64_t timestamp = 0;
        bool     keyframe  = false;

        void*                 priv = nullptr;
        std::function<void()> release;

        ~Frame()
        {
            if (release)
                release();
        }
    };

    using FramePtr = std::shared_ptr<Frame>;

    /* 回调类型 */
    using H264Cb  = std::function<void(const FramePtr& frame)>;
    using JpegCb  = std::function<void(const FramePtr& frame)>;
    using ErrorCb = std::function<void(Error err, const char* msg)>;
    using PhotoCb = std::function<void(const std::string& path, Error err)>;

    /* 配置结构 */
    struct H264Cfg
    {
        uint16_t width   = 1920;
        uint16_t height  = 1080;
        uint8_t  fps     = 30;
        uint16_t bitrate = 2000;
        uint8_t  gop     = 30;
        Codec    codec   = Codec::H264;
    };

    struct JpegCfg
    {
        uint16_t width   = 640;
        uint16_t height  = 480;
        uint8_t  quality = 80;
    };

    struct MemoryCfg
    {
        size_t fixed_block_size  = 256 * 1024;
        size_t fixed_block_count = 8;
        size_t dynamic_max_size  = 2 * 1024 * 1024;
    };

    struct VpssAiispCfg
    {
        bool        enable_aiisp = false;
        std::string aiisp_model_path = "/oem/usr/lib/";
        uint32_t    aiisp_frame_buf_cnt = 2;
    };

    struct CameraCfg
    {
        H264Cfg       h264;
        JpegCfg       jpeg;
        MemoryCfg     memory;
        VpssAiispCfg  vpss_aiisp;

        std::string iq_file_dir = "/etc/iqfiles";
        bool        enable_h264 = true;
        bool        enable_jpeg = true;
        int         jpeg_dst_fps = 0;  /* VPSS JPEG 输出帧率，<=0 不限制 */
    };

    /* 统计信息 */
    struct Stats
    {
        uint32_t h264_frames  = 0;
        uint32_t h264_drops   = 0;
        float    h264_fps     = 0.0f;
        uint32_t h264_bitrate = 0;

        uint32_t jpeg_frames = 0;
        uint32_t jpeg_drops  = 0;
        float    jpeg_fps    = 0.0f;

        uint32_t record_frames = 0;
        uint32_t record_sec    = 0;

        size_t mem_used  = 0;
        size_t mem_total = 0;
    };

} // namespace app::media::camera
