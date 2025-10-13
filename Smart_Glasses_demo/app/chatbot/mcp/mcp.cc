// #include "mcp.h"
// #include <iostream>
// #include <algorithm>
// #include <stdexcept>

// namespace glasses {
// namespace chatbot {
// namespace mcp {

// using json = nlohmann::json;

// // ============================================================================
// // Property 实现
// // ============================================================================

// Property::Property(const std::string& name, PropertyType type)
//     : name_(name), type_(type), has_default_value_(false) {}

// Property::Property(const std::string& name, PropertyType type, int min_value, int max_value)
//     : name_(name), type_(type), has_default_value_(false), 
//       min_value_(min_value), max_value_(max_value) {
//     if (type != PropertyType::Integer) {
//         throw std::invalid_argument("Range limits only apply to integer properties");
//     }
// }

// Property::Property(const std::string& name, PropertyType type, int default_value, int min_value, int max_value)
//     : name_(name), type_(type), has_default_value_(true),
//       min_value_(min_value), max_value_(max_value) {
//     if (type != PropertyType::Integer) {
//         throw std::invalid_argument("Range limits only apply to integer properties");
//     }
//     if (default_value < min_value || default_value > max_value) {
//         throw std::invalid_argument("Default value must be within the specified range");
//     }
//     value_ = default_value;
// }

// json Property::to_json() const {
//     json j;
    
//     switch (type_) {
//         case PropertyType::Boolean:
//             j["type"] = "boolean";
//             if (has_default_value_) {
//                 j["default"] = value<bool>();
//             }
//             break;
            
//         case PropertyType::Integer:
//             j["type"] = "integer";
//             if (has_default_value_) {
//                 j["default"] = value<int>();
//             }
//             if (min_value_.has_value()) {
//                 j["minimum"] = min_value_.value();
//             }
//             if (max_value_.has_value()) {
//                 j["maximum"] = max_value_.value();
//             }
//             break;
            
//         case PropertyType::Number:
//             j["type"] = "number";
//             if (has_default_value_) {
//                 j["default"] = value<double>();
//             }
//             break;
            
//         case PropertyType::String:
//             j["type"] = "string";
//             if (has_default_value_) {
//                 j["default"] = value<std::string>();
//             }
//             break;
//     }
    
//     return j;
// }

// // ============================================================================
// // PropertyList 实现
// // ============================================================================

// PropertyList::PropertyList(const std::vector<Property>& properties)
//     : properties_(properties) {}

// void PropertyList::add(const Property& property) {
//     properties_.push_back(property);
// }

// const Property& PropertyList::operator[](const std::string& name) const {
//     for (const auto& property : properties_) {
//         if (property.name() == name) {
//             return property;
//         }
//     }
//     throw std::runtime_error("Property not found: " + name);
// }

// Property& PropertyList::operator[](const std::string& name) {
//     for (auto& property : properties_) {
//         if (property.name() == name) {
//             return property;
//         }
//     }
//     throw std::runtime_error("Property not found: " + name);
// }

// std::vector<std::string> PropertyList::get_required() const {
//     std::vector<std::string> required;
//     for (const auto& property : properties_) {
//         if (!property.has_default_value()) {
//             required.push_back(property.name());
//         }
//     }
//     return required;
// }

// json PropertyList::to_json() const {
//     json j;
//     for (const auto& property : properties_) {
//         j[property.name()] = property.to_json();
//     }
//     return j;
// }

// // ============================================================================
// // McpTool 实现
// // ============================================================================

// McpTool::McpTool(const std::string& name,
//                  const std::string& description,
//                  const PropertyList& properties,
//                  Callback callback)
//     : name_(name), description_(description), properties_(properties), callback_(callback) {}

// json McpTool::to_json() const {
//     json j;
//     j["name"] = name_;
//     j["description"] = description_;
    
//     json input_schema;
//     input_schema["type"] = "object";
//     input_schema["properties"] = properties_.to_json();
    
//     auto required = properties_.get_required();
//     if (!required.empty()) {
//         input_schema["required"] = required;
//     }
    
//     j["inputSchema"] = input_schema;
    
//     return j;
// }

// std::string McpTool::call(const PropertyList& properties) {
//     try {
//         ReturnValue return_value = callback_(properties);
        
//         json result;
//         json content = json::array();
//         json text_item;
//         text_item["type"] = "text";
        
//         // 转换返回值为字符串
//         if (std::holds_alternative<std::string>(return_value)) {
//             text_item["text"] = std::get<std::string>(return_value);
//         } else if (std::holds_alternative<bool>(return_value)) {
//             text_item["text"] = std::get<bool>(return_value) ? "true" : "false";
//         } else if (std::holds_alternative<int>(return_value)) {
//             text_item["text"] = std::to_string(std::get<int>(return_value));
//         } else if (std::holds_alternative<double>(return_value)) {
//             text_item["text"] = std::to_string(std::get<double>(return_value));
//         } else if (std::holds_alternative<json>(return_value)) {
//             text_item["text"] = std::get<json>(return_value).dump();
//         }
        
//         content.push_back(text_item);
//         result["content"] = content;
//         result["isError"] = false;
        
//         return result.dump();
        
//     } catch (const std::exception& e) {
//         json result;
//         json content = json::array();
//         json text_item;
//         text_item["type"] = "text";
//         text_item["text"] = std::string("Error: ") + e.what();
//         content.push_back(text_item);
//         result["content"] = content;
//         result["isError"] = true;
        
//         return result.dump();
//     }
// }

// // ============================================================================
// // McpServer::Impl
// // ============================================================================

// class McpServer::Impl {
// public:
//     std::vector<McpTool*> tools_;
    
//     ~Impl() {
//         for (auto tool : tools_) {
//             delete tool;
//         }
//         tools_.clear();
//     }
// };

// // ============================================================================
// // McpServer 实现
// // ============================================================================

// McpServer::McpServer() : pImpl(new Impl()) {
//     std::cout << "[MCP] Server created" << std::endl;
// }

// McpServer::~McpServer() {
//     std::cout << "[MCP] Server destroyed" << std::endl;
// }

// void McpServer::add_tool(McpTool* tool) {
//     // 防止重复添加
//     if (std::find_if(pImpl->tools_.begin(), pImpl->tools_.end(),
//                      [tool](const McpTool* t) { return t->name() == tool->name(); })
//         != pImpl->tools_.end()) {
//         std::cerr << "[MCP] ✗ Tool already added: " << tool->name() << std::endl;
//         delete tool;
//         return;
//     }
    
//     std::cout << "[MCP] ✓ Tool added: " << tool->name() << std::endl;
//     pImpl->tools_.push_back(tool);
// }

// void McpServer::add_tool(const std::string& name,
//                          const std::string& description,
//                          const PropertyList& properties,
//                          std::function<ReturnValue(const PropertyList&)> callback) {
//     add_tool(new McpTool(name, description, properties, callback));
// }

// size_t McpServer::tool_count() const {
//     return pImpl->tools_.size();
// }

// void McpServer::clear_tools() {
//     for (auto tool : pImpl->tools_) {
//         delete tool;
//     }
//     pImpl->tools_.clear();
//     std::cout << "[MCP] All tools cleared" << std::endl;
// }

// // ========================================================================
// // 消息处理
// // ========================================================================

// std::string McpServer::handle_message(const std::string& mcp_payload_str) {
//     try {
//         json mcp_payload = json::parse(mcp_payload_str);
//         return handle_message(mcp_payload);
//     } catch (const json::parse_error& e) {
//         std::cerr << "[MCP] ✗ Failed to parse MCP message: " << e.what() << std::endl;
//         return "";
//     }
// }

// std::string McpServer::handle_message(const json& mcp_payload) {
//     try {
//         // 检查JSONRPC版本
//         if (!mcp_payload.contains("jsonrpc") || mcp_payload["jsonrpc"] != "2.0") {
//             std::cerr << "[MCP] ✗ Invalid JSONRPC version" << std::endl;
//             return "";
//         }
        
//         // 检查方法
//         if (!mcp_payload.contains("method") || !mcp_payload["method"].is_string()) {
//             std::cerr << "[MCP] ✗ Missing method" << std::endl;
//             return "";
//         }
        
//         std::string method = mcp_payload["method"];
        
//         // 忽略通知消息
//         if (method.find("notifications/") == 0) {
//             return "";
//         }
        
//         // 检查ID
//         if (!mcp_payload.contains("id") || !mcp_payload["id"].is_number_integer()) {
//             std::cerr << "[MCP] ✗ Invalid id for method: " << method << std::endl;
//             return "";
//         }
        
//         int id = mcp_payload["id"];
//         json params = mcp_payload.value("params", json::object());
        
//         // 路由到具体处理函数
//         if (method == "initialize") {
//             return handle_initialize(id, params);
//         } else if (method == "tools/list") {
//             return handle_tools_list(id, params);
//         } else if (method == "tools/call") {
//             return handle_tools_call(id, params);
//         } else {
//             std::cerr << "[MCP] ✗ Method not implemented: " << method << std::endl;
//             return reply_error(id, "Method not implemented: " + method);
//         }
        
//     } catch (const std::exception& e) {
//         std::cerr << "[MCP] ✗ Exception handling message: " << e.what() << std::endl;
//         return "";
//     }
// }

// // ========================================================================
// // 处理具体请求
// // ========================================================================

// std::string McpServer::handle_initialize(int id, const json& params) {
//     std::cout << "[MCP] → Initialize request" << std::endl;
    
//     // TODO: 处理客户端能力（如vision）
//     if (params.contains("capabilities")) {
//         // 可以在这里解析vision等能力
//     }
    
//     json result;
//     result["protocolVersion"] = "2024-11-05";
//     result["capabilities"] = json::object({{"tools", json::object()}});
//     result["serverInfo"] = json::object({
//         {"name", "Smart_Glasses"},
//         {"version", "1.0.0"}
//     });
    
//     std::cout << "[MCP] ✓ Initialize success" << std::endl;
//     return reply_result(id, result);
// }

// std::string McpServer::handle_tools_list(int id, const json& params) {
//     std::cout << "[MCP] → Tools list request" << std::endl;
    
//     std::string cursor = params.value("cursor", "");
    
//     json tools_array = json::array();
//     bool found_cursor = cursor.empty();
//     std::string next_cursor = "";
    
//     for (const auto& tool : pImpl->tools_) {
//         // 如果还没找到起始位置，继续搜索
//         if (!found_cursor) {
//             if (tool->name() == cursor) {
//                 found_cursor = true;
//             } else {
//                 continue;
//             }
//         }
        
//         tools_array.push_back(tool->to_json());
        
//         // TODO: 可以添加分页逻辑（检查大小限制）
//     }
    
//     json result;
//     result["tools"] = tools_array;
//     if (!next_cursor.empty()) {
//         result["nextCursor"] = next_cursor;
//     }
    
//     std::cout << "[MCP] ✓ Tools list: " << tools_array.size() << " tools" << std::endl;
//     return reply_result(id, result);
// }

// std::string McpServer::handle_tools_call(int id, const json& params) {
//     try {
//         // 检查工具名称
//         if (!params.contains("name") || !params["name"].is_string()) {
//             return reply_error(id, "Missing tool name");
//         }
        
//         std::string tool_name = params["name"];
//         json arguments = params.value("arguments", json::object());
        
//         std::cout << "[MCP] → Calling tool: " << tool_name << std::endl;
        
//         // 查找工具
//         auto tool_iter = std::find_if(pImpl->tools_.begin(), pImpl->tools_.end(),
//                                       [&tool_name](const McpTool* tool) {
//                                           return tool->name() == tool_name;
//                                       });
        
//         if (tool_iter == pImpl->tools_.end()) {
//             std::cerr << "[MCP] ✗ Tool not found: " << tool_name << std::endl;
//             return reply_error(id, "Tool not found: " + tool_name);
//         }
        
//         // 准备参数
//         PropertyList call_properties = (*tool_iter)->properties();
        
//         for (auto& property : call_properties) {
//             bool found = false;
            
//             if (arguments.contains(property.name())) {
//                 const auto& value = arguments[property.name()];
                
//                 switch (property.type()) {
//                     case PropertyType::Boolean:
//                         if (value.is_boolean()) {
//                             property.set_value<bool>(value.get<bool>());
//                             found = true;
//                         }
//                         break;
                        
//                     case PropertyType::Integer:
//                         if (value.is_number_integer()) {
//                             property.set_value<int>(value.get<int>());
//                             found = true;
//                         }
//                         break;
                        
//                     case PropertyType::Number:
//                         if (value.is_number()) {
//                             property.set_value<double>(value.get<double>());
//                             found = true;
//                         }
//                         break;
                        
//                     case PropertyType::String:
//                         if (value.is_string()) {
//                             property.set_value<std::string>(value.get<std::string>());
//                             found = true;
//                         }
//                         break;
//                 }
//             }
            
//             if (!property.has_default_value() && !found) {
//                 std::cerr << "[MCP] ✗ Missing required argument: " << property.name() << std::endl;
//                 return reply_error(id, "Missing required argument: " + property.name());
//             }
//         }
        
//         // 调用工具
//         std::string result_str = (*tool_iter)->call(call_properties);
//         json result = json::parse(result_str);
        
//         std::cout << "[MCP] ✓ Tool call success: " << tool_name << std::endl;
//         return reply_result(id, result);
        
//     } catch (const std::exception& e) {
//         std::cerr << "[MCP] ✗ Tool call error: " << e.what() << std::endl;
//         return reply_error(id, std::string("Tool call error: ") + e.what());
//     }
// }

// // ========================================================================
// // 响应生成
// // ========================================================================

// std::string McpServer::reply_result(int id, const json& result) {
//     json response;
//     response["jsonrpc"] = "2.0";
//     response["id"] = id;
//     response["result"] = result;
//     return response.dump();
// }

// std::string McpServer::reply_error(int id, const std::string& message) {
//     json response;
//     response["jsonrpc"] = "2.0";
//     response["id"] = id;
//     response["error"] = json::object({{"message", message}});
//     return response.dump();
// }

// } // namespace mcp
// } // namespace chatbot
// } // namespace glasses

