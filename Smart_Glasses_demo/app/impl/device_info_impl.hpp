/*
 * device_info_impl.hpp - 设备信息实现
 */

#pragma once

#include "../interfaces/idevice_info.hpp"
#include <string>

namespace app
{

    class DeviceInfoImpl : public IDeviceInfo
    {
    public:
        explicit DeviceInfoImpl(const std::string& config_file_path = "./system_para.conf");

        std::string getDeviceId() const override;
        std::string getClientId() const override;

    private:
        std::string         config_file_path_;
        mutable std::string cached_device_id_;
        mutable std::string cached_client_id_;
        mutable bool        device_id_loaded_ = false;
        mutable bool        client_id_loaded_ = false;
    };

} // namespace app
