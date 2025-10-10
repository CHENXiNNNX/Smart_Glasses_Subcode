/**
 * @file mac.cc
 * @brief MAC地址获取工具实现
 */

#include "mac.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <cstring>
#include <cctype>

namespace glasses {
namespace tool {

/**
 * @brief 检查接口名称是否为无线网卡
 */
static bool isWirelessInterface(const std::string& interface_name) {
    // 检查是否以 "wlan" 或 "wlp" 开头
    return (interface_name.find("wlan") == 0 || interface_name.find("wlp") == 0);
}

/**
 * @brief 检查接口是否为有效的网络接口
 */
static bool isValidInterface(const std::string& interface_name) {
    // 排除回环接口、虚拟接口等
    if (interface_name == "lo" || 
        interface_name == "." || 
        interface_name == ".." ||
        interface_name.find("docker") == 0 ||
        interface_name.find("veth") == 0 ||
        interface_name.find("virbr") == 0) {
        return false;
    }
    return true;
}

/**
 * @brief 从sysfs读取MAC地址
 */
static std::string readMacFromSysfs(const std::string& interface_name) {
    std::string address_path = "/sys/class/net/" + interface_name + "/address";
    std::ifstream address_file(address_path);
    
    if (!address_file.is_open()) {
        return "";
    }
    
    std::string mac_address;
    std::getline(address_file, mac_address);
    address_file.close();
    
    // 去除尾部换行符
    if (!mac_address.empty() && mac_address.back() == '\n') {
        mac_address.pop_back();
    }
    
    // 检查是否为有效的MAC地址（不是全0）
    if (mac_address.empty() || mac_address == "00:00:00:00:00:00") {
        return "";
    }
    
    return mac_address;
}

std::string getWirelessMacAddress() {
    DIR *dir;
    struct dirent *entry;
    std::string mac_address;
    std::string first_mac_address;

    // 打开 /sys/class/net/ 目录
    dir = opendir("/sys/class/net/");
    if (dir == nullptr) {
        std::cerr << "[MAC] Failed to open /sys/class/net/ directory" << std::endl;
        return "";
    }

    // 遍历目录中的所有条目
    while ((entry = readdir(dir)) != nullptr) {
        std::string interface_name = entry->d_name;
        
        // 跳过无效接口
        if (!isValidInterface(interface_name)) {
            continue;
        }

        // 检查是否为无线网卡接口
        if (isWirelessInterface(interface_name)) {
            mac_address = readMacFromSysfs(interface_name);
            if (!mac_address.empty()) {
                std::cout << "[MAC] Found wireless interface: " << interface_name 
                          << " with MAC: " << mac_address << std::endl;
                closedir(dir);
                return mac_address;
            }
        } else {
            // 如果不是无线接口，记录第一个可用的有线接口
            if (first_mac_address.empty()) {
                std::string temp_mac = readMacFromSysfs(interface_name);
                if (!temp_mac.empty()) {
                    first_mac_address = temp_mac;
                    std::cout << "[MAC] Found wired interface: " << interface_name 
                              << " with MAC: " << first_mac_address << std::endl;
                }
            }
        }
    }

    closedir(dir);

    // 如果没有找到无线网卡，返回第一个可用的有线网卡MAC地址
    if (!first_mac_address.empty()) {
        std::cout << "[MAC] Using first available MAC address: " << first_mac_address << std::endl;
        return first_mac_address;
    }

    std::cerr << "[MAC] No valid network interface found" << std::endl;
    return "";
}

std::string getMacAddressByInterface(const std::string& interface_name) {
    if (interface_name.empty()) {
        std::cerr << "[MAC] Interface name is empty" << std::endl;
        return "";
    }
    
    std::string mac = readMacFromSysfs(interface_name);
    
    if (mac.empty()) {
        std::cerr << "[MAC] Failed to get MAC address for interface: " 
                  << interface_name << std::endl;
    } else {
        std::cout << "[MAC] Interface " << interface_name 
                  << " MAC address: " << mac << std::endl;
    }
    
    return mac;
}

std::vector<std::string> getAllNetworkInterfaces() {
    std::vector<std::string> interfaces;
    DIR *dir;
    struct dirent *entry;

    dir = opendir("/sys/class/net/");
    if (dir == nullptr) {
        std::cerr << "[MAC] Failed to open /sys/class/net/ directory" << std::endl;
        return interfaces;
    }

    while ((entry = readdir(dir)) != nullptr) {
        std::string interface_name = entry->d_name;
        
        // 跳过 . 和 .. 以及回环接口
        if (interface_name == "." || interface_name == ".." || interface_name == "lo") {
            continue;
        }
        
        interfaces.push_back(interface_name);
    }

    closedir(dir);
    
    // 排序（无线网卡优先）
    std::sort(interfaces.begin(), interfaces.end(), [](const std::string& a, const std::string& b) {
        bool a_wireless = isWirelessInterface(a);
        bool b_wireless = isWirelessInterface(b);
        
        if (a_wireless && !b_wireless) return true;
        if (!a_wireless && b_wireless) return false;
        return a < b;
    });
    
    return interfaces;
}

std::string formatMacAddress(const std::string& mac) {
    if (mac.empty()) {
        return "";
    }
    
    std::string formatted = mac;
    
    // 转换为小写
    std::transform(formatted.begin(), formatted.end(), formatted.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    // 移除所有非十六进制字符和冒号
    formatted.erase(std::remove_if(formatted.begin(), formatted.end(),
                    [](char c) {
                        return !std::isxdigit(c) && c != ':';
                    }), formatted.end());
    
    // 如果没有冒号分隔符，添加它们
    if (formatted.find(':') == std::string::npos && formatted.length() == 12) {
        std::string temp;
        for (size_t i = 0; i < formatted.length(); i += 2) {
            if (i > 0) temp += ":";
            temp += formatted.substr(i, 2);
        }
        formatted = temp;
    }
    
    // 验证格式是否正确（应该是 xx:xx:xx:xx:xx:xx）
    if (formatted.length() != 17 || 
        formatted[2] != ':' || formatted[5] != ':' || 
        formatted[8] != ':' || formatted[11] != ':' || 
        formatted[14] != ':') {
        std::cerr << "[MAC] Invalid MAC address format: " << mac << std::endl;
        return "";
    }
    
    return formatted;
}

} // namespace tool
} // namespace glasses

