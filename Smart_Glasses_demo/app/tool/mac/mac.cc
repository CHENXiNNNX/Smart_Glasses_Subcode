/* mac.cc - MAC地址获取 */

#include "mac.hpp"
#include "log/log.hpp"
#include <fstream>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <cstring>
#include <cctype>

#define MAC_ADDRESS_LENGTH 12
#define MAC_ADDRESS_FORMATTED_LENGTH 17
#define MAC_BYTE_SEPARATOR_STEP 2
#define MAC_COLON_POS_1 2
#define MAC_COLON_POS_2 5
#define MAC_COLON_POS_3 8
#define MAC_COLON_POS_4 11
#define MAC_COLON_POS_5 14

namespace app
{
    namespace tool
    {
        namespace mac
        {

            using namespace log;

            namespace
            {
                constexpr const char* LOG_TAG = "MAC";
            } // namespace

            static bool s_is_wireless_interface(const std::string& interface_name)
            {
                return (interface_name.find("wlan") == 0 || interface_name.find("wlp") == 0);
            }

            static bool s_is_valid_interface(const std::string& interface_name)
            {
                // 排除回环接口、虚拟接口等
                if (interface_name == "lo" || interface_name == "." || interface_name == ".." ||
                    interface_name.find("docker") == 0 || interface_name.find("veth") == 0 ||
                    interface_name.find("virbr") == 0)
                {
                    return false;
                }
                return true;
            }

            static std::string s_read_mac_from_sysfs(const std::string& interface_name)
            {
                std::string   address_path = "/sys/class/net/" + interface_name + "/address";
                std::ifstream address_file(address_path);

                if (!address_file.is_open())
                {
                    return "";
                }

                std::string mac_address;
                std::getline(address_file, mac_address);
                address_file.close();

                if (!mac_address.empty() && mac_address.back() == '\n')
                {
                    mac_address.pop_back();
                }

                if (mac_address.empty() || mac_address == "00:00:00:00:00:00")
                {
                    return "";
                }

                return mac_address;
            }

            std::string getWirelessMacAddress()
            {
                DIR*           dir;
                struct dirent* entry;
                std::string    mac_address;
                std::string    first_mac_address;

                dir = opendir("/sys/class/net/");
                if (dir == nullptr)
                {
                    LOG_ERROR(LOG_TAG, "打开 /sys/class/net/ 目录失败");
                    return "";
                }

                while ((entry = readdir(dir)) != nullptr)
                {
                    std::string interface_name = entry->d_name;

                    if (!s_is_valid_interface(interface_name))
                    {
                        continue;
                    }

                    if (s_is_wireless_interface(interface_name))
                    {
                        mac_address = s_read_mac_from_sysfs(interface_name);
                        if (!mac_address.empty())
                        {
                            closedir(dir);
                            return mac_address;
                        }
                    }
                    else if (first_mac_address.empty())
                    {
                        std::string temp_mac = s_read_mac_from_sysfs(interface_name);
                        if (!temp_mac.empty())
                            first_mac_address = temp_mac;
                    }
                }

                closedir(dir);

                if (!first_mac_address.empty())
                    return first_mac_address;

                LOG_ERROR(LOG_TAG, "未找到有效的网络接口");
                return "";
            }

            std::string getMacAddressByInterface(const std::string& interface_name)
            {
                if (interface_name.empty())
                {
                    LOG_ERROR(LOG_TAG, "接口名称为空");
                    return "";
                }

                std::string mac_address = s_read_mac_from_sysfs(interface_name);
                if (mac_address.empty())
                    LOG_ERROR(LOG_TAG, "获取接口 MAC 失败: %s", interface_name.c_str());
                return mac_address;
            }

            std::vector<std::string> getAllNetworkInterfaces()
            {
                std::vector<std::string> interfaces;
                DIR*                     dir;
                struct dirent*           entry;

                dir = opendir("/sys/class/net/");
                if (dir == nullptr)
                {
                    LOG_ERROR(LOG_TAG, "打开 /sys/class/net/ 目录失败");
                    return interfaces;
                }

                while ((entry = readdir(dir)) != nullptr)
                {
                    std::string interface_name = entry->d_name;

                    if (interface_name == "." || interface_name == ".." || interface_name == "lo")
                    {
                        continue;
                    }

                    interfaces.push_back(interface_name);
                }

                closedir(dir);

                std::sort(
                    interfaces.begin(), interfaces.end(),
                    [](const std::string& first_interface, const std::string& second_interface)
                    {
                        bool first_wireless  = s_is_wireless_interface(first_interface);
                        bool second_wireless = s_is_wireless_interface(second_interface);

                        if (first_wireless && !second_wireless)
                            return true;
                        if (!first_wireless && second_wireless)
                            return false;
                        return first_interface < second_interface;
                    });

                return interfaces;
            }

            std::string formatMacAddress(const std::string& mac)
            {
                if (mac.empty())
                {
                    return "";
                }

                std::string formatted = mac;

                std::transform(formatted.begin(), formatted.end(), formatted.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                formatted.erase(std::remove_if(formatted.begin(), formatted.end(),
                                               [](char c)
                                               { return !std::isxdigit(c) && c != ':'; }),
                                formatted.end());

                if (formatted.find(':') == std::string::npos &&
                    formatted.length() == MAC_ADDRESS_LENGTH)
                {
                    std::string formatted_temp;
                    for (size_t i = 0; i < formatted.length(); i += MAC_BYTE_SEPARATOR_STEP)
                    {
                        if (i > 0)
                            formatted_temp += ":";
                        formatted_temp += formatted.substr(i, MAC_BYTE_SEPARATOR_STEP);
                    }
                    formatted = formatted_temp;
                }

                if (formatted.length() != MAC_ADDRESS_FORMATTED_LENGTH ||
                    formatted[MAC_COLON_POS_1] != ':' || formatted[MAC_COLON_POS_2] != ':' ||
                    formatted[MAC_COLON_POS_3] != ':' || formatted[MAC_COLON_POS_4] != ':' ||
                    formatted[MAC_COLON_POS_5] != ':')
                {
                    LOG_ERROR(LOG_TAG, "无效的MAC地址格式: %s", formatted.c_str());
                    return "";
                }

                return formatted;
            }

        } // namespace mac
    }     // namespace tool
} // namespace app
