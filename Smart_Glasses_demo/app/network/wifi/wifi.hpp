/* wifi.hpp - WiFi 对外入口（薄封装 + 默认 Linux 后端） */

#pragma once

#include "wifi_ports.hpp"
#include "wifi_service.hpp"
#include "wifi_types.hpp"

#include <memory>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            /// 面向应用的 WiFi 入口。默认构造使用 `makeLinuxWifiPorts`；
            /// 测试或移植时可传入自定义 `WifiPorts`。
            class WifiManager
            {
            public:
                explicit WifiManager(WifiConfig config = {});
                WifiManager(WifiConfig config, WifiPorts ports);

                WifiManager(WifiManager&&) noexcept            = default;
                WifiManager& operator=(WifiManager&&) noexcept = default;

                ~WifiManager();

                WifiManager(const WifiManager&)            = delete;
                WifiManager& operator=(const WifiManager&) = delete;

                WifiService&       service() { return *svc_; }
                const WifiService& service() const { return *svc_; }

                WifiError init();
                void      deinit();

                WifiError scanNetworks(const WifiScanCallback& callback = WifiScanCallback{},
                                       std::vector<WifiInfo>*  networks = nullptr);

                void setAutoScan(bool enabled);
                bool isAutoScanRunning() const;

                WifiError connect(const std::string& ssid, const std::string& password = "",
                                  const WifiConnectCallback& callback = WifiConnectCallback{});

                WifiError connectSavedNetwork();
                WifiError disconnect();
                WifiError reconnect();

                WifiError                     saveCurrentNetwork();
                WifiError                     forgetNetwork(const std::string& ssid);
                std::vector<SavedNetworkInfo> getSavedNetworks() const;
                bool                          isNetworkSaved(const std::string& ssid) const;
                WifiError setNetworkPriority(const std::string& ssid, int priority);
                WifiError enableNetworkAutoConnect(const std::string& ssid, bool enabled);
                WifiError reloadConfig();

                WifiState   getState() const;
                bool        isConnected() const;
                bool        isInterfaceUp() const;
                bool        getConnectionInfo(WifiConnectionInfo& info) const;
                std::string getCurrentSSID() const;
                std::string getIPAddress() const;
                int         getSignalStrength() const;

                void setAutoReconnect(bool enabled, const std::string& ssid = "",
                                      const std::string& password = "");

                bool isAutoReconnectEnabled() const;

                void setStateCallback(const WifiStateCallback& callback);
                void setErrorCallback(const WifiErrorCallback& callback);
                void setReconnectCallback(const WifiReconnectCallback& callback);

                void getStats(WifiStats& stats) const;
                void resetStats();

            private:
                std::unique_ptr<WifiService> svc_;
            };

        } // namespace wifi
    }     // namespace network
} // namespace app
