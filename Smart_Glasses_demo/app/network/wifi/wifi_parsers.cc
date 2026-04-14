/* wifi_parsers.cc */

#include "wifi_parsers.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            namespace
            {
                constexpr int RSSI_STRONG_THRESHOLD = -50;
                constexpr int RSSI_WEAK_THRESHOLD   = -100;
                constexpr int RSSI_PERCENT_FACTOR   = 2;
                constexpr int RSSI_MAX_PERCENT      = 100;
                constexpr int RSSI_MIN_PERCENT      = 0;

                constexpr int CHANNEL_2G_LOWER_FREQ = 2412;
                constexpr int CHANNEL_2G_UPPER_FREQ = 2484;
                constexpr int CHANNEL_2G_OFFSET     = 2407;
                constexpr int CHANNEL_5G_LOWER_FREQ = 5170;
                constexpr int CHANNEL_5G_UPPER_FREQ = 5825;
                constexpr int CHANNEL_5G_OFFSET     = 5000;
                constexpr int CHANNEL_FREQ_STEP     = 5;

                constexpr std::size_t BSSID_PREFIX_LENGTH  = 6;
                constexpr std::size_t KEY_MGMT_PREFIX_LENGTH = 9;
                constexpr std::size_t SSID_PREFIX_LENGTH   = 5;
                constexpr std::size_t INET_PREFIX_LENGTH   = 5;
                constexpr std::size_t RSSI_PREFIX_LENGTH     = 5;
            } // namespace

            std::string trim(const std::string& str)
            {
                const char* whitespace = " \t\n\r\f\v";
                size_t      start      = str.find_first_not_of(whitespace);
                if (start == std::string::npos)
                    return "";
                size_t end = str.find_last_not_of(whitespace);
                return str.substr(start, end - start + 1);
            }

            bool wpaOutputIsOk(const std::string& output)
            {
                std::string t = trim(output);
                return t == "OK" || t.find("OK") == 0;
            }

            int signalDbmToPercent(int dbm)
            {
                if (dbm >= RSSI_STRONG_THRESHOLD)
                    return RSSI_MAX_PERCENT;
                if (dbm <= RSSI_WEAK_THRESHOLD)
                    return RSSI_MIN_PERCENT;
                return RSSI_PERCENT_FACTOR * (dbm - RSSI_WEAK_THRESHOLD);
            }

            int frequencyToChannel(int freq)
            {
                if (freq >= CHANNEL_2G_LOWER_FREQ && freq <= CHANNEL_2G_UPPER_FREQ)
                    return (freq - CHANNEL_2G_OFFSET) / CHANNEL_FREQ_STEP;
                if (freq >= CHANNEL_5G_LOWER_FREQ && freq <= CHANNEL_5G_UPPER_FREQ)
                    return (freq - CHANNEL_5G_OFFSET) / CHANNEL_FREQ_STEP;
                return 0;
            }

            WifiSecurity parseSecurityType(const std::string& flags)
            {
                if (flags.find("WPA3") != std::string::npos)
                    return WifiSecurity::WPA3_PSK;
                if (flags.find("WPA2") != std::string::npos)
                    return WifiSecurity::WPA2_PSK;
                if (flags.find("WPA") != std::string::npos)
                    return WifiSecurity::WPA_PSK;
                if (flags.find("WEP") != std::string::npos)
                    return WifiSecurity::WEP;
                if (flags.find("ESS") != std::string::npos && flags.find("WPA") == std::string::npos)
                    return WifiSecurity::NONE;
                return WifiSecurity::UNKNOWN;
            }

            void parseScanResults(const std::string& raw, std::vector<WifiInfo>& out)
            {
                std::istringstream              iss(raw);
                std::string                     line;
                std::map<std::string, WifiInfo> unique_networks;

                std::getline(iss, line); // header

                while (std::getline(iss, line))
                {
                    line = trim(line);
                    if (line.empty())
                        continue;

                    std::istringstream line_stream(line);
                    WifiInfo           info;
                    std::string        bssid;
                    std::string        freq_str;
                    std::string        signal_str;
                    std::string        flags;

                    line_stream >> bssid >> freq_str >> signal_str >> flags;

                    std::string ssid;
                    std::getline(line_stream, ssid);
                    ssid = trim(ssid);
                    if (ssid.empty())
                        continue;

                    info.ssid            = ssid;
                    info.bssid           = bssid;
                    info.frequency       = std::atoi(freq_str.c_str());
                    info.channel         = frequencyToChannel(info.frequency);
                    info.signal_strength = signalDbmToPercent(std::atoi(signal_str.c_str()));
                    info.security        = parseSecurityType(flags);

                    if (unique_networks.find(ssid) == unique_networks.end() ||
                        unique_networks[ssid].signal_strength < info.signal_strength)
                        unique_networks[ssid] = info;
                }

                out.clear();
                for (const auto& pair : unique_networks)
                    out.push_back(pair.second);

                std::sort(out.begin(), out.end(),
                          [](const WifiInfo& a, const WifiInfo& b)
                          { return a.signal_strength > b.signal_strength; });
            }

            std::string parseStatusField(const std::string& status, const std::string& key_prefix)
            {
                std::istringstream iss(status);
                std::string        line;
                while (std::getline(iss, line))
                {
                    if (line.find(key_prefix) == 0)
                        return trim(line.substr(key_prefix.size()));
                }
                return "";
            }

            bool statusHasWpaState(const std::string& status, const char* state_value)
            {
                std::string line = parseStatusField(status, "wpa_state=");
                return line == state_value;
            }

            void parseListNetworks(const std::string& raw, std::vector<SavedNetworkInfo>& out)
            {
                out.clear();
                std::istringstream iss(raw);
                std::string        line;
                std::getline(iss, line);

                while (std::getline(iss, line))
                {
                    line = trim(line);
                    if (line.empty())
                        continue;

                    std::istringstream line_stream(line);
                    SavedNetworkInfo   info;
                    std::string        id_str;
                    std::string        ssid;
                    std::string        bssid;
                    std::string        flags;
                    line_stream >> id_str >> ssid >> bssid;
                    std::getline(line_stream, flags);
                    flags = trim(flags);

                    info.network_id      = std::atoi(id_str.c_str());
                    info.ssid            = ssid;
                    info.is_current      = (flags.find("CURRENT") != std::string::npos);
                    info.is_enabled_auto = (flags.find("DISABLED") == std::string::npos);
                    info.priority        = 0;
                    out.push_back(info);
                }
            }

            int parseSignalPollRssi(const std::string& raw)
            {
                std::istringstream iss(raw);
                std::string        line;
                while (std::getline(iss, line))
                {
                    if (line.find("RSSI=") == 0)
                        return std::atoi(line.substr(RSSI_PREFIX_LENGTH).c_str());
                }
                return 0;
            }

            std::string parseIPv4FromAddrShow(const std::string& output)
            {
                size_t pos = output.find("inet ");
                if (pos != std::string::npos)
                {
                    pos += INET_PREFIX_LENGTH;
                    size_t end = output.find('/', pos);
                    if (end != std::string::npos)
                        return trim(output.substr(pos, end - pos));
                }
                return "";
            }

            bool linkShowHasUpFlag(const std::string& ip_link_show_output)
            {
                size_t start = ip_link_show_output.find('<');
                size_t end   = ip_link_show_output.find('>');
                if (start != std::string::npos && end != std::string::npos)
                {
                    std::string flags = ip_link_show_output.substr(start + 1, end - start - 1);
                    return flags.find("UP") != std::string::npos;
                }
                return false;
            }

            bool parseAddNetworkId(const std::string& wpa_output, int& out_id)
            {
                try
                {
                    out_id = std::stoi(trim(wpa_output));
                    return true;
                }
                catch (...)
                {
                    return false;
                }
            }

            void mergeSavedNetworkPriorities(const std::string& wpa_conf_path,
                                             std::vector<SavedNetworkInfo>& networks)
            {
                std::ifstream conf_file(wpa_conf_path);
                if (!conf_file.is_open())
                    return;

                std::string conf_line;
                int         current_net_id = -1;

                while (std::getline(conf_file, conf_line))
                {
                    conf_line = trim(conf_line);

                    if (conf_line.find("network=") == 0)
                        current_net_id = -1;
                    else if (conf_line.find("id_str=") == 0)
                    {
                        size_t pos = conf_line.find('=');
                        if (pos != std::string::npos)
                        {
                            std::string id = trim(conf_line.substr(pos + 1));
                            if (!id.empty() && id[0] == '"')
                                id = id.substr(1, id.length() - 2);
                            current_net_id = std::atoi(id.c_str());
                        }
                    }
                    else if (conf_line.find("priority=") == 0 && current_net_id >= 0)
                    {
                        size_t pos = conf_line.find('=');
                        if (pos != std::string::npos)
                        {
                            int priority = std::atoi(conf_line.substr(pos + 1).c_str());
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
            }

        } // namespace wifi
    }     // namespace network
} // namespace app
