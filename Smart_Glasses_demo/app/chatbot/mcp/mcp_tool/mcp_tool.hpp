/**
 * @file mcp_tool.hpp
 * @brief MCP工具注册管理
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
    } // namespace media
    namespace network
    {
        namespace wifi
        {
            class WifiManager;
        }
    } // namespace network
    namespace protocol
    {
        namespace webrtc
        {
            class Signaling;
            class WebRTCSystem;
        }
    } // namespace protocol
    namespace chatbot
    {
        class ChatbotSystem;
    }
} // namespace app

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
                 */
                class McpToolManager
                {
                public:
                    /**
                     * @brief 注册所有工具到MCP服务器
                     * @param mcp_server MCP服务器实例
                     * @param audio_system 音频系统指针
                     * @param video_system 视频系统指针
                     * @param wifi_manager WiFi管理器指针
                     * @param signaling 信令系统指针
                     * @param webrtc_system Webrtc系统指针
                     * @param chatbot_system Chatbot系统指针
                     * @return 成功注册的工具数量
                     */
                    static int
                    registerAllTools(McpServer&                           mcp_server,
                                     app::media::audio::AudioSystem*      audio_system  = nullptr,
                                     app::media::camera::VideoSystem*     video_system  = nullptr,
                                     app::network::wifi::WifiManager*     wifi_manager  = nullptr,
                                     app::protocol::webrtc::Signaling*    signaling     = nullptr,
                                     app::protocol::webrtc::WebRTCSystem* webrtc_system = nullptr,
                                     app::chatbot::ChatbotSystem*         chatbot_system = nullptr);

                    /**
                     * @brief 注册系统工具
                     * @param mcp_server MCP服务器实例
                     * @return 注册的工具数量
                     */
                    static int registerSystemTools(McpServer& mcp_server);

                    /**
                     * @brief 注册音频工具
                     * @param mcp_server MCP服务器实例
                     * @param audio_system 音频系统指针
                     * @return 注册的工具数量
                     */
                    static int registerAudioTools(McpServer&                      mcp_server,
                                                  app::media::audio::AudioSystem* audio_system = nullptr);

                    /**
                     * @brief 注册视频工具
                     * @param mcp_server MCP服务器实例
                     * @param video_system 视频系统指针
                     * @return 注册的工具数量
                     */
                    static int
                    registerVideoTools(McpServer&                       mcp_server,
                                       app::media::camera::VideoSystem* video_system = nullptr);

                    /**
                     * @brief 注册Webrtc工具
                     * @param mcp_server MCP服务器实例
                     * @param audio_system 音频系统指针
                     * @param signaling 信令系统指针
                     * @param webrtc_system Webrtc系统指针
                     * @param chatbot_system Chatbot系统指针
                     * @return 注册的工具数量
                     */
                    static int registerWebrtcTools(
                        McpServer&                           mcp_server,
                        app::media::audio::AudioSystem*      audio_system   = nullptr,
                        app::protocol::webrtc::Signaling*    signaling      = nullptr,
                        app::protocol::webrtc::WebRTCSystem* webrtc_system  = nullptr,
                        app::chatbot::ChatbotSystem*         chatbot_system = nullptr);

                    /**
                     * @brief 注册网络工具
                     * @param mcp_server MCP服务器实例
                     * @param wifi_manager WiFi管理器指针
                     * @return 注册的工具数量
                     */
                    static int
                    registerNetworkTools(McpServer&                       mcp_server,
                                         app::network::wifi::WifiManager* wifi_manager = nullptr);
                };

            } // namespace mcp_tool
        }     // namespace mcp
    }         // namespace chatbot
} // namespace app

#endif // MCP_TOOL_HPP
