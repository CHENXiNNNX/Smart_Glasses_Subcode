/*
 * network_service_impl.hpp - 网络服务实现
 */

#pragma once

#include "../interfaces/inetwork_service.hpp"

namespace app::network::wifi
{
    class WifiManager;
}

namespace app
{

    class NetworkServiceImpl : public INetworkService
    {
    public:
        explicit NetworkServiceImpl(network::wifi::WifiManager* mgr);

        bool getConnectionInfo(WifiConnectionInfo& info) const override;
        bool scan(std::vector<WifiNetwork>& networks) override;

    private:
        network::wifi::WifiManager* mgr_;
    };

} // namespace app
