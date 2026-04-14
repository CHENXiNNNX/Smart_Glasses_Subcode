/* wifi_service.hpp - WiFi 用例编排（与具体系统调用解耦） */

#pragma once

#include "wifi_ports.hpp"
#include "wifi_types.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            class WifiService
            {
            public:
                WifiService(WifiConfig config, WifiPorts ports);
                ~WifiService();

                WifiService(const WifiService&)            = delete;
                WifiService& operator=(const WifiService&) = delete;

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

                const WifiConfig& config() const { return config_; }

            private:
                WifiError checkPrerequisites();
                WifiError ensureInterfaceUp();
                WifiError ensureWpaSupplicantRunning();

                WifiError scanNetworksInternal(std::vector<WifiInfo>& networks);
                WifiError connectInternal(const std::string& ssid, const std::string& password);

                int         findNetworkIdBySSID(const std::string& ssid) const;
                std::string getCurrentSSIDInternal() const;
                std::string getIPAddressInternal() const;
                int         getSignalStrengthInternal() const;

                void updateState(WifiState new_state);
                void notifyError(WifiError error, const std::string& message);

                void autoScanThread();
                void autoReconnectThread();

                WifiConfig config_;

                std::shared_ptr<IShellRunner> shell_;
                std::shared_ptr<IWpaControl>  wpa_;
                std::shared_ptr<ILinkLayer>   link_;
                std::shared_ptr<IDhcpClient>  dhcp_;

                std::atomic<WifiState> current_state_;
                std::atomic<bool>      initialized_;
                std::atomic<bool>      shutdown_requested_;

                std::atomic<bool>            auto_scan_running_;
                std::unique_ptr<std::thread> auto_scan_thread_;

                std::atomic<bool>            auto_reconnect_enabled_;
                std::string                  reconnect_ssid_;
                std::string                  reconnect_password_;
                std::unique_ptr<std::thread> auto_reconnect_thread_;
                int                          reconnect_attempts_;

                WifiStateCallback     state_callback_;
                WifiErrorCallback     error_callback_;
                WifiReconnectCallback reconnect_callback_;

                mutable std::mutex stats_mutex_;
                WifiStats          stats_;

                mutable std::mutex      mutex_;
                std::condition_variable cv_;
            };

        } // namespace wifi
    }     // namespace network
} // namespace app
