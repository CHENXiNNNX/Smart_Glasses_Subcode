/*
 * pipeline_interface.hpp - 采集管道接口
 */

#pragma once

#include "../../types.hpp"

#include <memory>

namespace app::media::camera
{

    class IspCtrl;

    /* 管道配置，VI/VPSS 初始化用 */
    struct PipelineConfig
    {
        uint16_t    h264_width;
        uint16_t    h264_height;
        uint16_t    jpeg_width;
        uint16_t    jpeg_height;
        bool        enable_h264;
        int         jpeg_dst_fps; /* VPSS JPEG 输出帧率，<=0 不限制 */
        bool        enable_aiisp;
        std::string aiisp_model_path;
        uint32_t    aiisp_frame_buf_cnt;
    };

    /* 采集管道接口，VI+VPSS，平台可替换 */
    class IRawPipeline
    {
    public:
        virtual ~IRawPipeline() = default;

        virtual Error init(const PipelineConfig& cfg, IspCtrl* isp) = 0;
        virtual void  deinit()                                      = 0;
    };

    std::unique_ptr<IRawPipeline> create_rk_pipeline();

} // namespace app::media::camera
