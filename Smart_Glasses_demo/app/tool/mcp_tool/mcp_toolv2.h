/**
 * @file mcp_toolv2.h
 * @brief MCP工具注册管理V2
 * @details 负责注册所有Smart_Glasses设备工具到MCP服务器V2
 * 
 * @author Smart Glasses Team
 * @date 2025-01-11
 */

#ifndef MCP_TOOLV2_H
#define MCP_TOOLV2_H

namespace glasses {
namespace chatbot {
namespace mcp {

class McpServerV2;

}}} // namespace glasses::chatbot::mcp

namespace glasses {
namespace tool {

/**
 * @brief MCP工具管理器V2
 * @details 负责向MCP服务器V2注册所有设备工具
 */
class McpToolManagerV2 {
public:
    /**
     * @brief 注册所有工具到MCP服务器
     * @param mcp_server MCP服务器V2实例
     * @return 成功注册的工具数量
     */
    static int register_all_tools(chatbot::mcp::McpServerV2& mcp_server);
    
    /**
     * @brief 注册系统工具
     * @param mcp_server MCP服务器实例
     * @return 注册的工具数量
     */
    static int register_system_tools(chatbot::mcp::McpServerV2& mcp_server);
    
    /**
     * @brief 注册音频工具
     * @param mcp_server MCP服务器实例
     * @return 注册的工具数量
     */
    static int register_audio_tools(chatbot::mcp::McpServerV2& mcp_server);
    
    /**
     * @brief 注册视频工具
     * @param mcp_server MCP服务器实例
     * @return 注册的工具数量
     */
    static int register_video_tools(chatbot::mcp::McpServerV2& mcp_server);
    
    /**
     * @brief 注册网络工具
     * @param mcp_server MCP服务器实例
     * @return 注册的工具数量
     */
    static int register_network_tools(chatbot::mcp::McpServerV2& mcp_server);
};

} // namespace tool
} // namespace glasses

#endif // MCP_TOOLV2_H

