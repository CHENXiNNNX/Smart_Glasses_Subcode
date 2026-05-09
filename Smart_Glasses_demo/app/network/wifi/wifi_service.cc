/* wifi_service.cc */

#include "wifi_service.hpp"
#include "wifi_parsers.hpp"
#include "../../tool/log/log.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            using namespace app::tool::log;

            namespace
            {
                constexpr const char* LOG_TAG = "WiFi";

                constexpr int         POLL_INTERVAL_MS        = 500;
                constexpr int         PASSWORD_FAIL_THRESHOLD = 6;
                constexpr std::size_t MAC_ADDRESS_LENGTH      = 17;
                constexpr std::size_t MAC_COLON_POS_1         = 2;
                constexpr std::size_t MAC_COLON_POS_2         = 5;
            } // namespace

            WifiService::WifiService(WifiConfig config, WifiPorts ports)
                : config_(std::move(config)), shell_(std::move(ports.shell)),
                  wpa_(std::move(ports.wpa)), link_(std::move(ports.link)),
                  dhcp_(std::move(ports.dhcp)), current_state_(WifiState::UNKNOWN),
                  initialized_(false), shutdown_requested_(false), auto_scan_running_(false),
                  auto_reconnect_enabled_(false), reconnect_attempts_(0)
            {
                memset(&stats_, 0, sizeof(stats_));
            }

            WifiService::~WifiService()
            {
                deinit();
            }

            WifiError WifiService::checkPrerequisites()
            {
                std::string result = shell_->run("which wpa_cli 2>/dev/null");
                if (result.empty() || result.find("wpa_cli") == std::string::npos)
                {
                    LOG_ERROR(LOG_TAG, "wpa_cli 未找到，请安装 wpa_supplicant");
                    return WifiError::WPA_SUPPLICANT_NOT_FOUND;
                }

                result = link_->linkShow(config_.interface_name);
                if (result.find("does not exist") != std::string::npos)
                {
                    LOG_ERROR(LOG_TAG, "接口 %s 未找到", config_.interface_name.c_str());
                    return WifiError::INTERFACE_NOT_FOUND;
                }

                LOG_INFO(LOG_TAG, "接口 %s 就绪", config_.interface_name.c_str());
                return WifiError::NONE;
            }

            bool WifiService::isInterfaceUp() const
            {
                return linkShowHasUpFlag(link_->linkShow(config_.interface_name));
            }

            WifiError WifiService::ensureInterfaceUp()
            {
                LOG_DEBUG(LOG_TAG, "检查接口状态");

                if (isInterfaceUp())
                {
                    LOG_DEBUG(LOG_TAG, "接口 %s 已 UP", config_.interface_name.c_str());
                    return WifiError::NONE;
                }

                LOG_INFO(LOG_TAG, "接口 %s 启动中", config_.interface_name.c_str());
                link_->setLinkUp(config_.interface_name);
                stats_.interface_up_count++;

                auto start = std::chrono::steady_clock::now();
                while (true)
                {
                    if (isInterfaceUp())
                    {
                        LOG_INFO(LOG_TAG, "接口 %s 已 UP", config_.interface_name.c_str());
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

            WifiError WifiService::ensureWpaSupplicantRunning()
            {
                std::string result = wpa_->cli("status");

                if (result.find("wpa_state") != std::string::npos)
                {
                    LOG_DEBUG(LOG_TAG, "wpa_supplicant 运行中");
                    return WifiError::NONE;
                }

                LOG_WARN(LOG_TAG, "wpa_supplicant 未运行，尝试启动");

                std::string cmd = "wpa_supplicant -B -i " + config_.interface_name + " -c " +
                                  config_.wpa_conf_path + " 2>&1";
                shell_->run(cmd);

                std::this_thread::sleep_for(std::chrono::seconds(2));

                result = wpa_->cli("status");
                if (result.find("wpa_state") != std::string::npos)
                {
                    LOG_INFO(LOG_TAG, "wpa_supplicant 已启动");
                    return WifiError::NONE;
                }

                LOG_ERROR(LOG_TAG, "wpa_supplicant 启动失败: %s", result.c_str());
                return WifiError::WPA_SUPPLICANT_NOT_RUNNING;
            }

            void WifiService::updateState(WifiState new_state)
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
                        state_callback_(old_state, new_state);
                }
            }

            void WifiService::notifyError(WifiError error, const std::string& message)
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.errors++;

                LOG_ERROR(LOG_TAG, "错误 %s: %s", wifiErrorToString(error), message.c_str());

                if (error_callback_)
                    error_callback_(error, message);
            }

            std::string WifiService::getIPAddressInternal() const
            {
                return parseIPv4FromAddrShow(link_->addrShow(config_.interface_name));
            }

            std::string WifiService::getCurrentSSIDInternal() const
            {
                std::string status = wpa_->cli("status");

                std::istringstream iss(status);
                std::string        line;

                while (std::getline(iss, line))
                {
                    if (line.find("ssid=") == 0)
                    {
                        std::string ssid = trim(line.substr(5));

                        if (ssid.length() == MAC_ADDRESS_LENGTH && ssid[MAC_COLON_POS_1] == ':' &&
                            ssid[MAC_COLON_POS_2] == ':')
                        {
                            std::string        list_output = wpa_->cli("list_networks");
                            std::istringstream list_iss(list_output);
                            std::string        list_line;
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
                                        return real_ssid;
                                }
                            }
                        }

                        return ssid;
                    }
                }

                return "";
            }

            int WifiService::getSignalStrengthInternal() const
            {
                return signalDbmToPercent(parseSignalPollRssi(wpa_->cli("signal_poll")));
            }

            bool WifiService::isConnected() const
            {
                if (current_state_ == WifiState::CONNECTED)
                {
                    std::string ip = getIPAddressInternal();
                    return !ip.empty();
                }
                return false;
            }

            WifiError WifiService::init()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (initialized_)
                {
                    LOG_WARN(LOG_TAG, "WiFi 已初始化");
                    return WifiError::NONE;
                }

                LOG_INFO(LOG_TAG, "启动");

                WifiError err = checkPrerequisites();
                if (err != WifiError::NONE)
                    return err;

                err = ensureInterfaceUp();
                if (err != WifiError::NONE)
                    return err;

                err = ensureWpaSupplicantRunning();
                if (err != WifiError::NONE)
                    return err;

                std::string ssid = getCurrentSSIDInternal();
                if (!ssid.empty())
                {
                    std::string status = wpa_->cli("status");
                    if (status.find("wpa_state=COMPLETED") != std::string::npos)
                    {
                        std::string ip = getIPAddressInternal();
                        if (!ip.empty())
                        {
                            current_state_ = WifiState::CONNECTED;
                            LOG_INFO(LOG_TAG, "已连接: %s (IP: %s)", ssid.c_str(), ip.c_str());
                        }
                        else
                        {
                            LOG_INFO(LOG_TAG,
                                     "WiFi associated but IP not obtained, requesting DHCP...");
                            updateState(WifiState::OBTAINING_IP);

                            dhcp_->request(config_.interface_name);

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
                                    updateState(WifiState::ASSOCIATED);
                                    break;
                                }

                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            }
                        }
                    }
                    else
                    {
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

            void WifiService::deinit()
            {
                LOG_INFO(LOG_TAG, "关闭");

                shutdown_requested_ = true;

                setAutoScan(false);
                setAutoReconnect(false, "", "");

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

            WifiError WifiService::scanNetworksInternal(std::vector<WifiInfo>& networks)
            {
                LOG_DEBUG(LOG_TAG, "扫描中");
                updateState(WifiState::SCANNING);

                stats_.scans_performed++;

                std::string result = wpa_->cli("scan");
                if (!wpaOutputIsOk(result))
                {
                    LOG_ERROR(LOG_TAG, "扫描启动失败: %s", result.c_str());
                    updateState(WifiState::DISCONNECTED);
                    return WifiError::SCAN_FAILED;
                }

                std::this_thread::sleep_for(std::chrono::seconds(3));

                result = wpa_->cli("scan_results");
                parseScanResults(result, networks);

                LOG_INFO(LOG_TAG, "扫描完成 %zu 个网络", networks.size());

                std::string status = wpa_->cli("status");
                if (status.find("wpa_state=COMPLETED") != std::string::npos)
                    updateState(WifiState::CONNECTED);
                else
                    updateState(WifiState::DISCONNECTED);

                return WifiError::NONE;
            }

            WifiError WifiService::scanNetworks(const WifiScanCallback& callback,
                                                std::vector<WifiInfo>*  networks)
            {
                if (!initialized_)
                {
                    LOG_ERROR(LOG_TAG, "WiFi 未初始化");
                    return WifiError::INITIALIZATION_FAILED;
                }

                if (callback)
                {
                    WifiScanCallback callback_copy = callback;
                    std::thread(
                        [this, callback_copy]()
                        {
                            std::vector<WifiInfo> results;
                            WifiError             err = scanNetworksInternal(results);

                            if (err == WifiError::NONE)
                                callback_copy(results);
                            else
                            {
                                notifyError(err, "扫描失败");
                                callback_copy(results);
                            }
                        })
                        .detach();

                    return WifiError::NONE;
                }

                if (!networks)
                {
                    LOG_ERROR(LOG_TAG, "同步模式下 networks 不能为空");
                    return WifiError::UNKNOWN;
                }

                return scanNetworksInternal(*networks);
            }

            void WifiService::setAutoScan(bool enabled)
            {
                if (enabled == auto_scan_running_)
                    return;

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
                        auto_scan_thread_->join();
                    auto_scan_thread_.reset();
                }
            }

            bool WifiService::isAutoScanRunning() const
            {
                return auto_scan_running_;
            }

            void WifiService::autoScanThread()
            {
                LOG_DEBUG(LOG_TAG, "自动扫描线程已启动");

                while (auto_scan_running_ && !shutdown_requested_)
                {
                    std::vector<WifiInfo> nets;
                    scanNetworksInternal(nets);

                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait_for(lock, std::chrono::seconds(config_.timed_scan_sec),
                                 [this]() { return !auto_scan_running_ || shutdown_requested_; });
                }

                LOG_DEBUG(LOG_TAG, "自动扫描线程已停止");
            }

            WifiError WifiService::connectInternal(const std::string& ssid,
                                                   const std::string& password)
            {
                LOG_INFO(LOG_TAG, "连接: %s", ssid.c_str());

                stats_.connections_attempted++;

                if (current_state_ == WifiState::CONNECTED)
                {
                    std::string current_ssid = getCurrentSSIDInternal();
                    if (!current_ssid.empty() && current_ssid == ssid)
                    {
                        LOG_INFO(LOG_TAG, "已连接 %s", ssid.c_str());
                        return WifiError::ALREADY_CONNECTED;
                    }

                    if (!current_ssid.empty())
                    {
                        LOG_INFO(LOG_TAG, "断开 %s", current_ssid.c_str());
                        disconnect();
                    }
                }

                updateState(WifiState::CONNECTING);

                if (config_.clear_old_config_on_connect)
                {
                    int old_net_id = findNetworkIdBySSID(ssid);
                    if (old_net_id >= 0)
                    {
                        LOG_DEBUG(LOG_TAG, "移除旧配置 ID=%d", old_net_id);
                        wpa_->cli("remove_network " + std::to_string(old_net_id));
                    }
                }

                std::string output = wpa_->cli("add_network");
                int         net_id = -1;
                if (!parseAddNetworkId(output, net_id))
                {
                    LOG_ERROR(LOG_TAG, "添加网络失败: %s", output.c_str());
                    updateState(WifiState::FAILED);
                    return WifiError::CONNECTION_FAILED;
                }

                LOG_DEBUG(LOG_TAG, "新建网络 ID=%d", net_id);

                std::string cmd =
                    "set_network " + std::to_string(net_id) + " ssid '\"" + ssid + "\"'";
                output = wpa_->cli(cmd);
                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "设置 SSID 失败: %s", output.c_str());
                    wpa_->cli("remove_network " + std::to_string(net_id));
                    updateState(WifiState::FAILED);
                    return WifiError::CONNECTION_FAILED;
                }

                if (password.empty())
                    cmd = "set_network " + std::to_string(net_id) + " key_mgmt NONE";
                else
                    cmd = "set_network " + std::to_string(net_id) + " psk '\"" + password + "\"'";

                output = wpa_->cli(cmd);
                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "设置密码失败: %s", output.c_str());
                    wpa_->cli("remove_network " + std::to_string(net_id));
                    updateState(WifiState::FAILED);
                    stats_.password_errors++;
                    return WifiError::PASSWORD_INCORRECT;
                }

                cmd    = "select_network " + std::to_string(net_id);
                output = wpa_->cli(cmd);
                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "选择网络失败: %s", output.c_str());
                    wpa_->cli("remove_network " + std::to_string(net_id));
                    updateState(WifiState::FAILED);
                    return WifiError::CONNECTION_FAILED;
                }

                LOG_DEBUG(LOG_TAG, "等待连接");

                auto start              = std::chrono::steady_clock::now();
                int  disconnected_count = 0;

                while (true)
                {
                    std::string status = wpa_->cli("status");

                    if (status.find("wpa_state=COMPLETED") != std::string::npos)
                    {
                        updateState(WifiState::OBTAINING_IP);
                        LOG_DEBUG(LOG_TAG, "获取 IP");

                        std::string dhcp_result = dhcp_->request(config_.interface_name);

                        std::string ip = getIPAddressInternal();
                        if (!ip.empty())
                        {
                            updateState(WifiState::CONNECTED);
                            stats_.connections_successful++;

                            if (config_.auto_save_config)
                                saveCurrentNetwork();

                            LOG_INFO(LOG_TAG, "已连接 IP: %s", ip.c_str());
                            return WifiError::NONE;
                        }
                        LOG_ERROR(LOG_TAG, "DHCP 失败: %s", dhcp_result.c_str());
                        wpa_->cli("remove_network " + std::to_string(net_id));
                        updateState(WifiState::FAILED);
                        return WifiError::DHCP_FAILED;
                    }

                    if (status.find("wpa_state=DISCONNECTED") != std::string::npos ||
                        status.find("wpa_state=INACTIVE") != std::string::npos)
                    {
                        disconnected_count++;
                        if (disconnected_count >= PASSWORD_FAIL_THRESHOLD)
                        {
                            LOG_ERROR(LOG_TAG, "认证失败（密码错误?）");
                            wpa_->cli("remove_network " + std::to_string(net_id));
                            updateState(WifiState::FAILED);
                            stats_.password_errors++;
                            return WifiError::AUTHENTICATION_FAILED;
                        }
                    }
                    else
                    {
                        disconnected_count = 0;
                    }

                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();

                    if (elapsed >= config_.connect_timeout_ms)
                    {
                        LOG_ERROR(LOG_TAG, "连接超时");
                        wpa_->cli("remove_network " + std::to_string(net_id));
                        updateState(WifiState::FAILED);
                        return WifiError::TIMEOUT;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
                }
            }

            WifiError WifiService::connect(const std::string& ssid, const std::string& password,
                                           const WifiConnectCallback& callback)
            {
                if (!initialized_)
                {
                    LOG_ERROR(LOG_TAG, "WiFi 未初始化");
                    return WifiError::INITIALIZATION_FAILED;
                }

                if (callback)
                {
                    WifiConnectCallback callback_copy = callback;
                    std::thread(
                        [this, ssid, password, callback_copy]()
                        {
                            WifiError err = connectInternal(ssid, password);

                            if (err == WifiError::NONE)
                                callback_copy(true, "Connected successfully");
                            else
                                callback_copy(false, wifiErrorToString(err));
                        })
                        .detach();

                    return WifiError::NONE;
                }

                return connectInternal(ssid, password);
            }

            WifiError WifiService::connectSavedNetwork()
            {
                LOG_INFO(LOG_TAG, "连接已保存网络");

                std::vector<SavedNetworkInfo> saved = getSavedNetworks();
                if (saved.empty())
                {
                    LOG_WARN(LOG_TAG, "无已保存网络");
                    return WifiError::NETWORK_NOT_FOUND;
                }

                std::vector<WifiInfo> available;
                WifiError             err = scanNetworksInternal(available);
                if (err != WifiError::NONE)
                    return err;

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
                        continue;

                    for (const auto& avail : available)
                    {
                        if (avail.ssid == saved_net.ssid)
                        {
                            if (avail.signal_strength >= config_.auto_connect_min_signal)
                                candidates.push_back(
                                    {saved_net.ssid, saved_net.priority, avail.signal_strength});
                            break;
                        }
                    }
                }

                if (candidates.empty())
                {
                    LOG_WARN(LOG_TAG, "范围内无可用已保存网络");
                    return WifiError::NETWORK_NOT_FOUND;
                }

                std::sort(candidates.begin(), candidates.end(),
                          [](const Candidate& a, const Candidate& b)
                          {
                              if (a.priority != b.priority)
                                  return a.priority > b.priority;
                              return a.signal > b.signal;
                          });

                const auto& best = candidates[0];
                LOG_INFO(LOG_TAG, "选择网络: %s 优先级=%d 信号=%d%%", best.ssid.c_str(),
                         best.priority, best.signal);

                int net_id = findNetworkIdBySSID(best.ssid);
                if (net_id < 0)
                {
                    LOG_ERROR(LOG_TAG, "网络 %s 未找到", best.ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                wpa_->cli("disconnect");
                std::this_thread::sleep_for(std::chrono::seconds(1));

                std::string cmd    = "select_network " + std::to_string(net_id);
                std::string output = wpa_->cli(cmd);

                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "选择网络失败: %s", output.c_str());
                    return WifiError::CONNECTION_FAILED;
                }

                updateState(WifiState::CONNECTING);
                auto start = std::chrono::steady_clock::now();

                while (true)
                {
                    std::string status = wpa_->cli("status");

                    if (status.find("wpa_state=COMPLETED") != std::string::npos)
                    {
                        updateState(WifiState::OBTAINING_IP);

                        dhcp_->request(config_.interface_name);

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

            WifiError WifiService::disconnect()
            {
                LOG_INFO(LOG_TAG, "断开中");

                std::string output = wpa_->cli("disconnect");

                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "断开失败: %s", output.c_str());
                    return WifiError::DISCONNECTION_FAILED;
                }

                link_->addrFlush(config_.interface_name);

                updateState(WifiState::DISCONNECTED);
                stats_.disconnections++;

                LOG_INFO(LOG_TAG, "已断开");
                return WifiError::NONE;
            }

            WifiError WifiService::reconnect()
            {
                LOG_INFO(LOG_TAG, "重连中");

                stats_.reconnects++;

                std::string output = wpa_->cli("reconnect");

                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "重连失败: %s", output.c_str());
                    return WifiError::CONNECTION_FAILED;
                }

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

            WifiError WifiService::saveCurrentNetwork()
            {
                LOG_DEBUG(LOG_TAG, "保存配置");

                std::string output = wpa_->cli("save_config");

                if (wpaOutputIsOk(output))
                {
                    stats_.config_saves++;
                    LOG_INFO(LOG_TAG, "配置已保存");
                    return WifiError::NONE;
                }
                LOG_ERROR(LOG_TAG, "保存配置失败: %s", output.c_str());
                return WifiError::CONFIG_FILE_ERROR;
            }

            WifiError WifiService::forgetNetwork(const std::string& ssid)
            {
                LOG_DEBUG(LOG_TAG, "忘记网络: %s", ssid.c_str());

                int net_id = findNetworkIdBySSID(ssid);
                if (net_id < 0)
                {
                    LOG_WARN(LOG_TAG, "网络 %s 未在已保存列表中", ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                std::string current_ssid = getCurrentSSIDInternal();
                if (current_ssid == ssid)
                    disconnect();

                std::string cmd    = "remove_network " + std::to_string(net_id);
                std::string output = wpa_->cli(cmd);

                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "移除网络失败: %s", output.c_str());
                    return WifiError::CONFIG_FILE_ERROR;
                }

                saveCurrentNetwork();

                LOG_INFO(LOG_TAG, "已移除网络 %s", ssid.c_str());
                return WifiError::NONE;
            }

            std::vector<SavedNetworkInfo> WifiService::getSavedNetworks() const
            {
                std::vector<SavedNetworkInfo> networks;
                parseListNetworks(wpa_->cli("list_networks"), networks);
                mergeSavedNetworkPriorities(config_.wpa_conf_path, networks);
                return networks;
            }

            bool WifiService::isNetworkSaved(const std::string& ssid) const
            {
                return findNetworkIdBySSID(ssid) >= 0;
            }

            int WifiService::findNetworkIdBySSID(const std::string& ssid) const
            {
                for (const auto& net : getSavedNetworks())
                {
                    if (net.ssid == ssid)
                        return net.network_id;
                }
                return -1;
            }

            WifiError WifiService::setNetworkPriority(const std::string& ssid, int priority)
            {
                int net_id = findNetworkIdBySSID(ssid);
                if (net_id < 0)
                {
                    LOG_WARN(LOG_TAG, "网络 %s 未找到", ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                std::string cmd = "set_network " + std::to_string(net_id) + " priority " +
                                  std::to_string(priority);
                std::string output = wpa_->cli(cmd);

                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "设置优先级失败: %s", output.c_str());
                    return WifiError::CONFIG_FILE_ERROR;
                }

                saveCurrentNetwork();
                LOG_DEBUG(LOG_TAG, "优先级 %d %s", priority, ssid.c_str());
                return WifiError::NONE;
            }

            WifiError WifiService::enableNetworkAutoConnect(const std::string& ssid, bool enabled)
            {
                int net_id = findNetworkIdBySSID(ssid);
                if (net_id < 0)
                {
                    LOG_WARN(LOG_TAG, "网络 %s 未找到", ssid.c_str());
                    return WifiError::NETWORK_NOT_FOUND;
                }

                std::string cmd    = enabled ? ("enable_network " + std::to_string(net_id))
                                             : ("disable_network " + std::to_string(net_id));
                std::string output = wpa_->cli(cmd);

                if (!wpaOutputIsOk(output))
                {
                    LOG_ERROR(LOG_TAG, "%s 网络失败: %s", enabled ? "启用" : "禁用",
                              output.c_str());
                    return WifiError::CONFIG_FILE_ERROR;
                }

                saveCurrentNetwork();
                LOG_DEBUG(LOG_TAG, "自动连接 %s %s", enabled ? "开" : "关", ssid.c_str());
                return WifiError::NONE;
            }

            WifiError WifiService::reloadConfig()
            {
                LOG_DEBUG(LOG_TAG, "重载配置");

                std::string output = wpa_->cli("reconfigure");

                if (wpaOutputIsOk(output))
                {
                    LOG_INFO(LOG_TAG, "配置已重载");
                    return WifiError::NONE;
                }
                LOG_ERROR(LOG_TAG, "重载配置失败: %s", output.c_str());
                return WifiError::CONFIG_FILE_ERROR;
            }

            WifiState WifiService::getState() const
            {
                return current_state_;
            }

            std::string WifiService::getCurrentSSID() const
            {
                return getCurrentSSIDInternal();
            }

            std::string WifiService::getIPAddress() const
            {
                return getIPAddressInternal();
            }

            int WifiService::getSignalStrength() const
            {
                return getSignalStrengthInternal();
            }

            bool WifiService::getConnectionInfo(WifiConnectionInfo& info) const
            {
                if (!isConnected())
                    return false;

                info.ssid            = getCurrentSSIDInternal();
                info.ip_address      = getIPAddressInternal();
                info.signal_strength = getSignalStrengthInternal();
                info.state           = current_state_;

                std::string status = wpa_->cli("status");

                std::istringstream iss(status);
                std::string        line;

                while (std::getline(iss, line))
                {
                    if (line.find("bssid=") == 0)
                        info.bssid = trim(line.substr(6));
                    else if (line.find("key_mgmt=") == 0)
                    {
                        std::string key_mgmt = trim(line.substr(9));
                        if (key_mgmt == "WPA2-PSK")
                            info.security = WifiSecurity::WPA2_PSK;
                        else if (key_mgmt == "WPA-PSK")
                            info.security = WifiSecurity::WPA_PSK;
                        else if (key_mgmt == "NONE")
                            info.security = WifiSecurity::NONE;
                    }
                }

                return true;
            }

            void WifiService::setAutoReconnect(bool enabled, const std::string& ssid,
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
                        auto_reconnect_thread_->join();
                    auto_reconnect_thread_.reset();

                    LOG_INFO(LOG_TAG, "自动重连已禁用");
                }
            }

            bool WifiService::isAutoReconnectEnabled() const
            {
                return auto_reconnect_enabled_;
            }

            void WifiService::autoReconnectThread()
            {
                LOG_DEBUG(LOG_TAG, "自动重连线程已启动");

                while (auto_reconnect_enabled_ && !shutdown_requested_)
                {
                    if (!isConnected() && !reconnect_ssid_.empty())
                    {
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
                            reconnect_callback_(err == WifiError::NONE, reconnect_ssid_);

                        if (err == WifiError::NONE)
                        {
                            LOG_INFO(LOG_TAG, "自动重连成功");
                            reconnect_attempts_ = 0;
                        }
                        else
                            LOG_WARN(LOG_TAG, "自动重连失败: %s", wifiErrorToString(err));
                    }
                    else if (isConnected())
                    {
                        reconnect_attempts_ = 0;
                    }

                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait_for(lock, std::chrono::seconds(config_.reconnect_interval_sec),
                                 [this]()
                                 { return !auto_reconnect_enabled_ || shutdown_requested_; });
                }

                LOG_DEBUG(LOG_TAG, "自动重连线程已停止");
            }

            void WifiService::setStateCallback(const WifiStateCallback& callback)
            {
                state_callback_ = callback;
            }

            void WifiService::setErrorCallback(const WifiErrorCallback& callback)
            {
                error_callback_ = callback;
            }

            void WifiService::setReconnectCallback(const WifiReconnectCallback& callback)
            {
                reconnect_callback_ = callback;
            }

            void WifiService::getStats(WifiStats& stats) const
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats = stats_;
            }

            void WifiService::resetStats()
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                memset(&stats_, 0, sizeof(stats_));
                LOG_DEBUG(LOG_TAG, "统计已重置");
            }

        } // namespace wifi
    }     // namespace network
} // namespace app
