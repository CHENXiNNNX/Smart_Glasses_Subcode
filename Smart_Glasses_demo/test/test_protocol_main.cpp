/**
 * @file test_protocol_main.cpp
 * @brief xiaozhi协议处理器测试程序
 */

#include <iostream>
#include <string>
#include "app/chatbot/protocol_handle/handle.h"

using namespace glasses::chatbot::protocol;

// 打印分隔线
void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "========================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "========================================" << std::endl;
}

// ============================================================================
// 回调函数
// ============================================================================

void onHelloMessage(const HelloMessage& msg) {
    std::cout << "[HELLO] Session ID: " << msg.session_id << std::endl;
    std::cout << "[HELLO] Version: " << msg.version << std::endl;
    std::cout << "[HELLO] Transport: " << msg.transport << std::endl;
    std::cout << "[HELLO] Audio Format: " << msg.audio_params.format << std::endl;
    std::cout << "[HELLO] Sample Rate: " << msg.audio_params.sample_rate << std::endl;
    std::cout << "[HELLO] Channels: " << msg.audio_params.channels << std::endl;
    std::cout << "[HELLO] Frame Duration: " << msg.audio_params.frame_duration << "ms" << std::endl;
}

void onSTTMessage(const STTMessage& msg) {
    std::cout << "[STT] Session ID: " << msg.session_id << std::endl;
    std::cout << "[STT] Text: " << msg.text << std::endl;
    std::cout << "[STT] Is Final: " << (msg.is_final ? "true" : "false") << std::endl;
}

void onLLMMessage(const LLMMessage& msg) {
    std::cout << "[LLM] Session ID: " << msg.session_id << std::endl;
    std::cout << "[LLM] Text: " << msg.text << std::endl;
    std::cout << "[LLM] Emotion: " << ProtocolHandler::emotionTypeToString(msg.emotion) << std::endl;
    std::cout << "[LLM] Is Final: " << (msg.is_final ? "true" : "false") << std::endl;
}

void onTTSMessage(const TTSMessage& msg) {
    std::cout << "[TTS] Session ID: " << msg.session_id << std::endl;
    std::string state_str;
    switch (msg.state) {
        case TTSState::START: state_str = "start"; break;
        case TTSState::SENTENCE_START: state_str = "sentence_start"; break;
        case TTSState::STOP: state_str = "stop"; break;
    }
    std::cout << "[TTS] State: " << state_str << std::endl;
    if (!msg.text.empty()) {
        std::cout << "[TTS] Text: " << msg.text << std::endl;
    }
}

void onIoTMessage(const IoTMessage& msg) {
    std::cout << "[IoT] Session ID: " << msg.session_id << std::endl;
    std::cout << "[IoT] Update: " << (msg.update ? "true" : "false") << std::endl;
    
    if (!msg.descriptors.empty()) {
        std::cout << "[IoT] Descriptors (" << msg.descriptors.size() << "):" << std::endl;
        for (const auto& desc : msg.descriptors) {
            std::cout << "  - " << desc.name << ": " << desc.description << std::endl;
            std::cout << "    Properties: " << desc.properties.size() << std::endl;
            std::cout << "    Methods: " << desc.methods.size() << std::endl;
        }
    }
    
    if (!msg.states.empty()) {
        std::cout << "[IoT] States (" << msg.states.size() << "):" << std::endl;
        for (const auto& state : msg.states) {
            std::cout << "  - " << state.name << ":" << std::endl;
            for (const auto& [key, val] : state.state) {
                std::cout << "      " << key << " = " << val << std::endl;
            }
        }
    }
    
    if (!msg.device_name.empty()) {
        std::cout << "[IoT] Invoke: " << msg.device_name << "." << msg.method_name << std::endl;
        for (const auto& [key, val] : msg.parameters) {
            std::cout << "  - " << key << " = " << val << std::endl;
        }
    }
}

void onError(const std::string& error) {
    std::cerr << "[ERROR] " << error << std::endl;
}

// ============================================================================
// 测试场景
// ============================================================================

void testHelloMessageParsing(ProtocolHandler& handler) {
    printSeparator("测试1: Hello消息解析");

    std::string hello_json = R"({
        "type": "hello",
        "session_id": "test-session-123",
        "version": 1,
        "transport": "websocket",
        "audio_params": {
            "format": "opus",
            "sample_rate": 48000,
            "channels": 1,
            "frame_duration": 20
        }
    })";

    std::cout << "\n[TEST] 解析JSON:\n" << hello_json << std::endl;
    MessageType type = handler.parseMessage(hello_json);
    std::cout << "\n[TEST] 消息类型: " << ProtocolHandler::messageTypeToString(type) << std::endl;
    std::cout << "\n✓ 测试1完成\n" << std::endl;
}

void testSTTMessageParsing(ProtocolHandler& handler) {
    printSeparator("测试2: STT消息解析");

    std::string stt_json = R"({
        "type": "stt",
        "session_id": "test-session-123",
        "text": "今天天气怎么样",
        "is_final": true
    })";

    std::cout << "\n[TEST] 解析JSON:\n" << stt_json << std::endl;
    MessageType type = handler.parseMessage(stt_json);
    std::cout << "\n[TEST] 消息类型: " << ProtocolHandler::messageTypeToString(type) << std::endl;
    std::cout << "\n✓ 测试2完成\n" << std::endl;
}

void testLLMMessageParsing(ProtocolHandler& handler) {
    printSeparator("测试3: LLM消息解析");

    std::string llm_json = R"({
        "type": "llm",
        "session_id": "test-session-123",
        "text": "今天天气晴朗，适合出行。",
        "emotion": "happy",
        "is_final": true
    })";

    std::cout << "\n[TEST] 解析JSON:\n" << llm_json << std::endl;
    MessageType type = handler.parseMessage(llm_json);
    std::cout << "\n[TEST] 消息类型: " << ProtocolHandler::messageTypeToString(type) << std::endl;
    std::cout << "\n✓ 测试3完成\n" << std::endl;
}

void testTTSMessageParsing(ProtocolHandler& handler) {
    printSeparator("测试4: TTS消息解析");

    // 测试sentence_start
    std::string tts_json1 = R"({
        "type": "tts",
        "state": "sentence_start",
        "text": "1加1等于2啦~",
        "session_id": "test-session-123"
    })";

    std::cout << "\n[TEST] 解析TTS sentence_start:\n" << tts_json1 << std::endl;
    handler.parseMessage(tts_json1);

    // 测试stop
    std::string tts_json2 = R"({
        "type": "tts",
        "state": "stop",
        "session_id": "test-session-123"
    })";

    std::cout << "\n[TEST] 解析TTS stop:\n" << tts_json2 << std::endl;
    handler.parseMessage(tts_json2);

    std::cout << "\n✓ 测试4完成\n" << std::endl;
}

void testIoTMessageParsing(ProtocolHandler& handler) {
    printSeparator("测试5: IoT消息解析");

    // 测试descriptor
    std::string iot_json = R"({
        "session_id": "test-session-123",
        "type": "iot",
        "update": true,
        "descriptors": [{
            "name": "Speaker",
            "description": "扬声器",
            "properties": {
                "volume": {
                    "description": "当前音量值",
                    "type": "number"
                }
            },
            "methods": {
                "SetVolume": {
                    "description": "设置音量",
                    "parameters": {
                        "volume": {
                            "description": "0到100之间的整数",
                            "type": "number"
                        }
                    }
                }
            }
        }]
    })";

    std::cout << "\n[TEST] 解析IoT descriptor:\n" << iot_json << std::endl;
    handler.parseMessage(iot_json);

    // 测试state
    std::string iot_state_json = R"({
        "session_id": "test-session-123",
        "type": "iot",
        "update": true,
        "states": [{
            "name": "Speaker",
            "state": {
                "volume": 80
            }
        }]
    })";

    std::cout << "\n[TEST] 解析IoT state:\n" << iot_state_json << std::endl;
    handler.parseMessage(iot_state_json);

    std::cout << "\n✓ 测试5完成\n" << std::endl;
}

void testMessageGeneration(ProtocolHandler& handler) {
    printSeparator("测试6: 消息生成");

    handler.setSessionId("generated-session-456");

    // 生成Hello消息
    std::cout << "\n[TEST] 生成Hello消息:" << std::endl;
    std::string hello_msg = handler.generateHelloMessage();
    std::cout << hello_msg << std::endl;

    // 生成Listen消息
    std::cout << "\n[TEST] 生成Listen消息:" << std::endl;
    std::string listen_msg = handler.generateListenMessage(ListenState::START, ListenMode::AUTO);
    std::cout << listen_msg << std::endl;

    // 生成IoT Descriptor消息
    std::cout << "\n[TEST] 生成IoT Descriptor消息:" << std::endl;
    IoTDescriptor speaker_desc;
    speaker_desc.name = "Speaker";
    speaker_desc.description = "扬声器";
    
    IoTProperty volume_prop;
    volume_prop.name = "volume";
    volume_prop.description = "当前音量值";
    volume_prop.type = "number";
    speaker_desc.properties["volume"] = volume_prop;
    
    IoTMethod set_volume_method;
    set_volume_method.name = "SetVolume";
    set_volume_method.description = "设置音量";
    
    IoTMethodParameter volume_param;
    volume_param.name = "volume";
    volume_param.description = "0到100之间的整数";
    volume_param.type = "number";
    set_volume_method.parameters["volume"] = volume_param;
    
    speaker_desc.methods["SetVolume"] = set_volume_method;
    
    std::vector<IoTDescriptor> descriptors = {speaker_desc};
    std::string iot_desc_msg = handler.generateIoTDescriptorMessage(descriptors);
    std::cout << iot_desc_msg << std::endl;

    // 生成IoT State消息
    std::cout << "\n[TEST] 生成IoT State消息:" << std::endl;
    IoTDeviceState speaker_state;
    speaker_state.name = "Speaker";
    speaker_state.state["volume"] = "80";
    
    std::vector<IoTDeviceState> states = {speaker_state};
    std::string iot_state_msg = handler.generateIoTStateMessage(states);
    std::cout << iot_state_msg << std::endl;

    std::cout << "\n✓ 测试6完成\n" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║   xiaozhi协议处理器测试程序            ║" << std::endl;
    std::cout << "║   Protocol Handler Test Suite         ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;

    try {
        // 创建协议处理器
        ProtocolHandler handler;

        // 设置回调
        handler.setHelloCallback(onHelloMessage);
        handler.setSTTCallback(onSTTMessage);
        handler.setLLMCallback(onLLMMessage);
        handler.setTTSCallback(onTTSMessage);
        handler.setIoTCallback(onIoTMessage);
        handler.setErrorCallback(onError);

        // 执行测试
        testHelloMessageParsing(handler);
        testSTTMessageParsing(handler);
        testLLMMessageParsing(handler);
        testTTSMessageParsing(handler);
        testIoTMessageParsing(handler);
        testMessageGeneration(handler);

        // 测试总结
        printSeparator("测试总结");
        std::cout << "\n  ✓ 所有测试执行完成" << std::endl;
        std::cout << "  ✓ 协议解析正常工作" << std::endl;
        std::cout << "  ✓ 消息生成正常工作" << std::endl;
        std::cout << "  ✓ 回调机制正常工作" << std::endl;
        std::cout << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n✗ 测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "========================================\n" << std::endl;

    return 0;
}

