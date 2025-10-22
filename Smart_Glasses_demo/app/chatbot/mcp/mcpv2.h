/**
 * @file mcpv2.h
 * @brief MCP协议服务器V2 - 现代C++重写版本
 * @details 特性：
 *          - RAII资源管理（智能指针）
 *          - 哈希表O(1)工具查找
 *          - 线程安全的工具管理
 *          - 异常安全的工具调用
 *          - 统一日志系统
 *          - 完整统计监控
 *          - 工具执行时间追踪
 * 
 * @author Smart_Glasses Team
 * @date 2025-01-11
 */

#ifndef MCPV2_H
#define MCPV2_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <variant>
#include <optional>
#include <memory>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace app {
namespace chatbot {
namespace mcp {

// ============================================================================
// 前向声明
// ============================================================================
class McpServerV2;
class McpToolV2;
class PropertyV2;
class PropertyListV2;

// ============================================================================
// 类型定义
// ============================================================================

using json = nlohmann::json;

/**
 * @brief 工具返回值类型
 */
using ReturnValue = std::variant<bool, int, double, std::string, json>;

/**
 * @brief 属性类型枚举
 */
enum class PropertyType {
    Boolean,
    Integer,
    Number,
    String
};

/**
 * @brief MCP错误类型
 */
enum class McpError {
    NONE = 0,
    PARSE_ERROR,            // JSON解析错误
    INVALID_REQUEST,        // 无效请求
    METHOD_NOT_FOUND,       // 方法未找到
    TOOL_NOT_FOUND,         // 工具未找到
    INVALID_PARAMS,         // 无效参数
    TOOL_EXECUTION_ERROR,   // 工具执行错误
    CALLBACK_EXCEPTION,     // 回调异常
    TIMEOUT,                // 超时
    UNKNOWN                 // 未知错误
};

// ============================================================================
// PropertyV2 - 工具参数定义
// ============================================================================

/**
 * @brief 工具参数属性V2
 */
class PropertyV2 {
public:
    // 必需参数构造
    PropertyV2(const std::string& name, PropertyType type);
    
    // 可选参数构造（带默认值）
    template<typename T>
    PropertyV2(const std::string& name, PropertyType type, const T& default_value);
    
    // 整数范围参数构造
    PropertyV2(const std::string& name, PropertyType type, int min_value, int max_value);
    
    // 带默认值的整数范围参数构造
    PropertyV2(const std::string& name, PropertyType type, int default_value, int min_value, int max_value);
    
    // Getter
    const std::string& name() const { return name_; }
    PropertyType type() const { return type_; }
    bool has_default_value() const { return has_default_value_; }
    bool has_range() const { return min_value_.has_value() && max_value_.has_value(); }
    int min_value() const { return min_value_.value_or(0); }
    int max_value() const { return max_value_.value_or(0); }
    
    // 获取值
    template<typename T>
    T value() const;
    
    // 设置值（带范围检查）
    template<typename T>
    void set_value(const T& value);
    
    // 转换为JSON Schema
    json to_json() const;

private:
    std::string name_;
    PropertyType type_;
    std::variant<bool, int, double, std::string> value_;
    bool has_default_value_;
    std::optional<int> min_value_;
    std::optional<int> max_value_;
};

// ============================================================================
// PropertyListV2 - 参数列表
// ============================================================================

/**
 * @brief 工具参数列表V2
 */
class PropertyListV2 {
public:
    PropertyListV2() = default;
    PropertyListV2(const std::vector<PropertyV2>& properties);
    PropertyListV2(std::initializer_list<PropertyV2> properties);
    
    void add(const PropertyV2& property);
    
    const PropertyV2& operator[](const std::string& name) const;
    PropertyV2& operator[](const std::string& name);
    
    auto begin() { return properties_.begin(); }
    auto end() { return properties_.end(); }
    auto begin() const { return properties_.begin(); }
    auto end() const { return properties_.end(); }
    
    std::vector<std::string> get_required() const;
    json to_json() const;

private:
    std::vector<PropertyV2> properties_;
};

// ============================================================================
// McpToolV2 - MCP工具
// ============================================================================

/**
 * @brief MCP工具V2
 */
class McpToolV2 {
public:
    using Callback = std::function<ReturnValue(const PropertyListV2&)>;
    
    McpToolV2(const std::string& name,
             const std::string& description,
             const PropertyListV2& properties,
             Callback callback);
    
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    const PropertyListV2& properties() const { return properties_; }
    
    // 转换为JSON（tools/list格式）
    json to_json() const;
    
    // 调用工具
    std::string call(const PropertyListV2& properties);
    
    // 获取执行统计
    uint64_t getCallCount() const { return call_count_.load(); }
    uint64_t getTotalExecutionTime() const { return total_exec_time_.load(); }
    uint64_t getAverageExecutionTime() const;

private:
    std::string name_;
    std::string description_;
    PropertyListV2 properties_;
    Callback callback_;
    
    // 统计信息
    std::atomic<uint64_t> call_count_{0};
    std::atomic<uint64_t> total_exec_time_{0};  // 微秒
};

// ============================================================================
// MCP配置
// ============================================================================

/**
 * @brief MCP服务器配置
 */
struct McpConfig {
    // 服务器信息
    std::string server_name = "Smart_Glasses";
    std::string server_version = "1.0.0";
    std::string protocol_version = "2024-11-05";
    
    // 功能开关
    bool enable_tool_timeout = true;        // 启用工具超时检查
    int tool_timeout_ms = 30000;            // 工具执行超时（30秒）
    bool enable_statistics = true;          // 启用统计信息
    bool enable_detailed_logging = false;   // 详细日志
};

// ============================================================================
// McpServerV2 - MCP服务器V2
// ============================================================================

/**
 * @brief MCP协议服务器V2
 * @details 现代C++重写的MCP服务器，特性：
 *          - 智能指针管理工具（无内存泄漏）
 *          - 哈希表O(1)工具查找
 *          - 线程安全的工具管理
 *          - 异常安全的工具调用
 *          - 完整统计和监控
 */
class McpServerV2 {
public:
    /**
     * @brief 构造函数
     * @param config MCP配置
     */
    explicit McpServerV2(const McpConfig& config = McpConfig());
    
    /**
     * @brief 析构函数（RAII自动清理所有资源）
     */
    ~McpServerV2();
    
    // ========================================================================
    // 工具管理
    // ========================================================================
    
    /**
     * @brief 添加工具（智能指针，所有权转移）
     * @param tool 工具智能指针
     * @return McpError::NONE 成功
     */
    McpError add_tool(std::unique_ptr<McpToolV2> tool);
    
    /**
     * @brief 添加工具（便捷方法）
     * @param name 工具名称（建议格式：self.module.function）
     * @param description 工具描述
     * @param properties 参数列表
     * @param callback 回调函数
     * @return McpError::NONE 成功
     */
    McpError add_tool(const std::string& name,
                     const std::string& description,
                     const PropertyListV2& properties,
                     std::function<ReturnValue(const PropertyListV2&)> callback);
    
    /**
     * @brief 移除工具
     * @param name 工具名称
     * @return true 成功移除
     */
    bool remove_tool(const std::string& name);
    
    /**
     * @brief 检查工具是否存在
     * @param name 工具名称
     * @return true 工具存在
     */
    bool has_tool(const std::string& name) const;
    
    /**
     * @brief 获取工具数量
     * @return 工具数量
     */
    size_t tool_count() const;
    
    /**
     * @brief 获取所有工具名称
     * @return 工具名称列表
     */
    std::vector<std::string> get_tool_names() const;
    
    /**
     * @brief 清空所有工具
     */
    void clear_tools();
    
    // ========================================================================
    // 消息处理
    // ========================================================================
    
    /**
     * @brief 处理MCP消息（JSON对象）
     * @param mcp_payload JSON-RPC 2.0消息
     * @return 响应消息（JSON字符串），空字符串表示无需响应
     */
    std::string handle_message(const json& mcp_payload);
    
    /**
     * @brief 处理MCP消息（JSON字符串）
     * @param mcp_payload_str JSON字符串
     * @return 响应消息（JSON字符串）
     */
    std::string handle_message(const std::string& mcp_payload_str);
    
    // ========================================================================
    // 统计信息
    // ========================================================================
    
    /**
     * @brief MCP服务器统计信息
     */
    struct Stats {
        std::atomic<uint64_t> initialize_requests{0};   // initialize请求数
        std::atomic<uint64_t> tools_list_requests{0};   // tools/list请求数
        std::atomic<uint64_t> tools_call_requests{0};   // tools/call请求数
        std::atomic<uint64_t> tools_call_success{0};    // 工具调用成功数
        std::atomic<uint64_t> tools_call_errors{0};     // 工具调用错误数
        std::atomic<uint64_t> parse_errors{0};          // 解析错误数
        std::atomic<uint64_t> method_not_found{0};      // 方法未找到数
        std::atomic<uint64_t> total_exec_time_us{0};    // 总执行时间
        std::atomic<uint64_t> avg_exec_time_us{0};      // 平均执行时间
    };
    
    /**
     * @brief 获取统计信息
     */
    void getStats(Stats& out_stats) const;
    
    /**
     * @brief 重置统计信息
     */
    void resetStats();
    
    /**
     * @brief 输出统计日志
     */
    void logStats() const;
    
    /**
     * @brief 获取工具使用统计
     * @return 工具名称 → 调用次数映射
     */
    std::map<std::string, uint64_t> getToolUsageStats() const;
    
    // 禁用拷贝和赋值
    McpServerV2(const McpServerV2&) = delete;
    McpServerV2& operator=(const McpServerV2&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;  // ✅ 智能指针管理
};

} // namespace mcp
} // namespace chatbot
} // namespace app

// ============================================================================
// 模板实现
// ============================================================================

namespace app {
namespace chatbot {
namespace mcp {

template<typename T>
PropertyV2::PropertyV2(const std::string& name, PropertyType type, const T& default_value)
    : name_(name), type_(type), has_default_value_(true) {
    value_ = default_value;
}

template<typename T>
T PropertyV2::value() const {
    return std::get<T>(value_);
}

template<typename T>
void PropertyV2::set_value(const T& value) {
    if constexpr (std::is_same_v<T, int>) {
        if (min_value_.has_value() && value < min_value_.value()) {
            throw std::invalid_argument("Value is below minimum allowed: " + 
                                       std::to_string(min_value_.value()));
        }
        if (max_value_.has_value() && value > max_value_.value()) {
            throw std::invalid_argument("Value exceeds maximum allowed: " + 
                                       std::to_string(max_value_.value()));
        }
    }
    value_ = value;
}

} // namespace mcp
} // namespace chatbot
} // namespace app

#endif // MCPV2_H

