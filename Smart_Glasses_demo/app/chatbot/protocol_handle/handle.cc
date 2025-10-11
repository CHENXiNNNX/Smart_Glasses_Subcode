/**
 * @file handle.cc
 * @brief xiaozhi AI协议处理模块实现
 */

#include "handle.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

// 使用libdatachannel提供的nlohmann/json
using json = nlohmann::json;

namespace glasses {
namespace chatbot {
namespace protocol {

// ============================================================================
// 内部实现类（Pimpl惯用法）
// ============================================================================

class ProtocolHandler::Impl {
public:
    std::string session_id;
    
    // 回调函数
    HelloCallback hello_callback;
    STTCallback stt_callback;
    LLMCallback llm_callback;
    TTSCallback tts_callback;
    MCPCallback mcp_callback;
    ErrorCallback error_callback;

    Impl() : session_id("") {}
    
    ~Impl() {}
};

// ============================================================================
// ProtocolHandler 公共接口实现
// ============================================================================

ProtocolHandler::ProtocolHandler()
    : pimpl_(new Impl()) {
    std::cout << "[Protocol] Handler created" << std::endl;
}

ProtocolHandler::~ProtocolHandler() {
    if (pimpl_) {
        delete pimpl_;
        pimpl_ = nullptr;
    }
}

// ============================================================================
// 回调设置
// ============================================================================

void ProtocolHandler::setHelloCallback(HelloCallback callback) {
    pimpl_->hello_callback = callback;
}

void ProtocolHandler::setSTTCallback(STTCallback callback) {
    pimpl_->stt_callback = callback;
}

void ProtocolHandler::setLLMCallback(LLMCallback callback) {
    pimpl_->llm_callback = callback;
}

void ProtocolHandler::setTTSCallback(TTSCallback callback) {
    pimpl_->tts_callback = callback;
}

void ProtocolHandler::setMCPCallback(MCPCallback callback) {
    pimpl_->mcp_callback = callback;
}

void ProtocolHandler::setErrorCallback(ErrorCallback callback) {
    pimpl_->error_callback = callback;
}

// ============================================================================
// 消息解析（接收方向）
// ============================================================================

MessageType ProtocolHandler::parseMessage(const char* buffer, size_t size) {
    std::string json_str(buffer, size);
    return parseMessage(json_str);
}

MessageType ProtocolHandler::parseMessage(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        
        if (!j.contains("type")) {
            if (pimpl_->error_callback) {
                pimpl_->error_callback("Message missing 'type' field");
            }
            return MessageType::UNKNOWN;
        }

        std::string type_str = j["type"];
        MessageType type = stringToMessageType(type_str);

        // 提取session_id（如果有）
        if (j.contains("session_id")) {
            pimpl_->session_id = j["session_id"];
        }

        // 根据类型分发消息
        switch (type) {
            case MessageType::HELLO: {
                if (pimpl_->hello_callback) {
                    HelloMessage msg;
                    msg.session_id = j.value("session_id", "");
                    msg.version = j.value("version", 1);
                    msg.transport = j.value("transport", "websocket");
                    
                    if (j.contains("audio_params")) {
                        auto& ap = j["audio_params"];
                        msg.audio_params.format = ap.value("format", "opus");
                        msg.audio_params.sample_rate = ap.value("sample_rate", 48000);
                        msg.audio_params.channels = ap.value("channels", 1);
                        msg.audio_params.frame_duration = ap.value("frame_duration", 20);
                    }
                    
                    pimpl_->hello_callback(msg);
                }
                break;
            }

            case MessageType::STT: {
                if (pimpl_->stt_callback) {
                    STTMessage msg;
                    msg.session_id = j.value("session_id", "");
                    msg.text = j.value("text", "");
                    msg.is_final = j.value("is_final", true);
                    
                    pimpl_->stt_callback(msg);
                }
                break;
            }

            case MessageType::LLM: {
                if (pimpl_->llm_callback) {
                    LLMMessage msg;
                    msg.session_id = j.value("session_id", "");
                    msg.text = j.value("text", "");
                    msg.is_final = j.value("is_final", true);
                    
                    std::string emotion_str = j.value("emotion", "neutral");
                    msg.emotion = stringToEmotionType(emotion_str);
                    
                    pimpl_->llm_callback(msg);
                }
                break;
            }

            case MessageType::TTS: {
                if (pimpl_->tts_callback) {
                    TTSMessage msg;
                    msg.session_id = j.value("session_id", "");
                    msg.text = j.value("text", "");
                    
                    std::string state_str = j.value("state", "start");
                    if (state_str == "start") {
                        msg.state = TTSState::START;
                    } else if (state_str == "sentence_start") {
                        msg.state = TTSState::SENTENCE_START;
                    } else if (state_str == "stop") {
                        msg.state = TTSState::STOP;
                    } else {
                        msg.state = TTSState::START;
                    }
                    
                    pimpl_->tts_callback(msg);
                }
                break;
            }

            case MessageType::MCP: {
                if (pimpl_->mcp_callback) {
                    // MCP协议：提取payload并处理
                    if (j.contains("payload")) {
                        std::string mcp_payload = j["payload"].dump();
                        std::string response = pimpl_->mcp_callback(mcp_payload);
                        // 响应会由回调处理并发送
                        (void)response;
                    }
                }
                break;
            }

            case MessageType::ERROR: {
                if (pimpl_->error_callback) {
                    std::string error_msg = j.value("message", "Unknown error");
                    pimpl_->error_callback(error_msg);
                }
                break;
            }

            default:
                if (pimpl_->error_callback) {
                    pimpl_->error_callback("Unknown message type: " + type_str);
                }
                break;
        }

        return type;

    } catch (const json::parse_error& e) {
        if (pimpl_->error_callback) {
            pimpl_->error_callback(std::string("JSON parse error: ") + e.what());
        }
        return MessageType::ERROR;
    } catch (const std::exception& e) {
        if (pimpl_->error_callback) {
            pimpl_->error_callback(std::string("Exception: ") + e.what());
        }
        return MessageType::ERROR;
    }
}

// ============================================================================
// 消息生成（发送方向）
// ============================================================================

std::string ProtocolHandler::generateHelloMessage(int sample_rate, 
                                                  int channels, 
                                                  int frame_duration) {
    json j;
    j["type"] = "hello";
    j["version"] = 1;
    j["transport"] = "websocket";
    j["audio_params"]["format"] = "opus";
    j["audio_params"]["sample_rate"] = sample_rate;
    j["audio_params"]["channels"] = channels;
    j["audio_params"]["frame_duration"] = frame_duration;
    
    return j.dump();
}

std::string ProtocolHandler::generateListenMessage(ListenState state, ListenMode mode) {
    json j;
    j["session_id"] = pimpl_->session_id;
    j["type"] = "listen";
    j["state"] = (state == ListenState::START) ? "start" : "stop";
    
    switch (mode) {
        case ListenMode::AUTO:
            j["mode"] = "auto";
            break;
        case ListenMode::MANUAL:
            j["mode"] = "manual";
            break;
        case ListenMode::REALTIME:
            j["mode"] = "realtime";
            break;
    }
    
    return j.dump();
}

// ============================================================================
// 会话管理
// ============================================================================

void ProtocolHandler::setSessionId(const std::string& session_id) {
    pimpl_->session_id = session_id;
    std::cout << "[Protocol] Session ID set: " << session_id << std::endl;
}

std::string ProtocolHandler::getSessionId() const {
    return pimpl_->session_id;
}

// ============================================================================
// 工具函数
// ============================================================================

MessageType ProtocolHandler::stringToMessageType(const std::string& type_str) {
    if (type_str == "hello") return MessageType::HELLO;
    if (type_str == "listen") return MessageType::LISTEN;
    if (type_str == "stt") return MessageType::STT;
    if (type_str == "llm") return MessageType::LLM;
    if (type_str == "tts") return MessageType::TTS;
    if (type_str == "mcp") return MessageType::MCP;
    if (type_str == "error") return MessageType::ERROR;
    return MessageType::UNKNOWN;
}

std::string ProtocolHandler::messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::HELLO:   return "hello";
        case MessageType::LISTEN:  return "listen";
        case MessageType::STT:     return "stt";
        case MessageType::LLM:     return "llm";
        case MessageType::TTS:     return "tts";
        case MessageType::MCP:     return "mcp";
        case MessageType::ERROR:   return "error";
        default:                   return "unknown";
    }
}

EmotionType ProtocolHandler::stringToEmotionType(const std::string& emotion_str) {
    if (emotion_str == "neutral") return EmotionType::NEUTRAL;
    if (emotion_str == "happy") return EmotionType::HAPPY;
    if (emotion_str == "laughing") return EmotionType::LAUGHING;
    if (emotion_str == "funny") return EmotionType::FUNNY;
    if (emotion_str == "sad") return EmotionType::SAD;
    if (emotion_str == "angry") return EmotionType::ANGRY;
    if (emotion_str == "crying") return EmotionType::CRYING;
    if (emotion_str == "loving") return EmotionType::LOVING;
    if (emotion_str == "embarrassed") return EmotionType::EMBARRASSED;
    if (emotion_str == "surprised") return EmotionType::SURPRISED;
    if (emotion_str == "shocked") return EmotionType::SHOCKED;
    if (emotion_str == "thinking") return EmotionType::THINKING;
    if (emotion_str == "winking") return EmotionType::WINKING;
    if (emotion_str == "cool") return EmotionType::COOL;
    if (emotion_str == "relaxed") return EmotionType::RELAXED;
    if (emotion_str == "delicious") return EmotionType::DELICIOUS;
    if (emotion_str == "kissy") return EmotionType::KISSY;
    if (emotion_str == "confident") return EmotionType::CONFIDENT;
    if (emotion_str == "sleepy") return EmotionType::SLEEPY;
    if (emotion_str == "silly") return EmotionType::SILLY;
    if (emotion_str == "confused") return EmotionType::CONFUSED;
    return EmotionType::NEUTRAL;
}

std::string ProtocolHandler::emotionTypeToString(EmotionType emotion) {
    switch (emotion) {
        case EmotionType::NEUTRAL:     return "neutral";
        case EmotionType::HAPPY:       return "happy";
        case EmotionType::LAUGHING:    return "laughing";
        case EmotionType::FUNNY:       return "funny";
        case EmotionType::SAD:         return "sad";
        case EmotionType::ANGRY:       return "angry";
        case EmotionType::CRYING:      return "crying";
        case EmotionType::LOVING:      return "loving";
        case EmotionType::EMBARRASSED: return "embarrassed";
        case EmotionType::SURPRISED:   return "surprised";
        case EmotionType::SHOCKED:     return "shocked";
        case EmotionType::THINKING:    return "thinking";
        case EmotionType::WINKING:     return "winking";
        case EmotionType::COOL:        return "cool";
        case EmotionType::RELAXED:     return "relaxed";
        case EmotionType::DELICIOUS:   return "delicious";
        case EmotionType::KISSY:       return "kissy";
        case EmotionType::CONFIDENT:   return "confident";
        case EmotionType::SLEEPY:      return "sleepy";
        case EmotionType::SILLY:       return "silly";
        case EmotionType::CONFUSED:    return "confused";
        default:                       return "neutral";
    }
}

} // namespace protocol
} // namespace chatbot
} // namespace glasses

