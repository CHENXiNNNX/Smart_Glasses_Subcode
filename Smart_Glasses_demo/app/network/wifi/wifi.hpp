/**
 * @file wifi.hpp
 * @brief Linux WiFi管理模块
 */

#ifndef NETWORK_WIFI_HPP
#define NETWORK_WIFI_HPP

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

            // ============================================================================
            // WiFi状态枚举
            // ============================================================================

            /**
             * @brief WiFi连接状态
             */
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

            /**
             * @brief WiFi错误类型
             */
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

            /**
             * @brief WiFi加密类型
             */
            enum class WifiSecurity
            {
                NONE = 0, // 无加密
                WEP,      // WEP加密
                WPA_PSK,  // WPA-PSK
                WPA2_PSK, // WPA2-PSK
                WPA3_PSK, // WPA3-PSK
                UNKNOWN   // 未知
            };

            // ============================================================================
            // WiFi信息结构体
            // ============================================================================

            /**
             * @brief WiFi网络信息（扫描结果）
             */
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

            /**
             * @brief 当前连接信息
             */
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

            /**
             * @brief 已保存的WiFi网络信息
             */
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

            // ============================================================================
            // 回调函数类型
            // ============================================================================

            /**
             * @brief WiFi状态变化回调
             */
            using WifiStateCallback = std::function<void(WifiState old_state, WifiState new_state)>;

            /**
             * @brief WiFi错误回调
             */
            using WifiErrorCallback =
                std::function<void(WifiError error, const std::string& message)>;

            /**
             * @brief WiFi扫描完成回调
             */
            using WifiScanCallback = std::function<void(const std::vector<WifiInfo>& networks)>;

            /**
             * @brief WiFi连接结果回调
             */
            using WifiConnectCallback =
                std::function<void(bool success, const std::string& message)>;

            /**
             * @brief WiFi重连回调
             */
            using WifiReconnectCallback =
                std::function<void(bool success, const std::string& ssid)>;

            // ============================================================================
            // wifiManager 主控制器
            // ============================================================================

            /**
             * @brief WiFi管理器配置
             */
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

            /**
             * @brief WiFi管理器
             */
            class WifiManager
            {
            public:
                /**
                 * @brief 构造函数
                 * @param config WiFi配置
                 */
                explicit WifiManager(const WifiConfig& config = WifiConfig());

                /**
                 * @brief 析构函数
                 */
                ~WifiManager();

                // ========================================================================
                // 初始化和关闭
                // ========================================================================

                /**
                 * @brief 初始化WiFi管理器
                 * @return WifiError::NONE 成功
                 */
                WifiError init();

                /**
                 * @brief 关闭WiFi管理器
                 */
                void deinit();

                // ========================================================================
                // WiFi扫描操作
                // ========================================================================

                /**
                 * @brief 扫描可用WiFi网络
                 * @param callback 扫描完成回调
                 * @param networks 输出结果
                 * @return WifiError::NONE 成功
                 */
                WifiError scanNetworks(const WifiScanCallback& callback = WifiScanCallback{},
                                       std::vector<WifiInfo>*  networks = nullptr);

                /**
                 * @brief 启用/禁用自动扫描
                 * @param enabled true-启动，false-停止
                 */
                void setAutoScan(bool enabled);

                /**
                 * @brief 检查自动扫描是否运行
                 */
                bool isAutoScanRunning() const;

                // ========================================================================
                // WiFi连接操作
                // ========================================================================

                /**
                 * @brief 连接到WiFi网络
                 * @details 支持同步和异步两种模式：
                 *          - 同步模式：callback=nullptr，阻塞等待连接完成
                 *          - 异步模式：callback非空，立即返回，结果通过callback返回
                 *
                 *          连接流程：
                 *          1. 检查当前是否已连接
                 *          2. 如果已连接且SSID相同，直接返回成功
                 *          3. 如果已连接但SSID不同，先断开旧连接
                 *          4. 如果clear_old_config_on_connect=true，删除旧配置
                 *          5. 创建新网络配置
                 *          6. 设置SSID和密码
                 *          7. 启用网络并等待连接
                 *          8. 快速检测密码错误（连续3秒DISCONNECTED）
                 *          9. 获取IP地址
                 *          10. 保存配置（如果启用）
                 *
                 * @param ssid 网络SSID
                 * @param password 密码（空字符串表示开放网络）
                 * @param callback 连接结果回调（空回调表示同步模式）
                 * @return WifiError::NONE 成功
                 */
                WifiError connect(const std::string& ssid, const std::string& password = "",
                                  const WifiConnectCallback& callback = WifiConnectCallback{});

                /**
                 * @brief 连接已保存的WiFi
                 * @details 选择最佳WiFi进行连接：
                 *          1. 优先级高的优先连接
                 *          2. 相同优先级下选择信号最强的
                 *          3. 扫描可用网络并匹配已保存列表
                 * @return WifiError::NONE 成功
                 */
                WifiError connectSavedNetwork();

                /**
                 * @brief 断开当前WiFi连接
                 * @return WifiError::NONE 成功
                 */
                WifiError disconnect();

                /**
                 * @brief 重新连接当前WiFi
                 * @return WifiError::NONE 成功
                 */
                WifiError reconnect();

                // ========================================================================
                // WiFi配置管理
                // ========================================================================

                /**
                 * @brief 保存当前连接的WiFi配置
                 * @return WifiError::NONE 成功
                 */
                WifiError saveCurrentNetwork();

                /**
                 * @brief 删除已保存的WiFi配置
                 * @param ssid 网络SSID
                 * @return WifiError::NONE 成功
                 */
                WifiError forgetNetwork(const std::string& ssid);

                /**
                 * @brief 获取已保存的网络列表
                 * @return 已保存的网络列表
                 */
                std::vector<SavedNetworkInfo> getSavedNetworks() const;

                /**
                 * @brief 检查指定SSID是否已保存
                 * @param ssid 网络SSID
                 * @return true 已保存
                 */
                bool isNetworkSaved(const std::string& ssid) const;

                /**
                 * @brief 设置网络优先级
                 * @details 用于控制自动连接顺序，值越大优先级越高
                 * @param ssid 网络SSID
                 * @param priority 优先级
                 * @return WifiError::NONE 成功
                 */
                WifiError setNetworkPriority(const std::string& ssid, int priority);

                /**
                 * @brief 启用/禁用网络自动连接
                 * @param ssid 网络SSID
                 * @param enabled true-启用，false-禁用
                 * @return WifiError::NONE 成功
                 */
                WifiError enableNetworkAutoConnect(const std::string& ssid, bool enabled);

                /**
                 * @brief 重新加载配置文件
                 * @return WifiError::NONE 成功
                 */
                WifiError reloadConfig();

                // ========================================================================
                // 状态查询
                // ========================================================================

                /**
                 * @brief 获取WiFi状态
                 */
                WifiState getState() const;

                /**
                 * @brief 检查是否已连接
                 */
                bool isConnected() const;

                /**
                 * @brief 检查网络接口是否UP
                 */
                bool isInterfaceUp() const;

                /**
                 * @brief 获取当前连接信息
                 * @param info 输出连接信息
                 * @return true 已连接且信息有效
                 */
                bool getConnectionInfo(WifiConnectionInfo& info) const;

                /**
                 * @brief 获取当前SSID
                 * @return 当前SSID（未连接时返回空字符串）
                 */
                std::string getCurrentSSID() const;

                /**
                 * @brief 获取当前IP地址
                 * @return IP地址（未连接时返回空字符串）
                 */
                std::string getIPAddress() const;

                /**
                 * @brief 获取信号强度 (0-100)
                 * @return 信号强度百分比
                 */
                int getSignalStrength() const;

                // ========================================================================
                // 自动重连控制
                // ========================================================================

                /**
                 * @brief 启用/禁用自动重连
                 * @param enabled true-启用，false-禁用
                 * @param ssid 要重连的SSID（空字符串表示使用当前SSID）
                 * @param password 密码（如果SSID已保存可省略）
                 */
                void setAutoReconnect(bool enabled, const std::string& ssid = "",
                                      const std::string& password = "");

                /**
                 * @brief 检查自动重连是否启用
                 */
                bool isAutoReconnectEnabled() const;

                // ========================================================================
                // 回调设置
                // ========================================================================

                /**
                 * @brief 设置状态变化回调
                 */
                void setStateCallback(const WifiStateCallback& callback);

                /**
                 * @brief 设置错误回调
                 */
                void setErrorCallback(const WifiErrorCallback& callback);

                /**
                 * @brief 设置重连回调
                 */
                void setReconnectCallback(const WifiReconnectCallback& callback);

                // ========================================================================
                // 统计信息
                // ========================================================================

                /**
                 * @brief WiFi统计信息
                 */
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

                /**
                 * @brief 获取统计信息
                 */
                void getStats(Stats& stats) const;

                /**
                 * @brief 重置统计信息
                 */
                void resetStats();

                // 禁止拷贝和赋值
                WifiManager(const WifiManager&)            = delete;
                WifiManager& operator=(const WifiManager&) = delete;

            private:
                class Impl;
                std::unique_ptr<Impl> pImpl_;
            };

            // ============================================================================
            // 辅助函数
            // ============================================================================

            /**
             * @brief 将WiFi状态转换为字符串
             */
            const char* wifiStateToString(WifiState state);

            /**
             * @brief 将WiFi错误转换为字符串
             */
            const char* wifiErrorToString(WifiError error);

            /**
             * @brief 将WiFi加密类型转换为字符串
             */
            const char* wifiSecurityToString(WifiSecurity security);

        } // namespace wifi
    }     // namespace network
} // namespace app

#endif // NETWORK_WIFI_HPP
