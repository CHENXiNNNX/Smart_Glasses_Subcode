/**
 * @file test_mac.cc
 * @brief MAC地址工具测试程序
 */

#include "mac.h"
#include <iostream>

using namespace glasses::tool;

int main()
{
    std::cout << "=== MAC地址获取工具测试 ===" << std::endl;
    std::cout << std::endl;

    // 1. 测试获取无线网卡MAC地址
    std::cout << "1. 获取无线网卡MAC地址：" << std::endl;
    std::string wireless_mac = getWirelessMacAddress();
    if (!wireless_mac.empty())
    {
        std::cout << "   成功！MAC地址：" << wireless_mac << std::endl;
    }
    else
    {
        std::cout << "   失败：未找到可用的网络接口" << std::endl;
    }
    std::cout << std::endl;

    // 2. 测试获取所有网络接口
    std::cout << "2. 获取所有网络接口列表：" << std::endl;
    auto interfaces = getAllNetworkInterfaces();
    if (interfaces.empty())
    {
        std::cout << "   未找到任何网络接口" << std::endl;
    }
    else
    {
        for (const auto& iface : interfaces)
        {
            std::string mac = getMacAddressByInterface(iface);
            std::cout << "   - " << iface << ": " << (mac.empty() ? "无效" : mac) << std::endl;
        }
    }
    std::cout << std::endl;

    // 3. 测试指定接口
    std::cout << "3. 测试获取指定接口MAC地址：" << std::endl;
    std::string eth0_mac = getMacAddressByInterface("eth0");
    if (!eth0_mac.empty())
    {
        std::cout << "   eth0: " << eth0_mac << std::endl;
    }
    else
    {
        std::cout << "   eth0: 接口不存在或无效" << std::endl;
    }
    std::cout << std::endl;

    // 4. 测试MAC地址格式化
    std::cout << "4. 测试MAC地址格式化：" << std::endl;
    std::string test_mac1 = "AA:BB:CC:DD:EE:FF";
    std::string test_mac2 = "aabbccddeeff";
    std::string test_mac3 = "AA-BB-CC-DD-EE-FF";

    std::cout << "   原始: " << test_mac1 << " -> 格式化: " << formatMacAddress(test_mac1)
              << std::endl;
    std::cout << "   原始: " << test_mac2 << " -> 格式化: " << formatMacAddress(test_mac2)
              << std::endl;
    std::cout << "   原始: " << test_mac3 << " -> 格式化: " << formatMacAddress(test_mac3)
              << std::endl;
    std::cout << std::endl;

    std::cout << "=== 测试完成 ===" << std::endl;
    return 0;
}
