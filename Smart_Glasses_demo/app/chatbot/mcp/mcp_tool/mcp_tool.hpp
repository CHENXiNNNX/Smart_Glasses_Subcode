/**
 * @file mcp_tool.hpp
 * @brief MCP工具注册管理
 * @details 负责注册所有Smart_Glasses设备工具到MCP服务器
 *
 * @author Smart Glasses Team
 * @date 2025-01-29
 */

#ifndef MCP_TOOL_HPP
#define MCP_TOOL_HPP

// 前向声明
namespace app
{
    namespace media
    {
        namespace audio
        {
            class AudioSystem;
        }
        namespace camera
        {
            class VideoSystem;
        }
    }
    namespace network
    {
        namespace wifi
        {
            class WifiManager;
        }
    }
}

namespace app
{
    namespace chatbot
    {
        namespace mcp
        {

            class McpServer;

            namespace mcp_tool
            {

                /**
                 * @brief MCP工具管理器
                 * @details 负责向MCP服务器注册所有设备工具
                 */
                class McpToolManager
                {
                public:
                    /**
                     * @brief 注册所有工具到MCP服务器
                     * @param mcp_server MCP服务器实例
                     * @param audio_system 音频系统指针（可选）
                     * @param video_system 视频系统指针（可选）
                     * @param wifi_manager WiFi管理器指针（可选）
                     * @return 成功注册的工具数量
                     */
                    static int registerAllTools(McpServer& mcp_server, 
                                               app::media::audio::AudioSystem* audio_system = nullptr,
                                               app::media::camera::VideoSystem* video_system = nullptr,
                                               app::network::wifi::WifiManager* wifi_manager = nullptr);

                    /**
                     * @brief 注册系统工具
                     * @param mcp_server MCP服务器实例
                     * @return 注册的工具数量
                     */
                    static int registerSystemTools(McpServer& mcp_server);

                    /**
                     * @brief 注册音频工具
                     * @param mcp_server MCP服务器实例
                     * @param audio_system 音频系统指针（必需）
                     * @return 注册的工具数量
                     */
                    static int registerAudioTools(McpServer& mcp_server,
                                                 app::media::audio::AudioSystem* audio_system);

                    /**
                     * @brief 注册视频工具
                     * @param mcp_server MCP服务器实例
                     * @param video_system 视频系统指针（可选）
                     * @return 注册的工具数量
                     */
                    static int registerVideoTools(McpServer& mcp_server,
                                                 app::media::camera::VideoSystem* video_system = nullptr);

                    /**
                     * @brief 注册网络工具
                     * @param mcp_server MCP服务器实例
                     * @param wifi_manager WiFi管理器指针（可选）
                     * @return 注册的工具数量
                     */
                    static int registerNetworkTools(McpServer& mcp_server,
                                                    app::network::wifi::WifiManager* wifi_manager = nullptr);
                };

            } // namespace mcp_tool
        }     // namespace mcp
    }         // namespace chatbot
} // namespace app

#endif // MCP_TOOL_HPP
