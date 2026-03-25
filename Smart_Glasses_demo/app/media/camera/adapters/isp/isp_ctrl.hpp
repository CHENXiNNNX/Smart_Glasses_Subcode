/*
 * isp_ctrl.hpp - ISP 控制
 */

#pragma once

#include "../../types.hpp"

#include <memory>
#include <string>

namespace app::media::camera
{

    class IspCtrl
    {
    public:
        IspCtrl();
        ~IspCtrl();

        IspCtrl(const IspCtrl&)            = delete;
        IspCtrl& operator=(const IspCtrl&) = delete;

        Error init(const std::string& iq_dir);
        void  deinit();
        bool  is_init() const;

        enum class AeMode
        {
            AUTO,
            MANUAL
        };
        Error set_ae_mode(AeMode mode);
        Error set_exposure(float time_ms, float gain);
        Error lock_ae(bool lock);

        enum class AwbMode
        {
            AUTO,
            MANUAL
        };
        Error set_awb_mode(AwbMode mode);
        Error set_wb_gain(float r_gain, float b_gain);
        Error lock_awb(bool lock);

        Error set_brightness(uint8_t val);
        Error set_contrast(uint8_t val);
        Error set_saturation(uint8_t val);
        Error set_sharpness(uint8_t val);

        void* aiq_ctx() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::camera
