/**
 * @file test_xiaozhi_full.cpp
 * @brief xiaozhi AI 完整测试程序 - 激活 + WebSocket连接
 * @details 
 *   1. 检查设备激活状态
 *   2. 未激活则显示激活码并等待激活
 *   3. 激活成功后连接WebSocket
 *   4. 发送Hello消息并注册IoT设备
 *   5. 接收并处理云端消息
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "app/protocol/websocket/websocket.h"
#include "app/chatbot/protocol_handle/handle.h"
#include "app/chatbot/uuid/uuid.h"
#include "app/tool/mac/mac.h"

using namespace glasses::protocol::websocket;
using namespace glasses::chatbot::protocol;
using namespace glasses::tool;
using json = nlohmann::json;

// ============================================================================
// 全局变量
// ============================================================================
static WebSocketClient* g_ws_client = nullptr;
static ProtocolHandler* g_protocol_handler = nullptr;
static std::string g_mac_address;
static std::string g_client_uuid;
static bool g_connected = false;
static bool g_exit_flag = false;

// ============================================================================
// HTTP激活相关函数
// ============================================================================

// CURL回调函数
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

/**
 * @brief 激活设备
 * @param mac MAC地址
 * @param uuid UUID
 * @param activation_code 输出激活码
 * @return 0-已激活, 1-需要激活(返回激活码), -1-失败
 */
int activateDevice(const std::string& mac, const std::string& uuid, std::string& activation_code) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    // 激活URL
    std::string url = "https://api.tenclass.net/xiaozhi/ota/";

    // POST数据
    json post_json;
    post_json["platform"] = "linux";
    post_json["version"] = "1.0.0";
    post_json["board"]["type"] = "smart_glasses";
    post_json["board"]["name"] = "smart_glasses_board";
    std::string post_data = post_json.dump();

    // HTTP Headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Device-Id: " + mac).c_str());
    headers = curl_slist_append(headers, "User-Agent: SmartGlasses/1.0");
    headers = curl_slist_append(headers, "Accept-Language: zh-CN");

    std::cout << "\n[HTTP] 激活请求:" << std::endl;
    std::cout << "  URL: " << url << std::endl;
    std::cout << "  Device-Id: " << mac << std::endl;
    std::cout << "  POST: " << post_data << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (!curl) {
        std::cerr << "[HTTP] ✗ CURL初始化失败" << std::endl;
        return -1;
    }

    // 设置CURL选项
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // 简化SSL验证
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // 执行请求
    res = curl_easy_perform(curl);

    int result = -1;
    if (res != CURLE_OK) {
        std::cerr << "[HTTP] ✗ 请求失败: " << curl_easy_strerror(res) << std::endl;
    } else {
        std::cout << "[HTTP] 响应: " << readBuffer << std::endl;

        try {
            json response = json::parse(readBuffer);
            
            if (response.contains("activation") && response["activation"].contains("code")) {
                // 未激活，返回激活码
                activation_code = response["activation"]["code"];
                std::cout << "\n[HTTP] ⚠ 设备未激活" << std::endl;
                result = 1;
            } else {
                // 已激活
                std::cout << "\n[HTTP] ✓ 设备已激活" << std::endl;
                result = 0;
            }
        } catch (const json::parse_error& e) {
            std::cerr << "[HTTP] ✗ JSON解析失败: " << e.what() << std::endl;
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return result;
}

// ============================================================================
// 协议回调函数
// ============================================================================

void onHelloMessage(const HelloMessage& msg) {
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   收到 Hello 消息                      ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    std::cout << "  Session ID: " << msg.session_id << std::endl;
    std::cout << "  Version: " << msg.version << std::endl;
    std::cout << "  Audio: " << msg.audio_params.format 
              << " " << msg.audio_params.sample_rate << "Hz "
              << msg.audio_params.channels << "ch "
              << msg.audio_params.frame_duration << "ms" << std::endl;

    // 保存session_id
    g_protocol_handler->setSessionId(msg.session_id);

    // 发送IoT设备描述符
    std::cout << "\n[IoT] 注册设备能力..." << std::endl;
    
    // 1. Speaker设备
    IoTDescriptor speaker;
    speaker.name = "Speaker";
    speaker.description = "扬声器";
    
    IoTProperty volume;
    volume.name = "volume";
    volume.description = "当前音量值";
    volume.type = "number";
    speaker.properties["volume"] = volume;
    
    IoTMethod set_volume;
    set_volume.name = "SetVolume";
    set_volume.description = "设置音量";
    IoTMethodParameter vol_param;
    vol_param.name = "volume";
    vol_param.description = "0到100之间的整数";
    vol_param.type = "number";
    set_volume.parameters["volume"] = vol_param;
    speaker.methods["SetVolume"] = set_volume;

    // 2. Display设备
    IoTDescriptor display;
    display.name = "Display";
    display.description = "显示屏";
    
    IoTProperty brightness;
    brightness.name = "brightness";
    brightness.description = "当前亮度百分比";
    brightness.type = "number";
    display.properties["brightness"] = brightness;
    
    IoTMethod set_brightness;
    set_brightness.name = "SetBrightness";
    set_brightness.description = "设置亮度";
    IoTMethodParameter bright_param;
    bright_param.name = "brightness";
    bright_param.description = "0到100之间的整数";
    bright_param.type = "number";
    set_brightness.parameters["brightness"] = bright_param;
    display.methods["SetBrightness"] = set_brightness;

    // 发送描述符
    std::vector<IoTDescriptor> descriptors = {speaker, display};
    std::string desc_msg = g_protocol_handler->generateIoTDescriptorMessage(descriptors);
    g_ws_client->sendText(desc_msg.c_str(), desc_msg.length());
    std::cout << "[IoT] ✓ 已发送设备描述符" << std::endl;

    // 发送设备状态
    IoTDeviceState speaker_state;
    speaker_state.name = "Speaker";
    speaker_state.state["volume"] = "80";
    
    IoTDeviceState display_state;
    display_state.name = "Display";
    display_state.state["brightness"] = "75";
    
    std::vector<IoTDeviceState> states = {speaker_state, display_state};
    std::string state_msg = g_protocol_handler->generateIoTStateMessage(states);
    g_ws_client->sendText(state_msg.c_str(), state_msg.length());
    std::cout << "[IoT] ✓ 已发送设备状态" << std::endl;

    // 发送Listen消息，开始监听
    std::string listen_msg = g_protocol_handler->generateListenMessage(
        ListenState::START, ListenMode::AUTO);
    g_ws_client->sendText(listen_msg.c_str(), listen_msg.length());
    std::cout << "[Listen] ✓ 已开始监听" << std::endl;
}

void onSTTMessage(const STTMessage& msg) {
    std::cout << "\n[STT] 你说: \"" << msg.text << "\"";
    if (msg.is_final) {
        std::cout << " (最终结果)" << std::endl;
    } else {
        std::cout << " (中间结果)" << std::endl;
    }
}

void onLLMMessage(const LLMMessage& msg) {
    std::cout << "\n[LLM] AI回复: \"" << msg.text << "\"" << std::endl;
    std::cout << "[LLM] 情感: " << ProtocolHandler::emotionTypeToString(msg.emotion) << std::endl;
}

void onTTSMessage(const TTSMessage& msg) {
    switch (msg.state) {
        case TTSState::START:
            std::cout << "\n[TTS] ▶ 开始播放" << std::endl;
            break;
        case TTSState::SENTENCE_START:
            std::cout << "\n[TTS] AI说: \"" << msg.text << "\"" << std::endl;
            break;
        case TTSState::STOP:
            std::cout << "[TTS] ■ 播放结束" << std::endl;
            // 重新开始监听
            std::string listen_msg = g_protocol_handler->generateListenMessage(
                ListenState::START, ListenMode::AUTO);
            g_ws_client->sendText(listen_msg.c_str(), listen_msg.length());
            break;
    }
}

void onIoTMessage(const IoTMessage& msg) {
    // 处理IoT方法调用
    if (!msg.device_name.empty() && !msg.method_name.empty()) {
        std::cout << "\n[IoT] 收到方法调用: " << msg.device_name 
                  << "." << msg.method_name << std::endl;
        
        // 模拟执行方法
        if (msg.device_name == "Speaker" && msg.method_name == "SetVolume") {
            int volume = std::stoi(msg.parameters.at("volume"));
            std::cout << "[IoT] 设置音量: " << volume << std::endl;
            
            // 返回执行结果
            std::string result = g_protocol_handler->generateIoTInvokeResultMessage(
                "Speaker", "SetVolume", true, "音量已设置为" + std::to_string(volume));
            g_ws_client->sendText(result.c_str(), result.length());
            
            // 更新状态
            IoTDeviceState speaker_state;
            speaker_state.name = "Speaker";
            speaker_state.state["volume"] = std::to_string(volume);
            std::vector<IoTDeviceState> states = {speaker_state};
            std::string state_msg = g_protocol_handler->generateIoTStateMessage(states);
            g_ws_client->sendText(state_msg.c_str(), state_msg.length());
        }
        else if (msg.device_name == "Display" && msg.method_name == "SetBrightness") {
            int brightness = std::stoi(msg.parameters.at("brightness"));
            std::cout << "[IoT] 设置亮度: " << brightness << std::endl;
            
            // 返回执行结果
            std::string result = g_protocol_handler->generateIoTInvokeResultMessage(
                "Display", "SetBrightness", true, "亮度已设置为" + std::to_string(brightness));
            g_ws_client->sendText(result.c_str(), result.length());
            
            // 更新状态
            IoTDeviceState display_state;
            display_state.name = "Display";
            display_state.state["brightness"] = std::to_string(brightness);
            std::vector<IoTDeviceState> states = {display_state};
            std::string state_msg = g_protocol_handler->generateIoTStateMessage(states);
            g_ws_client->sendText(state_msg.c_str(), state_msg.length());
        }
    }
}

void onErrorMessage(const std::string& error) {
    std::cerr << "\n[ERROR] " << error << std::endl;
}

// WebSocket消息回调
void onBinaryMessage(const char* buffer, size_t size, void* user_data) {
    std::cout << "[WS] 收到二进制消息: " << size << " 字节 (TTS音频)" << std::endl;
    // 这里应该播放TTS音频
}

void onTextMessage(const char* buffer, size_t size, void* user_data) {
    // 解析JSON消息
    g_protocol_handler->parseMessage(buffer, size);
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   xiaozhi AI 完整测试程序              ║" << std::endl;
    std::cout << "║   Smart Glasses Full Test             ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;

    // 1. 获取设备信息
    std::cout << "\n[步骤1] 获取设备信息..." << std::endl;
    g_mac_address = getWirelessMacAddress();
    g_client_uuid = generateUUID();

    if (g_mac_address.empty()) {
        std::cerr << "✗ 无法获取MAC地址，使用默认值" << std::endl;
        g_mac_address = "00:00:00:00:00:00";
    }

    std::cout << "  MAC地址: " << g_mac_address << std::endl;
    std::cout << "  UUID:    " << g_client_uuid << std::endl;

    // 2. 设备激活
    std::cout << "\n[步骤2] 检查设备激活状态..." << std::endl;
    std::string activation_code;
    
    int activation_status = activateDevice(g_mac_address, g_client_uuid, activation_code);
    
    if (activation_status == 1) {
        // 需要激活
        std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
        std::cout << "║   设备未激活，请访问以下网址激活       ║" << std::endl;
        std::cout << "╚════════════════════════════════════════╝" << std::endl;
        std::cout << "\n  激活网址: https://xiaozhi.me" << std::endl;
        std::cout << "  激活码:   " << activation_code << std::endl;
        std::cout << "\n请在网站上输入激活码，然后按Enter继续..." << std::endl;
        std::cout << "程序将每5秒自动检查激活状态..." << std::endl;

        // 轮询检查激活状态
        while (activation_status != 0) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::cout << "\n[激活] 检查激活状态..." << std::endl;
            activation_status = activateDevice(g_mac_address, g_client_uuid, activation_code);
        }
    } else if (activation_status == -1) {
        std::cerr << "\n✗ 激活检查失败，但程序将继续尝试连接..." << std::endl;
    }

    std::cout << "\n✓ 设备已激活，准备连接..." << std::endl;

    // 3. 创建协议处理器
    std::cout << "\n[步骤3] 初始化协议处理器..." << std::endl;
    g_protocol_handler = new ProtocolHandler();
    g_protocol_handler->setHelloCallback(onHelloMessage);
    g_protocol_handler->setSTTCallback(onSTTMessage);
    g_protocol_handler->setLLMCallback(onLLMMessage);
    g_protocol_handler->setTTSCallback(onTTSMessage);
    g_protocol_handler->setIoTCallback(onIoTMessage);
    g_protocol_handler->setErrorCallback(onErrorMessage);
    std::cout << "✓ 协议处理器初始化完成" << std::endl;

    // 4. 创建WebSocket客户端
    std::cout << "\n[步骤4] 创建WebSocket客户端..." << std::endl;
    g_ws_client = createXiaozhiClient(
        g_mac_address,
        g_client_uuid,
        onBinaryMessage,
        onTextMessage,
        nullptr
    );
    std::cout << "✓ WebSocket客户端创建完成" << std::endl;

    // 5. 连接到xiaozhi服务器
    std::cout << "\n[步骤5] 连接到xiaozhi服务器..." << std::endl;
    if (!g_ws_client->connect()) {
        std::cerr << "✗ 连接失败！" << std::endl;
        delete g_ws_client;
        delete g_protocol_handler;
        return 1;
    }

    // 等待连接建立
    std::cout << "等待连接..." << std::endl;
    for (int i = 0; i < 10; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_ws_client->isConnected()) {
            g_connected = true;
            std::cout << "✓ 连接成功！" << std::endl;
            break;
        }
    }

    if (!g_connected) {
        std::cerr << "✗ 连接超时！" << std::endl;
        delete g_ws_client;
        delete g_protocol_handler;
        return 1;
    }

    // 6. 保持连接并处理消息
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   连接成功！现在可以对话了             ║" << std::endl;
    std::cout << "║   按Ctrl+C退出程序                     ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    std::cout << "\n提示：对着设备说\"小智小智\"唤醒，然后说出你的问题" << std::endl;
    std::cout << "      例如：\"今天天气怎么样\"、\"讲个笑话\"等\n" << std::endl;

    // 主循环
    while (!g_exit_flag) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (!g_ws_client->isConnected()) {
            std::cerr << "\n✗ 连接断开！" << std::endl;
            break;
        }
    }

    // 清理
    std::cout << "\n清理资源..." << std::endl;
    g_ws_client->disconnect();
    delete g_ws_client;
    delete g_protocol_handler;

    std::cout << "\n程序退出" << std::endl;
    return 0;
}

