/* wifi.hpp - WiFi管理 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            enum class WifiState
            {
                UNKNOWN = 0,    // 未知状态
                DISCONNECTED,   // 已断开
                SCANNING,       // 扫描中
                CONNECTING,     // 连接中
                AUTHENTICATING, // 认证中
                ASSOCIATED,     // 已关联（但未获取IP）
                OBTAINING_IP,   // 获取IP中
                CONNECTED,      // 已连接
                FAILED          // 失败
            };

            enum class WifiError
            {
                NONE = 0,
                INITIALIZATION_FAILED,      // 初始化失败
                INTERFACE_NOT_FOUND,        // 接口未找到
                INTERFACE_NOT_UP,           // 接口未UP
                WPA_SUPPLICANT_NOT_FOUND,   // wpa_supplicant未找到
                WPA_SUPPLICANT_NOT_RUNNING, // wpa_supplicant未运行
                SCAN_FAILED,                // 扫描失败
                CONNECTION_FAILED,          // 连接失败
                DISCONNECTION_FAILED,       // 断开失败
                AUTHENTICATION_FAILED,      // 认证失败
                PASSWORD_INCORRECT,         // 密码错误
                TIMEOUT,                    // 超时
                NETWORK_NOT_FOUND,          // 网络未找到
                NETWORK_WEAK_SIGNAL,        // 信号太弱
                DHCP_FAILED,                // DHCP失败
                ALREADY_CONNECTED,          // 已经连接
                ALREADY_SAVED,              // WiFi已保存
                CONFIG_FILE_ERROR,          // 配置文件错误
                UNKNOWN                     // 未知错误
            };

            enum class WifiSecurity
            {
                NONE = 0, // 无加密
                WEP,      // WEP加密
                WPA_PSK,  // WPA-PSK
                WPA2_PSK, // WPA2-PSK
                WPA3_PSK, // WPA3-PSK
                UNKNOWN   // 未知
            };

            struct WifiInfo
            {
                std::string  ssid;            // 网络名称
                std::string  bssid;           // MAC地址
                WifiSecurity security;        // 加密类型
                int          signal_strength; // 信号强度 (0-100)
                int          frequency;       // 频率 (MHz)
                int          channel;         // 信道

                WifiInfo()
                    : security(WifiSecurity::NONE), signal_strength(0), frequency(0), channel(0)
                {
                }
            };

            struct WifiConnectionInfo
            {
                std::string  ssid;            // 当前SSID
                std::string  bssid;           // 当前BSSID
                std::string  ip_address;      // IP地址
                int          signal_strength; // 信号强度 (0-100)
                WifiSecurity security;        // 加密类型
                WifiState    state;           // 连接状态

                WifiConnectionInfo()
                    : signal_strength(0), security(WifiSecurity::NONE),
                      state(WifiState::DISCONNECTED)
                {
                }
            };

            struct SavedNetworkInfo
            {
                int         network_id;      // wpa_supplicant中的ID
                std::string ssid;            // 网络SSID
                bool        is_enabled_auto; // 是否启用自动连接
                bool        is_current;      // 是否为当前连接
                int         priority;        // 优先级（值越大优先级越高）

                SavedNetworkInfo()
                    : network_id(-1), is_enabled_auto(true), is_current(false), priority(0)
                {
                }
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
                // 固定配置
                static constexpr const char* INTERFACE_NAME = "wlan0";
                static constexpr const char* WPA_CONF_PATH  = "/etc/wpa_supplicant.conf";
                static constexpr const char* WPA_CTRL_PATH  = "/var/run/wpa_supplicant";

                // 超时配置常量
                static constexpr int DEFAULT_INTERFACE_UP_TIMEOUT_MS        = 5000;
                static constexpr int DEFAULT_INTERFACE_UP_CHECK_INTERVAL_MS = 500;
                static constexpr int DEFAULT_SCAN_TIMEOUT_MS                = 10000;
                static constexpr int DEFAULT_CONNECT_TIMEOUT_MS             = 30000;
                static constexpr int DEFAULT_DHCP_TIMEOUT_MS                = 15000;
                static constexpr int DEFAULT_WPA_COMMAND_TIMEOUT_MS         = 5000;

                // 自动扫描配置常量
                static constexpr int DEFAULT_TIMED_SCAN_SEC = 60;

                // 自动连接配置常量
                static constexpr int DEFAULT_AUTO_CONNECT_MAX_ATTEMPTS = 1;
                static constexpr int DEFAULT_AUTO_CONNECT_MIN_SIGNAL   = 30;

                // 自动重连配置常量
                static constexpr int DEFAULT_RECONNECT_INTERVAL_SEC = 10;
                static constexpr int DEFAULT_RECONNECT_MAX_ATTEMPTS = 5;
                static constexpr int DEFAULT_RECONNECT_DELAY_SEC    = 3;

                // 超时配置
                int interface_up_timeout_ms        = DEFAULT_INTERFACE_UP_TIMEOUT_MS;
                int interface_up_check_interval_ms = DEFAULT_INTERFACE_UP_CHECK_INTERVAL_MS;
                int scan_timeout_ms                = DEFAULT_SCAN_TIMEOUT_MS;
                int connect_timeout_ms             = DEFAULT_CONNECT_TIMEOUT_MS;
                int dhcp_timeout_ms                = DEFAULT_DHCP_TIMEOUT_MS;
                int wpa_command_timeout_ms         = DEFAULT_WPA_COMMAND_TIMEOUT_MS;

                // 功能开关
                bool auto_save_config            = true;
                bool clear_old_config_on_connect = true;

                // 自动扫描配置
                bool auto_scan      = false;
                int  timed_scan_sec = DEFAULT_TIMED_SCAN_SEC;

                // 自动连接配置
                bool auto_connect_on_init      = true;
                int  auto_connect_max_attempts = DEFAULT_AUTO_CONNECT_MAX_ATTEMPTS;
                bool auto_connect_best_signal  = true;
                int  auto_connect_min_signal   = DEFAULT_AUTO_CONNECT_MIN_SIGNAL;

                // 自动重连配置
                bool enable_auto_reconnect  = false;
                int  reconnect_interval_sec = DEFAULT_RECONNECT_INTERVAL_SEC;
                int  reconnect_max_attempts = DEFAULT_RECONNECT_MAX_ATTEMPTS;
                int  reconnect_delay_sec    = DEFAULT_RECONNECT_DELAY_SEC;

                // 调试选项
                bool enable_detailed_logging = false;
            };

            class WifiManager
            {
            public:
                explicit WifiManager(const WifiConfig& config = WifiConfig());
                ~WifiManager();

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

                struct Stats
                {
                    uint64_t scans_performed;        // 扫描次数
                    uint64_t connections_attempted;  // 连接尝试次数
                    uint64_t connections_successful; // 连接成功次数
                    uint64_t disconnections;         // 断开次数
                    uint64_t reconnects;
                    uint64_t auto_reconnects;
                    uint64_t auto_connects;
                    uint64_t errors;             // 错误次数
                    uint64_t password_errors;    // 密码错误次数
                    uint64_t interface_up_count; // 接口UP次数
                    uint64_t config_saves;       // 配置保存次数
                };

                void getStats(Stats& stats) const;
                void resetStats();

                // 禁止拷贝和赋值
                WifiManager(const WifiManager&)            = delete;
                WifiManager& operator=(const WifiManager&) = delete;

            private:
                class Impl;
                std::unique_ptr<Impl> pImpl_;
            };

            const char* wifiStateToString(WifiState state);
            const char* wifiErrorToString(WifiError error);
            const char* wifiSecurityToString(WifiSecurity security);

        } // namespace wifi
    }     // namespace network
} // namespace app
