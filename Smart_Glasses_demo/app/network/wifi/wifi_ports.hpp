/* wifi_ports.hpp - WiFi 端口（可替换底层实现） */

#pragma once

#include "wifi_types.hpp"

#include <memory>
#include <string>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            /// 通用 shell 执行（测试时可 mock）
            class IShellRunner
            {
            public:
                virtual ~IShellRunner()                                  = default;
                virtual std::string run(const std::string& command_line) = 0;
            };

            /// wpa_cli 命令（不含可执行文件名；实现侧负责拼 `wpa_cli -i <iface> ...`）
            class IWpaControl
            {
            public:
                virtual ~IWpaControl()                                   = default;
                virtual std::string cli(const std::string& wpa_cli_args) = 0;
            };

            class ILinkLayer
            {
            public:
                virtual ~ILinkLayer()                                    = default;
                virtual std::string linkShow(const std::string& ifname)  = 0;
                virtual std::string addrShow(const std::string& ifname)  = 0;
                virtual std::string setLinkUp(const std::string& ifname) = 0;
                virtual std::string addrFlush(const std::string& ifname) = 0;
            };

            class IDhcpClient
            {
            public:
                virtual ~IDhcpClient()                                 = default;
                virtual std::string request(const std::string& ifname) = 0;
            };

            /// 注入到 WifiService / WifiManager 的硬件抽象组合
            struct WifiPorts
            {
                std::shared_ptr<IShellRunner> shell;
                std::shared_ptr<IWpaControl>  wpa;
                std::shared_ptr<ILinkLayer>   link;
                std::shared_ptr<IDhcpClient>  dhcp;
            };

            /// 默认 Linux：popen + wpa_cli + ip + udhcpc
            WifiPorts makeLinuxWifiPorts(const WifiConfig& config);

        } // namespace wifi
    }     // namespace network
} // namespace app
