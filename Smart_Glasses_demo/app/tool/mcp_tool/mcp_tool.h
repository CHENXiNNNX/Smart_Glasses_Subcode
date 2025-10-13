// /**
//  * @file mcp_tool.h
//  * @brief MCP工具注册管理
//  * @details 负责注册所有IoT设备工具到MCP服务器
//  * 
//  * @author Smart Glasses Team
//  * @date 2025-10-11
//  */

// #ifndef MCP_TOOL_H
// #define MCP_TOOL_H

// namespace glasses {
// namespace chatbot {
// namespace mcp {

// class McpServer;

// }}} // namespace glasses::chatbot::mcp

// namespace glasses {
// namespace tool {

// /**
//  * @brief MCP工具管理器
//  * @details 负责向MCP服务器注册所有设备工具
//  */
// class McpToolManager {
// public:
//     /**
//      * @brief 注册所有工具
//      * @param mcp_server MCP服务器实例
//      */
//     static void register_all_tools(chatbot::mcp::McpServer& mcp_server);

// private:
//     // 分类注册函数
//     static void register_system_tools(chatbot::mcp::McpServer& mcp_server);
//     static void register_audio_tools(chatbot::mcp::McpServer& mcp_server);
// };

// } // namespace tool
// } // namespace glasses

// #endif // MCP_TOOL_H

