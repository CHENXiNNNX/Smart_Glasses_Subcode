/* mcp_tool.hpp - MCP 工具注册 */

#pragma once

#include <functional>
#include <string>

namespace app::chatbot::mcp
{
    class McpServer;
}

namespace app::chatbot::mcp::mcp_tool
{

    /*============================================================================
     * 媒体能力回调（可选，空则跳过对应工具）
     *============================================================================*/

    struct MediaHandles
    {
        /* 音频 */
        std::function<void(int)> set_volume;
        std::function<int()>     get_volume;

        /* 相机 - AI 识图 */
        std::function<void(const std::string&, const std::string&)> set_explain_url;
        std::function<std::string(const std::string&)>              explain_image;

        /* 相机 - 拍照 */
        std::function<bool(const std::string&, std::function<void(bool)>)> save_photo;

        /* 相机 - 录像 */
        std::function<bool(const std::string&, int)> start_record; /* path, duration_sec */
        std::function<void()>                        stop_record;
        std::function<bool()>                        is_recording;

        /* 相机 - 流控制 */
        std::function<bool()> is_running;
        std::function<bool()> start_stream;
        std::function<void()> stop_stream;
    };

    /*============================================================================
     * 工具注册
     *============================================================================*/

    /* 注册所有工具到 MCP 服务器 */
    int register_tools(mcp::McpServer& server, const MediaHandles& handles);

} // namespace app::chatbot::mcp::mcp_tool
