/**
 * @file mcp.h
 * @brief MCP (Model Context Protocol) 工具管理器
 * @details 管理xiaozhi AI的MCP设备能力注册、状态上报和方法调用
 * 
 * @author Smart Glasses Team
 * @date 2025-10-10
 */

#ifndef MCP_H
#define MCP_H

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>
#include "../protocol_handle/handle.h"

namespace glasses {
namespace chatbot {

// 导入protocol命名空间的类型
using protocol::IoTDescriptor;
using protocol::IoTDeviceState;
using protocol::IoTMessage;

namespace mcp {

// ============================================================================
// 类型定义
// ============================================================================

/**
 * @brief 方法调用处理函数
 * @param device_name 设备名称
 * @param method_name 方法名称
 * @param parameters 方法参数（键值对）
 * @return true=调用成功, false=调用失败
 */
using MethodHandler = std::function<bool(
    const std::string& device_name,
    const std::string& method_name,
    const std::map<std::string, std::string>& parameters
)>;

/**
 * @brief 设备状态获取函数
 * @param device_name 设备名称
 * @return 设备当前状态（键值对）
 */
using StateGetter = std::function<std::map<std::string, std::string>(
    const std::string& device_name
)>;

// ============================================================================
// MCP设备管理器
// ============================================================================

/**
 * @brief MCP工具管理器
 * @details 负责管理所有IoT设备的注册、状态和方法调用
 */
class MCPManager {
public:
    MCPManager();
    ~MCPManager();

    // ========================================================================
    // 设备注册
    // ========================================================================

    /**
     * @brief 注册IoT设备
     * @param descriptor 设备描述符（包含属性和方法定义）
     * @param handler 方法调用处理函数
     * @param getter 状态获取函数
     * @return true=注册成功, false=注册失败
     */
    bool registerDevice(
        const IoTDescriptor& descriptor,
        MethodHandler handler,
        StateGetter getter
    );

    /**
     * @brief 注销IoT设备
     * @param device_name 设备名称
     * @return true=注销成功, false=设备不存在
     */
    bool unregisterDevice(const std::string& device_name);

    /**
     * @brief 检查设备是否已注册
     * @param device_name 设备名称
     * @return true=已注册, false=未注册
     */
    bool isDeviceRegistered(const std::string& device_name) const;

    // ========================================================================
    // 描述符生成
    // ========================================================================

    /**
     * @brief 获取所有设备的描述符列表
     * @return 设备描述符列表
     */
    std::vector<IoTDescriptor> getAllDescriptors() const;

    /**
     * @brief 生成IoT描述符消息（JSON格式）
     * @param session_id 会话ID
     * @return JSON字符串
     */
    std::string generateDescriptorMessage(const std::string& session_id) const;

    // ========================================================================
    // 状态管理
    // ========================================================================

    /**
     * @brief 获取所有设备的当前状态
     * @return 设备状态列表
     */
    std::vector<IoTDeviceState> getAllStates() const;

    /**
     * @brief 生成IoT状态消息（JSON格式）
     * @param session_id 会话ID
     * @return JSON字符串
     */
    std::string generateStateMessage(const std::string& session_id) const;

    /**
     * @brief 获取单个设备的状态
     * @param device_name 设备名称
     * @param state 输出参数：设备状态
     * @return true=成功, false=设备不存在
     */
    bool getDeviceState(const std::string& device_name, 
                        std::map<std::string, std::string>& state) const;

    // ========================================================================
    // 方法调用
    // ========================================================================

    /**
     * @brief 调用设备方法
     * @param device_name 设备名称
     * @param method_name 方法名称
     * @param parameters 方法参数
     * @return true=调用成功, false=调用失败
     */
    bool invokeMethod(
        const std::string& device_name,
        const std::string& method_name,
        const std::map<std::string, std::string>& parameters
    );

    /**
     * @brief 处理IoT调用消息
     * @param msg IoT消息（来自协议层）
     * @return true=处理成功, false=处理失败
     */
    bool handleIoTInvoke(const IoTMessage& msg);

    // ========================================================================
    // 工具函数
    // ========================================================================

    /**
     * @brief 获取已注册设备数量
     * @return 设备数量
     */
    size_t getDeviceCount() const;

    /**
     * @brief 清空所有设备
     */
    void clear();

    // 禁用拷贝和赋值
    MCPManager(const MCPManager&) = delete;
    MCPManager& operator=(const MCPManager&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 创建简单的IoT设备描述符
 * @param name 设备名称
 * @param description 设备描述
 * @return IoT设备描述符
 */
IoTDescriptor createSimpleDescriptor(
    const std::string& name,
    const std::string& description
);

/**
 * @brief 为描述符添加属性
 * @param descriptor 设备描述符
 * @param prop_name 属性名称
 * @param prop_description 属性描述
 * @param prop_type 属性类型（number/string/boolean）
 */
void addProperty(
    IoTDescriptor& descriptor,
    const std::string& prop_name,
    const std::string& prop_description,
    const std::string& prop_type
);

/**
 * @brief 为描述符添加方法
 * @param descriptor 设备描述符
 * @param method_name 方法名称
 * @param method_description 方法描述
 */
void addMethod(
    IoTDescriptor& descriptor,
    const std::string& method_name,
    const std::string& method_description
);

/**
 * @brief 为方法添加参数
 * @param descriptor 设备描述符
 * @param method_name 方法名称
 * @param param_name 参数名称
 * @param param_description 参数描述
 * @param param_type 参数类型（number/string/boolean）
 */
void addMethodParameter(
    IoTDescriptor& descriptor,
    const std::string& method_name,
    const std::string& param_name,
    const std::string& param_description,
    const std::string& param_type
);

} // namespace mcp
} // namespace chatbot
} // namespace glasses

#endif // MCP_H


