/**
 * @file mcp_tool.cc
 * @brief MCP工具注册管理实现
 */

#include "mcp_tool.hpp"
#include "../mcp.hpp"
#include "../../../tool/log/log.hpp"
#include "../../../media/audio/audio.hpp"
#include "../../../media/camera/camera.hpp"

#include <string>
#include <exception>

using namespace app::tool::log;

namespace app
{
    namespace chatbot
    {
        namespace mcp
        {
            namespace mcp_tool
            {

                // ============================================================================
                // 系统工具注册
                // ============================================================================

                int McpToolManager::registerSystemTools(McpServer& mcp_server)
                {
                    (void)mcp_server; // 暂未实现
                    int count = 0;

                    // TODO: 等待真实的系统信息接口实现后再注册工具
                    // 如：获取设备状态、CPU使用率、内存使用率、温度等

                    LOG_INFO("MCP_Tool", "已注册 %d 个系统工具", count);
                    return count;
                }

                // ============================================================================
                // 音频工具注册
                // ============================================================================

                int McpToolManager::registerAudioTools(McpServer& mcp_server,
                                                       app::media::audio::AudioSystem* audio_system)
                {
                    int count = 0;

                    if (!audio_system)
                    {
                        LOG_WARN("MCP_Tool", "音频系统指针为空，跳过音频工具注册");
                        return count;
                    }

                    // ========================================================================
                    // 工具：设置音量
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.audio_speaker.set_volume",
                            "Set the current audio volume to a specific value within the range of 0 to 100. When I say to set the volume to a certain value, this MCP tool will be invoked for the setting.",
                            mcp::PropertyList({mcp::Property("volume", mcp::PropertyType::Integer,
                                                             50, 0, 100)}),
                            [audio_system](const mcp::PropertyList& props) -> mcp::ReturnValue
                            {
                                int volume = props["volume"].value<int>();
                                LOG_INFO("MCP_Tool", "设置音量为 %d%%", volume);

                                // 设置音量
                                audio_system->setOutputVolume(volume);

                                mcp::json result;
                                result["success"] = true;
                                result["volume"]  = volume;
                                result["message"] = "音量已设置为 " + std::to_string(volume) + "%";
                                return result;
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    // ========================================================================
                    // 工具：获取音量
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.audio_speaker.get_volume",
                            "Obtain the specific numerical value of the current audio volume. That is, when I say the statement related to obtaining the current volume, this mcp tool will be called to check it.）",
                            mcp::PropertyList(),
                            [audio_system](const mcp::PropertyList& props [[maybe_unused]])
                                -> mcp::ReturnValue
                            {
                                // 获取当前音量
                                int volume = audio_system->getOutputVolume();
                                LOG_DEBUG("MCP_Tool", "当前音量: %d%%", volume);

                                mcp::json result;
                                result["success"] = true;
                                result["volume"]  = volume;
                                result["message"] = "当前音量: " + std::to_string(volume) + "%";
                                return result;
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    LOG_INFO("MCP_Tool", "已注册 %d 个音频工具", count);
                    return count;
                }

                // ============================================================================
                // 视频工具注册
                // ============================================================================

                int McpToolManager::registerVideoTools(McpServer& mcp_server,
                                                        app::media::camera::VideoSystem* video_system)
                {
                    int count = 0;

                    if (!video_system)
                    {
                        LOG_WARN("MCP_Tool", "视频系统指针为空，跳过视频工具注册");
                        return count;
                    }

                    // ========================================================================
                    // 工具：拍照并解释图像
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.camera.take_photo",
                            "Take a photo and explain it using AI. Use this tool after the user asks you to see something or analyze an image.\n"
                            "Args:\n"
                            "  `question`: The question that you want to ask about the photo.\n"
                            "Return:\n"
                            "  A JSON object that provides the photo analysis result.",
                            mcp::PropertyList({mcp::Property("question", mcp::PropertyType::String)}),
                            [video_system](const mcp::PropertyList& props) -> mcp::ReturnValue
                            {
                                std::string question = props["question"].value<std::string>();
                                LOG_INFO("MCP_Tool", "拍照并解释图像，问题: %s", question.c_str());

                                try
                                {
                                    // 调用图像解释功能
                                    std::string result = video_system->explainImage(question);
                                    LOG_INFO("MCP_Tool", "图像解释完成，结果长度: %zu", result.size());

                                    // 尝试解析JSON响应
                                    mcp::json result_json;
                                    try
                                    {
                                        result_json = mcp::json::parse(result);
                                    }
                                    catch (const std::exception& e)
                                    {
                                        // 如果解析失败，将原始响应作为字符串返回
                                        LOG_WARN("MCP_Tool", "无法解析JSON响应，返回原始字符串: %s", e.what());
                                        result_json["success"] = true;
                                        result_json["raw_response"] = result;
                                    }

                                    return result_json;
                                }
                                catch (const std::exception& e)
                                {
                                    LOG_ERROR("MCP_Tool", "图像解释失败: %s", e.what());
                                    mcp::json error_result;
                                    error_result["success"] = false;
                                    error_result["message"] = std::string("图像解释失败: ") + e.what();
                                    return error_result;
                                }
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    LOG_INFO("MCP_Tool", "已注册 %d 个视频工具", count);
                    return count;
                }

                // ============================================================================
                // 网络工具注册
                // ============================================================================

                int McpToolManager::registerNetworkTools(McpServer& mcp_server)
                {
                    (void)mcp_server; // 暂未实现
                    int count = 0;

                    // TODO: 等待网络系统接口暴露后再注册工具
                    // 如：获取WiFi状态、信号强度、切换网络等
                    // 需要 App 类将 wifi_manager_ 注入到 MCP 工具中

                    LOG_INFO("MCP_Tool", "已注册 %d 个网络工具", count);
                    return count;
                }

                // ============================================================================
                // 注册所有工具
                // ============================================================================

                int McpToolManager::registerAllTools(McpServer& mcp_server,
                                                      app::media::audio::AudioSystem* audio_system,
                                                      app::media::camera::VideoSystem* video_system)
                {
                    LOG_INFO("MCP_Tool", "========================================");
                    LOG_INFO("MCP_Tool", "  开始注册MCP工具...");
                    LOG_INFO("MCP_Tool", "========================================");

                    int total = 0;

                    total += registerSystemTools(mcp_server);
                    total += registerAudioTools(mcp_server, audio_system);
                    total += registerVideoTools(mcp_server, video_system);
                    total += registerNetworkTools(mcp_server);

                    LOG_INFO("MCP_Tool", "========================================");
                    if (total == 0)
                    {
                        LOG_INFO("MCP_Tool", "  暂无可用工具（等待实现）");
                    }
                    else
                    {
                        LOG_INFO("MCP_Tool", "  工具注册总数: %d", total);
                    }
                    LOG_INFO("MCP_Tool", "========================================");

                    return total;
                }

            } // namespace mcp_tool
        }     // namespace mcp
    }         // namespace chatbot
} // namespace app
