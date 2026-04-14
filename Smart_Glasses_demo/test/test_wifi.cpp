/* test_wifi.cpp - WiFi 模块测试 */

#include "app/network/wifi/wifi.hpp"
#include "app/tool/log/log.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <iomanip>
#include <string>
#include <unistd.h>

using namespace app::network::wifi;
using namespace app::tool::log;

std::atomic<bool> scan_done{false};
std::atomic<bool> connect_done{false};

namespace
{
    std::string trim_input(std::string s)
    {
        auto n = s.find_last_not_of(" \t\r\n\f\v");
        if (n == std::string::npos)
            return "";
        s.erase(n + 1);
        n = s.find_first_not_of(" \t\r\n\f\v");
        if (n != std::string::npos)
            s.erase(0, n);
        return s;
    }

    bool is_all_digits(const std::string& s)
    {
        if (s.empty())
            return false;
        return std::all_of(s.begin(), s.end(),
                           [](unsigned char c) { return std::isdigit(c) != 0; });
    }
} // namespace

void onStateChanged(WifiState old_state, WifiState new_state)
{
    std::cout << "\n[状态] " << wifiStateToString(old_state) << " -> "
              << wifiStateToString(new_state) << std::endl;
}

void onScanComplete(const std::vector<WifiInfo>& networks)
{
    std::cout << "\n扫描结果 " << networks.size() << " 个网络:\n";

    int count = 1;
    for (const auto& net : networks)
    {
        std::cout << "[" << count++ << "] " << std::left << std::setw(25) << net.ssid
                  << " 信号: " << std::setw(3) << net.signal_strength << "% "
                  << " 加密: " << wifiSecurityToString(net.security) << std::endl;
    }

    scan_done = true;
}

void onConnectResult(bool success, const std::string& message)
{
    std::cout << "\n" << (success ? "连接成功" : "连接失败") << " " << message << std::endl;
    connect_done = true;
}

void printHeader()
{
    std::cout << "\n--- WiFi 测试 (wpa_supplicant) ---\n";
}

void printMenu()
{
    std::cout << "\n--- 菜单 ---\n";
    std::cout << "  1 - WiFi扫描\n";
    std::cout << "  2 - WiFi连接\n";
    std::cout << "  3 - WiFi断开\n";
    std::cout << "  4 - 查询连接状态\n";
    std::cout << "  5 - 查询已保存的WiFi\n";
    std::cout << "  6 - 删除已保存的WiFi\n";
    std::cout << "  0 - 退出\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
}

void testScan(WifiManager& wifi)
{
    std::cout << "\n━━━━ WiFi扫描 ━━━━\n";
    std::cout << "正在扫描WiFi网络...\n";

    scan_done     = false;
    WifiError err = wifi.scanNetworks(onScanComplete);

    if (err != WifiError::NONE)
    {
        std::cout << "❌ 启动扫描失败: " << wifiErrorToString(err) << std::endl;
        return;
    }

    // 等待扫描完成
    int timeout = 20; // 20秒超时
    while (!scan_done && timeout-- > 0)
    {
        sleep(1);
    }

    if (!scan_done)
    {
        std::cout << "⚠️  扫描超时\n";
    }
}

void testConnect(WifiManager& wifi)
{
    std::cout << "\n━━━━ WiFi连接 ━━━━\n";

    // 先扫描一下可用网络
    std::vector<WifiInfo> networks;
    WifiError             err = wifi.scanNetworks(nullptr, &networks);

    if (err != WifiError::NONE)
    {
        std::cout << "❌ 扫描失败: " << wifiErrorToString(err) << std::endl;
        return;
    }

    if (networks.empty())
    {
        std::cout << "❌ 没有找到可用WiFi网络\n";
        return;
    }

    constexpr size_t kMaxList = 10;
    const size_t     shown    = std::min(networks.size(), kMaxList);

    std::cout << "\n可用网络列表:\n";
    for (size_t i = 0; i < shown; ++i)
    {
        std::cout << "[" << (i + 1) << "] " << std::left << std::setw(25) << networks[i].ssid
                  << " (" << networks[i].signal_strength << "%)" << std::endl;
    }

    std::string line;
    std::cout << "\n请输入列表序号(1-" << shown << ") 或完整 SSID: ";
    std::getline(std::cin, line);
    line = trim_input(line);

    if (line.empty())
    {
        std::cout << "❌ 序号/SSID 不能为空\n";
        return;
    }

    std::string ssid = line;
    if (is_all_digits(line))
    {
        try
        {
            const int n = std::stoi(line);
            if (n >= 1 && static_cast<size_t>(n) <= shown)
            {
                ssid = networks[static_cast<size_t>(n - 1)].ssid;
                std::cout << "→ 已选 [" << n << "] SSID: \"" << ssid << "\"\n";
            }
        }
        catch (...)
        {
            /* 非合法数字范围则仍按 SSID 字面量连接 */
        }
    }

    // 输入密码
    std::string password;
    std::cout << "请输入密码（开放网络直接回车）: ";
    std::getline(std::cin, password);

    // 开始连接
    std::cout << "\n📡 正在连接到 SSID \"" << ssid << "\" ...\n";

    connect_done = false;
    err          = wifi.connect(ssid, password, onConnectResult);

    if (err != WifiError::NONE)
    {
        std::cout << "❌ 启动连接失败: " << wifiErrorToString(err) << std::endl;
        return;
    }

    // 等待连接完成
    int timeout = 40; // 40秒超时
    while (!connect_done && timeout-- > 0)
    {
        sleep(1);
    }

    if (!connect_done)
    {
        std::cout << "⚠️  连接超时\n";
    }
}

void testDisconnect(WifiManager& wifi)
{
    std::cout << "\n━━━━ WiFi断开 ━━━━\n";

    // 检查当前是否已连接
    if (!wifi.isConnected())
    {
        std::cout << "⚠️  当前未连接任何WiFi\n";
        return;
    }

    std::string current_ssid = wifi.getCurrentSSID();
    std::cout << "正在断开: " << current_ssid << " ...\n";

    WifiError err = wifi.disconnect();

    if (err == WifiError::NONE)
    {
        std::cout << "✅ 已断开连接\n";
    }
    else
    {
        std::cout << "❌ 断开失败: " << wifiErrorToString(err) << std::endl;
    }
}

void testQueryStatus(WifiManager& wifi)
{
    std::cout << "\n━━━━ 连接状态 ━━━━\n";

    WifiState state = wifi.getState();
    std::cout << "WiFi状态: " << wifiStateToString(state);

    if (state == WifiState::CONNECTED)
    {
        std::cout << " ✅ 已连接\n";

        WifiConnectionInfo info;
        if (wifi.getConnectionInfo(info))
        {
            std::cout << "\n连接信息:\n";
            std::cout << "  SSID:       " << info.ssid << "\n";
            std::cout << "  BSSID:      " << info.bssid << "\n";
            std::cout << "  IP地址:     " << info.ip_address << "\n";
            std::cout << "  信号强度:   " << info.signal_strength << "%\n";
            std::cout << "  加密类型:   " << wifiSecurityToString(info.security) << "\n";

            // 测试网络连接
            std::cout << "\n测试网络连接...\n";
            int result = system("ping -c 2 -W 2 8.8.8.8 > /dev/null 2>&1");
            if (result == 0)
            {
                std::cout << "  ✅ 网络连接正常\n";
            }
            else
            {
                std::cout << "  ❌ 网络连接失败\n";
            }
        }
    }
    else if (state == WifiState::DISCONNECTED)
    {
        std::cout << " ⚠️  未连接\n";
    }
    else
    {
        std::cout << "\n";
    }

    // 显示接口状态
    std::cout << "\n接口状态: ";
    if (wifi.isInterfaceUp())
    {
        std::cout << "UP ✓\n";
    }
    else
    {
        std::cout << "DOWN ✗\n";
    }
}

void testQuerySavedNetworks(WifiManager& wifi)
{
    std::cout << "\n━━━━ 已保存的WiFi ━━━━\n";

    std::vector<SavedNetworkInfo> saved = wifi.getSavedNetworks();

    if (saved.empty())
    {
        std::cout << "⚠️  没有已保存的WiFi网络\n";
        return;
    }

    std::cout << "找到 " << saved.size() << " 个已保存的WiFi网络:\n\n";

    for (size_t i = 0; i < saved.size(); ++i)
    {
        const auto& net = saved[i];
        std::cout << "[" << (i + 1) << "] " << std::left << std::setw(25) << net.ssid
                  << " 网络ID: " << std::setw(3) << net.network_id;

        if (net.is_current)
        {
            std::cout << "  [当前连接]";
        }

        if (!net.is_enabled_auto)
        {
            std::cout << "  [自动连接已禁用]";
        }

        if (net.priority > 0)
        {
            std::cout << "  优先级: " << net.priority;
        }

        std::cout << std::endl;
    }
}

void testDeleteWiFi(WifiManager& wifi)
{
    std::cout << "\n━━━━ 删除已保存的WiFi ━━━━\n";

    // 先显示已保存的网络
    std::vector<SavedNetworkInfo> saved = wifi.getSavedNetworks();

    if (saved.empty())
    {
        std::cout << "⚠️  没有已保存的WiFi网络\n";
        return;
    }

    std::cout << "已保存的WiFi网络列表:\n\n";
    for (size_t i = 0; i < saved.size(); ++i)
    {
        const auto& net = saved[i];
        std::cout << "[" << (i + 1) << "] " << net.ssid;
        if (net.is_current)
        {
            std::cout << " [当前连接]";
        }
        std::cout << std::endl;
    }

    // 输入要删除的SSID
    std::string ssid;
    std::cout << "\n请输入要删除的SSID: ";
    std::getline(std::cin, ssid);

    if (ssid.empty())
    {
        std::cout << "❌ SSID不能为空\n";
        return;
    }

    // 检查是否存在
    if (!wifi.isNetworkSaved(ssid))
    {
        std::cout << "❌ WiFi \"" << ssid << "\" 未在已保存列表中\n";
        return;
    }

    // 确认删除
    std::cout << "\n⚠️  确认删除 WiFi \"" << ssid << "\" ? (y/n): ";
    std::string confirm;
    std::getline(std::cin, confirm);

    if (confirm != "y" && confirm != "Y")
    {
        std::cout << "已取消删除\n";
        return;
    }

    // 执行删除
    WifiError err = wifi.forgetNetwork(ssid);

    if (err == WifiError::NONE)
    {
        std::cout << "✅ WiFi \"" << ssid << "\" 已删除\n";
    }
    else
    {
        std::cout << "❌ 删除失败: " << wifiErrorToString(err) << std::endl;
    }
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // 检查root权限
    if (geteuid() != 0)
    {
        std::cerr << "❌ 错误: 需要root权限运行此程序\n";
        std::cerr << "请使用: sudo " << argv[0] << std::endl;
        return 1;
    }

    printHeader();

    // 初始化日志系统
    Logger::inst().init(LogConfig());

    // 创建WiFi管理器
    std::cout << "\n正在初始化WiFi管理器...\n";

    WifiConfig config;
    config.auto_connect_on_init    = false; // 不自动连接
    config.enable_detailed_logging = false;

    WifiManager wifi(config);

    // 设置回调
    wifi.setStateCallback(onStateChanged);

    // 初始化
    WifiError err = wifi.init();
    if (err != WifiError::NONE)
    {
        std::cerr << "\n❌ WiFi管理器初始化失败: " << wifiErrorToString(err) << std::endl;

        std::cerr << "\n可能的原因:\n";
        std::cerr << "  1. wpa_supplicant未安装或未运行\n";
        std::cerr << "  2. wlan0接口不存在\n";
        std::cerr << "  3. 权限不足\n";

        return 1;
    }

    std::cout << "✅ WiFi管理器初始化成功\n";

    // 主循环
    while (true)
    {
        printMenu();

        std::cout << "请选择: ";
        std::string choice;
        std::getline(std::cin, choice);

        if (choice.empty())
        {
            continue;
        }

        switch (choice[0])
        {
        case '1':
            testScan(wifi);
            break;

        case '2':
            testConnect(wifi);
            break;

        case '3':
            testDisconnect(wifi);
            break;

        case '4':
            testQueryStatus(wifi);
            break;

        case '5':
            testQuerySavedNetworks(wifi);
            break;

        case '6':
            testDeleteWiFi(wifi);
            break;

        case '0':
            std::cout << "\n正在退出...\n";
            wifi.deinit();
            Logger::inst().deinit();
            std::cout << "再见!\n";
            return 0;

        default:
            std::cout << "\n❌ 无效选择，请重新输入\n";
            break;
        }

        std::cout << "\n按回车键继续...";
        std::cin.get();
    }

    return 0;
}
