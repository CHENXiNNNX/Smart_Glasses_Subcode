/* wifi.cc - WiFi管理 */

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

namespace app
{
    namespace network
    {
        namespace wifi
        {

            using namespace app::tool::log;

            static constexpr const char* LOG_TAG                 = "WiFi";
            static constexpr std::size_t COMMAND_BUFFER_SIZE     = 4096;
            static constexpr int         RSSI_STRONG_THRESHOLD   = -50;
            static constexpr int         RSSI_WEAK_THRESHOLD     = -100;
            static constexpr int         RSSI_PERCENT_FACTOR     = 2;
            static constexpr int         RSSI_MAX_PERCENT        = 100;
            static constexpr int         RSSI_MIN_PERCENT        = 0;
            static constexpr int         CHANNEL_2G_LOWER_FREQ   = 2412;
            static constexpr int         CHANNEL_2G_UPPER_FREQ   = 2484;
            static constexpr int         CHANNEL_2G_OFFSET       = 2407;
            static constexpr int         CHANNEL_5G_LOWER_FREQ   = 5170;
            static constexpr int         CHANNEL_5G_UPPER_FREQ   = 5825;
            static constexpr int         CHANNEL_5G_OFFSET       = 5000;
            static constexpr int         CHANNEL_FREQ_STEP       = 5;
            static constexpr int         PASSWORD_FAIL_THRESHOLD = 6;
            static constexpr int         POLL_INTERVAL_MS        = 500;
            static constexpr std::size_t BSSID_PREFIX_LENGTH     = 6;  // "bssid=" 长度
            static constexpr std::size_t KEY_MGMT_PREFIX_LENGTH  = 9;  // "key_mgmt=" 长度
            static constexpr std::size_t SSID_PREFIX_LENGTH      = 5;  // "ssid=" 长度
            static constexpr std::size_t MAC_ADDRESS_LENGTH      = 17; // MAC地址格式长度
            static constexpr std::size_t MAC_COLON_POS_1    = 2; // MAC地址第1个冒号位置
            static constexpr std::size_t MAC_COLON_POS_2    = 5; // MAC地址第2个冒号位置
            static constexpr std::size_t INET_PREFIX_LENGTH = 5; // "inet " 长度
            static constexpr std::size_t RSSI_PREFIX_LENGTH = 5; // "RSSI=" 长度

            static std::string trim(const std::string& str)
            {
                const char* whitespace = " \t\n\r\f\v";
                size_t      start      = str.find_first_not_of(whitespace);
                if (start == std::string::npos)
                {
                    return "";
                }
                size_t end = str.find_last_not_of(whitespace);
                return str.substr(start, end - start + 1);
            }

            static std::string executeCommand(const std::string& cmd)
            {
                char        buffer[COMMAND_BUFFER_SIZE];
                std::string result;

                FILE* pipe = popen(cmd.c_str(), "r");
                if (!pipe)
                {
                    LOG_ERROR(LOG_TAG, "执行命令失败: %s", cmd.c_str());
                    return "";
                }

                while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
                {
                    result += buffer;
                }

                pclose(pipe);
                return result;
            }

            static std::string
            executeWpaCli(const std::string& cmd,
                          const std::string& interface = WifiConfig::INTERFACE_NAME)
            {
                std::string full_cmd = "wpa_cli -i " + interface + " " + cmd + " 2>&1";
                return executeCommand(full_cmd);
            }

            static bool isWpaCommandSuccess(const std::string& output)
            {
                std::string trimmed = trim(output);
                return trimmed == "OK" || trimmed.find("OK") == 0;
            }

            static int signalToPercent(int dbm)
            {
                if (dbm >= RSSI_STRONG_THRESHOLD)
                {
                    return RSSI_MAX_PERCENT;
                }
                if (dbm <= RSSI_WEAK_THRESHOLD)
                {
                    return RSSI_MIN_PERCENT;
                }
                return RSSI_PERCENT_FACTOR * (dbm - RSSI_WEAK_THRESHOLD);
            }

            static int frequencyToChannel(int freq)
            {
                if (freq >= CHANNEL_2G_LOWER_FREQ && freq <= CHANNEL_2G_UPPER_FREQ)
                {
                    return (freq - CHANNEL_2G_OFFSET) / CHANNEL_FREQ_STEP;
                }
                if (freq >= CHANNEL_5G_LOWER_FREQ && freq <= CHANNEL_5G_UPPER_FREQ)
                {
                    return (freq - CHANNEL_5G_OFFSET) / CHANNEL_FREQ_STEP;
                }
                return 0;
            }

            static WifiSecurity parseSecurityType(const std::string& flags)
            {
                if (flags.find("WPA3") != std::string::npos)
                {
                    return WifiSecurity::WPA3_PSK;
                }
                if (flags.find("WPA2") != std::string::npos)
                {
                    return WifiSecurity::WPA2_PSK;
                }
                if (flags.find("WPA") != std::string::npos)
                {
                    return WifiSecurity::WPA_PSK;
                }
                if (flags.find("WEP") != std::string::npos)
                {
                    return WifiSecurity::WEP;
                }
                if (flags.find("ESS") != std::string::npos &&
                    flags.find("WPA") == std::string::npos)
                {
                    return WifiSecurity::NONE;
                }
                return WifiSecurity::UNKNOWN;
            }

            class WifiManager::Impl
            {
            public:
                explicit Impl(const WifiConfig& config);
                ~Impl();

                // 初始化和关闭
                WifiError init();
                void      deinit();

                // 扫描
                WifiError scanNetworks(const WifiScanCallback& callback,
                                       std::vector<WifiInfo>*  networks);
                void      setAutoScan(bool enabled);
                bool      isAutoScanRunning() const
                {
                    return auto_scan_running_;
                }

                // 连接
                WifiError connect(const std::string& ssid, const std::string& password,
                                  const WifiConnectCallback& callback);
                WifiError connectSavedNetwork();
                WifiError disconnect();
                WifiError reconnect();

                // 配置管理
                WifiError                     saveCurrentNetwork();
                WifiError                     forgetNetwork(const std::string& ssid);
                std::vector<SavedNetworkInfo> getSavedNetworks() const;
                bool                          isNetworkSaved(const std::string& ssid) const;
                WifiError setNetworkPriority(const std::string& ssid, int priority);
                WifiError enableNetworkAutoConnect(const std::string& ssid, bool enabled);
                WifiError reloadConfig();

                // 状态查询
                WifiState getState() const
                {
                    return current_state_;
                }
                bool isConnected() const
                {
                    if (current_state_ == WifiState::CONNECTED)
                    {
                        std::string ip = getIPAddressInternal();
                        if (ip.empty())
                        {
                            return false;
                        }
                        return true;
                    }
                    return false;
                }
                bool        isInterfaceUp() const;
                bool        getConnectionInfo(WifiConnectionInfo& info) const;
                std::string getCurrentSSID() const;
                std::string getIPAddress() const;
                int         getSignalStrength() const;

                // 自动重连
                void setAutoReconnect(bool enabled, const std::string& ssid,
                                      const std::string& password);
                bool isAutoReconnectEnabled() const
                {
                    return auto_reconnect_enabled_;
                }

                // 回调设置
                void setStateCallback(const WifiStateCallback& callback)
                {
                    state_callback_ = callback;
                }
                void setErrorCallback(const WifiErrorCallback& callback)
                {
                    error_callback_ = callback;
                }
                void setReconnectCallback(const WifiReconnectCallback& callback)
                {
                    reconnect_callback_ = callback;
                }

                // 统计信息
                void getStats(WifiManager::Stats& stats) const;
                void resetStats();

            private:
                // 内部辅助函数
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

                // 后台线程
                void autoScanThread();
                void autoReconnectThread();

                // 配置
                WifiConfig config_;

                // 状态
                std::atomic<WifiState> current_state_;
                std::atomic<bool>      initialized_;
                std::atomic<bool>      shutdown_requested_;

                // 自动扫描
                std::atomic<bool>            auto_scan_running_;
                std::unique_ptr<std::thread> auto_scan_thread_;

                // 自动重连
                std::atomic<bool>            auto_reconnect_enabled_;
                std::string                  reconnect_ssid_;
                std::string                  reconnect_password_;
                std::unique_ptr<std::thread> auto_reconnect_thread_;
                int                          reconnect_attempts_;

                // 回调
                WifiStateCallback     state_callback_;
                WifiErrorCallback     error_callback_;
                WifiReconnectCallback reconnect_callback_;

                // 统计
                mutable std::mutex stats_mutex_;
                WifiManager::Stats stats_;

                // 线程同步
                mutable std::mutex      mutex_;
                std::condition_variable cv_;
            };

            WifiManager::Impl::Impl(const WifiConfig& config)
                : config_(config), current_state_(WifiState::UNKNOWN), initialized_(false),
                  shutdown_requested_(false), auto_scan_running_(false),
                  auto_reconnect_enabled_(false), reconnect_attempts_(0)
            {

                memset(&stats_, 0, sizeof(stats_));
            }

            WifiManager::Impl::~Impl()
            {
                deinit();
            }

            WifiError WifiManager::Impl::init()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (initialized_)
                {
                    LOG_WARN(LOG_TAG, "WiFi 已初始化");
                    return WifiError::NONE;
                }

                LOG_INFO(LOG_TAG, "启动");

                // 检查前置条件
                WifiError err = checkPrerequisites();
                if (err != WifiError::NONE)
                {
                    return err;
                }

                // 确保接口UP
                err = ensureInterfaceUp();
                if (err != WifiError::NONE)
                {
                    return err;
                }

                // 确保wpa_supplicant运行
                err = ensureWpaSupplicantRunning();
                if (err != WifiError::NONE)
                {
                    return err;
                }

                std::string ssid = getCurrentSSIDInternal();
                if (!ssid.empty())
                {
                    // 检查wpa_supplicant状态
                    std::string status = executeWpaCli("status");
                    if (status.find("wpa_state=COMPLETED") != std::string::npos)
                    {
                        // WiFi已关联，检查IP地址
                        std::string ip = getIPAddressInternal();
                        if (!ip.empty())
                        {
                            // IP地址已获取，完全连接
                            current_state_ = WifiState::CONNECTED;
                            LOG_INFO(LOG_TAG, "已连接: %s (IP: %s)", ssid.c_str(), ip.c_str());
                        }
                        else
                        {
                            // WiFi已关联但IP未获取，尝试获取IP
                            LOG_INFO(LOG_TAG,
                                     "WiFi associated but IP not obtained, requesting DHCP...");
                            updateState(WifiState::OBTAINING_IP);

                            // 触发DHCP请求
                            std::string dhcp_cmd = "udhcpc -i " +
                                                   std::string(WifiConfig::INTERFACE_NAME) +
                                                   " -n -q -t 5 2>&1";
                            executeCommand(dhcp_cmd);

                            // 等待IP地址分配（最多等待config_.dhcp_timeout_ms）
                            auto start = std::chrono::steady_clock::now();
                            while (true)
                            {
                                ip = getIPAddressInternal();
                                if (!ip.empty())
                                {
                                    updateState(WifiState::CONNECTED);
                                    LOG_INFO(LOG_TAG, "IP: %s", ip.c_str());
                                    break;
                                }

                                auto elapsed =
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - start)
                                        .count();

                                if (elapsed >= config_.dhcp_timeout_ms)
                                {
                                    LOG_WARN(LOG_TAG,
                                             "DHCP timeout, WiFi associated but no IP address");
                                    updateState(WifiState::ASSOCIATED); // 设置为已关联但未获取IP
                                    break;
                                }

                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            }
                        }
                    }
                    else
                    {
                        // WiFi未完全连接
                        current_state_ = WifiState::DISCONNECTED;
                        LOG_INFO(LOG_TAG, "WiFi 未完全连接 状态: %s", status.c_str());
                    }
                }
                else
                {
                    current_state_ = WifiState::DISCONNECTED;
                }

                initialized_ = true;
                LOG_INFO(LOG_TAG, "就绪 状态: %s", wifiStateToString(current_state_));

                // 自动连接已保存WiFi
                if (config_.auto_connect_on_init && current_state_ != WifiState::CONNECTED)
                {
                    LOG_INFO(LOG_TAG, "自动连接中");

                    for (int attempt = 0; attempt < config_.auto_connect_max_attempts; ++attempt)
                    {
                        if (connectSavedNetwork() == WifiError::NONE)
                        {
                            stats_.auto_connects++;
                            LOG_INFO(LOG_TAG, "自动连接成功 %d", attempt + 1);
                            break;
                        }

                        if (attempt < config_.auto_connect_max_attempts - 1)
                        {
                            LOG_WARN(LOG_TAG, "自动连接失败 %d 重试", attempt + 1);
                            std::this_thread::sleep_for(
                                std::chrono::seconds(config_.reconnect_delay_sec));
                        }
                    }
                }

                return WifiError::NONE;
            }

            void WifiManager::Impl::deinit()
            {
                LOG_INFO(LOG_TAG, "关闭");

                shutdown_requested_ = true;

                // 停止自动扫描
                setAutoScan(false);

                // 停止自动重连
                setAutoReconnect(false, "", "");

                // 等待线程结束
                if (auto_scan_thread_ && auto_scan_thread_->joinable())
                {
                    cv_.notify_all();
                    auto_scan_thread_->join();
                }

                if (auto_reconnect_thread_ && auto_reconnect_thread_->joinable())
                {
                    cv_.notify_all();
                    auto_reconnect_thread_->join();
                }

                initialized_ = false;
                LOG_INFO(LOG_TAG, "已关闭");
            }

            WifiError WifiManager::Impl::checkPrerequisites()
            {
                // 检查wpa_cli
                std::string result = executeCommand("which wpa_cli 2>/dev/null");
                if (result.empty() || result.find("wpa_cli") == std::string::npos)
                {
                    LOG_ERROR(LOG_TAG, "wpa_cli 未找到，请安装 wpa_supplicant");
                    return WifiError::WPA_SUPPLICANT_NOT_FOUND;
                }

                // 检查接口是否存在
                result = executeCommand("ip link show " + std::string(WifiConfig::INTERFACE_NAME) +
                                        " 2>&1");
                if (result.find("does not exist") != std::string::npos)
                {
                    LOG_ERROR(LOG_TAG, "接口 %s 未找到", WifiConfig::INTERFACE_NAME);
                    return WifiError::INTERFACE_NOT_FOUND;
                }

                LOG_INFO(LOG_TAG, "接口 %s 就绪", WifiConfig::INTERFACE_NAME);
                return WifiError::NONE;
            }

            WifiError WifiManager::Impl::ensureInterfaceUp()
            {
                LOG_DEBUG(LOG_TAG, "检查接口状态");

                // 检查接口是否UP
                if (isInterfaceUp())
                {
                    LOG_DEBUG(LOG_TAG, "接口 %s 已 UP", WifiConfig::INTERFACE_NAME);
                    return WifiError::NONE;
                }

                // 接口未UP，尝试打开
                LOG_INFO(LOG_TAG, "接口 %s 启动中", WifiConfig::INTERFACE_NAME);
                std::string cmd =
                    "ip link set " + std::string(WifiConfig::INTERFACE_NAME) + " up 2>&1";
                std::string result = executeCommand(cmd);

                stats_.interface_up_count++;

                // 等待接口UP
                auto start = std::chrono::steady_clock::now();
                while (true)
                {
                    if (isInterfaceUp())
                    {
                        LOG_INFO(LOG_TAG, "接口 %s 已 UP", WifiConfig::INTERFACE_NAME);
                        return WifiError::NONE;
                    }

                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();

                    if (elapsed >= config_.interface_up_timeout_ms)
                    {
                        LOG_ERROR(LOG_TAG, "接口启动超时");
                        return WifiError::INTERFACE_NOT_UP;
                    }

                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(config_.interface_up_check_interval_ms));
                }
            }

            bool WifiManager::Impl::isInterfaceUp() const
            {
                std::string result = executeCommand(
                    "ip link show " + std::string(WifiConfig::INTERFACE_NAME) + " 2>&1");

                // 查找 <...UP...> 格式
                size_t start = result.find('<');
                size_t end   = result.find('>');
                if (start != std::string::npos && end != std::string::npos)
                {
                    std::string flags = result.substr(start + 1, end - start - 1);
                    return flags.find("UP") != std::string::npos;
                }

                return false;
            }

            WifiError WifiManager::Impl::ensureWpaSupplicantRunning()
            {
                // 检查wpa_supplicant是否运行
                std::string result = executeWpaCli("status");

                if (result.find("wpa_state") != std::string::npos)
                {
                    LOG_DEBUG(LOG_TAG, "wpa_supplicant 运行中");
                    return WifiError::NONE;
                }

                LOG_WARN(LOG_TAG, "wpa_supplicant 未运行，尝试启动");

                // 尝试启动wpa_supplicant
                std::string cmd = "wpa_supplicant -B -i " +
                                  std::string(WifiConfig::INTERFACE_NAME) + " -c " +
                                  std::string(WifiConfig::WPA_CONF_PATH) + " 2>&1";
                result = executeCommand(cmd);

                // 等待wpa_supplicant启动
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // 再次检查
                result = executeWpaCli("status");
                if (result.find("wpa_state") != std::string::npos)
                {
                    LOG_INFO(LOG_TAG, "wpa_supplicant 已启动");
                    return WifiError::NONE;
                }

                LOG_ERROR(LOG_TAG, "wpa_supplicant 启动失败: %s", result.c_str());
                return WifiError::WPA_SUPPLICANT_NOT_RUNNING;
            }

            WifiError WifiManager::Impl::scanNetworks(const WifiScanCallback& callback,
                                                      std::vector<WifiInfo>*  networks)
            {
                if (!initialized_)
                {
                    LOG_ERROR(LOG_TAG, "WiFi 未初始化");
                    return WifiError::INITIALIZATION_FAILED;
                }

                // 异步模式
                if (callback)
                {
                    WifiScanCallback callback_copy = callback;
                    std::thread(
                        [this, callback_copy]()
                        {
                            std::vector<WifiInfo> results;
                            WifiError             err = scanNetworksInternal(results);

                            if (err == WifiError::NONE)
                            {
                                callback_copy(results);
                            }
                            else
                            {
                                notifyError(err, "扫描失败");
                                callback_copy(results); // 返回空列表
                            }
                        })
                        .detach();

                    return WifiError::NONE;
                }

                // 同步模式
                if (!networks)
                {
                    LOG_ERROR(LOG_TAG, "同步模式下 networks 不能为空");
                    return WifiError::UNKNOWN;
                }

                return scanNetworksInternal(*networks);
            }

            WifiError WifiManager::Impl::scanNetworksInternal(std::vector<WifiInfo>& networks)
            {
                LOG_DEBUG(LOG_TAG, "扫描中");
                updateState(WifiState::SCANNING);

                stats_.scans_performed++;

                // 触发扫描
                std::string result = executeWpaCli("scan");
                if (!isWpaCommandSuccess(result))
                {
                    LOG_ERROR(LOG_TAG, "扫描启动失败: %s", result.c_str());
                    updateState(WifiState::DISCONNECTED);
                    return WifiError::SCAN_FAILED;
                }

                // 等待扫描完成
                std::this_thread::sleep_for(std::chrono::seconds(3));

                // 获取扫描结果
                result = executeWpaCli("scan_results");

                // 解析结果
                std::istringstream              iss(result);
                std::string                     line;
                std::map<std::string, WifiInfo> unique_networks; // 使用SSID去重

                // 跳过标题行
                std::getline(iss, line);

                while (std::getline(iss, line))
                {
                    line = trim(line);
                    if (line.empty())
                    {
                        continue;
                    }

                    // 格式: bssid / frequency / signal level / flags / ssid
                    std::istringstream line_stream(line);
                    WifiInfo           info;
                    std::string        bssid;
                    std::string        freq_str;
                    std::string        signal_str;
                    std::string        flags;

                    line_stream >> bssid >> freq_str >> signal_str >> flags;

                    // 剩余部分是SSID
                    std::string ssid;
                    std::getline(line_stream, ssid);
                    ssid = trim(ssid);

                    if (ssid.empty())
                    {
                        continue;
                    }

                    info.ssid            = ssid;
                    info.bssid           = bssid;
                    info.frequency       = std::atoi(freq_str.c_str());
                    info.channel         = frequencyToChannel(info.frequency);
                    info.signal_strength = signalToPercent(std::atoi(signal_str.c_str()));
                    info.security        = parseSecurityType(flags);

                    // 保留信号最强的
                    if (unique_networks.find(ssid) == unique_networks.end() ||
                        unique_networks[ssid].signal_strength < info.signal_strength)
                    {
                        unique_networks[ssid] = info;
                    }
                }

                // 转换为vector并按信号强度排序
                networks.clear();
                for (const auto& pair : unique_networks)
                {
                    networks.push_back(pair.second);
                }

                std::sort(networks.begin(), networks.end(),
                          [](const WifiInfo& a, const WifiInfo& b)
                          { return a.signal_strength > b.signal_strength; });

                LOG_INFO(LOG_TAG, "扫描完成 %zu 个网络", networks.size());

                // 恢复之前的状态（通过实际查询wpa_supplicant状态）
                std::string status = executeWpaCli("status");
                if (status.find("wpa_state=COMPLETED") != std::string::npos)
                {
                    updateState(WifiState::CONNECTED);
                }
                else
                {
                    updateState(WifiState::DISCONNECTED);
                }

                return WifiError::NONE;
            }

            void WifiManager::Impl::setAutoScan(bool enabled)
            {
                if (enabled == auto_scan_running_)
                {
                    return;
                }

                if (enabled)
                {
                    LOG_DEBUG(LOG_TAG, "自动扫描 间隔 %d 秒", config_.timed_scan_sec);
                    auto_scan_running_ = true;

                    auto_scan_thread_ =
                        std::make_unique<std::thread>([this]() { autoScanThread(); });
                }
                else
                {
                    LOG_DEBUG(LOG_TAG, "停止自动扫描");
                    auto_scan_running_ = false;
                    cv_.notify_all();

                    if (auto_scan_thread_ && auto_scan_thread_->joinable())
                    {
                        auto_scan_thread_->join();
                    }
                    auto_scan_thread_.reset();
                }
            }

            void WifiManager::Impl::autoScanThread()
            {
                LOG_DEBUG(LOG_TAG, "自动扫描线程已启动");

                while (auto_scan_running_ && !shutdown_requested_)
                {
                    std::vector<WifiInfo> networks;
                    scanNetworksInternal(networks);

                    // 等待下一次扫描
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait_for(lock, std::chrono::seconds(config_.timed_scan_sec),
                                 [this]() { return !auto_scan_running_ || shutdown_requested_; });
                }

                LOG_DEBUG(LOG_TAG, "自动扫描线程已停止");
            }

            WifiError WifiManager::Impl::connect(const std::string&         ssid,
                                                 const std::string&         password,
                                                 const WifiConnectCallback& callback)
            {
                if (!initialized_)
                {
                    LOG_ERROR(LOG_TAG, "WiFi 未初始化");
                    return WifiError::INITIALIZATION_FAILED;
                }

                // 异步模式
                if (callback)
                {
                    WifiConnectCallback callback_copy = callback;
                    std::thread(
                        [this, ssid, password, callback_copy]()
                        {
                            WifiError err = connectInternal(ssid, password);

                            if (err == WifiError::NONE)
                            {
                                callback_copy(true, "Connected successfully");
                            }
                            else
                            {
                                callback_copy(false, wifiErrorToString(err));
                            }
                        })
                        .detach();

                    return WifiError::NONE;
                }

                // 同步模式
                return connectInternal(ssid, password);
            }

            WifiError WifiManager::Impl::connectInternal(const std::string& ssid,
                                                         const std::string& password)
            {
                LOG_INFO(LOG_TAG, "连接: %s", ssid.c_str());

                stats_.connections_attempted++;

                // 检查是否已连接到同一个SSID
                if (current_state_ == WifiState::CONNECTED)
                {
                    std::string current_ssid = getCurrentSSIDInternal();
                    if (!current_ssid.empty() && current_ssid == ssid)
                    {
                        LOG_INFO(LOG_TAG, "已连接 %s", ssid.c_str());
                        return WifiError::ALREADY_CONNECTED;
                    }

                    // 如果已连接到其他WiFi，先断开
                    if (!current_ssid.empty())
                    {
                        LOG_INFO(LOG_TAG, "断开 %s", current_ssid.c_str());
                        disconnect();
                    }
                }

                updateState(WifiState::CONNECTING);

                // 删除旧配置
                if (config_.clear_old_config_on_connect)
                {
                    int old_net_id = findNetworkIdBySSID(ssid);
                    if (old_net_id >= 0)
                    {
                        LOG_DEBUG(LOG_TAG, "移除旧配置 ID=%d", old_net_id);
                        std::string remove_cmd = "remove_network " + std::to_string(old_net_id);
                        executeWpaCli(remove_cmd);
                    }
                }

                // 添加新网络配置
                std::string output = executeWpaCli("add_network");
                int         net_id = -1;
                try
                {
                    net_id = std::stoi(trim(output));
                    LOG_DEBUG(LOG_TAG, "新建网络 ID=%d", net_id);
                }
                catch (...)
                {
                    LOG_ERROR(LOG_TAG, "添加网络失败: %s", output.c_str());
                    updateState(WifiState::FAILED);
                    return WifiError::CONNECTION_FAILED;
                }

                // 设置SSID
                std::string cmd =
                    "set_network " + std::to_string(net_id) + " ssid '\"" + ssid + "\"'";
                output = executeWpaCli(cmd);
                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "设置 SSID 失败: %s", output.c_str());
                    executeWpaCli("remove_network " + std::to_string(net_id));
                    updateState(WifiState::FAILED);
                    return WifiError::CONNECTION_FAILED;
                }

                // 设置密码或开放网络
                if (password.empty())
                {
                    cmd = "set_network " + std::to_string(net_id) + " key_mgmt NONE";
                }
                else
                {
                    cmd = "set_network " + std::to_string(net_id) + " psk '\"" + password + "\"'";
                }

                output = executeWpaCli(cmd);
                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "设置密码失败: %s", output.c_str());
                    executeWpaCli("remove_network " + std::to_string(net_id));
                    updateState(WifiState::FAILED);
                    stats_.password_errors++;
                    return WifiError::PASSWORD_INCORRECT;
                }

                // 选择网络
                cmd    = "select_network " + std::to_string(net_id);
                output = executeWpaCli(cmd);
                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "选择网络失败: %s", output.c_str());
                    executeWpaCli("remove_network " + std::to_string(net_id));
                    updateState(WifiState::FAILED);
                    return WifiError::CONNECTION_FAILED;
                }

                LOG_DEBUG(LOG_TAG, "等待连接");

                // 等待连接成功
                auto start              = std::chrono::steady_clock::now();
                int  disconnected_count = 0;

                while (true)
                {
                    std::string status = executeWpaCli("status");

                    // 检查wpa_state
                    if (status.find("wpa_state=COMPLETED") != std::string::npos)
                    {
                        updateState(WifiState::OBTAINING_IP);
                        LOG_DEBUG(LOG_TAG, "获取 IP");

                        // 获取IP地址（使用udhcpc）
                        std::string dhcp_cmd = "udhcpc -i " +
                                               std::string(WifiConfig::INTERFACE_NAME) +
                                               " -n -q -t 5 2>&1";
                        std::string dhcp_result = executeCommand(dhcp_cmd);

                        std::string ip = getIPAddressInternal();
                        if (!ip.empty())
                        {
                            updateState(WifiState::CONNECTED);
                            stats_.connections_successful++;

                            // 保存配置
                            if (config_.auto_save_config)
                            {
                                saveCurrentNetwork();
                            }

                            LOG_INFO(LOG_TAG, "已连接 IP: %s", ip.c_str());
                            return WifiError::NONE;
                        }
                        LOG_ERROR(LOG_TAG, "DHCP 失败: %s", dhcp_result.c_str());
                        executeWpaCli("remove_network " + std::to_string(net_id));
                        updateState(WifiState::FAILED);
                        return WifiError::DHCP_FAILED;
                    }

                    // 检测密码错误（连续DISCONNECTED状态）
                    if (status.find("wpa_state=DISCONNECTED") != std::string::npos ||
                        status.find("wpa_state=INACTIVE") != std::string::npos)
                    {
                        disconnected_count++;
                        if (disconnected_count >= PASSWORD_FAIL_THRESHOLD)
                        {
                            LOG_ERROR(LOG_TAG, "认证失败（密码错误?）");
                            executeWpaCli("remove_network " + std::to_string(net_id));
                            updateState(WifiState::FAILED);
                            stats_.password_errors++;
                            return WifiError::AUTHENTICATION_FAILED;
                        }
                    }
                    else
                    {
                        disconnected_count = 0; // 重置计数器
                    }

                    // 检查超时
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();

                    if (elapsed >= config_.connect_timeout_ms)
                    {
                        LOG_ERROR(LOG_TAG, "连接超时");
                        executeWpaCli("remove_network " + std::to_string(net_id));
                        updateState(WifiState::FAILED);
                        return WifiError::TIMEOUT;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
                }
            }

            WifiError WifiManager::Impl::connectSavedNetwork()
            {
                LOG_INFO(LOG_TAG, "连接已保存网络");

                // 获取已保存的网络列表
                std::vector<SavedNetworkInfo> saved = getSavedNetworks();
                if (saved.empty())
                {
                    LOG_WARN(LOG_TAG, "无已保存网络");
                    return WifiError::NETWORK_NOT_FOUND;
                }

                // 扫描可用网络
                std::vector<WifiInfo> available;
                WifiError             err = scanNetworksInternal(available);
                if (err != WifiError::NONE)
                {
                    return err;
                }

                // 创建候选列表
                struct Candidate
                {
                    std::string ssid;
                    int         priority;
                    int         signal;
                };

                std::vector<Candidate> candidates;

                for (const auto& saved_net : saved)
                {
                    if (!saved_net.is_enabled_auto)
                    {
                        continue; // 跳过禁用自动连接的网络
                    }

                    // 查找是否在可用列表中
                    for (const auto& avail : available)
                    {
                        if (avail.ssid == saved_net.ssid)
                        {
                            // 检查信号强度
                            if (avail.signal_strength >= config_.auto_connect_min_signal)
                            {
                                candidates.push_back(
                                    {saved_net.ssid, saved_net.priority, avail.signal_strength});
                            }
                            break;
                        }
                    }
                }

                if (candidates.empty())
                {
                    LOG_WARN(LOG_TAG, "范围内无可用已保存网络");
                    return WifiError::NETWORK_NOT_FOUND;
                }

                // 排序候选列表
                std::sort(candidates.begin(), candidates.end(),
                          [](const Candidate& a, const Candidate& b)
                          {
                              if (a.priority != b.priority)
                              {
                                  return a.priority > b.priority;
                              }
                              return a.signal > b.signal;
                          });

                // 尝试连接最佳候选
                const auto& best = candidates[0];
                LOG_INFO(LOG_TAG, "选择网络: %s 优先级=%d 信号=%d%%", best.ssid.c_str(),
                         best.priority, best.signal);

                // 使用wpa_cli重新连接
                int net_id = findNetworkIdBySSID(best.ssid);
                if (net_id < 0)
                {
                    LOG_ERROR(LOG_TAG, "网络 %s 未找到", best.ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                // 断开当前连接
                executeWpaCli("disconnect");
                std::this_thread::sleep_for(std::chrono::seconds(1));

                // 启用并选择网络
                std::string cmd    = "select_network " + std::to_string(net_id);
                std::string output = executeWpaCli(cmd);

                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "选择网络失败: %s", output.c_str());
                    return WifiError::CONNECTION_FAILED;
                }

                // 等待连接
                updateState(WifiState::CONNECTING);
                auto start = std::chrono::steady_clock::now();

                while (true)
                {
                    std::string status = executeWpaCli("status");

                    if (status.find("wpa_state=COMPLETED") != std::string::npos)
                    {
                        updateState(WifiState::OBTAINING_IP);

                        // 获取IP
                        std::string dhcp_cmd = "udhcpc -i " +
                                               std::string(WifiConfig::INTERFACE_NAME) +
                                               " -n -q -t 5 2>&1";
                        executeCommand(dhcp_cmd);

                        std::string ip = getIPAddressInternal();
                        if (!ip.empty())
                        {
                            updateState(WifiState::CONNECTED);
                            LOG_INFO(LOG_TAG, "已连接 %s IP: %s", best.ssid.c_str(), ip.c_str());
                            return WifiError::NONE;
                        }
                    }

                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();

                    if (elapsed >= config_.connect_timeout_ms)
                    {
                        LOG_ERROR(LOG_TAG, "连接超时");
                        updateState(WifiState::FAILED);
                        return WifiError::TIMEOUT;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
                }
            }

            WifiError WifiManager::Impl::disconnect()
            {
                LOG_INFO(LOG_TAG, "断开中");

                std::string output = executeWpaCli("disconnect");

                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "断开失败: %s", output.c_str());
                    return WifiError::DISCONNECTION_FAILED;
                }

                // 释放IP
                std::string cmd = "ip addr flush dev " + std::string(WifiConfig::INTERFACE_NAME);
                executeCommand(cmd);

                updateState(WifiState::DISCONNECTED);
                stats_.disconnections++;

                LOG_INFO(LOG_TAG, "已断开");
                return WifiError::NONE;
            }

            WifiError WifiManager::Impl::reconnect()
            {
                LOG_INFO(LOG_TAG, "重连中");

                stats_.reconnects++;

                std::string output = executeWpaCli("reconnect");

                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "重连失败: %s", output.c_str());
                    return WifiError::CONNECTION_FAILED;
                }

                // 等待连接
                updateState(WifiState::CONNECTING);
                std::this_thread::sleep_for(std::chrono::seconds(3));

                if (isConnected())
                {
                    LOG_INFO(LOG_TAG, "重连成功");
                    return WifiError::NONE;
                }
                LOG_ERROR(LOG_TAG, "重连失败");
                return WifiError::CONNECTION_FAILED;
            }

            WifiError WifiManager::Impl::saveCurrentNetwork()
            {
                LOG_DEBUG(LOG_TAG, "保存配置");

                std::string output = executeWpaCli("save_config");

                if (isWpaCommandSuccess(output))
                {
                    stats_.config_saves++;
                    LOG_INFO(LOG_TAG, "配置已保存");
                    return WifiError::NONE;
                }
                LOG_ERROR(LOG_TAG, "保存配置失败: %s", output.c_str());
                return WifiError::CONFIG_FILE_ERROR;
            }

            WifiError WifiManager::Impl::forgetNetwork(const std::string& ssid)
            {
                LOG_DEBUG(LOG_TAG, "忘记网络: %s", ssid.c_str());

                int net_id = findNetworkIdBySSID(ssid);
                if (net_id < 0)
                {
                    LOG_WARN(LOG_TAG, "网络 %s 未在已保存列表中", ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                // 如果当前连接的是要删除的网络，先断开
                std::string current_ssid = getCurrentSSIDInternal();
                if (current_ssid == ssid)
                {
                    disconnect();
                }

                std::string cmd    = "remove_network " + std::to_string(net_id);
                std::string output = executeWpaCli(cmd);

                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "移除网络失败: %s", output.c_str());
                    return WifiError::CONFIG_FILE_ERROR;
                }

                // 保存配置
                saveCurrentNetwork();

                LOG_INFO(LOG_TAG, "已移除网络 %s", ssid.c_str());
                return WifiError::NONE;
            }

            std::vector<SavedNetworkInfo> WifiManager::Impl::getSavedNetworks() const
            {
                std::vector<SavedNetworkInfo> networks;

                std::string output = executeWpaCli("list_networks");

                std::istringstream iss(output);
                std::string        line;

                // 跳过标题行
                std::getline(iss, line);

                while (std::getline(iss, line))
                {
                    line = trim(line);
                    if (line.empty())
                    {
                        continue;
                    }

                    // 格式: network id / ssid / bssid / flags
                    std::istringstream line_stream(line);
                    SavedNetworkInfo   info;

                    std::string id_str;
                    std::string ssid;
                    std::string bssid;
                    std::string flags;
                    line_stream >> id_str >> ssid >> bssid;
                    std::getline(line_stream, flags);
                    flags = trim(flags);

                    info.network_id      = std::atoi(id_str.c_str());
                    info.ssid            = ssid;
                    info.is_current      = (flags.find("CURRENT") != std::string::npos);
                    info.is_enabled_auto = (flags.find("DISABLED") == std::string::npos);
                    info.priority        = 0; // 默认优先级

                    networks.push_back(info);
                }

                // 从配置文件读取优先级
                std::ifstream conf_file(WifiConfig::WPA_CONF_PATH);
                if (conf_file.is_open())
                {
                    std::string conf_line;
                    int         current_net_id = -1;

                    while (std::getline(conf_file, conf_line))
                    {
                        conf_line = trim(conf_line);

                        if (conf_line.find("network=") == 0)
                        {
                            current_net_id = -1; // 新网络块
                        }
                        else if (conf_line.find("id_str=") == 0)
                        {
                            // 提取ID
                            size_t pos = conf_line.find('=');
                            if (pos != std::string::npos)
                            {
                                std::string id = conf_line.substr(pos + 1);
                                id             = trim(id);
                                // 移除引号
                                if (!id.empty() && id[0] == '"')
                                {
                                    id = id.substr(1, id.length() - 2);
                                }
                                current_net_id = std::atoi(id.c_str());
                            }
                        }
                        else if (conf_line.find("priority=") == 0 && current_net_id >= 0)
                        {
                            size_t pos = conf_line.find('=');
                            if (pos != std::string::npos)
                            {
                                int priority = std::atoi(conf_line.substr(pos + 1).c_str());

                                // 更新对应网络的优先级
                                for (auto& net : networks)
                                {
                                    if (net.network_id == current_net_id)
                                    {
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

            bool WifiManager::Impl::isNetworkSaved(const std::string& ssid) const
            {
                return findNetworkIdBySSID(ssid) >= 0;
            }

            WifiError WifiManager::Impl::setNetworkPriority(const std::string& ssid, int priority)
            {
                int net_id = findNetworkIdBySSID(ssid);
                if (net_id < 0)
                {
                    LOG_WARN(LOG_TAG, "网络 %s 未找到", ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                std::string cmd = "set_network " + std::to_string(net_id) + " priority " +
                                  std::to_string(priority);
                std::string output = executeWpaCli(cmd);

                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "设置优先级失败: %s", output.c_str());
                    return WifiError::CONFIG_FILE_ERROR;
                }

                saveCurrentNetwork();
                LOG_DEBUG(LOG_TAG, "优先级 %d %s", priority, ssid.c_str());
                return WifiError::NONE;
            }

            WifiError WifiManager::Impl::enableNetworkAutoConnect(const std::string& ssid,
                                                                  bool               enabled)
            {
                int net_id = findNetworkIdBySSID(ssid);
                if (net_id < 0)
                {
                    LOG_WARN(LOG_TAG, "网络 %s 未找到", ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                std::string cmd    = enabled ? ("enable_network " + std::to_string(net_id))
                                             : ("disable_network " + std::to_string(net_id));
                std::string output = executeWpaCli(cmd);

                if (!isWpaCommandSuccess(output))
                {
                    LOG_ERROR(LOG_TAG, "%s 网络失败: %s", enabled ? "启用" : "禁用",
                              output.c_str());
                    return WifiError::CONFIG_FILE_ERROR;
                }

                saveCurrentNetwork();
                LOG_DEBUG(LOG_TAG, "自动连接 %s %s", enabled ? "开" : "关", ssid.c_str());
                return WifiError::NONE;
            }

            WifiError WifiManager::Impl::reloadConfig()
            {
                LOG_DEBUG(LOG_TAG, "重载配置");

                std::string output = executeWpaCli("reconfigure");

                if (isWpaCommandSuccess(output))
                {
                    LOG_INFO(LOG_TAG, "配置已重载");
                    return WifiError::NONE;
                }
                LOG_ERROR(LOG_TAG, "重载配置失败: %s", output.c_str());
                return WifiError::CONFIG_FILE_ERROR;
            }

            int WifiManager::Impl::findNetworkIdBySSID(const std::string& ssid) const
            {
                std::vector<SavedNetworkInfo> networks = getSavedNetworks();

                for (const auto& net : networks)
                {
                    if (net.ssid == ssid)
                    {
                        return net.network_id;
                    }
                }

                return -1;
            }

            bool WifiManager::Impl::getConnectionInfo(WifiConnectionInfo& info) const
            {
                if (!isConnected())
                {
                    return false;
                }

                info.ssid            = getCurrentSSIDInternal();
                info.ip_address      = getIPAddressInternal();
                info.signal_strength = getSignalStrengthInternal();
                info.state           = current_state_;

                // 获取BSSID和安全类型
                std::string status = executeWpaCli("status");

                std::istringstream iss(status);
                std::string        line;

                while (std::getline(iss, line))
                {
                    if (line.find("bssid=") == 0)
                    {
                        info.bssid = trim(line.substr(BSSID_PREFIX_LENGTH));
                    }
                    else if (line.find("key_mgmt=") == 0)
                    {
                        std::string key_mgmt = trim(line.substr(KEY_MGMT_PREFIX_LENGTH));
                        if (key_mgmt == "WPA2-PSK")
                        {
                            info.security = WifiSecurity::WPA2_PSK;
                        }
                        else if (key_mgmt == "WPA-PSK")
                        {
                            info.security = WifiSecurity::WPA_PSK;
                        }
                        else if (key_mgmt == "NONE")
                        {
                            info.security = WifiSecurity::NONE;
                        }
                    }
                }

                return true;
            }

            std::string WifiManager::Impl::getCurrentSSID() const
            {
                return getCurrentSSIDInternal();
            }

            std::string WifiManager::Impl::getCurrentSSIDInternal() const
            {
                std::string status = executeWpaCli("status");

                std::istringstream iss(status);
                std::string        line;

                while (std::getline(iss, line))
                {
                    if (line.find("ssid=") == 0)
                    {
                        std::string ssid = trim(line.substr(SSID_PREFIX_LENGTH));

                        // 检查是否是MAC地址格式（xx:xx:xx:xx:xx:xx）
                        if (ssid.length() == MAC_ADDRESS_LENGTH && ssid[MAC_COLON_POS_1] == ':' &&
                            ssid[MAC_COLON_POS_2] == ':')
                        {
                            // 这是BSSID，尝试从list_networks获取真实SSID
                            std::string        list_output = executeWpaCli("list_networks");
                            std::istringstream list_iss(list_output);
                            std::string        list_line;

                            // 跳过标题
                            std::getline(list_iss, list_line);

                            while (std::getline(list_iss, list_line))
                            {
                                if (list_line.find("CURRENT") != std::string::npos)
                                {
                                    std::istringstream line_stream(list_line);
                                    std::string        id;
                                    std::string        real_ssid;
                                    line_stream >> id >> real_ssid;
                                    if (!real_ssid.empty())
                                    {
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

            std::string WifiManager::Impl::getIPAddress() const
            {
                return getIPAddressInternal();
            }

            std::string WifiManager::Impl::getIPAddressInternal() const
            {
                std::string cmd =
                    "ip addr show " + std::string(WifiConfig::INTERFACE_NAME) + " 2>&1";
                std::string output = executeCommand(cmd);

                // 查找 inet xxx.xxx.xxx.xxx
                size_t pos = output.find("inet ");
                if (pos != std::string::npos)
                {
                    pos += INET_PREFIX_LENGTH; // 跳过 "inet "
                    size_t end = output.find('/', pos);
                    if (end != std::string::npos)
                    {
                        return trim(output.substr(pos, end - pos));
                    }
                }

                return "";
            }

            int WifiManager::Impl::getSignalStrength() const
            {
                return getSignalStrengthInternal();
            }

            int WifiManager::Impl::getSignalStrengthInternal() const
            {
                std::string output = executeWpaCli("signal_poll");

                std::istringstream iss(output);
                std::string        line;

                while (std::getline(iss, line))
                {
                    if (line.find("RSSI=") == 0)
                    {
                        int rssi = std::atoi(line.substr(RSSI_PREFIX_LENGTH).c_str());
                        return signalToPercent(rssi);
                    }
                }

                return 0;
            }

            void WifiManager::Impl::setAutoReconnect(bool enabled, const std::string& ssid,
                                                     const std::string& password)
            {
                if (enabled)
                {
                    LOG_DEBUG(LOG_TAG, "启用自动重连");

                    reconnect_ssid_         = ssid.empty() ? getCurrentSSIDInternal() : ssid;
                    reconnect_password_     = password;
                    reconnect_attempts_     = 0;
                    auto_reconnect_enabled_ = true;

                    if (!auto_reconnect_thread_ || !auto_reconnect_thread_->joinable())
                    {
                        auto_reconnect_thread_ =
                            std::make_unique<std::thread>([this]() { autoReconnectThread(); });
                    }

                    LOG_INFO(LOG_TAG, "自动重连已启用: %s", reconnect_ssid_.c_str());
                }
                else
                {
                    LOG_DEBUG(LOG_TAG, "禁用自动重连");

                    auto_reconnect_enabled_ = false;
                    cv_.notify_all();

                    if (auto_reconnect_thread_ && auto_reconnect_thread_->joinable())
                    {
                        auto_reconnect_thread_->join();
                    }
                    auto_reconnect_thread_.reset();

                    LOG_INFO(LOG_TAG, "自动重连已禁用");
                }
            }

            void WifiManager::Impl::autoReconnectThread()
            {
                LOG_DEBUG(LOG_TAG, "自动重连线程已启动");

                while (auto_reconnect_enabled_ && !shutdown_requested_)
                {
                    // 检查连接状态
                    if (!isConnected() && !reconnect_ssid_.empty())
                    {
                        // 检查重连次数限制
                        if (config_.reconnect_max_attempts > 0 &&
                            reconnect_attempts_ >= config_.reconnect_max_attempts)
                        {
                            LOG_WARN(LOG_TAG, "重连次数已达上限 %d",
                                     config_.reconnect_max_attempts);
                            notifyError(WifiError::TIMEOUT, "重连次数已达上限");
                            break;
                        }

                        reconnect_attempts_++;
                        LOG_INFO(LOG_TAG, "自动重连 %d/%s", reconnect_attempts_,
                                 reconnect_ssid_.c_str());

                        stats_.auto_reconnects++;

                        WifiError err = connectInternal(reconnect_ssid_, reconnect_password_);

                        if (reconnect_callback_)
                        {
                            reconnect_callback_(err == WifiError::NONE, reconnect_ssid_);
                        }

                        if (err == WifiError::NONE)
                        {
                            LOG_INFO(LOG_TAG, "自动重连成功");
                            reconnect_attempts_ = 0; // 重置计数器
                        }
                        else
                        {
                            LOG_WARN(LOG_TAG, "自动重连失败: %s", wifiErrorToString(err));
                        }
                    }
                    else if (isConnected())
                    {
                        reconnect_attempts_ = 0; // 已连接，重置计数器
                    }

                    // 等待下一次检查
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait_for(lock, std::chrono::seconds(config_.reconnect_interval_sec),
                                 [this]()
                                 { return !auto_reconnect_enabled_ || shutdown_requested_; });
                }

                LOG_DEBUG(LOG_TAG, "自动重连线程已停止");
            }

            void WifiManager::Impl::updateState(WifiState new_state)
            {
                WifiState old_state = current_state_.exchange(new_state);

                if (old_state != new_state)
                {
                    if (config_.enable_detailed_logging)
                    {
                        LOG_DEBUG(LOG_TAG, "状态: %s -> %s", wifiStateToString(old_state),
                                  wifiStateToString(new_state));
                    }

                    if (state_callback_)
                    {
                        state_callback_(old_state, new_state);
                    }
                }
            }

            void WifiManager::Impl::notifyError(WifiError error, const std::string& message)
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.errors++;

                LOG_ERROR(LOG_TAG, "错误 %s: %s", wifiErrorToString(error), message.c_str());

                if (error_callback_)
                {
                    error_callback_(error, message);
                }
            }

            void WifiManager::Impl::getStats(WifiManager::Stats& stats) const
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats = stats_;
            }

            void WifiManager::Impl::resetStats()
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                memset(&stats_, 0, sizeof(stats_));
                LOG_DEBUG(LOG_TAG, "统计已重置");
            }

            WifiManager::WifiManager(const WifiConfig& config)
                : pImpl_(std::make_unique<Impl>(config))
            {
            }

            WifiManager::~WifiManager() = default;

            WifiError WifiManager::init()
            {
                return pImpl_->init();
            }

            void WifiManager::deinit()
            {
                pImpl_->deinit();
            }

            WifiError WifiManager::scanNetworks(const WifiScanCallback& callback,
                                                std::vector<WifiInfo>*  networks)
            {
                return pImpl_->scanNetworks(callback, networks);
            }

            void WifiManager::setAutoScan(bool enabled)
            {
                pImpl_->setAutoScan(enabled);
            }

            bool WifiManager::isAutoScanRunning() const
            {
                return pImpl_->isAutoScanRunning();
            }

            WifiError WifiManager::connect(const std::string& ssid, const std::string& password,
                                           const WifiConnectCallback& callback)
            {
                return pImpl_->connect(ssid, password, callback);
            }

            WifiError WifiManager::connectSavedNetwork()
            {
                return pImpl_->connectSavedNetwork();
            }

            WifiError WifiManager::disconnect()
            {
                return pImpl_->disconnect();
            }

            WifiError WifiManager::reconnect()
            {
                return pImpl_->reconnect();
            }

            WifiError WifiManager::saveCurrentNetwork()
            {
                return pImpl_->saveCurrentNetwork();
            }

            WifiError WifiManager::forgetNetwork(const std::string& ssid)
            {
                return pImpl_->forgetNetwork(ssid);
            }

            std::vector<SavedNetworkInfo> WifiManager::getSavedNetworks() const
            {
                return pImpl_->getSavedNetworks();
            }

            bool WifiManager::isNetworkSaved(const std::string& ssid) const
            {
                return pImpl_->isNetworkSaved(ssid);
            }

            WifiError WifiManager::setNetworkPriority(const std::string& ssid, int priority)
            {
                return pImpl_->setNetworkPriority(ssid, priority);
            }

            WifiError WifiManager::enableNetworkAutoConnect(const std::string& ssid, bool enabled)
            {
                return pImpl_->enableNetworkAutoConnect(ssid, enabled);
            }

            WifiError WifiManager::reloadConfig()
            {
                return pImpl_->reloadConfig();
            }

            WifiState WifiManager::getState() const
            {
                return pImpl_->getState();
            }

            bool WifiManager::isConnected() const
            {
                return pImpl_->isConnected();
            }

            bool WifiManager::isInterfaceUp() const
            {
                return pImpl_->isInterfaceUp();
            }

            bool WifiManager::getConnectionInfo(WifiConnectionInfo& info) const
            {
                return pImpl_->getConnectionInfo(info);
            }

            std::string WifiManager::getCurrentSSID() const
            {
                return pImpl_->getCurrentSSID();
            }

            std::string WifiManager::getIPAddress() const
            {
                return pImpl_->getIPAddress();
            }

            int WifiManager::getSignalStrength() const
            {
                return pImpl_->getSignalStrength();
            }

            void WifiManager::setAutoReconnect(bool enabled, const std::string& ssid,
                                               const std::string& password)
            {
                pImpl_->setAutoReconnect(enabled, ssid, password);
            }

            bool WifiManager::isAutoReconnectEnabled() const
            {
                return pImpl_->isAutoReconnectEnabled();
            }

            void WifiManager::setStateCallback(const WifiStateCallback& callback)
            {
                pImpl_->setStateCallback(callback);
            }

            void WifiManager::setErrorCallback(const WifiErrorCallback& callback)
            {
                pImpl_->setErrorCallback(callback);
            }

            void WifiManager::setReconnectCallback(const WifiReconnectCallback& callback)
            {
                pImpl_->setReconnectCallback(callback);
            }

            void WifiManager::getStats(Stats& stats) const
            {
                pImpl_->getStats(stats);
            }

            void WifiManager::resetStats()
            {
                pImpl_->resetStats();
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
