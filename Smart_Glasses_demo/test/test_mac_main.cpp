/* test_mac_main.cpp - MAC 地址工具测试 */

#include "app/tool/log/log.hpp"
#include "app/tool/mac/mac.hpp"

#include <iostream>

using namespace app::tool::mac;
using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "MAC";
} // namespace

int main()
{
    Logger::inst().init(LogConfig());
    LOG_INFO(LOG_TAG, "MAC 工具测试");

    std::string wireless = getWirelessMacAddress();
    if (!wireless.empty())
        LOG_INFO(LOG_TAG, "无线 MAC: %s", wireless.c_str());
    else
        LOG_WARN(LOG_TAG, "未获取到无线 MAC");

    auto ifaces = getAllNetworkInterfaces();
    for (const auto& iface : ifaces)
    {
        std::string mac = getMacAddressByInterface(iface);
        LOG_INFO(LOG_TAG, "%s: %s", iface.c_str(), mac.empty() ? "无效" : mac.c_str());
    }

    std::string eth0 = getMacAddressByInterface("eth0");
    if (!eth0.empty())
        LOG_INFO(LOG_TAG, "eth0: %s", eth0.c_str());
    else
        LOG_INFO(LOG_TAG, "eth0: 无");

    std::string f1 = formatMacAddress("AA:BB:CC:DD:EE:FF");
    std::string f2 = formatMacAddress("aabbccddeeff");
    LOG_INFO(LOG_TAG, "格式化: %s -> %s", "AA:BB:CC:DD:EE:FF", f1.c_str());
    LOG_INFO(LOG_TAG, "格式化: %s -> %s", "aabbccddeeff", f2.c_str());

    LOG_INFO(LOG_TAG, "测试完成");
    return 0;
}
