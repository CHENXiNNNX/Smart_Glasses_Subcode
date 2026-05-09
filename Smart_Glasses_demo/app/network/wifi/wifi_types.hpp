/* wifi_types.hpp - WiFi 领域类型与配置 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            enum class WifiState
            {
                UNKNOWN = 0,
                DISCONNECTED,
                SCANNING,
                CONNECTING,
                AUTHENTICATING,
                ASSOCIATED,
                OBTAINING_IP,
                CONNECTED,
                FAILED
            };

            enum class WifiError
            {
                NONE = 0,
                INITIALIZATION_FAILED,
                INTERFACE_NOT_FOUND,
                INTERFACE_NOT_UP,
                WPA_SUPPLICANT_NOT_FOUND,
                WPA_SUPPLICANT_NOT_RUNNING,
                SCAN_FAILED,
                CONNECTION_FAILED,
                DISCONNECTION_FAILED,
                AUTHENTICATION_FAILED,
                PASSWORD_INCORRECT,
                TIMEOUT,
                NETWORK_NOT_FOUND,
                NETWORK_WEAK_SIGNAL,
                DHCP_FAILED,
                ALREADY_CONNECTED,
                ALREADY_SAVED,
                CONFIG_FILE_ERROR,
                UNKNOWN
            };

            enum class WifiSecurity
            {
                NONE = 0,
                WEP,
                WPA_PSK,
                WPA2_PSK,
                WPA3_PSK,
                UNKNOWN
            };

            struct WifiInfo
            {
                std::string  ssid;
                std::string  bssid;
                WifiSecurity security        = WifiSecurity::NONE;
                int          signal_strength = 0;
                int          frequency       = 0;
                int          channel         = 0;
            };

            struct WifiConnectionInfo
            {
                std::string  ssid;
                std::string  bssid;
                std::string  ip_address;
                int          signal_strength = 0;
                WifiSecurity security        = WifiSecurity::NONE;
                WifiState    state           = WifiState::DISCONNECTED;
            };

            struct SavedNetworkInfo
            {
                int         network_id = -1;
                std::string ssid;
                bool        is_enabled_auto = true;
                bool        is_current      = false;
                int         priority        = 0;
            };

            using WifiStateCallback = std::function<void(WifiState old_state, WifiState new_state)>;
            using WifiErrorCallback =
                std::function<void(WifiError error, const std::string& message)>;
            using WifiScanCallback = std::function<void(const std::vector<WifiInfo>& networks)>;
            using WifiConnectCallback =
                std::function<void(bool success, const std::string& message)>;
            using WifiReconnectCallback =
                std::function<void(bool success, const std::string& ssid)>;

            struct WifiConfig
            {
                std::string interface_name = "wlan0";
                std::string wpa_conf_path  = "/etc/wpa_supplicant.conf";
                /// 控制套接字目录，当前启动命令与 stock wpa_supplicant 一致时可不单独使用
                std::string wpa_ctrl_dir = "/var/run/wpa_supplicant";

                static constexpr int DEFAULT_INTERFACE_UP_TIMEOUT_MS        = 5000;
                static constexpr int DEFAULT_INTERFACE_UP_CHECK_INTERVAL_MS = 500;
                static constexpr int DEFAULT_SCAN_TIMEOUT_MS                = 10000;
                static constexpr int DEFAULT_CONNECT_TIMEOUT_MS             = 30000;
                static constexpr int DEFAULT_DHCP_TIMEOUT_MS                = 15000;
                static constexpr int DEFAULT_WPA_COMMAND_TIMEOUT_MS         = 5000;
                static constexpr int DEFAULT_TIMED_SCAN_SEC                 = 60;
                static constexpr int DEFAULT_AUTO_CONNECT_MAX_ATTEMPTS      = 1;
                static constexpr int DEFAULT_AUTO_CONNECT_MIN_SIGNAL        = 30;
                static constexpr int DEFAULT_RECONNECT_INTERVAL_SEC         = 10;
                static constexpr int DEFAULT_RECONNECT_MAX_ATTEMPTS         = 5;
                static constexpr int DEFAULT_RECONNECT_DELAY_SEC            = 3;

                int interface_up_timeout_ms        = DEFAULT_INTERFACE_UP_TIMEOUT_MS;
                int interface_up_check_interval_ms = DEFAULT_INTERFACE_UP_CHECK_INTERVAL_MS;
                int scan_timeout_ms                = DEFAULT_SCAN_TIMEOUT_MS;
                int connect_timeout_ms             = DEFAULT_CONNECT_TIMEOUT_MS;
                int dhcp_timeout_ms                = DEFAULT_DHCP_TIMEOUT_MS;
                int wpa_command_timeout_ms         = DEFAULT_WPA_COMMAND_TIMEOUT_MS;

                bool auto_save_config            = true;
                bool clear_old_config_on_connect = true;

                bool auto_scan      = false;
                int  timed_scan_sec = DEFAULT_TIMED_SCAN_SEC;

                bool auto_connect_on_init      = true;
                int  auto_connect_max_attempts = DEFAULT_AUTO_CONNECT_MAX_ATTEMPTS;
                bool auto_connect_best_signal  = true;
                int  auto_connect_min_signal   = DEFAULT_AUTO_CONNECT_MIN_SIGNAL;

                bool enable_auto_reconnect  = false;
                int  reconnect_interval_sec = DEFAULT_RECONNECT_INTERVAL_SEC;
                int  reconnect_max_attempts = DEFAULT_RECONNECT_MAX_ATTEMPTS;
                int  reconnect_delay_sec    = DEFAULT_RECONNECT_DELAY_SEC;

                bool enable_detailed_logging = false;
            };

            struct WifiStats
            {
                uint64_t scans_performed;
                uint64_t connections_attempted;
                uint64_t connections_successful;
                uint64_t disconnections;
                uint64_t reconnects;
                uint64_t auto_reconnects;
                uint64_t auto_connects;
                uint64_t errors;
                uint64_t password_errors;
                uint64_t interface_up_count;
                uint64_t config_saves;
            };

            const char* wifiStateToString(WifiState state);
            const char* wifiErrorToString(WifiError error);
            const char* wifiSecurityToString(WifiSecurity security);

        } // namespace wifi
    }     // namespace network
} // namespace app
