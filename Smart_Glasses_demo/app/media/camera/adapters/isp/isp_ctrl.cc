/*
 * isp_ctrl.cc - ISP 控制
 */

#include "isp_ctrl.hpp"
#include "media/camera/platform.hpp"
#include "tool/log/log.hpp"

namespace app::media::camera
{

    using namespace tool::log;

#define TAG "Camera"

    class IspCtrl::Impl
    {
    public:
        bool init_ = false;
#if CAM_HAS_SDK
        rk_aiq_sys_ctx_t* aiq_ctx = nullptr;
#else
        void* aiq_ctx = nullptr;
#endif

        Error init(const std::string& iq_dir);
        void  deinit();
    };

    Error IspCtrl::Impl::init(const std::string& iq_dir)
    {
#if CAM_HAS_SDK
        system("RkLunch-stop.sh 2>/dev/null");
        rk_aiq_static_info_t info{};
        if (rk_aiq_uapi2_sysctl_enumStaticMetas(0, &info) != 0)
        {
            LOG_ERROR(TAG, "ISP: 枚举传感器失败");
            return Error::DEVICE_ERROR;
        }
        const char* sns = info.sensor_info.sensor_name;
        LOG_INFO(TAG, "ISP: 传感器=%s", sns);
        aiq_ctx = rk_aiq_uapi2_sysctl_init(sns, iq_dir.c_str(), nullptr, nullptr);
        if (!aiq_ctx)
        {
            LOG_ERROR(TAG, "ISP: 初始化失败");
            return Error::DEVICE_ERROR;
        }
        if (rk_aiq_uapi2_sysctl_prepare(aiq_ctx, 0, 0, RK_AIQ_WORKING_MODE_NORMAL) != 0)
        {
            LOG_ERROR(TAG, "ISP: prepare 失败");
            rk_aiq_uapi2_sysctl_deinit(aiq_ctx);
            aiq_ctx = nullptr;
            return Error::DEVICE_ERROR;
        }
        if (rk_aiq_uapi2_sysctl_start(aiq_ctx) != 0)
        {
            LOG_ERROR(TAG, "ISP: start 失败");
            rk_aiq_uapi2_sysctl_deinit(aiq_ctx);
            aiq_ctx = nullptr;
            return Error::DEVICE_ERROR;
        }
        init_ = true;
        LOG_INFO(TAG, "ISP: 就绪");
#else
        (void)iq_dir;
        init_ = true;
        LOG_WARN(TAG, "ISP: 跳过(无SDK)");
#endif
        return Error::OK;
    }

    void IspCtrl::Impl::deinit()
    {
#if CAM_HAS_SDK
        if (aiq_ctx)
        {
            rk_aiq_uapi2_sysctl_stop(aiq_ctx, false);
            rk_aiq_uapi2_sysctl_deinit(aiq_ctx);
            aiq_ctx = nullptr;
        }
#endif
        init_ = false;
    }

    IspCtrl::IspCtrl() : impl_(std::make_unique<Impl>()) {}
    IspCtrl::~IspCtrl() { deinit(); }
    Error IspCtrl::init(const std::string& iq_dir) { return impl_->init(iq_dir); }
    void  IspCtrl::deinit() { impl_->deinit(); }
    bool  IspCtrl::is_init() const { return impl_->init_; }

    Error IspCtrl::set_ae_mode(AeMode mode)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        opMode_t om = (mode == AeMode::AUTO) ? OP_AUTO : OP_MANUAL;
        return (rk_aiq_uapi2_setExpMode(impl_->aiq_ctx, om) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)mode;
        return Error::OK;
#endif
    }

    Error IspCtrl::set_exposure(float time_ms, float gain)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        paRange_t tr, gr;
        float     time_s = time_ms / 1000.0f;
        tr.min = tr.max = time_s;
        gr.min = gr.max = gain;
        if (rk_aiq_uapi2_setExpTimeRange(impl_->aiq_ctx, &tr) != XCAM_RETURN_NO_ERROR)
            return Error::DEVICE_ERROR;
        if (rk_aiq_uapi2_setExpGainRange(impl_->aiq_ctx, &gr) != XCAM_RETURN_NO_ERROR)
            return Error::DEVICE_ERROR;
        return Error::OK;
#else
        (void)time_ms;
        (void)gain;
        return Error::OK;
#endif
    }

    Error IspCtrl::lock_ae(bool lock)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        return (rk_aiq_uapi2_setAeLock(impl_->aiq_ctx, lock) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)lock;
        return Error::OK;
#endif
    }

    Error IspCtrl::set_awb_mode(AwbMode mode)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        opMode_t om = (mode == AwbMode::AUTO) ? OP_AUTO : OP_MANUAL;
        return (rk_aiq_uapi2_setWBMode(impl_->aiq_ctx, om) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)mode;
        return Error::OK;
#endif
    }

    Error IspCtrl::set_wb_gain(float r_gain, float b_gain)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        rk_aiq_wb_gain_t gain{};
        gain.rgain  = r_gain;
        gain.grgain = 1.0f;
        gain.gbgain = 1.0f;
        gain.bgain  = b_gain;
        return (rk_aiq_uapi2_setMWBGain(impl_->aiq_ctx, &gain) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)r_gain;
        (void)b_gain;
        return Error::OK;
#endif
    }

    Error IspCtrl::lock_awb(bool lock)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        XCamReturn r = lock ? rk_aiq_uapi2_lockAWB(impl_->aiq_ctx)
                            : rk_aiq_uapi2_unlockAWB(impl_->aiq_ctx);
        return (r == XCAM_RETURN_NO_ERROR) ? Error::OK : Error::DEVICE_ERROR;
#else
        (void)lock;
        return Error::OK;
#endif
    }

    Error IspCtrl::set_brightness(uint8_t val)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        return (rk_aiq_uapi2_setBrightness(impl_->aiq_ctx, val) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)val;
        return Error::OK;
#endif
    }

    Error IspCtrl::set_contrast(uint8_t val)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        return (rk_aiq_uapi2_setContrast(impl_->aiq_ctx, val) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)val;
        return Error::OK;
#endif
    }

    Error IspCtrl::set_saturation(uint8_t val)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        return (rk_aiq_uapi2_setSaturation(impl_->aiq_ctx, val) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)val;
        return Error::OK;
#endif
    }

    Error IspCtrl::set_sharpness(uint8_t val)
    {
#if CAM_HAS_SDK
        if (!impl_->aiq_ctx)
            return Error::OK;
        unsigned int level = (val <= 100) ? val : (val * 100u / 255u);
        return (rk_aiq_uapi2_setSharpness(impl_->aiq_ctx, level) == XCAM_RETURN_NO_ERROR)
                   ? Error::OK
                   : Error::DEVICE_ERROR;
#else
        (void)val;
        return Error::OK;
#endif
    }

    void* IspCtrl::aiq_ctx() const
    {
#if CAM_HAS_SDK
        return impl_->aiq_ctx;
#else
        return nullptr;
#endif
    }

} // namespace app::media::camera
