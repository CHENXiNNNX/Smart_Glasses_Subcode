/*
 * inetwork_service.hpp - 网络服务接口
 */

#pragma once

#include <string>
#include <vector>

namespace app
{

    struct WifiConnectionInfo
    {
        std::string ssid;
        int         signal_strength; // dBm 或 0-100
        std::string ip_address;
    };

    struct WifiNetwork
    {
        std::string ssid;
        int         signal_strength;
        bool        is_secured;
    };

    class INetworkService
    {
    public:
        virtual ~INetworkService() = default;

        virtual bool getConnectionInfo(WifiConnectionInfo& info) const = 0;
        virtual bool scan(std::vector<WifiNetwork>& networks)          = 0;
    };

} // namespace app
