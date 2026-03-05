/* mac.hpp - MAC地址获取 */

#pragma once

#include <string>
#include <vector>

namespace app
{
    namespace tool
    {
        namespace mac
        {

            /**
             * @brief 获取无线网卡的MAC地址
             *
             * @details 该函数遍历系统网络接口，查找无线网卡（wlan*, wlp*等）并获取其MAC地址
             *          如果没有找到无线网卡，则返回第一个可用的有线网卡MAC地址
             *
             * @return std::string MAC地址（格式：xx:xx:xx:xx:xx:xx），失败返回空字符串
             *
             * @note 优先级：
             *       1. 无线网卡（wlan*, wlp*）
             *       2. 第一个可用的有线网卡
             *       3. 失败返回空字符串
             */
            std::string getWirelessMacAddress();

            /**
             * @brief 获取指定网络接口的MAC地址
             *
             * @param interface_name 网络接口名称（如 "eth0", "wlan0" 等）
             * @return std::string MAC地址（格式：xx:xx:xx:xx:xx:xx），失败返回空字符串
             */
            std::string getMacAddressByInterface(const std::string& interface_name);

            /**
             * @brief 获取所有网络接口列表
             *
             * @return std::vector<std::string> 网络接口名称列表
             */
            std::vector<std::string> getAllNetworkInterfaces();

            /**
             * @brief 格式化MAC地址（转换为标准格式）
             *
             * @param mac 原始MAC地址
             * @return std::string 格式化后的MAC地址（小写，冒号分隔）
             */
            std::string formatMacAddress(const std::string& mac);

        } // namespace mac
    }     // namespace tool
} // namespace app
