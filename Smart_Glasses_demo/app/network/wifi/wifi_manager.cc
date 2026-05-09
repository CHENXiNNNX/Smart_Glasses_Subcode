/* wifi_manager.cc */

#include "wifi.hpp"

namespace app
{
    namespace network
    {
        namespace wifi
        {

            WifiManager::WifiManager(WifiConfig config)
            {
                WifiPorts ports = makeLinuxWifiPorts(config);
                svc_ = std::make_unique<WifiService>(std::move(config), std::move(ports));
            }

            WifiManager::WifiManager(WifiConfig config, WifiPorts ports)
                : svc_(std::make_unique<WifiService>(std::move(config), std::move(ports)))
            {
            }

            WifiManager::~WifiManager() = default;

            WifiError WifiManager::init()
            {
                return svc_->init();
            }

            void WifiManager::deinit()
            {
                svc_->deinit();
            }

            WifiError WifiManager::scanNetworks(const WifiScanCallback& callback,
                                                std::vector<WifiInfo>*  networks)
            {
                return svc_->scanNetworks(callback, networks);
            }

            void WifiManager::setAutoScan(bool enabled)
            {
                svc_->setAutoScan(enabled);
            }

            bool WifiManager::isAutoScanRunning() const
            {
                return svc_->isAutoScanRunning();
            }

            WifiError WifiManager::connect(const std::string& ssid, const std::string& password,
                                           const WifiConnectCallback& callback)
            {
                return svc_->connect(ssid, password, callback);
            }

            WifiError WifiManager::connectSavedNetwork()
            {
                return svc_->connectSavedNetwork();
            }

            WifiError WifiManager::disconnect()
            {
                return svc_->disconnect();
            }

            WifiError WifiManager::reconnect()
            {
                return svc_->reconnect();
            }

            WifiError WifiManager::saveCurrentNetwork()
            {
                return svc_->saveCurrentNetwork();
            }

            WifiError WifiManager::forgetNetwork(const std::string& ssid)
            {
                return svc_->forgetNetwork(ssid);
            }

            std::vector<SavedNetworkInfo> WifiManager::getSavedNetworks() const
            {
                return svc_->getSavedNetworks();
            }

            bool WifiManager::isNetworkSaved(const std::string& ssid) const
            {
                return svc_->isNetworkSaved(ssid);
            }

            WifiError WifiManager::setNetworkPriority(const std::string& ssid, int priority)
            {
                return svc_->setNetworkPriority(ssid, priority);
            }

            WifiError WifiManager::enableNetworkAutoConnect(const std::string& ssid, bool enabled)
            {
                return svc_->enableNetworkAutoConnect(ssid, enabled);
            }

            WifiError WifiManager::reloadConfig()
            {
                return svc_->reloadConfig();
            }

            WifiState WifiManager::getState() const
            {
                return svc_->getState();
            }

            bool WifiManager::isConnected() const
            {
                return svc_->isConnected();
            }

            bool WifiManager::isInterfaceUp() const
            {
                return svc_->isInterfaceUp();
            }

            bool WifiManager::getConnectionInfo(WifiConnectionInfo& info) const
            {
                return svc_->getConnectionInfo(info);
            }

            std::string WifiManager::getCurrentSSID() const
            {
                return svc_->getCurrentSSID();
            }

            std::string WifiManager::getIPAddress() const
            {
                return svc_->getIPAddress();
            }

            int WifiManager::getSignalStrength() const
            {
                return svc_->getSignalStrength();
            }

            void WifiManager::setAutoReconnect(bool enabled, const std::string& ssid,
                                               const std::string& password)
            {
                svc_->setAutoReconnect(enabled, ssid, password);
            }

            bool WifiManager::isAutoReconnectEnabled() const
            {
                return svc_->isAutoReconnectEnabled();
            }

            void WifiManager::setStateCallback(const WifiStateCallback& callback)
            {
                svc_->setStateCallback(callback);
            }

            void WifiManager::setErrorCallback(const WifiErrorCallback& callback)
            {
                svc_->setErrorCallback(callback);
            }

            void WifiManager::setReconnectCallback(const WifiReconnectCallback& callback)
            {
                svc_->setReconnectCallback(callback);
            }

            void WifiManager::getStats(WifiStats& stats) const
            {
                svc_->getStats(stats);
            }

            void WifiManager::resetStats()
            {
                svc_->resetStats();
            }

            const char* wifiStateToString(WifiState state)
            {
                switch (state)
                {
                case WifiState::UNKNOWN:
                    return "UNKNOWN";
                case WifiState::DISCONNECTED:
                    return "DISCONNECTED";
                case WifiState::SCANNING:
                    return "SCANNING";
                case WifiState::CONNECTING:
                    return "CONNECTING";
                case WifiState::AUTHENTICATING:
                    return "AUTHENTICATING";
                case WifiState::ASSOCIATED:
                    return "ASSOCIATED";
                case WifiState::OBTAINING_IP:
                    return "OBTAINING_IP";
                case WifiState::CONNECTED:
                    return "CONNECTED";
                case WifiState::FAILED:
                    return "FAILED";
                default:
                    return "INVALID";
                }
            }

            const char* wifiErrorToString(WifiError error)
            {
                switch (error)
                {
                case WifiError::NONE:
                    return "NONE";
                case WifiError::INITIALIZATION_FAILED:
                    return "INITIALIZATION_FAILED";
                case WifiError::INTERFACE_NOT_FOUND:
                    return "INTERFACE_NOT_FOUND";
                case WifiError::INTERFACE_NOT_UP:
                    return "INTERFACE_NOT_UP";
                case WifiError::WPA_SUPPLICANT_NOT_FOUND:
                    return "WPA_SUPPLICANT_NOT_FOUND";
                case WifiError::WPA_SUPPLICANT_NOT_RUNNING:
                    return "WPA_SUPPLICANT_NOT_RUNNING";
                case WifiError::SCAN_FAILED:
                    return "SCAN_FAILED";
                case WifiError::CONNECTION_FAILED:
                    return "CONNECTION_FAILED";
                case WifiError::DISCONNECTION_FAILED:
                    return "DISCONNECTION_FAILED";
                case WifiError::AUTHENTICATION_FAILED:
                    return "AUTHENTICATION_FAILED";
                case WifiError::PASSWORD_INCORRECT:
                    return "PASSWORD_INCORRECT";
                case WifiError::TIMEOUT:
                    return "TIMEOUT";
                case WifiError::NETWORK_NOT_FOUND:
                    return "NETWORK_NOT_FOUND";
                case WifiError::NETWORK_WEAK_SIGNAL:
                    return "NETWORK_WEAK_SIGNAL";
                case WifiError::DHCP_FAILED:
                    return "DHCP_FAILED";
                case WifiError::ALREADY_CONNECTED:
                    return "ALREADY_CONNECTED";
                case WifiError::ALREADY_SAVED:
                    return "ALREADY_SAVED";
                case WifiError::CONFIG_FILE_ERROR:
                    return "CONFIG_FILE_ERROR";
                case WifiError::UNKNOWN:
                    return "UNKNOWN";
                default:
                    return "INVALID";
                }
            }

            const char* wifiSecurityToString(WifiSecurity security)
            {
                switch (security)
                {
                case WifiSecurity::NONE:
                    return "NONE";
                case WifiSecurity::WEP:
                    return "WEP";
                case WifiSecurity::WPA_PSK:
                    return "WPA-PSK";
                case WifiSecurity::WPA2_PSK:
                    return "WPA2-PSK";
                case WifiSecurity::WPA3_PSK:
                    return "WPA3-PSK";
                case WifiSecurity::UNKNOWN:
                    return "UNKNOWN";
                default:
                    return "INVALID";
                }
            }

        } // namespace wifi
    }     // namespace network
} // namespace app
