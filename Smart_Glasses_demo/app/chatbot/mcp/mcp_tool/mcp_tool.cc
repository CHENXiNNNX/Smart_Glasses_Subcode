/**
 * @file mcp_tool.cc
 * @brief MCP工具注册管理实现
 */

#include "mcp_tool.hpp"
#include "../mcp.hpp"
#include "../../../tool/log/log.hpp"
#include "../../../media/audio/audio.hpp"
#include "../../../media/camera/camera.hpp"
#include "../../../network/wifi/wifi.hpp"
#include "../../../protocol/webrtc/signaling.hpp"
#include "../../../protocol/webrtc/webrtc.hpp"
#include "../../chatbot.hpp"  

#include <string>
#include <exception>
#include <thread>
#include <chrono>

using namespace app::tool::log;

namespace app
{
    namespace chatbot
    {
        namespace mcp
        {
            namespace mcp_tool
            {

                namespace
                {
                    constexpr const char* LOG_TAG = "MCP_TOOL";
                } // namespace

                // ============================================================================
                // 系统工具注册
                // ============================================================================

                int McpToolManager::registerSystemTools(McpServer& mcp_server)
                {
                    (void)mcp_server; // 暂未实现
                    int count = 0;

                    // TODO: 等待真实的系统信息接口实现后再注册工具
                    // 如：获取设备状态、CPU使用率、内存使用率、温度等

                    LOG_INFO(LOG_TAG, "已注册 %d 个系统工具", count);
                    return count;
                }

                // ============================================================================
                // 音频工具注册
                // ============================================================================

                int McpToolManager::registerAudioTools(McpServer&                      mcp_server,
                                                       app::media::audio::AudioSystem* audio_system)
                {
                    int count = 0;

                    if (!audio_system)
                    {
                        LOG_WARN(LOG_TAG, "音频系统指针为空，跳过音频工具注册");
                        return count;
                    }

                    // ========================================================================
                    // 工具：设置音量
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.audio_speaker.set_volume",
                            "Set the current audio volume to a specific value within the range of "
                            "0 to 100. When I say to set the volume to a certain value, this MCP "
                            "tool will be invoked for the setting.",
                            mcp::PropertyList(
                                {mcp::Property("volume", mcp::PropertyType::Integer, 50, 0, 100)}),
                            [audio_system](const mcp::PropertyList& props) -> mcp::ReturnValue
                            {
                                int volume = props["volume"].value<int>();
                                LOG_INFO(LOG_TAG, "设置音量为 %d%%", volume);

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
                            "Obtain the specific numerical value of the current audio volume. That "
                            "is, when I say the statement related to obtaining the current volume, "
                            "this mcp tool will be called to check it.）",
                            mcp::PropertyList(),
                            [audio_system](const mcp::PropertyList& props
                                           [[maybe_unused]]) -> mcp::ReturnValue
                            {
                                // 获取当前音量
                                int volume = audio_system->getOutputVolume();
                                LOG_DEBUG(LOG_TAG, "当前音量: %d%%", volume);

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

                    LOG_INFO(LOG_TAG, "已注册 %d 个音频工具", count);
                    return count;
                }

                // ============================================================================
                // 视频工具注册
                // ============================================================================

                int
                McpToolManager::registerVideoTools(McpServer&                       mcp_server,
                                                   app::media::camera::VideoSystem* video_system)
                {
                    int count = 0;

                    if (!video_system)
                    {
                        LOG_WARN(LOG_TAG, "视频系统指针为空，跳过视频工具注册");
                        return count;
                    }

                    // ========================================================================
                    // 工具：拍照并解释图像
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.camera.take_photo",
                            "Take a photo and explain it using AI. Use this tool after the user "
                            "asks you to see something or analyze an image.\n"
                            "Args:\n"
                            "  `question`: The question that you want to ask about the photo.\n"
                            "Return:\n"
                            "  A JSON object that provides the photo analysis result.",
                            mcp::PropertyList(
                                {mcp::Property("question", mcp::PropertyType::String)}),
                            [video_system](const mcp::PropertyList& props) -> mcp::ReturnValue
                            {
                                std::string question = props["question"].value<std::string>();
                                LOG_INFO(LOG_TAG, "拍照并解释图像，问题: %s", question.c_str());

                                try
                                {
                                    // 调用图像解释功能
                                    std::string result = video_system->explainImage(question);
                                    LOG_INFO(LOG_TAG, "图像解释完成，结果长度: %zu",
                                             result.size());

                                    // 尝试解析JSON响应
                                    mcp::json result_json;
                                    try
                                    {
                                        result_json = mcp::json::parse(result);
                                    }
                                    catch (const std::exception& e)
                                    {
                                        // 如果解析失败，将原始响应作为字符串返回
                                        LOG_WARN(LOG_TAG, "无法解析JSON响应，返回原始字符串: %s",
                                                 e.what());
                                        result_json["success"]      = true;
                                        result_json["raw_response"] = result;
                                    }

                                    return result_json;
                                }
                                catch (const std::exception& e)
                                {
                                    LOG_ERROR(LOG_TAG, "图像解释失败: %s", e.what());
                                    mcp::json error_result;
                                    error_result["success"] = false;
                                    error_result["message"] =
                                        std::string("图像解释失败: ") + e.what();
                                    return error_result;
                                }
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    // ========================================================================
                    // 工具：拍照并保存到本地
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.camera.save_photo",
                            "Take a photo and save it to local storage. The photo will be saved as "
                            "a JPEG file.\n"
                            "Args:\n"
                            "  `filename` (optional): The filename to save the photo. If not "
                            "provided, a filename will be auto-generated.\n"
                            "Return:\n"
                            "  A JSON object containing the saved file path and status.",
                            mcp::PropertyList({mcp::Property("filename", mcp::PropertyType::String,
                                                             std::string(""))}),
                            [video_system](const mcp::PropertyList& props) -> mcp::ReturnValue
                            {
                                std::string filename = props["filename"].value<std::string>();
                                LOG_INFO(LOG_TAG, "拍照并保存，文件名: %s",
                                         filename.empty() ? "(自动生成)" : filename.c_str());

                                // 检查是否正在拍照
                                if (video_system->isPhotoCapturing())
                                {
                                    LOG_WARN(LOG_TAG, "正在拍照中，请稍候...");
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] = "正在拍照中，请稍候再试";
                                    return result;
                                }

                                // 确保流已启动（拍照需要流处理线程）
                                if (!video_system->isStreaming())
                                {
                                    LOG_INFO(LOG_TAG, "启动视频流以支持拍照...");
                                    app::media::camera::VideoError stream_err =
                                        video_system->startStream();
                                    if (stream_err != app::media::camera::VideoError::NONE)
                                    {
                                        LOG_ERROR(LOG_TAG, "启动视频流失败，错误码: %d",
                                                  static_cast<int>(stream_err));
                                        mcp::json result;
                                        result["success"] = false;
                                        result["message"] =
                                            "启动视频流失败，错误码: " +
                                            std::to_string(static_cast<int>(stream_err));
                                        return result;
                                    }
                                    // 等待流稳定
                                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                }

                                // 设置主状态为PHOTO（这样流处理线程才会处理拍照帧）
                                app::media::camera::VideoMainState old_state =
                                    video_system->getMainState();
                                app::media::camera::VideoError state_err =
                                    video_system->setMainState(
                                        app::media::camera::VideoMainState::PHOTO);
                                if (state_err != app::media::camera::VideoError::NONE)
                                {
                                    LOG_ERROR(LOG_TAG, "设置主状态失败，错误码: %d",
                                              static_cast<int>(state_err));
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] = "设置主状态失败";
                                    return result;
                                }

                                // 调用拍照功能
                                app::media::camera::VideoError photo_err =
                                    video_system->takePhoto(filename, true);
                                if (photo_err != app::media::camera::VideoError::NONE)
                                {
                                    // 恢复主状态
                                    video_system->setMainState(old_state);
                                    LOG_ERROR(LOG_TAG, "拍照失败，错误码: %d",
                                              static_cast<int>(photo_err));
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] = "拍照失败，错误码: " +
                                                        std::to_string(static_cast<int>(photo_err));
                                    return result;
                                }

                                // 等待拍照完成（最多等待5秒）
                                const int max_wait_ms       = 5000;
                                const int check_interval_ms = 100;
                                int       waited_ms         = 0;

                                while (video_system->isPhotoCapturing() && waited_ms < max_wait_ms)
                                {
                                    std::this_thread::sleep_for(
                                        std::chrono::milliseconds(check_interval_ms));
                                    waited_ms += check_interval_ms;
                                }

                                // 恢复主状态
                                video_system->setMainState(old_state);

                                if (video_system->isPhotoCapturing())
                                {
                                    LOG_ERROR(LOG_TAG, "拍照超时");
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] = "拍照超时";
                                    return result;
                                }

                                // 构建返回结果
                                mcp::json result;
                                result["success"] = true;

                                if (!filename.empty())
                                {
                                    // 如果指定了文件名，返回完整路径
                                    result["filename"] = filename;
                                    result["filepath"] = filename;
                                    result["message"]  = "照片已保存: " + filename;
                                }
                                else
                                {
                                    // 文件名自动生成，返回保存目录信息
                                    result["filename"] = "auto_generated";
                                    result["filepath"] = "/root/picture/photo_*.jpg";
                                    result["message"]  = "照片已保存到 /root/picture/ 目录";
                                }

                                LOG_INFO(LOG_TAG, "拍照完成: %s",
                                         result["message"].get<std::string>().c_str());
                                return result;
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    // ========================================================================
                    // 工具：开始录像
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.camera.start_record",
                            "Start recording video and save it to local storage. The video will be "
                            "saved as an H.264 file.\n"
                            "Args:\n"
                            "  `filename` (optional): The filename to save the video. If not "
                            "provided, a filename will be auto-generated.\n"
                            "  `duration_sec` (optional): Recording duration in seconds. If 0 or "
                            "not provided, recording will continue until manually stopped.\n"
                            "Return:\n"
                            "  A JSON object containing the recording status and file path.",
                            mcp::PropertyList(
                                {mcp::Property("filename", mcp::PropertyType::String,
                                               std::string("")),
                                 mcp::Property("duration_sec", mcp::PropertyType::Integer, 0, 0,
                                               3600)}),
                            [video_system](const mcp::PropertyList& props) -> mcp::ReturnValue
                            {
                                std::string filename     = props["filename"].value<std::string>();
                                int         duration_sec = props["duration_sec"].value<int>();
                                LOG_INFO(LOG_TAG, "开始录像，文件名: %s, 时长: %d秒",
                                         filename.empty() ? "(自动生成)" : filename.c_str(),
                                         duration_sec);

                                // 检查是否正在录像
                                if (video_system->isRecording())
                                {
                                    LOG_WARN(LOG_TAG, "正在录像中...");
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] = "正在录像中，请先停止当前录像";
                                    return result;
                                }

                                // 确保流已启动（录像需要流处理线程）
                                if (!video_system->isStreaming())
                                {
                                    LOG_INFO(LOG_TAG, "启动视频流以支持录像...");
                                    app::media::camera::VideoError stream_err =
                                        video_system->startStream();
                                    if (stream_err != app::media::camera::VideoError::NONE)
                                    {
                                        LOG_ERROR(LOG_TAG, "启动视频流失败，错误码: %d",
                                                  static_cast<int>(stream_err));
                                        mcp::json result;
                                        result["success"] = false;
                                        result["message"] =
                                            "启动视频流失败，错误码: " +
                                            std::to_string(static_cast<int>(stream_err));
                                        return result;
                                    }
                                    // 等待流稳定
                                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                }

                                // 设置主状态为RECORD（这样流处理线程才会处理录像帧）
                                app::media::camera::VideoMainState old_state =
                                    video_system->getMainState();
                                app::media::camera::VideoError state_err =
                                    video_system->setMainState(
                                        app::media::camera::VideoMainState::RECORD);
                                if (state_err != app::media::camera::VideoError::NONE)
                                {
                                    LOG_ERROR(LOG_TAG, "设置主状态失败，错误码: %d",
                                              static_cast<int>(state_err));
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] = "设置主状态失败";
                                    return result;
                                }

                                // 调用开始录像功能
                                app::media::camera::VideoError record_err =
                                    video_system->startRecord(filename, duration_sec);
                                if (record_err != app::media::camera::VideoError::NONE)
                                {
                                    // 恢复主状态
                                    video_system->setMainState(old_state);
                                    LOG_ERROR(LOG_TAG, "开始录像失败，错误码: %d",
                                              static_cast<int>(record_err));
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] =
                                        "开始录像失败，错误码: " +
                                        std::to_string(static_cast<int>(record_err));
                                    return result;
                                }

                                // 构建返回结果
                                mcp::json result;
                                result["success"]   = true;
                                result["recording"] = true;
                                if (!filename.empty())
                                {
                                    result["filename"] = filename;
                                    result["filepath"] = filename;
                                    result["message"]  = "录像已开始: " + filename;
                                }
                                else
                                {
                                    result["filename"] = "auto_generated";
                                    result["filepath"] = "/root/video/record_*.h264";
                                    result["message"] = "录像已开始（文件名自动生成）";
                                }
                                if (duration_sec > 0)
                                {
                                    result["duration_sec"] = duration_sec;
                                    result["message"]      = result["message"].get<std::string>() +
                                                        "，时长: " + std::to_string(duration_sec) +
                                                        "秒";
                                }
                                else
                                {
                                    result["duration_sec"] = 0;
                                    result["message"] =
                                        result["message"].get<std::string>() + "（手动停止）";
                                }

                                LOG_INFO(LOG_TAG, "录像已开始: %s",
                                         result["message"].get<std::string>().c_str());
                                return result;
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    // ========================================================================
                    // 工具：停止录像
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.camera.stop_record",
                            "Stop the current video recording. The recorded video will be saved to "
                            "local storage.",
                            mcp::PropertyList(),
                            [video_system](const mcp::PropertyList& props
                                           [[maybe_unused]]) -> mcp::ReturnValue
                            {
                                LOG_INFO(LOG_TAG, "停止录像");

                                // 检查是否正在录像
                                if (!video_system->isRecording())
                                {
                                    LOG_WARN(LOG_TAG, "当前没有正在进行的录像");
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] = "当前没有正在进行的录像";
                                    return result;
                                }

                                // 停止录像
                                app::media::camera::VideoError record_err =
                                    video_system->stopRecord();
                                if (record_err != app::media::camera::VideoError::NONE)
                                {
                                    LOG_ERROR(LOG_TAG, "停止录像失败，错误码: %d",
                                              static_cast<int>(record_err));
                                    mcp::json result;
                                    result["success"] = false;
                                    result["message"] =
                                        "停止录像失败，错误码: " +
                                        std::to_string(static_cast<int>(record_err));
                                    return result;
                                }

                                // 恢复主状态为NONE
                                video_system->setMainState(
                                    app::media::camera::VideoMainState::NONE);

                                mcp::json result;
                                result["success"]   = true;
                                result["recording"] = false;
                                result["message"]   = "录像已停止";

                                LOG_INFO(LOG_TAG, "录像已停止");
                                return result;
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    LOG_INFO(LOG_TAG, "已注册 %d 个视频工具", count);
                    return count;
                }

                // ============================================================================
                // 网络工具注册
                // ============================================================================

                int
                McpToolManager::registerNetworkTools(McpServer&                       mcp_server,
                                                     app::network::wifi::WifiManager* wifi_manager)
                {
                    int count = 0;

                    if (!wifi_manager)
                    {
                        LOG_WARN(LOG_TAG, "WiFi管理器指针为空，跳过网络工具注册");
                        return count;
                    }

                    // ========================================================================
                    // 工具：获取WiFi信号强度
                    // ========================================================================
                    {
                        auto err = mcp_server.add_tool(
                            "self.network.get_wifi_signal_strength",
                            "Obtain the connection status of Wi-Fi, including the Wi-Fi name and "
                            "signal strength",
                            mcp::PropertyList(),
                            [wifi_manager](const mcp::PropertyList& props
                                           [[maybe_unused]]) -> mcp::ReturnValue
                            {
                                // 获取连接信息（包含信号强度）
                                app::network::wifi::WifiConnectionInfo info;
                                bool connected = wifi_manager->getConnectionInfo(info);

                                mcp::json result;
                                result["connected"] = connected;

                                if (connected)
                                {
                                    // 已连接，返回信号强度和SSID
                                    int         signal_strength = wifi_manager->getSignalStrength();
                                    std::string ssid            = wifi_manager->getCurrentSSID();

                                    result["success"]         = true;
                                    result["signal_strength"] = signal_strength;
                                    result["ssid"]            = ssid;
                                    result["message"] =
                                        "WiFi信号强度: " + std::to_string(signal_strength) +
                                        "% (网络: " + ssid + ")";

                                    LOG_INFO(LOG_TAG, "WiFi信号强度查询: SSID=%s, 信号强度=%d%%",
                                             ssid.c_str(), signal_strength);
                                }
                                else
                                {
                                    // 未连接
                                    result["success"]         = false;
                                    result["signal_strength"] = 0;
                                    result["ssid"]            = "";
                                    result["message"] = "WiFi未连接，无法获取信号强度";

                                    LOG_INFO(LOG_TAG, "WiFi信号强度查询: 未连接");
                                }

                                return result;
                            });

                        if (err == mcp::McpError::NONE)
                        {
                            count++;
                        }
                    }

                    LOG_INFO(LOG_TAG, "已注册 %d 个网络工具", count);
                    return count;
                }

                // ============================================================================
                // Webrtc工具注册
                // ============================================================================
                
                // int McpToolManager::registerWebrtcTools(
                //     McpServer&                           mcp_server,
                //     app::media::audio::AudioSystem*      audio_system,
                //     app::protocol::webrtc::Signaling*    signaling,
                //     app::protocol::webrtc::WebRTCSystem* webrtc_system,
                //     app::chatbot::ChatbotSystem*         chatbot_system)
                // {
                //     int count = 0;

                //     if (!signaling || !webrtc_system)
                //     {
                //         LOG_WARN(LOG_TAG, "信令或WebRTC系统指针为空，跳过WebRTC工具注册");
                //         return count;
                //     }

                //     auto err = mcp_server.add_tool(
                //         "self.webrtc.start_pairing",
                //         "Switch Smart Glasses from the chatbot AI dialogue to WebRTC pairing mode. "
                //         "This tool stops the AI conversation audio pipeline and requests the "
                //         "signaling server to pair with a remote peer so that WebRTC media can flow.",
                //         mcp::PropertyList(),
                //         [audio_system, signaling, webrtc_system,
                //          chatbot_system](const mcp::PropertyList& props [[maybe_unused]])
                //             -> mcp::ReturnValue
                //         {
                //             mcp::json result;
                //             result["success"] = false;

                //             if (!signaling || !webrtc_system)
                //             {
                //                 result["message"] = "信令或WebRTC系统不可用";
                //                 return result;
                //             }

                //             // 断开AI服务器连接
                //             bool ws_disconnected = false;
                //             if (chatbot_system)
                //             {
                //                 chatbot_system->disconnectWebSocket();
                //                 ws_disconnected = true;
                //             }

                //             // 停止当前AI对话音频流
                //             bool ai_audio_stopped   = false;
                //             bool playback_stopped   = false;
                //             bool awaiting_webrtc_on = false;
                //             if (audio_system)
                //             {
                //                 auto stop_err = audio_system->stopAIMode();
                //                 if (stop_err != app::media::audio::AudioError::NONE &&
                //                     stop_err != app::media::audio::AudioError::NOT_INITIALIZED)
                //                 {
                //                     LOG_WARN(LOG_TAG, "停止AI模式失败，错误码: %d",
                //                              static_cast<int>(stop_err));
                //                     result["audio_stop_error"] = static_cast<int>(stop_err);
                //                 }
                //                 else
                //                 {
                //                     ai_audio_stopped = true;
                //                 }

                //                 if (audio_system->isStreamRunning(app::media::audio::StreamDirection::OUTPUT))
                //                 {
                //                     audio_system->stopStream(app::media::audio::StreamDirection::OUTPUT);
                //                     playback_stopped = true;
                //                 }

                //                 awaiting_webrtc_on = true;
                //             }
                //             else
                //             {
                //                 LOG_WARN(LOG_TAG, "音频系统为空，无法主动终止AI对话音频");
                //             }
                //             result["ws_disconnected"] = ws_disconnected;

                //             // 确认WebRTC系统已初始化
                //             if (!webrtc_system->isOpen())
                //             {
                //                 LOG_ERROR(LOG_TAG, "WebRTC系统未初始化");
                //                 result["message"] = "WebRTC系统未初始化";
                //                 return result;
                //             }

                //             // 确保信令已连接并加入房间
                //             if (!signaling->isConnected())
                //             {
                //                 LOG_INFO(LOG_TAG, "信令未连接，尝试重新连接...");
                //                 if (!signaling->connect())
                //                 {
                //                     result["message"] = "信令服务器连接失败";
                //                     return result;
                //                 }
                //             }

                //             auto status = signaling->getStatus();
                //             if (status == app::protocol::webrtc::SignalingStatus::DISCONNECTED)
                //             {
                //                 result["message"] = "信令服务器未连接";
                //                 return result;
                //             }

                //             if (status == app::protocol::webrtc::SignalingStatus::CONNECTED)
                //             {
                //                 LOG_INFO(LOG_TAG, "加入信令房间，等待配对...");
                //                 if (!signaling->joinRoom())
                //                 {
                //                     result["message"] = "加入房间失败";
                //                     return result;
                //                 }
                //                 status = signaling->getStatus();
                //             }

                //             // 获取房间信息
                //             if (!signaling->requestRoomInfo())
                //             {
                //                 LOG_WARN(LOG_TAG, "房间信息请求失败");
                //             }

                //             // 返回状态
                //             result["success"]          = true;
                //             result["message"]          = "已切换至WebRTC配对模式，请等待对端设备";
                //             result["signaling_status"] =
                //                 app::protocol::webrtc::Signaling::statusToString(
                //                     signaling->getStatus());
                //             result["webrtc_state"] =
                //                 static_cast<int>(webrtc_system->getState());
                //             result["ai_audio_stopped"] = ai_audio_stopped;
                //             result["playback_stopped"] = playback_stopped;
                //             result["awaiting_webrtc_audio"] = awaiting_webrtc_on;
                //             return result;
                //         });

                //     if (err == mcp::McpError::NONE)
                //     {
                //         count++;
                //     }

                //     LOG_INFO(LOG_TAG, "已注册 %d 个Webrtc工具", count);
                //     return count;
                // }

                // ============================================================================
                // 注册所有工具
                // ============================================================================

                int McpToolManager::registerAllTools(McpServer&                           mcp_server,
                                                     app::media::audio::AudioSystem*      audio_system,
                                                     app::media::camera::VideoSystem*     video_system,
                                                     app::network::wifi::WifiManager*     wifi_manager,
                                                     app::protocol::webrtc::Signaling*    signaling,
                                                     app::protocol::webrtc::WebRTCSystem* webrtc_system,
                                                     app::chatbot::ChatbotSystem*         chatbot_system)
                {
                    LOG_INFO(LOG_TAG, "========================================");
                    LOG_INFO(LOG_TAG, "  开始注册MCP工具...");
                    LOG_INFO(LOG_TAG, "========================================");

                    int total = 0;

                    total += registerSystemTools(mcp_server);
                    total += registerAudioTools(mcp_server, audio_system);
                    total += registerVideoTools(mcp_server, video_system);
                    total += registerNetworkTools(mcp_server, wifi_manager);
                    // total += registerWebrtcTools(mcp_server, audio_system, signaling, webrtc_system, chatbot_system);

                    LOG_INFO(LOG_TAG, "========================================");
                    if (total == 0)
                    {
                        LOG_INFO(LOG_TAG, "  暂无可用工具（等待实现）");
                    }
                    else
                    {
                        LOG_INFO(LOG_TAG, "  工具注册总数: %d", total);
                    }
                    LOG_INFO(LOG_TAG, "========================================");

                    return total;
                }

            } // namespace mcp_tool
        }     // namespace mcp
    }         // namespace chatbot
} // namespace app
