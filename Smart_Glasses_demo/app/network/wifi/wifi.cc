/**
 * @file wifi.cc
 * @brief Linux WiFi管理模块实现 - 基于wpa_supplicant
 * @author Smart_Glasses Team
 * @date 2025-01-11
 */

#include "wifi.hpp"
#include "../../tool/log/log.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <map>

namespace app {
namespace network {
namespace wifi {

using namespace app::tool::log;

// ============================================================================
// 常量定义
// ============================================================================

static constexpr const char* LOG_TAG = "WiFi";
static constexpr int MAX_COMMAND_OUTPUT = 65536;  // 64KB

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 去除字符串首尾空白
 */
static std::string trim(const std::string& str) {
    const char* whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

/**
 * @brief 执行shell命令并获取输出
 */
static std::string executeCommand(const std::string& cmd) {
    char buffer[4096];
    std::string result;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOG_ERROR(LOG_TAG, "Failed to execute command: %s", cmd.c_str());
        return "";
    }
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    pclose(pipe);
    return result;
}

/**
 * @brief 执行wpa_cli命令
 */
static std::string executeWpaCli(const std::string& cmd, 
                                  const std::string& interface = wifiConfig::INTERFACE_NAME) {
    std::string full_cmd = "wpa_cli -i " + interface + " " + cmd + " 2>&1";
    return executeCommand(full_cmd);
}

/**
 * @brief 检查wpa_cli命令是否成功
 */
static bool isWpaCommandSuccess(const std::string& output) {
    std::string trimmed = trim(output);
    return trimmed == "OK" || trimmed.find("OK") == 0;
}

/**
 * @brief 解析信号强度（dBm转百分比）
 */
static int signalToPercent(int dbm) {
    if (dbm >= -50) return 100;
    if (dbm <= -100) return 0;
    return 2 * (dbm + 100);
}

/**
 * @brief 解析频率获取信道号
 */
static int frequencyToChannel(int freq) {
    if (freq >= 2412 && freq <= 2484) {
        return (freq - 2407) / 5;
    } else if (freq >= 5170 && freq <= 5825) {
        return (freq - 5000) / 5;
    }
    return 0;
}

/**
 * @brief 解析加密类型字符串
 */
static wifiSecurity parseSecurityType(const std::string& flags) {
    if (flags.find("WPA3") != std::string::npos) {
        return wifiSecurity::WPA3_PSK;
    } else if (flags.find("WPA2") != std::string::npos) {
        return wifiSecurity::WPA2_PSK;
    } else if (flags.find("WPA") != std::string::npos) {
        return wifiSecurity::WPA_PSK;
    } else if (flags.find("WEP") != std::string::npos) {
        return wifiSecurity::WEP;
    } else if (flags.find("ESS") != std::string::npos && flags.find("WPA") == std::string::npos) {
        return wifiSecurity::NONE;
    }
    return wifiSecurity::UNKNOWN;
}

// ============================================================================
// wifiManager::Impl 实现类
// ============================================================================

class wifiManager::Impl {
public:
    explicit Impl(const wifiConfig& config);
    ~Impl();
    
    // 初始化和关闭
    wifiError initialize();
    void shutdown();
    
    // 扫描
    wifiError scanNetworks(wifiScanCallback callback, std::vector<wifiInfo>* networks);
    void setAutoScan(bool enabled);
    bool isAutoScanRunning() const { return auto_scan_running_; }
    
    // 连接
    wifiError connect(const std::string& ssid, const std::string& password, 
                      wifiConnectCallback callback);
    wifiError connectSavedNetwork();
    wifiError disconnect();
    wifiError reconnect();
    
    // 配置管理
    wifiError saveCurrentNetwork();
    wifiError forgetNetwork(const std::string& ssid);
    std::vector<savedNetworkInfo> getSavedNetworks() const;
    bool isNetworkSaved(const std::string& ssid) const;
    wifiError setNetworkPriority(const std::string& ssid, int priority);
    wifiError enableNetworkAutoConnect(const std::string& ssid, bool enabled);
    wifiError reloadConfig();
    
    // 状态查询
    wifiState getState() const { return current_state_; }
    bool isConnected() const { return current_state_ == wifiState::CONNECTED; }
    bool isInterfaceUp() const;
    bool getConnectionInfo(wifiConnectionInfo& info) const;
    std::string getCurrentSSID() const;
    std::string getIPAddress() const;
    int getSignalStrength() const;
    
    // 自动重连
    void setAutoReconnect(bool enabled, const std::string& ssid, const std::string& password);
    bool isAutoReconnectEnabled() const { return auto_reconnect_enabled_; }
    
    // 回调设置
    void setStateCallback(wifiStateCallback cb) { state_callback_ = cb; }
    void setErrorCallback(wifiErrorCallback cb) { error_callback_ = cb; }
    void setReconnectCallback(wifiReconnectCallback cb) { reconnect_callback_ = cb; }
    
    // 统计信息
    void getStats(wifiManager::Stats& stats) const;
    void resetStats();

private:
    // 内部辅助函数
    wifiError checkPrerequisites();
    wifiError ensureInterfaceUp();
    wifiError ensureWpaSupplicantRunning();
    
    wifiError scanNetworksInternal(std::vector<wifiInfo>& networks);
    wifiError connectInternal(const std::string& ssid, const std::string& password);
    
    int findNetworkIdBySSID(const std::string& ssid) const;
    std::string getCurrentSSIDInternal() const;
    std::string getIPAddressInternal() const;
    int getSignalStrengthInternal() const;
    
    void updateState(wifiState new_state);
    void notifyError(wifiError error, const std::string& message);
    
    // 后台线程
    void autoScanThread();
    void autoReconnectThread();
    
    // 配置
    wifiConfig config_;
    
    // 状态
    std::atomic<wifiState> current_state_;
    std::atomic<bool> initialized_;
    std::atomic<bool> shutdown_requested_;
    
    // 自动扫描
    std::atomic<bool> auto_scan_running_;
    std::unique_ptr<std::thread> auto_scan_thread_;
    
    // 自动重连
    std::atomic<bool> auto_reconnect_enabled_;
    std::string reconnect_ssid_;
    std::string reconnect_password_;
    std::unique_ptr<std::thread> auto_reconnect_thread_;
    int reconnect_attempts_;
    
    // 回调
    wifiStateCallback state_callback_;
    wifiErrorCallback error_callback_;
    wifiReconnectCallback reconnect_callback_;
    
    // 统计
    mutable std::mutex stats_mutex_;
    wifiManager::Stats stats_;
    
    // 线程同步
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

// ============================================================================
// Impl 构造和析构
// ============================================================================

wifiManager::Impl::Impl(const wifiConfig& config)
    : config_(config),
      current_state_(wifiState::UNKNOWN),
      initialized_(false),
      shutdown_requested_(false),
      auto_scan_running_(false),
      auto_reconnect_enabled_(false),
      reconnect_attempts_(0) {
    
    memset(&stats_, 0, sizeof(stats_));
}

wifiManager::Impl::~Impl() {
    shutdown();
}

// ============================================================================
// 初始化和关闭
// ============================================================================

wifiError wifiManager::Impl::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        LOG_WARN(LOG_TAG, "WiFi manager already initialized");
        return wifiError::NONE;
    }
    
    LOG_INFO(LOG_TAG, "Initializing WiFi manager...");
    
    // 1. 检查前置条件
    wifiError err = checkPrerequisites();
    if (err != wifiError::NONE) {
        return err;
    }
    
    // 2. 确保接口UP
    err = ensureInterfaceUp();
    if (err != wifiError::NONE) {
        return err;
    }
    
    // 3. 确保wpa_supplicant运行
    err = ensureWpaSupplicantRunning();
    if (err != wifiError::NONE) {
        return err;
    }
    
    // 4. 获取当前状态
    std::string ssid = getCurrentSSIDInternal();
    if (!ssid.empty()) {
        current_state_ = wifiState::CONNECTED;
        LOG_INFO(LOG_TAG, "Already connected to: %s", ssid.c_str());
    } else {
        current_state_ = wifiState::DISCONNECTED;
    }
    
    initialized_ = true;
    LOG_INFO(LOG_TAG, "WiFi manager initialized successfully. State: %s", 
             wifiStateToString(current_state_));
    
    // 5. 自动连接已保存WiFi（如果启用）
    if (config_.auto_connect_on_init && current_state_ != wifiState::CONNECTED) {
        LOG_INFO(LOG_TAG, "Auto-connect on init enabled, attempting to connect...");
        
        for (int attempt = 0; attempt < config_.auto_connect_max_attempts; ++attempt) {
            if (connectSavedNetwork() == wifiError::NONE) {
                stats_.auto_connects++;
                LOG_INFO(LOG_TAG, "Auto-connect succeeded on attempt %d", attempt + 1);
                break;
            }
            
            if (attempt < config_.auto_connect_max_attempts - 1) {
                LOG_WARN(LOG_TAG, "Auto-connect attempt %d failed, retrying...", attempt + 1);
                std::this_thread::sleep_for(std::chrono::seconds(config_.reconnect_delay_sec));
            }
        }
    }
    
    return wifiError::NONE;
}

void wifiManager::Impl::shutdown() {
    LOG_INFO(LOG_TAG, "Shutting down WiFi manager...");
    
    shutdown_requested_ = true;
    
    // 停止自动扫描
    setAutoScan(false);
    
    // 停止自动重连
    setAutoReconnect(false, "", "");
    
    // 等待线程结束
    if (auto_scan_thread_ && auto_scan_thread_->joinable()) {
        cv_.notify_all();
        auto_scan_thread_->join();
    }
    
    if (auto_reconnect_thread_ && auto_reconnect_thread_->joinable()) {
        cv_.notify_all();
        auto_reconnect_thread_->join();
    }
    
    initialized_ = false;
    LOG_INFO(LOG_TAG, "WiFi manager shut down");
}

// ============================================================================
// 前置条件检查
// ============================================================================

wifiError wifiManager::Impl::checkPrerequisites() {
    // 检查wpa_cli
    std::string result = executeCommand("which wpa_cli 2>/dev/null");
    if (result.empty() || result.find("wpa_cli") == std::string::npos) {
        LOG_ERROR(LOG_TAG, "wpa_cli not found. Please install wpa_supplicant.");
        return wifiError::WPA_SUPPLICANT_NOT_FOUND;
    }
    
    // 检查接口是否存在
    result = executeCommand("ip link show " + std::string(wifiConfig::INTERFACE_NAME) + " 2>&1");
    if (result.find("does not exist") != std::string::npos) {
        LOG_ERROR(LOG_TAG, "Interface %s not found", wifiConfig::INTERFACE_NAME);
        return wifiError::INTERFACE_NOT_FOUND;
    }
    
    LOG_INFO(LOG_TAG, "Interface %s found", wifiConfig::INTERFACE_NAME);
    return wifiError::NONE;
}

wifiError wifiManager::Impl::ensureInterfaceUp() {
    LOG_INFO(LOG_TAG, "Checking interface status...");
    
    // 检查接口是否UP
    if (isInterfaceUp()) {
        LOG_INFO(LOG_TAG, "Interface %s is already UP", wifiConfig::INTERFACE_NAME);
        return wifiError::NONE;
    }
    
    // 接口未UP，尝试打开
    LOG_INFO(LOG_TAG, "Interface %s is DOWN, bringing it UP...", wifiConfig::INTERFACE_NAME);
    std::string cmd = "ip link set " + std::string(wifiConfig::INTERFACE_NAME) + " up 2>&1";
    std::string result = executeCommand(cmd);
    
    stats_.interface_up_count++;
    
    // 等待接口UP
    auto start = std::chrono::steady_clock::now();
    while (true) {
        if (isInterfaceUp()) {
            LOG_INFO(LOG_TAG, "Interface %s is now UP", wifiConfig::INTERFACE_NAME);
            return wifiError::NONE;
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        
        if (elapsed >= config_.interface_up_timeout_ms) {
            LOG_ERROR(LOG_TAG, "Timeout waiting for interface to come UP");
            return wifiError::INTERFACE_NOT_UP;
        }
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.interface_up_check_interval_ms));
    }
}

bool wifiManager::Impl::isInterfaceUp() const {
    std::string result = executeCommand(
        "ip link show " + std::string(wifiConfig::INTERFACE_NAME) + " 2>&1");
    
    // 查找 <...UP...> 格式
    size_t start = result.find('<');
    size_t end = result.find('>');
    if (start != std::string::npos && end != std::string::npos) {
        std::string flags = result.substr(start + 1, end - start - 1);
        return flags.find("UP") != std::string::npos;
    }
    
    return false;
}

wifiError wifiManager::Impl::ensureWpaSupplicantRunning() {
    // 检查wpa_supplicant是否运行
    std::string result = executeWpaCli("status");
    
    if (result.find("wpa_state") != std::string::npos) {
        LOG_INFO(LOG_TAG, "wpa_supplicant is running");
        return wifiError::NONE;
    }
    
    LOG_WARN(LOG_TAG, "wpa_supplicant not running, attempting to start...");
    
    // 尝试启动wpa_supplicant
    std::string cmd = "wpa_supplicant -B -i " + std::string(wifiConfig::INTERFACE_NAME) +
                      " -c " + std::string(wifiConfig::WPA_CONF_PATH) + " 2>&1";
    result = executeCommand(cmd);
    
    // 等待wpa_supplicant启动
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 再次检查
    result = executeWpaCli("status");
    if (result.find("wpa_state") != std::string::npos) {
        LOG_INFO(LOG_TAG, "wpa_supplicant started successfully");
        return wifiError::NONE;
    }
    
    LOG_ERROR(LOG_TAG, "Failed to start wpa_supplicant: %s", result.c_str());
    return wifiError::WPA_SUPPLICANT_NOT_RUNNING;
}

// ============================================================================
// 扫描功能
// ============================================================================

wifiError wifiManager::Impl::scanNetworks(wifiScanCallback callback, 
                                          std::vector<wifiInfo>* networks) {
    if (!initialized_) {
        LOG_ERROR(LOG_TAG, "WiFi manager not initialized");
        return wifiError::INITIALIZATION_FAILED;
    }
    
    // 异步模式
    if (callback) {
        std::thread([this, callback]() {
            std::vector<wifiInfo> results;
            wifiError err = scanNetworksInternal(results);
            
            if (err == wifiError::NONE) {
                callback(results);
            } else {
                notifyError(err, "Scan failed");
                callback(results);  // 返回空列表
            }
        }).detach();
        
        return wifiError::NONE;
    }
    
    // 同步模式
    if (!networks) {
        LOG_ERROR(LOG_TAG, "networks parameter cannot be null in sync mode");
        return wifiError::UNKNOWN;
    }
    
    return scanNetworksInternal(*networks);
}

wifiError wifiManager::Impl::scanNetworksInternal(std::vector<wifiInfo>& networks) {
    LOG_INFO(LOG_TAG, "Starting network scan...");
    updateState(wifiState::SCANNING);
    
    stats_.scans_performed++;
    
    // 触发扫描
    std::string result = executeWpaCli("scan");
    if (!isWpaCommandSuccess(result)) {
        LOG_ERROR(LOG_TAG, "Failed to start scan: %s", result.c_str());
        updateState(wifiState::DISCONNECTED);
        return wifiError::SCAN_FAILED;
    }
    
    // 等待扫描完成
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 获取扫描结果
    result = executeWpaCli("scan_results");
    
    // 解析结果
    std::istringstream iss(result);
    std::string line;
    std::map<std::string, wifiInfo> unique_networks;  // 使用SSID去重
    
    // 跳过标题行
    std::getline(iss, line);
    
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        
        // 格式: bssid / frequency / signal level / flags / ssid
        std::istringstream lineStream(line);
        wifiInfo info;
        std::string bssid, freq_str, signal_str, flags;
        
        lineStream >> bssid >> freq_str >> signal_str >> flags;
        
        // 剩余部分是SSID
        std::string ssid;
        std::getline(lineStream, ssid);
        ssid = trim(ssid);
        
        if (ssid.empty()) continue;
        
        info.ssid = ssid;
        info.bssid = bssid;
        info.frequency = std::atoi(freq_str.c_str());
        info.channel = frequencyToChannel(info.frequency);
        info.signal_strength = signalToPercent(std::atoi(signal_str.c_str()));
        info.security = parseSecurityType(flags);
        
        // 保留信号最强的
        if (unique_networks.find(ssid) == unique_networks.end() ||
            unique_networks[ssid].signal_strength < info.signal_strength) {
            unique_networks[ssid] = info;
        }
    }
    
    // 转换为vector并按信号强度排序
    networks.clear();
    for (const auto& pair : unique_networks) {
        networks.push_back(pair.second);
    }
    
    std::sort(networks.begin(), networks.end(),
              [](const wifiInfo& a, const wifiInfo& b) {
                  return a.signal_strength > b.signal_strength;
              });
    
    LOG_INFO(LOG_TAG, "Scan complete, found %zu unique networks", networks.size());
    
    // 恢复之前的状态（通过实际查询wpa_supplicant状态）
    std::string status = executeWpaCli("status");
    if (status.find("wpa_state=COMPLETED") != std::string::npos) {
        updateState(wifiState::CONNECTED);
    } else {
        updateState(wifiState::DISCONNECTED);
    }
    
    return wifiError::NONE;
}

void wifiManager::Impl::setAutoScan(bool enabled) {
    if (enabled == auto_scan_running_) {
        return;
    }
    
    if (enabled) {
        LOG_INFO(LOG_TAG, "Starting auto-scan (interval: %d seconds)", config_.timed_scan_sec);
        auto_scan_running_ = true;
        
        auto_scan_thread_ = std::make_unique<std::thread>([this]() {
            autoScanThread();
        });
    } else {
        LOG_INFO(LOG_TAG, "Stopping auto-scan");
        auto_scan_running_ = false;
        cv_.notify_all();
        
        if (auto_scan_thread_ && auto_scan_thread_->joinable()) {
            auto_scan_thread_->join();
        }
        auto_scan_thread_.reset();
    }
}

void wifiManager::Impl::autoScanThread() {
    LOG_DEBUG(LOG_TAG, "Auto-scan thread started");
    
    while (auto_scan_running_ && !shutdown_requested_) {
        std::vector<wifiInfo> networks;
        scanNetworksInternal(networks);
        
        // 等待下一次扫描
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(config_.timed_scan_sec),
                     [this]() { return !auto_scan_running_ || shutdown_requested_; });
    }
    
    LOG_DEBUG(LOG_TAG, "Auto-scan thread stopped");
}

// ============================================================================
// 连接功能
// ============================================================================

wifiError wifiManager::Impl::connect(const std::string& ssid,
                                      const std::string& password,
                                      wifiConnectCallback callback) {
    if (!initialized_) {
        LOG_ERROR(LOG_TAG, "WiFi manager not initialized");
        return wifiError::INITIALIZATION_FAILED;
    }
    
    // 异步模式
    if (callback) {
        std::thread([this, ssid, password, callback]() {
            wifiError err = connectInternal(ssid, password);
            
            if (err == wifiError::NONE) {
                callback(true, "Connected successfully");
            } else {
                callback(false, wifiErrorToString(err));
            }
        }).detach();
        
        return wifiError::NONE;
    }
    
    // 同步模式
    return connectInternal(ssid, password);
}

wifiError wifiManager::Impl::connectInternal(const std::string& ssid,
                                              const std::string& password) {
    LOG_INFO(LOG_TAG, "Connecting to: %s", ssid.c_str());
    
    stats_.connections_attempted++;
    
    // 1. 检查是否已连接到同一个SSID
    if (current_state_ == wifiState::CONNECTED) {
        std::string current_ssid = getCurrentSSIDInternal();
        if (!current_ssid.empty() && current_ssid == ssid) {
            LOG_INFO(LOG_TAG, "Already connected to %s", ssid.c_str());
            return wifiError::ALREADY_CONNECTED;
        }
        
        // 2. 如果已连接到其他WiFi，先断开
        if (!current_ssid.empty()) {
            LOG_INFO(LOG_TAG, "Disconnecting from %s", current_ssid.c_str());
            disconnect();
        }
    }
    
    updateState(wifiState::CONNECTING);
    
    // 3. 删除旧配置（如果启用）
    if (config_.clear_old_config_on_connect) {
        int old_net_id = findNetworkIdBySSID(ssid);
        if (old_net_id >= 0) {
            LOG_INFO(LOG_TAG, "Removing old network configuration (ID: %d)", old_net_id);
            std::string remove_cmd = "remove_network " + std::to_string(old_net_id);
            executeWpaCli(remove_cmd);
        }
    }
    
    // 4. 添加新网络配置
    std::string output = executeWpaCli("add_network");
    int net_id = -1;
    try {
        net_id = std::stoi(trim(output));
        LOG_INFO(LOG_TAG, "Created new network with ID: %d", net_id);
    } catch (...) {
        LOG_ERROR(LOG_TAG, "Failed to add network: %s", output.c_str());
        updateState(wifiState::FAILED);
        return wifiError::CONNECTION_FAILED;
    }
    
    // 5. 设置SSID
    std::string cmd = "set_network " + std::to_string(net_id) + " ssid '\"" + ssid + "\"'";
    output = executeWpaCli(cmd);
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to set SSID: %s", output.c_str());
        executeWpaCli("remove_network " + std::to_string(net_id));
        updateState(wifiState::FAILED);
        return wifiError::CONNECTION_FAILED;
    }
    
    // 6. 设置密码或开放网络
    if (password.empty()) {
        cmd = "set_network " + std::to_string(net_id) + " key_mgmt NONE";
    } else {
        cmd = "set_network " + std::to_string(net_id) + " psk '\"" + password + "\"'";
    }
    
    output = executeWpaCli(cmd);
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to set password: %s", output.c_str());
        executeWpaCli("remove_network " + std::to_string(net_id));
        updateState(wifiState::FAILED);
        stats_.password_errors++;
        return wifiError::PASSWORD_INCORRECT;
    }
    
    // 7. 选择网络（会自动禁用其他网络并连接）
    cmd = "select_network " + std::to_string(net_id);
    output = executeWpaCli(cmd);
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to select network: %s", output.c_str());
        executeWpaCli("remove_network " + std::to_string(net_id));
        updateState(wifiState::FAILED);
        return wifiError::CONNECTION_FAILED;
    }
    
    LOG_INFO(LOG_TAG, "Waiting for connection...");
    
    // 8. 等待连接成功
    auto start = std::chrono::steady_clock::now();
    int disconnected_count = 0;
    
    while (true) {
        std::string status = executeWpaCli("status");
        
        // 检查wpa_state
        if (status.find("wpa_state=COMPLETED") != std::string::npos) {
            updateState(wifiState::OBTAINING_IP);
            LOG_INFO(LOG_TAG, "Obtaining IP address...");
            
            // 获取IP地址（使用udhcpc）
            std::string dhcp_cmd = "udhcpc -i " + std::string(wifiConfig::INTERFACE_NAME) + 
                                   " -n -q -t 5 2>&1";
            std::string dhcp_result = executeCommand(dhcp_cmd);
            
            // 验证IP地址
            std::string ip = getIPAddressInternal();
            if (!ip.empty()) {
                updateState(wifiState::CONNECTED);
                stats_.connections_successful++;
                
                // 保存配置（如果启用）
                if (config_.auto_save_config) {
                    saveCurrentNetwork();
                }
                
                LOG_INFO(LOG_TAG, "Connected successfully. IP: %s", ip.c_str());
                return wifiError::NONE;
            } else {
                LOG_ERROR(LOG_TAG, "DHCP failed: %s", dhcp_result.c_str());
                executeWpaCli("remove_network " + std::to_string(net_id));
                updateState(wifiState::FAILED);
                return wifiError::DHCP_FAILED;
            }
        }
        
        // 检测密码错误（连续DISCONNECTED状态）
        if (status.find("wpa_state=DISCONNECTED") != std::string::npos ||
            status.find("wpa_state=INACTIVE") != std::string::npos) {
            disconnected_count++;
            if (disconnected_count >= 6) {  // 3秒（500ms * 6）
                LOG_ERROR(LOG_TAG, "Authentication failed (wrong password?)");
                executeWpaCli("remove_network " + std::to_string(net_id));
                updateState(wifiState::FAILED);
                stats_.password_errors++;
                return wifiError::AUTHENTICATION_FAILED;
            }
        } else {
            disconnected_count = 0;  // 重置计数器
        }
        
        // 检查超时
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        
        if (elapsed >= config_.connect_timeout_ms) {
            LOG_ERROR(LOG_TAG, "Connection timeout");
            executeWpaCli("remove_network " + std::to_string(net_id));
            updateState(wifiState::FAILED);
            return wifiError::TIMEOUT;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

wifiError wifiManager::Impl::connectSavedNetwork() {
    LOG_INFO(LOG_TAG, "Connecting to saved network...");
    
    // 1. 获取已保存的网络列表
    std::vector<savedNetworkInfo> saved = getSavedNetworks();
    if (saved.empty()) {
        LOG_WARN(LOG_TAG, "No saved networks found");
        return wifiError::NETWORK_NOT_FOUND;
    }
    
    // 2. 扫描可用网络
    std::vector<wifiInfo> available;
    wifiError err = scanNetworksInternal(available);
    if (err != wifiError::NONE) {
        return err;
    }
    
    // 3. 创建候选列表（已保存且在范围内）
    struct Candidate {
        std::string ssid;
        int priority;
        int signal;
    };
    
    std::vector<Candidate> candidates;
    
    for (const auto& saved_net : saved) {
        if (!saved_net.is_enabled_auto) {
            continue;  // 跳过禁用自动连接的网络
        }
        
        // 查找是否在可用列表中
        for (const auto& avail : available) {
            if (avail.ssid == saved_net.ssid) {
                // 检查信号强度
                if (avail.signal_strength >= config_.auto_connect_min_signal) {
                    candidates.push_back({
                        saved_net.ssid,
                        saved_net.priority,
                        avail.signal_strength
                    });
                }
                break;
            }
        }
    }
    
    if (candidates.empty()) {
        LOG_WARN(LOG_TAG, "No suitable saved networks in range");
        return wifiError::NETWORK_NOT_FOUND;
    }
    
    // 4. 排序：优先级优先，相同优先级下信号强度优先
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.priority != b.priority) {
                      return a.priority > b.priority;
                  }
                  return a.signal > b.signal;
              });
    
    // 5. 尝试连接最佳候选
    const auto& best = candidates[0];
    LOG_INFO(LOG_TAG, "Selected best network: %s (priority=%d, signal=%d%%)",
             best.ssid.c_str(), best.priority, best.signal);
    
    // 6. 使用wpa_cli重新连接（已保存的网络不需要密码）
    int net_id = findNetworkIdBySSID(best.ssid);
    if (net_id < 0) {
        LOG_ERROR(LOG_TAG, "Network ID not found for %s", best.ssid.c_str());
        return wifiError::NETWORK_NOT_FOUND;
    }
    
    // 断开当前连接
    executeWpaCli("disconnect");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 启用并选择网络
    std::string cmd = "select_network " + std::to_string(net_id);
    std::string output = executeWpaCli(cmd);
    
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to select network: %s", output.c_str());
        return wifiError::CONNECTION_FAILED;
    }
    
    // 等待连接
    updateState(wifiState::CONNECTING);
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        std::string status = executeWpaCli("status");
        
        if (status.find("wpa_state=COMPLETED") != std::string::npos) {
            updateState(wifiState::OBTAINING_IP);
            
            // 获取IP
            std::string dhcp_cmd = "udhcpc -i " + std::string(wifiConfig::INTERFACE_NAME) + 
                                   " -n -q -t 5 2>&1";
            executeCommand(dhcp_cmd);
            
            std::string ip = getIPAddressInternal();
            if (!ip.empty()) {
                updateState(wifiState::CONNECTED);
                LOG_INFO(LOG_TAG, "Connected to %s, IP: %s", best.ssid.c_str(), ip.c_str());
                return wifiError::NONE;
            }
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        
        if (elapsed >= config_.connect_timeout_ms) {
            LOG_ERROR(LOG_TAG, "Connection timeout");
            updateState(wifiState::FAILED);
            return wifiError::TIMEOUT;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

wifiError wifiManager::Impl::disconnect() {
    LOG_INFO(LOG_TAG, "Disconnecting...");
    
    std::string output = executeWpaCli("disconnect");
    
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to disconnect: %s", output.c_str());
        return wifiError::DISCONNECTION_FAILED;
    }
    
    // 释放IP
    std::string cmd = "ip addr flush dev " + std::string(wifiConfig::INTERFACE_NAME);
    executeCommand(cmd);
    
    updateState(wifiState::DISCONNECTED);
    stats_.disconnections++;
    
    LOG_INFO(LOG_TAG, "Disconnected successfully");
    return wifiError::NONE;
}

wifiError wifiManager::Impl::reconnect() {
    LOG_INFO(LOG_TAG, "Reconnecting...");
    
    stats_.reconnects++;
    
    std::string output = executeWpaCli("reconnect");
    
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to reconnect: %s", output.c_str());
        return wifiError::CONNECTION_FAILED;
    }
    
    // 等待连接
    updateState(wifiState::CONNECTING);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    if (isConnected()) {
        LOG_INFO(LOG_TAG, "Reconnected successfully");
        return wifiError::NONE;
    } else {
        LOG_ERROR(LOG_TAG, "Reconnection failed");
        return wifiError::CONNECTION_FAILED;
    }
}

// ============================================================================
// 配置管理
// ============================================================================

wifiError wifiManager::Impl::saveCurrentNetwork() {
    LOG_INFO(LOG_TAG, "Saving current network configuration...");
    
    std::string output = executeWpaCli("save_config");
    
    if (isWpaCommandSuccess(output)) {
        stats_.config_saves++;
        LOG_INFO(LOG_TAG, "Configuration saved successfully");
        return wifiError::NONE;
    } else {
        LOG_ERROR(LOG_TAG, "Failed to save configuration: %s", output.c_str());
        return wifiError::CONFIG_FILE_ERROR;
    }
}

wifiError wifiManager::Impl::forgetNetwork(const std::string& ssid) {
    LOG_INFO(LOG_TAG, "Forgetting network: %s", ssid.c_str());
    
    int net_id = findNetworkIdBySSID(ssid);
    if (net_id < 0) {
        LOG_WARN(LOG_TAG, "Network %s not found in saved list", ssid.c_str());
        return wifiError::NETWORK_NOT_FOUND;
    }
    
    // 如果当前连接的是要删除的网络，先断开
    std::string current_ssid = getCurrentSSIDInternal();
    if (current_ssid == ssid) {
        disconnect();
    }
    
    std::string cmd = "remove_network " + std::to_string(net_id);
    std::string output = executeWpaCli(cmd);
    
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to remove network: %s", output.c_str());
        return wifiError::CONFIG_FILE_ERROR;
    }
    
    // 保存配置
    saveCurrentNetwork();
    
    LOG_INFO(LOG_TAG, "Network %s removed successfully", ssid.c_str());
    return wifiError::NONE;
}

std::vector<savedNetworkInfo> wifiManager::Impl::getSavedNetworks() const {
    std::vector<savedNetworkInfo> networks;
    
    std::string output = executeWpaCli("list_networks");
    
    std::istringstream iss(output);
    std::string line;
    
    // 跳过标题行
    std::getline(iss, line);
    
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        
        // 格式: network id / ssid / bssid / flags
        std::istringstream lineStream(line);
        savedNetworkInfo info;
        
        std::string id_str, ssid, bssid, flags;
        lineStream >> id_str >> ssid >> bssid;
        std::getline(lineStream, flags);
        flags = trim(flags);
        
        info.network_id = std::atoi(id_str.c_str());
        info.ssid = ssid;
        info.is_current = (flags.find("CURRENT") != std::string::npos);
        info.is_enabled_auto = (flags.find("DISABLED") == std::string::npos);
        info.priority = 0;  // 默认优先级
        
        networks.push_back(info);
    }
    
    // 从配置文件读取优先级
    std::ifstream conf_file(wifiConfig::WPA_CONF_PATH);
    if (conf_file.is_open()) {
        std::string conf_line;
        int current_net_id = -1;
        
        while (std::getline(conf_file, conf_line)) {
            conf_line = trim(conf_line);
            
            if (conf_line.find("network=") == 0) {
                current_net_id = -1;  // 新网络块
            } else if (conf_line.find("id_str=") == 0) {
                // 提取ID
                size_t pos = conf_line.find('=');
                if (pos != std::string::npos) {
                    std::string id = conf_line.substr(pos + 1);
                    id = trim(id);
                    // 移除引号
                    if (!id.empty() && id[0] == '"') {
                        id = id.substr(1, id.length() - 2);
                    }
                    current_net_id = std::atoi(id.c_str());
                }
            } else if (conf_line.find("priority=") == 0 && current_net_id >= 0) {
                size_t pos = conf_line.find('=');
                if (pos != std::string::npos) {
                    int priority = std::atoi(conf_line.substr(pos + 1).c_str());
                    
                    // 更新对应网络的优先级
                    for (auto& net : networks) {
                        if (net.network_id == current_net_id) {
                            net.priority = priority;
                            break;
                        }
                    }
                }
            }
        }
        conf_file.close();
    }
    
    return networks;
}

bool wifiManager::Impl::isNetworkSaved(const std::string& ssid) const {
    return findNetworkIdBySSID(ssid) >= 0;
}

wifiError wifiManager::Impl::setNetworkPriority(const std::string& ssid, int priority) {
    int net_id = findNetworkIdBySSID(ssid);
    if (net_id < 0) {
        LOG_WARN(LOG_TAG, "Network %s not found", ssid.c_str());
        return wifiError::NETWORK_NOT_FOUND;
    }
    
    std::string cmd = "set_network " + std::to_string(net_id) + " priority " + 
                      std::to_string(priority);
    std::string output = executeWpaCli(cmd);
    
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to set priority: %s", output.c_str());
        return wifiError::CONFIG_FILE_ERROR;
    }
    
    saveCurrentNetwork();
    LOG_INFO(LOG_TAG, "Priority set to %d for %s", priority, ssid.c_str());
    return wifiError::NONE;
}

wifiError wifiManager::Impl::enableNetworkAutoConnect(const std::string& ssid, bool enabled) {
    int net_id = findNetworkIdBySSID(ssid);
    if (net_id < 0) {
        LOG_WARN(LOG_TAG, "Network %s not found", ssid.c_str());
        return wifiError::NETWORK_NOT_FOUND;
    }
    
    std::string cmd = enabled ? ("enable_network " + std::to_string(net_id)) :
                                ("disable_network " + std::to_string(net_id));
    std::string output = executeWpaCli(cmd);
    
    if (!isWpaCommandSuccess(output)) {
        LOG_ERROR(LOG_TAG, "Failed to %s network: %s", 
                  enabled ? "enable" : "disable", output.c_str());
        return wifiError::CONFIG_FILE_ERROR;
    }
    
    saveCurrentNetwork();
    LOG_INFO(LOG_TAG, "Auto-connect %s for %s", enabled ? "enabled" : "disabled", ssid.c_str());
    return wifiError::NONE;
}

wifiError wifiManager::Impl::reloadConfig() {
    LOG_INFO(LOG_TAG, "Reloading configuration...");
    
    std::string output = executeWpaCli("reconfigure");
    
    if (isWpaCommandSuccess(output)) {
        LOG_INFO(LOG_TAG, "Configuration reloaded successfully");
        return wifiError::NONE;
    } else {
        LOG_ERROR(LOG_TAG, "Failed to reload configuration: %s", output.c_str());
        return wifiError::CONFIG_FILE_ERROR;
    }
}

int wifiManager::Impl::findNetworkIdBySSID(const std::string& ssid) const {
    std::vector<savedNetworkInfo> networks = getSavedNetworks();
    
    for (const auto& net : networks) {
        if (net.ssid == ssid) {
            return net.network_id;
        }
    }
    
    return -1;
}

// ============================================================================
// 状态查询
// ============================================================================

bool wifiManager::Impl::getConnectionInfo(wifiConnectionInfo& info) const {
    if (!isConnected()) {
        return false;
    }
    
    info.ssid = getCurrentSSIDInternal();
    info.ip_address = getIPAddressInternal();
    info.signal_strength = getSignalStrengthInternal();
    info.state = current_state_;
    
    // 获取BSSID和安全类型
    std::string status = executeWpaCli("status");
    
    std::istringstream iss(status);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.find("bssid=") == 0) {
            info.bssid = trim(line.substr(6));
        } else if (line.find("key_mgmt=") == 0) {
            std::string key_mgmt = trim(line.substr(9));
            if (key_mgmt == "WPA2-PSK") {
                info.security = wifiSecurity::WPA2_PSK;
            } else if (key_mgmt == "WPA-PSK") {
                info.security = wifiSecurity::WPA_PSK;
            } else if (key_mgmt == "NONE") {
                info.security = wifiSecurity::NONE;
            }
        }
    }
    
    return true;
}

std::string wifiManager::Impl::getCurrentSSID() const {
    return getCurrentSSIDInternal();
}

std::string wifiManager::Impl::getCurrentSSIDInternal() const {
    std::string status = executeWpaCli("status");
    
    std::istringstream iss(status);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.find("ssid=") == 0) {
            std::string ssid = trim(line.substr(5));
            
            // 检查是否是MAC地址格式（xx:xx:xx:xx:xx:xx）
            if (ssid.length() == 17 && ssid[2] == ':' && ssid[5] == ':') {
                // 这是BSSID，尝试从list_networks获取真实SSID
                std::string list_output = executeWpaCli("list_networks");
                std::istringstream list_iss(list_output);
                std::string list_line;
                
                // 跳过标题
                std::getline(list_iss, list_line);
                
                while (std::getline(list_iss, list_line)) {
                    if (list_line.find("CURRENT") != std::string::npos) {
                        std::istringstream line_stream(list_line);
                        std::string id, real_ssid;
                        line_stream >> id >> real_ssid;
                        if (!real_ssid.empty()) {
                            return real_ssid;
                        }
                    }
                }
            }
            
            return ssid;
        }
    }
    
    return "";
}

std::string wifiManager::Impl::getIPAddress() const {
    return getIPAddressInternal();
}

std::string wifiManager::Impl::getIPAddressInternal() const {
    std::string cmd = "ip addr show " + std::string(wifiConfig::INTERFACE_NAME) + " 2>&1";
    std::string output = executeCommand(cmd);
    
    // 查找 inet xxx.xxx.xxx.xxx
    size_t pos = output.find("inet ");
    if (pos != std::string::npos) {
        pos += 5;  // 跳过 "inet "
        size_t end = output.find('/', pos);
        if (end != std::string::npos) {
            return trim(output.substr(pos, end - pos));
        }
    }
    
    return "";
}

int wifiManager::Impl::getSignalStrength() const {
    return getSignalStrengthInternal();
}

int wifiManager::Impl::getSignalStrengthInternal() const {
    std::string output = executeWpaCli("signal_poll");
    
    std::istringstream iss(output);
    std::string line;
    
    while (std::getline(iss, line)) {
        if (line.find("RSSI=") == 0) {
            int rssi = std::atoi(line.substr(5).c_str());
            return signalToPercent(rssi);
        }
    }
    
    return 0;
}

// ============================================================================
// 自动重连
// ============================================================================

void wifiManager::Impl::setAutoReconnect(bool enabled,
                                         const std::string& ssid,
                                         const std::string& password) {
    if (enabled) {
        LOG_INFO(LOG_TAG, "Enabling auto-reconnect...");
        
        reconnect_ssid_ = ssid.empty() ? getCurrentSSIDInternal() : ssid;
        reconnect_password_ = password;
        reconnect_attempts_ = 0;
        auto_reconnect_enabled_ = true;
        
        if (!auto_reconnect_thread_ || !auto_reconnect_thread_->joinable()) {
            auto_reconnect_thread_ = std::make_unique<std::thread>([this]() {
                autoReconnectThread();
            });
        }
        
        LOG_INFO(LOG_TAG, "Auto-reconnect enabled for: %s", reconnect_ssid_.c_str());
    } else {
        LOG_INFO(LOG_TAG, "Disabling auto-reconnect...");
        
        auto_reconnect_enabled_ = false;
        cv_.notify_all();
        
        if (auto_reconnect_thread_ && auto_reconnect_thread_->joinable()) {
            auto_reconnect_thread_->join();
        }
        auto_reconnect_thread_.reset();
        
        LOG_INFO(LOG_TAG, "Auto-reconnect disabled");
    }
}

void wifiManager::Impl::autoReconnectThread() {
    LOG_DEBUG(LOG_TAG, "Auto-reconnect thread started");
    
    while (auto_reconnect_enabled_ && !shutdown_requested_) {
        // 检查连接状态
        if (!isConnected() && !reconnect_ssid_.empty()) {
            // 检查重连次数限制
            if (config_.reconnect_max_attempts > 0 && 
                reconnect_attempts_ >= config_.reconnect_max_attempts) {
                LOG_WARN(LOG_TAG, "Max reconnect attempts (%d) reached", 
                         config_.reconnect_max_attempts);
                notifyError(wifiError::TIMEOUT, "Max reconnect attempts reached");
                break;
            }
            
            reconnect_attempts_++;
            LOG_INFO(LOG_TAG, "Auto-reconnect attempt %d to %s", 
                     reconnect_attempts_, reconnect_ssid_.c_str());
            
            stats_.auto_reconnects++;
            
            wifiError err = connectInternal(reconnect_ssid_, reconnect_password_);
            
            if (reconnect_callback_) {
                reconnect_callback_(err == wifiError::NONE, reconnect_ssid_);
            }
            
            if (err == wifiError::NONE) {
                LOG_INFO(LOG_TAG, "Auto-reconnect succeeded");
                reconnect_attempts_ = 0;  // 重置计数器
            } else {
                LOG_WARN(LOG_TAG, "Auto-reconnect failed: %s", wifiErrorToString(err));
            }
        } else if (isConnected()) {
            reconnect_attempts_ = 0;  // 已连接，重置计数器
        }
        
        // 等待下一次检查
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(config_.reconnect_interval_sec),
                     [this]() { return !auto_reconnect_enabled_ || shutdown_requested_; });
    }
    
    LOG_DEBUG(LOG_TAG, "Auto-reconnect thread stopped");
}

// ============================================================================
// 状态更新和通知
// ============================================================================

void wifiManager::Impl::updateState(wifiState new_state) {
    wifiState old_state = current_state_.exchange(new_state);
    
    if (old_state != new_state) {
        if (config_.enable_detailed_logging) {
            LOG_DEBUG(LOG_TAG, "State change: %s → %s",
                     wifiStateToString(old_state),
                     wifiStateToString(new_state));
        }
        
        if (state_callback_) {
            state_callback_(old_state, new_state);
        }
    }
}

void wifiManager::Impl::notifyError(wifiError error, const std::string& message) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.errors++;
    
    LOG_ERROR(LOG_TAG, "[ERROR] %s: %s", wifiErrorToString(error), message.c_str());
    
    if (error_callback_) {
        error_callback_(error, message);
    }
}

// ============================================================================
// 统计信息
// ============================================================================

void wifiManager::Impl::getStats(wifiManager::Stats& stats) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats = stats_;
}

void wifiManager::Impl::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    memset(&stats_, 0, sizeof(stats_));
    LOG_INFO(LOG_TAG, "Statistics reset");
}

// ============================================================================
// wifiManager 公共接口实现
// ============================================================================

wifiManager::wifiManager(const wifiConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {}

wifiManager::~wifiManager() = default;

wifiError wifiManager::initialize() {
    return pImpl_->initialize();
}

void wifiManager::shutdown() {
    pImpl_->shutdown();
}

wifiError wifiManager::scanNetworks(wifiScanCallback callback, std::vector<wifiInfo>* networks) {
    return pImpl_->scanNetworks(callback, networks);
}

void wifiManager::setAutoScan(bool enabled) {
    pImpl_->setAutoScan(enabled);
}

bool wifiManager::isAutoScanRunning() const {
    return pImpl_->isAutoScanRunning();
}

wifiError wifiManager::connect(const std::string& ssid,
                                const std::string& password,
                                wifiConnectCallback callback) {
    return pImpl_->connect(ssid, password, callback);
}

wifiError wifiManager::connectSavedNetwork() {
    return pImpl_->connectSavedNetwork();
}

wifiError wifiManager::disconnect() {
    return pImpl_->disconnect();
}

wifiError wifiManager::reconnect() {
    return pImpl_->reconnect();
}

wifiError wifiManager::saveCurrentNetwork() {
    return pImpl_->saveCurrentNetwork();
}

wifiError wifiManager::forgetNetwork(const std::string& ssid) {
    return pImpl_->forgetNetwork(ssid);
}

std::vector<savedNetworkInfo> wifiManager::getSavedNetworks() const {
    return pImpl_->getSavedNetworks();
}

bool wifiManager::isNetworkSaved(const std::string& ssid) const {
    return pImpl_->isNetworkSaved(ssid);
}

wifiError wifiManager::setNetworkPriority(const std::string& ssid, int priority) {
    return pImpl_->setNetworkPriority(ssid, priority);
}

wifiError wifiManager::enableNetworkAutoConnect(const std::string& ssid, bool enabled) {
    return pImpl_->enableNetworkAutoConnect(ssid, enabled);
}

wifiError wifiManager::reloadConfig() {
    return pImpl_->reloadConfig();
}

wifiState wifiManager::getState() const {
    return pImpl_->getState();
}

bool wifiManager::isConnected() const {
    return pImpl_->isConnected();
}

bool wifiManager::isInterfaceUp() const {
    return pImpl_->isInterfaceUp();
}

bool wifiManager::getConnectionInfo(wifiConnectionInfo& info) const {
    return pImpl_->getConnectionInfo(info);
}

std::string wifiManager::getCurrentSSID() const {
    return pImpl_->getCurrentSSID();
}

std::string wifiManager::getIPAddress() const {
    return pImpl_->getIPAddress();
}

int wifiManager::getSignalStrength() const {
    return pImpl_->getSignalStrength();
}

void wifiManager::setAutoReconnect(bool enabled,
                                   const std::string& ssid,
                                   const std::string& password) {
    pImpl_->setAutoReconnect(enabled, ssid, password);
}

bool wifiManager::isAutoReconnectEnabled() const {
    return pImpl_->isAutoReconnectEnabled();
}

void wifiManager::setStateCallback(wifiStateCallback callback) {
    pImpl_->setStateCallback(callback);
}

void wifiManager::setErrorCallback(wifiErrorCallback callback) {
    pImpl_->setErrorCallback(callback);
}

void wifiManager::setReconnectCallback(wifiReconnectCallback callback) {
    pImpl_->setReconnectCallback(callback);
}

void wifiManager::getStats(Stats& stats) const {
    pImpl_->getStats(stats);
}

void wifiManager::resetStats() {
    pImpl_->resetStats();
}

// ============================================================================
// 辅助函数实现
// ============================================================================

const char* wifiStateToString(wifiState state) {
    switch (state) {
        case wifiState::UNKNOWN: return "UNKNOWN";
        case wifiState::DISCONNECTED: return "DISCONNECTED";
        case wifiState::SCANNING: return "SCANNING";
        case wifiState::CONNECTING: return "CONNECTING";
        case wifiState::AUTHENTICATING: return "AUTHENTICATING";
        case wifiState::ASSOCIATED: return "ASSOCIATED";
        case wifiState::OBTAINING_IP: return "OBTAINING_IP";
        case wifiState::CONNECTED: return "CONNECTED";
        case wifiState::FAILED: return "FAILED";
        default: return "INVALID";
    }
}

const char* wifiErrorToString(wifiError error) {
    switch (error) {
        case wifiError::NONE: return "NONE";
        case wifiError::INITIALIZATION_FAILED: return "INITIALIZATION_FAILED";
        case wifiError::INTERFACE_NOT_FOUND: return "INTERFACE_NOT_FOUND";
        case wifiError::INTERFACE_NOT_UP: return "INTERFACE_NOT_UP";
        case wifiError::WPA_SUPPLICANT_NOT_FOUND: return "WPA_SUPPLICANT_NOT_FOUND";
        case wifiError::WPA_SUPPLICANT_NOT_RUNNING: return "WPA_SUPPLICANT_NOT_RUNNING";
        case wifiError::SCAN_FAILED: return "SCAN_FAILED";
        case wifiError::CONNECTION_FAILED: return "CONNECTION_FAILED";
        case wifiError::DISCONNECTION_FAILED: return "DISCONNECTION_FAILED";
        case wifiError::AUTHENTICATION_FAILED: return "AUTHENTICATION_FAILED";
        case wifiError::PASSWORD_INCORRECT: return "PASSWORD_INCORRECT";
        case wifiError::TIMEOUT: return "TIMEOUT";
        case wifiError::NETWORK_NOT_FOUND: return "NETWORK_NOT_FOUND";
        case wifiError::NETWORK_WEAK_SIGNAL: return "NETWORK_WEAK_SIGNAL";
        case wifiError::DHCP_FAILED: return "DHCP_FAILED";
        case wifiError::ALREADY_CONNECTED: return "ALREADY_CONNECTED";
        case wifiError::ALREADY_SAVED: return "ALREADY_SAVED";
        case wifiError::CONFIG_FILE_ERROR: return "CONFIG_FILE_ERROR";
        case wifiError::UNKNOWN: return "UNKNOWN";
        default: return "INVALID";
    }
}

const char* wifiSecurityToString(wifiSecurity security) {
    switch (security) {
        case wifiSecurity::NONE: return "NONE";
        case wifiSecurity::WEP: return "WEP";
        case wifiSecurity::WPA_PSK: return "WPA-PSK";
        case wifiSecurity::WPA2_PSK: return "WPA2-PSK";
        case wifiSecurity::WPA3_PSK: return "WPA3-PSK";
        case wifiSecurity::UNKNOWN: return "UNKNOWN";
        default: return "INVALID";
    }
}

} // namespace wifi
} // namespace network
} // namespace app

