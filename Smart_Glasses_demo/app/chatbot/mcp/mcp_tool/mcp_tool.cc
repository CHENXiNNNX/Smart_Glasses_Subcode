/* mcp_tool.cc - MCP 工具注册 */

#include "mcp_tool.hpp"
#include "../mcp.hpp"
#include "../../../tool/log/log.hpp"

#include <chrono>
#include <thread>

#define TAG "MCP_TOOL"

namespace app::chatbot::mcp::mcp_tool
{

    using namespace app::tool::log;

#define HAS(h, field) (static_cast<bool>((h).field))

    int register_tools(mcp::McpServer& server, const MediaHandles& handles)
    {
        int count = 0;

        /* 音频工具 */
        if (HAS(handles, set_volume))
        {
            auto err = server.add_tool(
                "self.audio_speaker.set_volume", "Set the current audio volume (0-100).",
                {mcp::Property("volume", mcp::PropertyType::Integer, 50, 0, 100)},
                [set_vol = handles.set_volume](const mcp::PropertyList& props) -> mcp::ReturnValue
                {
                    int vol = props["volume"].value<int>();
                    set_vol(vol);
                    return mcp::json{{"success", true}, {"volume", vol}};
                });
            if (err == mcp::McpError::NONE)
                count++;
        }

        if (HAS(handles, get_volume))
        {
            auto err = server.add_tool(
                "self.audio_speaker.get_volume", "Get the current audio volume.", {},
                [get_vol = handles.get_volume](const mcp::PropertyList&) -> mcp::ReturnValue
                {
                    int vol = get_vol();
                    return mcp::json{{"success", true}, {"volume", vol}};
                });
            if (err == mcp::McpError::NONE)
                count++;
        }

        /* 相机 - AI 识图 */
        if (HAS(handles, explain_image))
        {
            auto err = server.add_tool(
                "self.camera.take_photo",
                "Take a photo and explain it using AI. Use after user asks to see something.",
                {mcp::Property("question", mcp::PropertyType::String)},
                [explain =
                     handles.explain_image](const mcp::PropertyList& props) -> mcp::ReturnValue
                {
                    std::string q      = props["question"].value<std::string>();
                    std::string result = explain(q);
                    try
                    {
                        return mcp::json::parse(result);
                    }
                    catch (...)
                    {
                        return mcp::json{{"success", true}, {"raw_response", result}};
                    }
                });
            if (err == mcp::McpError::NONE)
                count++;
        }

        /* 相机 - 保存照片 */
        if (HAS(handles, save_photo))
        {
            auto err = server.add_tool(
                "self.camera.save_photo", "Take a photo and save to local storage.",
                {mcp::Property("filename", mcp::PropertyType::String, std::string(""))},
                [&handles](const mcp::PropertyList& props) -> mcp::ReturnValue
                {
                    std::string fn   = props["filename"].value<std::string>();
                    std::string path = fn.empty() ? "/tmp/photo.jpg" : fn;
                    if (path.find('/') == std::string::npos)
                        path = "/tmp/" + path;

                    mcp::json         result;
                    std::atomic<bool> done{false};
                    handles.save_photo(path,
                                       [&](bool ok)
                                       {
                                           result["success"] = ok;
                                           result["path"]    = path;
                                           result["message"] = ok ? "Saved" : "Failed";
                                           done              = true;
                                       });

                    int timeout = 50;
                    while (!done && timeout-- > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    return result;
                });
            if (err == mcp::McpError::NONE)
                count++;
        }

        /* 相机 - 录像 */
        if (HAS(handles, start_record) && HAS(handles, stop_record))
        {
            auto err = server.add_tool(
                "self.camera.start_record", "Start video recording.",
                {mcp::Property("path", mcp::PropertyType::String, std::string("/tmp/video.h264")),
                 mcp::Property("duration_sec", mcp::PropertyType::Integer, 0)},
                [&handles](const mcp::PropertyList& props) -> mcp::ReturnValue
                {
                    std::string path = props["path"].value<std::string>();
                    int         dur  = props["duration_sec"].value<int>();
                    bool        ok   = handles.start_record(path, dur);
                    return mcp::json{{"success", ok}, {"path", path}};
                });
            if (err == mcp::McpError::NONE)
                count++;

            err = server.add_tool(
                "self.camera.stop_record", "Stop video recording.", {},
                [stop = handles.stop_record](const mcp::PropertyList&) -> mcp::ReturnValue
                {
                    stop();
                    return mcp::json{{"success", true}};
                });
            if (err == mcp::McpError::NONE)
                count++;
        }

        LOG_DEBUG(TAG, "注册 %d 个工具", count);
        return count;
    }

} // namespace app::chatbot::mcp::mcp_tool
