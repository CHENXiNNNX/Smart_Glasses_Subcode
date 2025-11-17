/**
 * @file mcp.hpp
 * @brief MCP协议服务器实现
 */

#ifndef MCP_HPP
#define MCP_HPP

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#else
#include "../../../third_party/libdatachannel/deps/json/single_include/nlohmann/json.hpp"
#endif

#include <map>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <variant>
#include <optional>
#include <utility>

namespace app
{
    namespace chatbot
    {
        namespace mcp
        {

            // ============================================================================
            // 前向声明
            // ============================================================================
            class McpServer;
            class McpTool;
            class Property;
            class PropertyList;

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
            enum class PropertyType
            {
                Boolean,
                Integer,
                Number,
                String
            };

            /**
             * @brief MCP错误类型
             */
            enum class McpError
            {
                NONE = 0,
                PARSE_ERROR,          // JSON解析错误
                INVALID_REQUEST,      // 无效请求
                METHOD_NOT_FOUND,     // 方法未找到
                TOOL_NOT_FOUND,       // 工具未找到
                INVALID_PARAMS,       // 无效参数
                TOOL_EXECUTION_ERROR, // 工具执行错误
                CALLBACK_EXCEPTION,   // 回调异常
                TIMEOUT,              // 超时
                UNKNOWN               // 未知错误
            };

            // ============================================================================
            // Property - 工具参数定义
            // ============================================================================

            /**
             * @brief 工具参数属性
             */
            class Property
            {
            public:
                // 必需参数构造
                Property(std::string name, PropertyType type);

                // 可选参数构造（带默认值）
                template <typename T>
                Property(std::string name, PropertyType type, const T& default_value);

                // 整数范围参数构造
                Property(std::string name, PropertyType type, int min_value, int max_value);

                // 带默认值的整数范围参数构造
                Property(std::string name, PropertyType type, int default_value, int min_value,
                         int max_value);

                // Getter
                const std::string& name() const
                {
                    return name_;
                }
                PropertyType type() const
                {
                    return type_;
                }
                bool has_default_value() const
                {
                    return has_default_value_;
                }
                bool has_range() const
                {
                    return min_value_.has_value() && max_value_.has_value();
                }
                int min_value() const
                {
                    return min_value_.value_or(0);
                }
                int max_value() const
                {
                    return max_value_.value_or(0);
                }

                // 获取值
                template <typename T> T value() const;

                // 设置值（带范围检查）
                template <typename T> void set_value(const T& value);

                // 转换为JSON Schema
                json to_json() const;

            private:
                std::string                                  name_;
                PropertyType                                 type_;
                std::variant<bool, int, double, std::string> value_;
                bool                                         has_default_value_;
                std::optional<int>                           min_value_;
                std::optional<int>                           max_value_;
            };

            // ============================================================================
            // PropertyList - 参数列表
            // ============================================================================

            /**
             * @brief 工具参数列表
             */
            class PropertyList
            {
            public:
                PropertyList() = default;
                PropertyList(std::vector<Property> properties);
                PropertyList(std::initializer_list<Property> properties);

                void add(const Property& property);

                const Property& operator[](const std::string& name) const;
                Property&       operator[](const std::string& name);

                auto begin()
                {
                    return properties_.begin();
                }
                auto end()
                {
                    return properties_.end();
                }
                auto begin() const
                {
                    return properties_.begin();
                }
                auto end() const
                {
                    return properties_.end();
                }

                std::vector<std::string> get_required() const;
                json                     to_json() const;

            private:
                std::vector<Property> properties_;
            };

            // ============================================================================
            // McpTool - MCP工具
            // ============================================================================

            /**
             * @brief MCP工具
             */
            class McpTool
            {
            public:
                using Callback = std::function<ReturnValue(const PropertyList&)>;

                McpTool(std::string name, std::string description, PropertyList properties,
                        Callback callback);

                const std::string& name() const
                {
                    return name_;
                }
                const std::string& description() const
                {
                    return description_;
                }
                const PropertyList& properties() const
                {
                    return properties_;
                }

                // 转换为JSON（tools/list格式）
                json to_json() const;

                // 调用工具
                std::string call(const PropertyList& properties);

                // 获取执行统计
                uint64_t getCallCount() const
                {
                    return call_count_.load();
                }
                uint64_t getTotalExecutionTime() const
                {
                    return total_exec_time_.load();
                }
                uint64_t getAverageExecutionTime() const;

            private:
                std::string  name_;
                std::string  description_;
                PropertyList properties_;
                Callback     callback_;

                // 统计信息
                std::atomic<uint64_t> call_count_{0};
                std::atomic<uint64_t> total_exec_time_{0}; // 微秒
            };

            // ============================================================================
            // MCP配置
            // ============================================================================

            /**
             * @brief MCP服务器配置
             */
            struct McpConfig
            {
                // 服务器信息
                std::string server_name      = "Smart_Glasses";
                std::string server_version   = "1.0.0";
                std::string protocol_version = "2024-11-05";

                // 功能开关
                bool enable_tool_timeout     = true;  // 启用工具超时检查
                int  tool_timeout_ms         = 30000; // 工具执行超时（30秒）
                bool enable_statistics       = true;  // 启用统计信息
                bool enable_detailed_logging = false; // 详细日志
            };

            // ============================================================================
            // McpServer - MCP服务器
            // ============================================================================

            /**
             * @brief MCP协议服务器
             */
            class McpServer
            {
            public:
                /**
                 * @brief 构造函数
                 * @param config MCP配置
                 */
                explicit McpServer(McpConfig config = McpConfig());

                /**
                 * @brief 析构函数（RAII自动清理所有资源）
                 */
                ~McpServer();

                // ========================================================================
                // 工具管理
                // ========================================================================

                /**
                 * @brief 添加工具（智能指针，所有权转移）
                 * @param tool 工具智能指针
                 * @return McpError::NONE 成功
                 */
                McpError add_tool(std::unique_ptr<McpTool> tool);

                /**
                 * @brief 添加工具（便捷方法）
                 * @param name 工具名称（建议格式：self.module.function）
                 * @param description 工具描述
                 * @param properties 参数列表
                 * @param callback 回调函数
                 * @return McpError::NONE 成功
                 */
                McpError add_tool(std::string name, std::string description,
                                  PropertyList                                    properties,
                                  std::function<ReturnValue(const PropertyList&)> callback);

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
                struct Stats
                {
                    std::atomic<uint64_t> initialize_requests{0}; // initialize请求数
                    std::atomic<uint64_t> tools_list_requests{0}; // tools/list请求数
                    std::atomic<uint64_t> tools_call_requests{0}; // tools/call请求数
                    std::atomic<uint64_t> tools_call_success{0};  // 工具调用成功数
                    std::atomic<uint64_t> tools_call_errors{0};   // 工具调用错误数
                    std::atomic<uint64_t> parse_errors{0};        // 解析错误数
                    std::atomic<uint64_t> method_not_found{0};    // 方法未找到数
                    std::atomic<uint64_t> total_exec_time_us{0};  // 总执行时间
                    std::atomic<uint64_t> avg_exec_time_us{0};    // 平均执行时间
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
                McpServer(const McpServer&)            = delete;
                McpServer& operator=(const McpServer&) = delete;

            private:
                class Impl;
                std::unique_ptr<Impl> pImpl_;
            };

        } // namespace mcp
    }     // namespace chatbot
} // namespace app

// ============================================================================
// 模板实现
// ============================================================================

namespace app
{
    namespace chatbot
    {
        namespace mcp
        {

            template <typename T>
            Property::Property(std::string name, PropertyType type, const T& default_value)
                : name_(std::move(name)), type_(type), has_default_value_(true)
            {
                value_ = default_value;
            }

            template <typename T> T Property::value() const
            {
                return std::get<T>(value_);
            }

            template <typename T> void Property::set_value(const T& value)
            {
                if constexpr (std::is_same_v<T, int>)
                {
                    if (min_value_.has_value() && value < min_value_.value())
                    {
                        throw std::invalid_argument("Value is below minimum allowed: " +
                                                    std::to_string(min_value_.value()));
                    }
                    if (max_value_.has_value() && value > max_value_.value())
                    {
                        throw std::invalid_argument("Value exceeds maximum allowed: " +
                                                    std::to_string(max_value_.value()));
                    }
                }
                value_ = value;
            }

        } // namespace mcp
    }     // namespace chatbot
} // namespace app

#endif // MCP_HPP
