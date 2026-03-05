/*
 * network_service_impl.cc - 网络服务
 */

#include "network_service_impl.hpp"
#include "../network/wifi/wifi.hpp"

namespace app
{

    NetworkServiceImpl::NetworkServiceImpl(network::wifi::WifiManager* mgr) : mgr_(mgr) {}

    bool NetworkServiceImpl::getConnectionInfo(WifiConnectionInfo& info) const
    {
        if (!mgr_)
            return false;

        network::wifi::WifiConnectionInfo conn;
        bool                              ok = mgr_->getConnectionInfo(conn);
        if (!ok)
            return false;

        info.ssid            = conn.ssid;
        info.signal_strength = conn.signal_strength;
        info.ip_address      = conn.ip_address;
        return true;
    }

    bool NetworkServiceImpl::scan(std::vector<WifiNetwork>& networks)
    {
        if (!mgr_)
            return false;

        std::vector<network::wifi::WifiInfo> infos;
        auto                                 err = mgr_->scanNetworks({}, &infos);
        if (err != network::wifi::WifiError::NONE)
            return false;

        networks.clear();
        for (const auto& w : infos)
        {
            WifiNetwork n;
            n.ssid            = w.ssid;
            n.signal_strength = w.signal_strength;
            n.is_secured      = (w.security != network::wifi::WifiSecurity::NONE);
            networks.push_back(n);
        }
        return true;
    }

} // namespace app
