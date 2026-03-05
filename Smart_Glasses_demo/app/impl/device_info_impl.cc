/*
 * device_info_impl.cc - 设备信息
 */

#include "device_info_impl.hpp"
#include "../tool/mac/mac.hpp"
#include "../tool/uuid/uuid.hpp"

namespace app
{

    DeviceInfoImpl::DeviceInfoImpl(const std::string& config_file_path)
        : config_file_path_(config_file_path)
    {
    }

    std::string DeviceInfoImpl::getDeviceId() const
    {
        if (!device_id_loaded_)
        {
            cached_device_id_ = app::tool::mac::getWirelessMacAddress();
            device_id_loaded_ = true;
        }
        return cached_device_id_;
    }

    std::string DeviceInfoImpl::getClientId() const
    {
        if (!client_id_loaded_)
        {
            cached_client_id_ = app::tool::uuid::generateUUID(config_file_path_);
            client_id_loaded_ = true;
        }
        return cached_client_id_;
    }

} // namespace app
