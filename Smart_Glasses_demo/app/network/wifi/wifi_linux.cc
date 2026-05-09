/* wifi_linux.cc - Linux shell / wpa_cli / ip / udhcpc 适配器 */

#include "wifi_ports.hpp"
#include "../../tool/log/log.hpp"

#include <cstdio>
#include <memory>
#include <utility>

namespace app
{
    namespace network
    {
        namespace wifi
        {

            using namespace app::tool::log;

            namespace
            {
                constexpr const char* LOG_TAG             = "WiFi";
                constexpr std::size_t COMMAND_BUFFER_SIZE = 4096;
            } // namespace

            class LinuxShellRunner final : public IShellRunner
            {
            public:
                std::string run(const std::string& command_line) override
                {
                    char        buffer[COMMAND_BUFFER_SIZE];
                    std::string result;

                    FILE* pipe = popen(command_line.c_str(), "r");
                    if (!pipe)
                    {
                        LOG_ERROR(LOG_TAG, "执行命令失败: %s", command_line.c_str());
                        return "";
                    }
                    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
                        result += buffer;
                    pclose(pipe);
                    return result;
                }
            };

            class LinuxWpaControl final : public IWpaControl
            {
            public:
                LinuxWpaControl(std::shared_ptr<IShellRunner> shell, std::string ifname)
                    : shell_(std::move(shell)), ifname_(std::move(ifname))
                {
                }

                std::string cli(const std::string& wpa_cli_args) override
                {
                    std::string full = "wpa_cli -i " + ifname_ + " " + wpa_cli_args + " 2>&1";
                    return shell_->run(full);
                }

            private:
                std::shared_ptr<IShellRunner> shell_;
                std::string                   ifname_;
            };

            class LinuxLinkLayer final : public ILinkLayer
            {
            public:
                explicit LinuxLinkLayer(std::shared_ptr<IShellRunner> shell)
                    : shell_(std::move(shell))
                {
                }

                std::string linkShow(const std::string& ifname) override
                {
                    return shell_->run("ip link show " + ifname + " 2>&1");
                }

                std::string addrShow(const std::string& ifname) override
                {
                    return shell_->run("ip addr show " + ifname + " 2>&1");
                }

                std::string setLinkUp(const std::string& ifname) override
                {
                    return shell_->run("ip link set " + ifname + " up 2>&1");
                }

                std::string addrFlush(const std::string& ifname) override
                {
                    return shell_->run("ip addr flush dev " + ifname + " 2>&1");
                }

            private:
                std::shared_ptr<IShellRunner> shell_;
            };

            class LinuxUdhcpcClient final : public IDhcpClient
            {
            public:
                explicit LinuxUdhcpcClient(std::shared_ptr<IShellRunner> shell)
                    : shell_(std::move(shell))
                {
                }

                std::string request(const std::string& ifname) override
                {
                    return shell_->run("udhcpc -i " + ifname + " -n -q -t 5 2>&1");
                }

            private:
                std::shared_ptr<IShellRunner> shell_;
            };

            WifiPorts makeLinuxWifiPorts(const WifiConfig& config)
            {
                auto      shell = std::make_shared<LinuxShellRunner>();
                WifiPorts ports;
                ports.shell = shell;
                ports.wpa   = std::make_shared<LinuxWpaControl>(shell, config.interface_name);
                ports.link  = std::make_shared<LinuxLinkLayer>(shell);
                ports.dhcp  = std::make_shared<LinuxUdhcpcClient>(shell);
                return ports;
            }

        } // namespace wifi
    }     // namespace network
} // namespace app
