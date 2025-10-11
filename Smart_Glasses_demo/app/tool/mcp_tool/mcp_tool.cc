/**
 * @file mcp_tool.cc
 * @brief MCP工具注册管理实现
 * 
 * @author Smart Glasses Team
 * @date 2025-10-11
 */

#include "mcp_tool.h"
#include "../../chatbot/mcp/mcp.h"
#include <iostream>

namespace glasses {
namespace tool {

using namespace chatbot::mcp;

// ============================================================================
// 系统工具
// ============================================================================

void McpToolManager::register_system_tools(McpServer& mcp_server) {
    // 示例：获取设备状态
    mcp_server.add_tool("self.get_device_status",
        "获取设备的实时状态信息",
        PropertyList(),
        [](const PropertyList& props) -> ReturnValue {
            // TODO: 实现设备状态获取
            json status;
            status["device"] = "Smart_Glasses";
            status["version"] = "1.0.0";
            status["status"] = "ok";
            return status;
        });
}

// ============================================================================
// 音频工具
// ============================================================================

void McpToolManager::register_audio_tools(McpServer& mcp_server) {
    // 示例：设置音量
    mcp_server.add_tool("self.audio_speaker.set_volume",
        "设置音频扬声器音量。如果当前音量未知，必须先调用 self.get_device_status 获取当前音量",
        PropertyList({
            Property("volume", PropertyType::Integer, 0, 100)
        }),
        [](const PropertyList& props) -> ReturnValue {
            int volume = props["volume"].value<int>();
            std::cout << "[MCP Tool] Setting volume to " << volume << std::endl;
            
            // TODO: 实现音量设置
            // audio_system_set_volume(volume);
            
            return true;
        });

    // 示例：获取音量
    mcp_server.add_tool("self.audio_speaker.get_volume",
        "获取当前音频扬声器音量",
        PropertyList(),
        [](const PropertyList& props) -> ReturnValue {
            // TODO: 实现音量获取
            // int volume = audio_system_get_volume();
            int volume = 50; // 占位值
            
            std::cout << "[MCP Tool] Current volume: " << volume << std::endl;
            return volume;
        });
}


// ============================================================================
// 注册所有工具
// ============================================================================

void McpToolManager::register_all_tools(McpServer& mcp_server) {
    std::cout << "[MCP Tool] ======================================" << std::endl;
    std::cout << "[MCP Tool] Registering all tools..." << std::endl;
    std::cout << "[MCP Tool] ======================================" << std::endl;

    register_system_tools(mcp_server);
    register_audio_tools(mcp_server);

    std::cout << "[MCP Tool] ======================================" << std::endl;
    std::cout << "[MCP Tool] ✓ Total tools registered: " << mcp_server.tool_count() << std::endl;
    std::cout << "[MCP Tool] ======================================" << std::endl;
}

} // namespace tool
} // namespace glasses

