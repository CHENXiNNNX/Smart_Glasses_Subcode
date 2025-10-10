/**
 * @file mcp.cc
 * @brief MCP (Model Context Protocol) 工具管理器实现
 * 
 * @author Smart Glasses Team
 * @date 2025-10-10
 */

#include "mcp.h"
#include <iostream>
#include <json/json.h>

namespace glasses {
namespace chatbot {

// 导入protocol命名空间的类型
using protocol::IoTDescriptor;
using protocol::IoTDeviceState;
using protocol::IoTMessage;
using protocol::IoTProperty;
using protocol::IoTMethod;
using protocol::IoTMethodParameter;

namespace mcp {

// ============================================================================
// 设备信息结构
// ============================================================================

struct DeviceInfo {
    IoTDescriptor descriptor;  // 设备描述符
    MethodHandler handler;     // 方法处理函数
    StateGetter getter;        // 状态获取函数
};

// ============================================================================
// MCPManager::Impl (Pimpl)
// ============================================================================

class MCPManager::Impl {
public:
    std::map<std::string, DeviceInfo> devices_;  // 设备映射表（key=设备名称）
};

// ============================================================================
// MCPManager 实现
// ============================================================================

MCPManager::MCPManager() : pImpl(new Impl()) {
    std::cout << "[MCP] Manager created" << std::endl;
}

MCPManager::~MCPManager() {
    std::cout << "[MCP] Manager destroyed" << std::endl;
}

// ========================================================================
// 设备注册
// ========================================================================

bool MCPManager::registerDevice(
    const IoTDescriptor& descriptor,
    MethodHandler handler,
    StateGetter getter
) {
    if (descriptor.name.empty()) {
        std::cerr << "[MCP] ✗ Device name is empty" << std::endl;
        return false;
    }

    if (pImpl->devices_.count(descriptor.name) > 0) {
        std::cerr << "[MCP] ✗ Device already registered: " << descriptor.name << std::endl;
        return false;
    }

    DeviceInfo info;
    info.descriptor = descriptor;
    info.handler = handler;
    info.getter = getter;

    pImpl->devices_[descriptor.name] = info;

    std::cout << "[MCP] ✓ Device registered: " << descriptor.name 
              << " (" << descriptor.methods.size() << " methods, "
              << descriptor.properties.size() << " properties)" << std::endl;

    return true;
}

bool MCPManager::unregisterDevice(const std::string& device_name) {
    auto it = pImpl->devices_.find(device_name);
    if (it == pImpl->devices_.end()) {
        std::cerr << "[MCP] ✗ Device not found: " << device_name << std::endl;
        return false;
    }

    pImpl->devices_.erase(it);
    std::cout << "[MCP] ✓ Device unregistered: " << device_name << std::endl;
    return true;
}

bool MCPManager::isDeviceRegistered(const std::string& device_name) const {
    return pImpl->devices_.count(device_name) > 0;
}

// ========================================================================
// 描述符生成
// ========================================================================

std::vector<IoTDescriptor> MCPManager::getAllDescriptors() const {
    std::vector<IoTDescriptor> descriptors;
    for (const auto& pair : pImpl->devices_) {
        descriptors.push_back(pair.second.descriptor);
    }
    return descriptors;
}

std::string MCPManager::generateDescriptorMessage(const std::string& session_id) const {
    Json::Value root;
    root["session_id"] = session_id;
    root["type"] = "iot";
    root["update"] = true;

    Json::Value descriptors(Json::arrayValue);

    for (const auto& pair : pImpl->devices_) {
        const auto& desc = pair.second.descriptor;
        Json::Value device;

        device["name"] = desc.name;
        device["description"] = desc.description;

        // 添加属性
        Json::Value properties(Json::objectValue);
        for (const auto& prop_pair : desc.properties) {
            const auto& prop = prop_pair.second;
            Json::Value prop_obj;
            prop_obj["description"] = prop.description;
            prop_obj["type"] = prop.type;
            properties[prop_pair.first] = prop_obj;
        }
        device["properties"] = properties;

        // 添加方法
        Json::Value methods(Json::objectValue);
        for (const auto& method_pair : desc.methods) {
            const auto& method = method_pair.second;
            Json::Value method_obj;
            method_obj["description"] = method.description;

            // 添加参数
            Json::Value params(Json::objectValue);
            for (const auto& param_pair : method.parameters) {
                const auto& param = param_pair.second;
                Json::Value param_obj;
                param_obj["description"] = param.description;
                param_obj["type"] = param.type;
                params[param_pair.first] = param_obj;
            }
            method_obj["parameters"] = params;

            methods[method_pair.first] = method_obj;
        }
        device["methods"] = methods;

        descriptors.append(device);
    }

    root["descriptors"] = descriptors;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";  // 紧凑格式
    return Json::writeString(writer, root);
}

// ========================================================================
// 状态管理
// ========================================================================

std::vector<IoTDeviceState> MCPManager::getAllStates() const {
    std::vector<IoTDeviceState> states;

    for (const auto& pair : pImpl->devices_) {
        const std::string& device_name = pair.first;
        const DeviceInfo& info = pair.second;

        if (info.getter) {
            IoTDeviceState state;
            state.name = device_name;
            state.state = info.getter(device_name);
            states.push_back(state);
        }
    }

    return states;
}

std::string MCPManager::generateStateMessage(const std::string& session_id) const {
    Json::Value root;
    root["session_id"] = session_id;
    root["type"] = "iot";
    root["update"] = true;

    Json::Value states(Json::arrayValue);

    for (const auto& pair : pImpl->devices_) {
        const std::string& device_name = pair.first;
        const DeviceInfo& info = pair.second;

        if (info.getter) {
            Json::Value device_state;
            device_state["name"] = device_name;

            Json::Value state_obj(Json::objectValue);
            auto state_map = info.getter(device_name);
            
            for (const auto& state_pair : state_map) {
                // 尝试解析为数字或布尔值
                const std::string& value = state_pair.second;
                if (value == "true") {
                    state_obj[state_pair.first] = true;
                } else if (value == "false") {
                    state_obj[state_pair.first] = false;
                } else {
                    // 尝试解析为数字
                    try {
                        size_t pos;
                        int int_value = std::stoi(value, &pos);
                        if (pos == value.length()) {
                            state_obj[state_pair.first] = int_value;
                        } else {
                            state_obj[state_pair.first] = value;
                        }
                    } catch (...) {
                        state_obj[state_pair.first] = value;
                    }
                }
            }

            device_state["state"] = state_obj;
            states.append(device_state);
        }
    }

    root["states"] = states;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";  // 紧凑格式
    return Json::writeString(writer, root);
}

bool MCPManager::getDeviceState(
    const std::string& device_name,
    std::map<std::string, std::string>& state
) const {
    auto it = pImpl->devices_.find(device_name);
    if (it == pImpl->devices_.end()) {
        return false;
    }

    if (!it->second.getter) {
        return false;
    }

    state = it->second.getter(device_name);
    return true;
}

// ========================================================================
// 方法调用
// ========================================================================

bool MCPManager::invokeMethod(
    const std::string& device_name,
    const std::string& method_name,
    const std::map<std::string, std::string>& parameters
) {
    auto it = pImpl->devices_.find(device_name);
    if (it == pImpl->devices_.end()) {
        std::cerr << "[MCP] ✗ Device not found: " << device_name << std::endl;
        return false;
    }

    // 检查方法是否存在
    const auto& descriptor = it->second.descriptor;
    if (descriptor.methods.count(method_name) == 0) {
        std::cerr << "[MCP] ✗ Method not found: " << method_name 
                  << " on device " << device_name << std::endl;
        return false;
    }

    // 调用处理函数
    if (!it->second.handler) {
        std::cerr << "[MCP] ✗ No handler for device: " << device_name << std::endl;
        return false;
    }

    std::cout << "[MCP] Invoking " << device_name << "." << method_name << "(";
    bool first = true;
    for (const auto& param : parameters) {
        if (!first) std::cout << ", ";
        std::cout << param.first << "=" << param.second;
        first = false;
    }
    std::cout << ")" << std::endl;

    bool result = it->second.handler(device_name, method_name, parameters);
    
    if (result) {
        std::cout << "[MCP] ✓ Method invoked successfully" << std::endl;
    } else {
        std::cerr << "[MCP] ✗ Method invocation failed" << std::endl;
    }

    return result;
}

bool MCPManager::handleIoTInvoke(const IoTMessage& msg) {
    if (msg.device_name.empty() || msg.method_name.empty()) {
        std::cerr << "[MCP] ✗ Invalid IoT invoke message" << std::endl;
        return false;
    }

    return invokeMethod(msg.device_name, msg.method_name, msg.parameters);
}

// ========================================================================
// 工具函数
// ========================================================================

size_t MCPManager::getDeviceCount() const {
    return pImpl->devices_.size();
}

void MCPManager::clear() {
    pImpl->devices_.clear();
    std::cout << "[MCP] All devices cleared" << std::endl;
}

// ============================================================================
// 辅助函数实现
// ============================================================================

IoTDescriptor createSimpleDescriptor(
    const std::string& name,
    const std::string& description
) {
    IoTDescriptor descriptor;
    descriptor.name = name;
    descriptor.description = description;
    return descriptor;
}

void addProperty(
    IoTDescriptor& descriptor,
    const std::string& prop_name,
    const std::string& prop_description,
    const std::string& prop_type
) {
    IoTProperty prop;
    prop.name = prop_name;
    prop.description = prop_description;
    prop.type = prop_type;
    descriptor.properties[prop_name] = prop;
}

void addMethod(
    IoTDescriptor& descriptor,
    const std::string& method_name,
    const std::string& method_description
) {
    IoTMethod method;
    method.name = method_name;
    method.description = method_description;
    descriptor.methods[method_name] = method;
}

void addMethodParameter(
    IoTDescriptor& descriptor,
    const std::string& method_name,
    const std::string& param_name,
    const std::string& param_description,
    const std::string& param_type
) {
    // 确保方法存在
    if (descriptor.methods.count(method_name) == 0) {
        std::cerr << "[MCP] ✗ Method not found: " << method_name << std::endl;
        return;
    }

    IoTMethodParameter param;
    param.name = param_name;
    param.description = param_description;
    param.type = param_type;

    descriptor.methods[method_name].parameters[param_name] = param;
}

} // namespace mcp
} // namespace chatbot
} // namespace glasses


