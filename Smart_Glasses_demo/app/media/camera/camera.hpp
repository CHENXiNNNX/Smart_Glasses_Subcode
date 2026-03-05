/* camera.hpp - 摄像头驱动 */

#pragma once

#include "../sync.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace app::media::camera
{

    /*============================================================================
     * 前向声明
     *============================================================================*/

    class H264Encoder;
    class JpegEncoder;
    class Recorder;
    class IspCtrl;
    class FramePool;

    /*============================================================================
     * 错误码
     *============================================================================*/

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

    /*============================================================================
     * 编码格式
     *============================================================================*/

    enum class Codec
    {
        H264,
        H265,
    };

    /*============================================================================
     * 帧数据
     *============================================================================*/

    struct Frame
    {
        uint8_t* data      = nullptr; // 数据指针
        size_t   size      = 0;       // 数据大小
        uint16_t width     = 0;       // 宽度
        uint16_t height    = 0;       // 高度
        uint64_t pts       = 0;       // 编码器时间戳
        uint64_t timestamp = 0;       // 系统时间戳 (us)
        bool     keyframe  = false;   // 是否关键帧

        /* 内部使用 */
        void*                 priv = nullptr; // 私有数据 (DMA buffer)
        std::function<void()> release;        // 释放回调
    };

    using FramePtr = std::shared_ptr<Frame>;

    /*============================================================================
     * 回调类型
     *============================================================================*/

    using H264Cb  = std::function<void(const FramePtr& frame)>;
    using JpegCb  = std::function<void(const FramePtr& frame)>;
    using ErrorCb = std::function<void(Error err, const char* msg)>;
    using PhotoCb = std::function<void(const std::string& path, Error err)>;

    /*============================================================================
     * 配置结构
     *============================================================================*/

    struct H264Cfg
    {
        uint16_t width   = 1920;
        uint16_t height  = 1080;
        uint8_t  fps     = 30;
        uint16_t bitrate = 2000; // kbps
        uint8_t  gop     = 30;
        Codec    codec   = Codec::H264;
    };

    struct JpegCfg
    {
        uint16_t width   = 640;
        uint16_t height  = 480;
        uint8_t  quality = 80; // 1-100
    };

    struct MemoryCfg
    {
        size_t fixed_block_size  = 256 * 1024;      // 固定块大小
        size_t fixed_block_count = 8;               // 固定块数量
        size_t dynamic_max_size  = 2 * 1024 * 1024; // 动态池上限
    };

    struct CameraCfg
    {
        H264Cfg   h264;
        JpegCfg   jpeg;
        MemoryCfg memory;

        std::string iq_file_dir = "/etc/iqfiles"; // ISP 配置目录
        bool        enable_h264 = true;
        bool        enable_jpeg = true;
    };

    /*============================================================================
     * 统计信息
     *============================================================================*/

    struct Stats
    {
        /* H264 */
        uint32_t h264_frames  = 0;
        uint32_t h264_drops   = 0;
        float    h264_fps     = 0.0f;
        uint32_t h264_bitrate = 0; // kbps

        /* JPEG */
        uint32_t jpeg_frames = 0;
        uint32_t jpeg_drops  = 0;
        float    jpeg_fps    = 0.0f;

        /* 录像 */
        uint32_t record_frames = 0;
        uint32_t record_sec    = 0;

        /* 内存 */
        size_t mem_used  = 0;
        size_t mem_total = 0;
    };

    /*============================================================================
     * H264 编码器
     *============================================================================*/

    class H264Encoder
    {
    public:
        H264Encoder();
        ~H264Encoder();

        H264Encoder(const H264Encoder&)            = delete;
        H264Encoder& operator=(const H264Encoder&) = delete;

        /* 生命周期 */
        Error init(const H264Cfg& cfg, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        /* 控制 */
        Error start();
        Error stop();
        bool  is_running() const;

        /* 回调 */
        void set_cb(H264Cb cb);

        /* 参数调整 */
        Error set_bitrate(uint16_t kbps);
        Error set_gop(uint8_t gop);

        /* 分辨率切换 (预留) */
        Error set_resolution(uint16_t w, uint16_t h);

        /* 信息 */
        const H264Cfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * JPEG 编码器
     *============================================================================*/

    class JpegEncoder
    {
    public:
        JpegEncoder();
        ~JpegEncoder();

        JpegEncoder(const JpegEncoder&)            = delete;
        JpegEncoder& operator=(const JpegEncoder&) = delete;

        /* 生命周期 */
        Error init(const JpegCfg& cfg, FramePool* pool);
        void  deinit();
        bool  is_init() const;

        /* 控制 */
        Error start();
        Error stop();
        bool  is_running() const;

        /* 回调 */
        void set_cb(JpegCb cb);

        /* 参数调整 */
        Error set_quality(uint8_t quality);

        /* 分辨率切换 (预留) */
        Error set_resolution(uint16_t w, uint16_t h);

        /* 保存图片 */
        Error save(const std::string& path, PhotoCb cb = nullptr);

        /* 信息 */
        const JpegCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * 录像
     *============================================================================*/

    class Recorder
    {
    public:
        Recorder();
        ~Recorder();

        Recorder(const Recorder&)            = delete;
        Recorder& operator=(const Recorder&) = delete;

        /* 控制 */
        Error start(const std::string& path, int duration_sec = 0);
        Error stop();
        bool  is_recording() const;

        /* 信息 */
        uint32_t duration_sec() const;
        uint64_t file_size() const;
        uint32_t frames() const;

        /* 内部使用 */
        void write_frame(const uint8_t* data, size_t size);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * ISP 控制
     *============================================================================*/

    class IspCtrl
    {
    public:
        IspCtrl();
        ~IspCtrl();

        IspCtrl(const IspCtrl&)            = delete;
        IspCtrl& operator=(const IspCtrl&) = delete;

        /* 生命周期 */
        Error init(const std::string& iq_dir);
        void  deinit();
        bool  is_init() const;

        /* 曝光 */
        enum class AeMode
        {
            AUTO,
            MANUAL
        };
        Error set_ae_mode(AeMode mode);
        Error set_exposure(float time_ms, float gain);
        Error lock_ae(bool lock);

        /* 白平衡 */
        enum class AwbMode
        {
            AUTO,
            MANUAL
        };
        Error set_awb_mode(AwbMode mode);
        Error set_wb_gain(float r_gain, float b_gain);
        Error lock_awb(bool lock);

        /* 图像调节 (0-255, 128=默认) */
        Error set_brightness(uint8_t val);
        Error set_contrast(uint8_t val);
        Error set_saturation(uint8_t val);
        Error set_sharpness(uint8_t val);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * RTSP 推流
     *============================================================================*/

    class RtspServer
    {
    public:
        RtspServer();
        ~RtspServer();

        RtspServer(const RtspServer&)            = delete;
        RtspServer& operator=(const RtspServer&) = delete;

        /* 控制 */
        Error start(uint16_t port, const std::string& path);
        Error stop();
        bool  is_running() const;

        /* 发送帧 (由 CameraDrv 内部调用) */
        void send_frame(const uint8_t* data, size_t size, uint64_t pts);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * 帧池
     *============================================================================*/

    class FramePool
    {
    public:
        FramePool();
        ~FramePool();

        FramePool(const FramePool&)            = delete;
        FramePool& operator=(const FramePool&) = delete;

        Error init(const MemoryCfg& cfg);
        void  deinit();

        FramePtr alloc(size_t size);

        size_t used() const;
        size_t total() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    /*============================================================================
     * 摄像头驱动 (主接口)
     *============================================================================*/

    class CameraDrv
    {
    public:
        CameraDrv();
        ~CameraDrv();

        CameraDrv(const CameraDrv&)            = delete;
        CameraDrv& operator=(const CameraDrv&) = delete;

        /*------------------------------------------------------------------------
         * 生命周期
         *------------------------------------------------------------------------*/

        Error init(const CameraCfg& cfg, std::shared_ptr<sync_context_t> sync_ctx = nullptr);
        void  deinit();
        bool  is_init() const;

        /*------------------------------------------------------------------------
         * 控制
         *------------------------------------------------------------------------*/

        Error start();
        Error stop();
        bool  is_running() const;

        /** 暂停 H264 流 */
        void pause_h264();
        /** 恢复 H264 流 */
        void resume_h264();

        /*------------------------------------------------------------------------
         * 子模块访问
         *------------------------------------------------------------------------*/

        H264Encoder& h264();
        JpegEncoder& jpeg();
        Recorder&    recorder();
        IspCtrl&     isp();
        RtspServer&  rtsp();

        /*------------------------------------------------------------------------
         * 回调
         *------------------------------------------------------------------------*/

        void set_h264_cb(H264Cb cb);
        void set_jpeg_cb(JpegCb cb);
        void set_error_cb(ErrorCb cb);

        /*------------------------------------------------------------------------
         * Smart_Glasses 扩展: WebRTC
         *------------------------------------------------------------------------*/

        void set_webrtc_cb(std::function<void(const FramePtr&)> cb);

        /*------------------------------------------------------------------------
         * Smart_Glasses 扩展: AI 识图
         *------------------------------------------------------------------------*/

        void        set_explain_url(const std::string& url, const std::string& token = "");
        std::string explain_image(const std::string& question);

        /*------------------------------------------------------------------------
         * 统计
         *------------------------------------------------------------------------*/

        Stats stats() const;
        void  reset_stats();

        /*------------------------------------------------------------------------
         * 配置
         *------------------------------------------------------------------------*/

        const CameraCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
