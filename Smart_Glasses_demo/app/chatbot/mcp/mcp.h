// #ifndef MCP_H
// #define MCP_H

// #include <string>
// #include <vector>
// #include <map>
// #include <functional>
// #include <variant>
// #include <optional>
// #include <memory>
// #include <nlohmann/json.hpp>

// namespace glasses {
// namespace chatbot {
// namespace mcp {

// // ============================================================================
// // 类型定义
// // ============================================================================

// using json = nlohmann::json;

// /**
//  * @brief 工具返回值类型
//  */
// using ReturnValue = std::variant<bool, int, double, std::string, json>;

// /**
//  * @brief 属性类型枚举
//  */
// enum class PropertyType {
//     Boolean,
//     Integer,
//     Number,
//     String
// };

// // ============================================================================
// // Property - 工具参数定义
// // ============================================================================

// /**
//  * @brief 工具参数属性
//  */
// class Property {
// public:
//     // 必需参数构造函数
//     Property(const std::string& name, PropertyType type);
    
//     // 可选参数构造函数（带默认值）
//     template<typename T>
//     Property(const std::string& name, PropertyType type, const T& default_value);
    
//     // 整数范围参数构造函数
//     Property(const std::string& name, PropertyType type, int min_value, int max_value);
    
//     // 带默认值的整数范围参数构造函数
//     Property(const std::string& name, PropertyType type, int default_value, int min_value, int max_value);

//     // Getter
//     const std::string& name() const { return name_; }
//     PropertyType type() const { return type_; }
//     bool has_default_value() const { return has_default_value_; }
//     bool has_range() const { return min_value_.has_value() && max_value_.has_value(); }
//     int min_value() const { return min_value_.value_or(0); }
//     int max_value() const { return max_value_.value_or(0); }

//     // 获取值
//     template<typename T>
//     T value() const;

//     // 设置值（带范围检查）
//     template<typename T>
//     void set_value(const T& value);

//     // 转换为JSON Schema
//     json to_json() const;

// private:
//     std::string name_;
//     PropertyType type_;
//     std::variant<bool, int, double, std::string> value_;
//     bool has_default_value_;
//     std::optional<int> min_value_;
//     std::optional<int> max_value_;
// };

// // ============================================================================
// // PropertyList - 参数列表
// // ============================================================================

// /**
//  * @brief 工具参数列表
//  */
// class PropertyList {
// public:
//     PropertyList() = default;
//     PropertyList(const std::vector<Property>& properties);

//     void add(const Property& property);
    
//     const Property& operator[](const std::string& name) const;
//     Property& operator[](const std::string& name);
    
//     auto begin() { return properties_.begin(); }
//     auto end() { return properties_.end(); }
//     auto begin() const { return properties_.begin(); }
//     auto end() const { return properties_.end(); }

//     std::vector<std::string> get_required() const;
//     json to_json() const;

// private:
//     std::vector<Property> properties_;
// };

// // ============================================================================
// // McpTool - MCP工具
// // ============================================================================

// /**
//  * @brief MCP工具定义
//  */
// class McpTool {
// public:
//     using Callback = std::function<ReturnValue(const PropertyList&)>;

//     McpTool(const std::string& name,
//             const std::string& description,
//             const PropertyList& properties,
//             Callback callback);

//     const std::string& name() const { return name_; }
//     const std::string& description() const { return description_; }
//     const PropertyList& properties() const { return properties_; }

//     // 转换为JSON（tools/list格式）
//     json to_json() const;

//     // 调用工具
//     std::string call(const PropertyList& properties);

// private:
//     std::string name_;
//     std::string description_;
//     PropertyList properties_;
//     Callback callback_;
// };

// // ============================================================================
// // McpServer - MCP服务器
// // ============================================================================

// /**
//  * @brief MCP协议服务器
//  * @details 处理initialize、tools/list、tools/call等MCP请求
//  */
// class McpServer {
// public:
//     McpServer();
//     ~McpServer();

//     // ========================================================================
//     // 工具管理
//     // ========================================================================

//     /**
//      * @brief 添加工具
//      * @param tool 工具对象（转移所有权）
//      */
//     void add_tool(McpTool* tool);

//     /**
//      * @brief 添加工具（便捷方法）
//      * @param name 工具名称（建议格式：self.module.function）
//      * @param description 工具描述
//      * @param properties 参数列表
//      * @param callback 回调函数
//      */
//     void add_tool(const std::string& name,
//                   const std::string& description,
//                   const PropertyList& properties,
//                   std::function<ReturnValue(const PropertyList&)> callback);

//     /**
//      * @brief 获取工具数量
//      */
//     size_t tool_count() const;

//     /**
//      * @brief 清空所有工具
//      */
//     void clear_tools();

//     // ========================================================================
//     // 消息处理
//     // ========================================================================

//     /**
//      * @brief 处理MCP消息（JSON对象）
//      * @return 响应消息（JSON字符串），如果无需响应则返回空字符串
//      */
//     std::string handle_message(const json& mcp_payload);

//     /**
//      * @brief 处理MCP消息（JSON字符串）
//      * @param mcp_payload_str JSON-RPC 2.0消息体字符串
//      * @return 响应消息（JSON字符串），如果无需响应则返回空字符串
//      */
//     std::string handle_message(const std::string& mcp_payload_str);

//     // 禁用拷贝和赋值
//     McpServer(const McpServer&) = delete;
//     McpServer& operator=(const McpServer&) = delete;

// private:
//     class Impl;
//     std::unique_ptr<Impl> pImpl;

//     // 内部处理函数
//     std::string handle_initialize(int id, const json& params);
//     std::string handle_tools_list(int id, const json& params);
//     std::string handle_tools_call(int id, const json& params);

//     // 响应生成
//     std::string reply_result(int id, const json& result);
//     std::string reply_error(int id, const std::string& message);
// };

// } // namespace mcp
// } // namespace chatbot
// } // namespace glasses

// // ============================================================================
// // 模板实现
// // ============================================================================

// namespace glasses {
// namespace chatbot {
// namespace mcp {

// template<typename T>
// Property::Property(const std::string& name, PropertyType type, const T& default_value)
//     : name_(name), type_(type), has_default_value_(true) {
//     value_ = default_value;
// }

// template<typename T>
// T Property::value() const {
//     return std::get<T>(value_);
// }

// template<typename T>
// void Property::set_value(const T& value) {
//     if constexpr (std::is_same_v<T, int>) {
//         if (min_value_.has_value() && value < min_value_.value()) {
//             throw std::invalid_argument("Value is below minimum allowed: " + std::to_string(min_value_.value()));
//         }
//         if (max_value_.has_value() && value > max_value_.value()) {
//             throw std::invalid_argument("Value exceeds maximum allowed: " + std::to_string(max_value_.value()));
//         }
//     }
//     value_ = value;
// }

// } // namespace mcp
// } // namespace chatbot
// } // namespace glasses

// #endif // MCP_H

