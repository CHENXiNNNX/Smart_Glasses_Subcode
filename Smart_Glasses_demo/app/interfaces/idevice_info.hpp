/*
 * idevice_info.hpp - 设备信息接口
 */

#pragma once

#include <string>

namespace app
{

    class IDeviceInfo
    {
    public:
        virtual ~IDeviceInfo() = default;

        virtual std::string getDeviceId() const = 0; // MAC 地址
        virtual std::string getClientId() const = 0; // UUID
    };

} // namespace app
