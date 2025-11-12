/**
 * @file wifi.hpp
 * @brief Linux WiFi管理模块 - 基于wpa_supplicant (嵌入式系统)
 * @details 特性：
 *          - 基于wpa_supplicant/wpa_cli实现
 *          - 适合嵌入式Linux系统
 *          - 自动连接已保存WiFi
 *          - 定时扫描功能
 *          - 优先级管理
 *          - 配置备份/恢复
 *          - RAII资源管理
 *          - 线程安全
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-11
 */

#ifndef NETWORK_WIFI_HPP
#define NETWORK_WIFI_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace app {
namespace network {
namespace wifi {

// ============================================================================
// WiFi状态枚举
// ============================================================================

/**
 * @brief WiFi连接状态
 */
enum class wifiState {
    UNKNOWN = 0,        // 未知状态
    DISCONNECTED,       // 已断开
    SCANNING,           // 扫描中
    CONNECTING,         // 连接中
    AUTHENTICATING,     // 认证中
    ASSOCIATED,         // 已关联（但未获取IP）
    OBTAINING_IP,       // 获取IP中
    CONNECTED,          // 已连接
    FAILED              // 失败
};

/**
 * @brief WiFi错误类型
 */
enum class wifiError {
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
enum class wifiSecurity {
    NONE = 0,           // 无加密
    WEP,                // WEP加密
    WPA_PSK,            // WPA-PSK
    WPA2_PSK,           // WPA2-PSK
    WPA3_PSK,           // WPA3-PSK
    UNKNOWN             // 未知
};

// ============================================================================
// WiFi信息结构体
// ============================================================================

/**
 * @brief WiFi网络信息（扫描结果）
 */
struct wifiInfo {
    std::string ssid;                   // 网络名称
    std::string bssid;                  // MAC地址
    wifiSecurity security;              // 加密类型
    int signal_strength;                // 信号强度 (0-100)
    int frequency;                      // 频率 (MHz)
    int channel;                        // 信道
    
    wifiInfo() 
        : security(wifiSecurity::NONE),
          signal_strength(0),
          frequency(0),
          channel(0) {}
};

/**
 * @brief 当前连接信息
 */
struct wifiConnectionInfo {
    std::string ssid;                   // 当前SSID
    std::string bssid;                  // 当前BSSID
    std::string ip_address;             // IP地址
    int signal_strength;                // 信号强度 (0-100)
    wifiSecurity security;              // 加密类型
    wifiState state;                    // 连接状态
    
    wifiConnectionInfo()
        : signal_strength(0),
          security(wifiSecurity::NONE),
          state(wifiState::DISCONNECTED) {}
};

/**
 * @brief 已保存的WiFi网络信息
 */
struct savedNetworkInfo {
    int network_id;                     // wpa_supplicant中的ID
    std::string ssid;                   // 网络SSID
    bool is_enabled_auto;               // 是否启用自动连接
    bool is_current;                    // 是否为当前连接
    int priority;                       // 优先级（值越大优先级越高）
    
    savedNetworkInfo()
        : network_id(-1),
          is_enabled_auto(true),
          is_current(false),
          priority(0) {}
};

// ============================================================================
// 回调函数类型
// ============================================================================

/**
 * @brief WiFi状态变化回调
 */
using wifiStateCallback = std::function<void(wifiState old_state, wifiState new_state)>;

/**
 * @brief WiFi错误回调
 */
using wifiErrorCallback = std::function<void(wifiError error, const std::string& message)>;

/**
 * @brief WiFi扫描完成回调
 */
using wifiScanCallback = std::function<void(const std::vector<wifiInfo>& networks)>;

/**
 * @brief WiFi连接结果回调
 */
using wifiConnectCallback = std::function<void(bool success, const std::string& message)>;

/**
 * @brief WiFi重连回调
 */
using wifiReconnectCallback = std::function<void(bool success, const std::string& ssid)>;

// ============================================================================
// wifiManager 主控制器
// ============================================================================

/**
 * @brief WiFi管理器配置
 */
struct wifiConfig {
    // 固定配置
    static constexpr const char* INTERFACE_NAME = "wlan0";
    static constexpr const char* WPA_CONF_PATH = "/etc/wpa_supplicant.conf";
    static constexpr const char* WPA_CTRL_PATH = "/var/run/wpa_supplicant";
    
    // 超时配置
    int interface_up_timeout_ms = 5000;         // 接口UP超时（5秒）
    int interface_up_check_interval_ms = 500;   // 接口UP检查间隔（500ms）
    int scan_timeout_ms = 10000;                // 扫描超时（10秒）
    int connect_timeout_ms = 30000;             // 连接超时（30秒）
    int dhcp_timeout_ms = 15000;                // DHCP超时（15秒）
    int wpa_command_timeout_ms = 5000;          // wpa_cli命令超时（5秒）
    
    // 功能开关
    bool auto_save_config = true;               // 连接成功后自动保存配置
    bool clear_old_config_on_connect = true;    // 连接时清除旧配置（确保使用新密码）
    
    // 自动扫描配置
    bool auto_scan = false;                     // 启用自动扫描
    int timed_scan_sec = 60;                    // 定时扫描间隔（秒）
    
    // 自动连接配置（初始化时）
    bool auto_connect_on_init = true;           // 初始化时自动连接已保存WiFi
    int auto_connect_max_attempts = 1;          // 自动连接最大尝试次数
    bool auto_connect_best_signal = true;       // 连接信号最强的已保存WiFi
    int auto_connect_min_signal = 30;           // 自动连接最小信号强度（百分比）
    
    // 自动重连配置
    bool enable_auto_reconnect = false;         // 启用自动重连
    int reconnect_interval_sec = 10;            // 重连间隔（秒）
    int reconnect_max_attempts = 5;             // 最大重连次数（0表示无限）
    int reconnect_delay_sec = 3;                // 重连前等待时间（秒）
    
    // 调试选项
    bool enable_detailed_logging = false;       // 详细日志（DEBUG级别）
};

/**
 * @brief Linux WiFi管理器 (基于wpa_supplicant)
 * @details 使用wpa_cli和udhcpc实现，适合嵌入式系统
 * 
 * 主要功能：
 * - 自动检测并打开网络接口
 * - 扫描、连接、断开WiFi网络
 * - 初始化时自动连接已保存WiFi
 * - 定时扫描功能
 * - 保存/删除/管理WiFi配置
 * - 优先级管理
 * - 配置备份/恢复
 * - 自动重连机制（可选）
 * - 完整的错误处理和日志记录
 */
class wifiManager {
public:
    /**
     * @brief 构造函数
     * @param config WiFi配置
     */
    explicit wifiManager(const wifiConfig& config = wifiConfig());
    
    /**
     * @brief 析构函数（自动清理资源）
     */
    ~wifiManager();
    
    // ========================================================================
    // 初始化和关闭
    // ========================================================================
    
    /**
     * @brief 初始化WiFi管理器
     * @details 执行以下操作：
     *          1. 检查wpa_supplicant和wpa_cli是否可用
     *          2. 检查wlan0接口是否存在
     *          3. 检查接口是否UP，如果未UP则自动打开并等待3秒
     *          4. 检查wpa_supplicant是否运行，如果未运行则自动启动
     *          5. 验证wpa_cli连接
     *          6. 获取当前状态
     *          7. 如果启用auto_connect_on_init，自动连接已保存WiFi
     * 
     * @return wifiError::NONE 成功
     */
    wifiError initialize();
    
    /**
     * @brief 关闭WiFi管理器
     * @details 停止所有线程，断开连接，清理资源
     */
    void shutdown();
    
    // ========================================================================
    // WiFi扫描操作
    // ========================================================================
    
    /**
     * @brief 扫描可用WiFi网络
     * @details 支持同步和异步两种模式：
     *          - 同步模式：callback=nullptr，networks非空，阻塞等待扫描完成
     *          - 异步模式：callback非空，立即返回，结果通过callback返回
     * 
     * @param callback 扫描完成回调（nullptr表示同步模式）
     * @param networks 同步模式输出结果（按信号强度降序排列）
     * @return wifiError::NONE 成功
     */
    wifiError scanNetworks(wifiScanCallback callback = nullptr,
                           std::vector<wifiInfo>* networks = nullptr);
    
    /**
     * @brief 启用/禁用自动扫描
     * @details 后台线程定时扫描WiFi网络
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
     * @param callback 连接结果回调（nullptr表示同步模式）
     * @return wifiError::NONE 成功
     */
    wifiError connect(const std::string& ssid,
                      const std::string& password = "",
                      wifiConnectCallback callback = nullptr);
    
    /**
     * @brief 连接已保存的WiFi
     * @details 选择最佳WiFi进行连接：
     *          1. 优先级高的优先连接
     *          2. 相同优先级下选择信号最强的
     *          3. 扫描可用网络并匹配已保存列表
     * @return wifiError::NONE 成功
     */
    wifiError connectSavedNetwork();
    
    /**
     * @brief 断开当前WiFi连接
     * @details 断开连接并释放IP地址
     * @return wifiError::NONE 成功
     */
    wifiError disconnect();
    
    /**
     * @brief 重新连接当前WiFi
     * @details 断开并重新连接当前已保存的网络
     * @return wifiError::NONE 成功
     */
    wifiError reconnect();
    
    // ========================================================================
    // WiFi配置管理
    // ========================================================================
    
    /**
     * @brief 保存当前连接的WiFi配置
     * @details 调用wpa_cli save_config保存到配置文件
     * @return wifiError::NONE 成功
     */
    wifiError saveCurrentNetwork();
    
    /**
     * @brief 删除已保存的WiFi配置
     * @param ssid 网络SSID
     * @return wifiError::NONE 成功
     */
    wifiError forgetNetwork(const std::string& ssid);
    
    /**
     * @brief 获取已保存的网络列表
     * @details 从wpa_cli list_networks和配置文件中读取
     * @return 已保存的网络列表
     */
    std::vector<savedNetworkInfo> getSavedNetworks() const;
    
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
     * @param priority 优先级（0-999）
     * @return wifiError::NONE 成功
     */
    wifiError setNetworkPriority(const std::string& ssid, int priority);
    
    /**
     * @brief 启用/禁用网络自动连接
     * @param ssid 网络SSID
     * @param enabled true-启用，false-禁用
     * @return wifiError::NONE 成功
     */
    wifiError enableNetworkAutoConnect(const std::string& ssid, bool enabled);
    
    /**
     * @brief 重新加载配置文件
     * @details 从/etc/wpa_supplicant.conf重新加载
     * @return wifiError::NONE 成功
     */
    wifiError reloadConfig();
    
    // ========================================================================
    // 状态查询
    // ========================================================================
    
    /**
     * @brief 获取WiFi状态
     */
    wifiState getState() const;
    
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
    bool getConnectionInfo(wifiConnectionInfo& info) const;
    
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
     * @details 当WiFi断开时，自动尝试重连指定的WiFi
     * @param enabled true-启用，false-禁用
     * @param ssid 要重连的SSID（空字符串表示使用当前SSID）
     * @param password 密码（如果SSID已保存可省略）
     */
    void setAutoReconnect(bool enabled,
                          const std::string& ssid = "", 
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
    void setStateCallback(wifiStateCallback callback);
    
    /**
     * @brief 设置错误回调
     */
    void setErrorCallback(wifiErrorCallback callback);
    
    /**
     * @brief 设置重连回调
     */
    void setReconnectCallback(wifiReconnectCallback callback);
    
    // ========================================================================
    // 统计信息
    // ========================================================================
    
    /**
     * @brief WiFi统计信息
     */
    struct Stats {
        uint64_t scans_performed;           // 扫描次数
        uint64_t connections_attempted;     // 连接尝试次数
        uint64_t connections_successful;    // 连接成功次数
        uint64_t disconnections;            // 断开次数
        uint64_t reconnects;                // 重连次数（自动+手动）
        uint64_t auto_reconnects;           // 自动重连次数
        uint64_t auto_connects;             // 自动连接次数（初始化时）
        uint64_t errors;                    // 错误次数
        uint64_t password_errors;           // 密码错误次数
        uint64_t interface_up_count;        // 接口UP次数
        uint64_t config_saves;              // 配置保存次数
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
    wifiManager(const wifiManager&) = delete;
    wifiManager& operator=(const wifiManager&) = delete;

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
const char* wifiStateToString(wifiState state);

/**
 * @brief 将WiFi错误转换为字符串
 */
const char* wifiErrorToString(wifiError error);

/**
 * @brief 将WiFi加密类型转换为字符串
 */
const char* wifiSecurityToString(wifiSecurity security);

} // namespace wifi
} // namespace network
} // namespace app

#endif // NETWORK_WIFI_HPP
