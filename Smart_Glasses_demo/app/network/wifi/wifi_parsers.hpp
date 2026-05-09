/* wifi_parsers.hpp - wpa_cli / ip 输出解析（纯逻辑，无 I/O） */

#pragma once

#include "wifi_types.hpp"

#include <map>
#include <string>
#include <vector>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            std::string trim(const std::string& str);
            bool        wpaOutputIsOk(const std::string& output);

            int          signalDbmToPercent(int dbm);
            int          frequencyToChannel(int freq);
            WifiSecurity parseSecurityType(const std::string& flags);

            void parseScanResults(const std::string& raw, std::vector<WifiInfo>& out);

            std::string parseStatusField(const std::string& status, const std::string& key_prefix);
            bool        statusHasWpaState(const std::string& status, const char* state_value);

            void parseListNetworks(const std::string& raw, std::vector<SavedNetworkInfo>& out);
            int  parseSignalPollRssi(const std::string& raw);

            std::string parseIPv4FromAddrShow(const std::string& ip_addr_show_output);
            bool        linkShowHasUpFlag(const std::string& ip_link_show_output);

            bool parseAddNetworkId(const std::string& wpa_output, int& out_id);

            void mergeSavedNetworkPriorities(const std::string&             wpa_conf_path,
                                             std::vector<SavedNetworkInfo>& networks);

        } // namespace wifi
    }     // namespace network
} // namespace app
