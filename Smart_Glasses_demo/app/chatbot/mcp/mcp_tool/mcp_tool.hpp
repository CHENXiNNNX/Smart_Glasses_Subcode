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
                     * @return 成功注册的工具数量
                     */
                    static int registerAllTools(McpServer& mcp_server);

                    /**
                     * @brief 注册系统工具
                     * @param mcp_server MCP服务器实例
                     * @return 注册的工具数量
                     */
                    static int registerSystemTools(McpServer& mcp_server);

                    /**
                     * @brief 注册音频工具
                     * @param mcp_server MCP服务器实例
                     * @return 注册的工具数量
                     */
                    static int registerAudioTools(McpServer& mcp_server);

                    /**
                     * @brief 注册视频工具
                     * @param mcp_server MCP服务器实例
                     * @return 注册的工具数量
                     */
                    static int registerVideoTools(McpServer& mcp_server);

                    /**
                     * @brief 注册网络工具
                     * @param mcp_server MCP服务器实例
                     * @return 注册的工具数量
                     */
                    static int registerNetworkTools(McpServer& mcp_server);
                };

            } // namespace mcp_tool
        }     // namespace mcp
    }         // namespace chatbot
} // namespace app

#endif // MCP_TOOL_HPP
