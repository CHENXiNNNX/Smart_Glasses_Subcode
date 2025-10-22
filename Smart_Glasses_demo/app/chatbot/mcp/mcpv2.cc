/**
 * @file mcpv2.cc
 * @brief MCP协议服务器V2实现
 */

#include "mcpv2.h"
#include "../../tool/log/log.h"
#include "../../../common/common.h"
#include <algorithm>
#include <stdexcept>

namespace app {
namespace chatbot {
namespace mcp {

using namespace tool::log;

// ============================================================================
// PropertyV2 实现
// ============================================================================

PropertyV2::PropertyV2(const std::string& name, PropertyType type)
    : name_(name), type_(type), has_default_value_(false) {}

PropertyV2::PropertyV2(const std::string& name, PropertyType type, int min_value, int max_value)
    : name_(name), type_(type), has_default_value_(false),
      min_value_(min_value), max_value_(max_value) {
    if (type != PropertyType::Integer) {
        throw std::invalid_argument("Range limits only apply to integer properties");
    }
}

PropertyV2::PropertyV2(const std::string& name, PropertyType type, 
                       int default_value, int min_value, int max_value)
    : name_(name), type_(type), has_default_value_(true),
      min_value_(min_value), max_value_(max_value) {
    if (type != PropertyType::Integer) {
        throw std::invalid_argument("Range limits only apply to integer properties");
    }
    if (default_value < min_value || default_value > max_value) {
        throw std::invalid_argument("Default value must be within the specified range");
    }
    value_ = default_value;
}

json PropertyV2::to_json() const {
    json j;
    
    switch (type_) {
        case PropertyType::Boolean:
            j["type"] = "boolean";
            if (has_default_value_) {
                j["default"] = value<bool>();
            }
            break;
            
        case PropertyType::Integer:
            j["type"] = "integer";
            if (has_default_value_) {
                j["default"] = value<int>();
            }
            if (min_value_.has_value()) {
                j["minimum"] = min_value_.value();
            }
            if (max_value_.has_value()) {
                j["maximum"] = max_value_.value();
            }
            break;
            
        case PropertyType::Number:
            j["type"] = "number";
            if (has_default_value_) {
                j["default"] = value<double>();
            }
            break;
            
        case PropertyType::String:
            j["type"] = "string";
            if (has_default_value_) {
                j["default"] = value<std::string>();
            }
            break;
    }
    
    return j;
}

// ============================================================================
// PropertyListV2 实现
// ============================================================================

PropertyListV2::PropertyListV2(const std::vector<PropertyV2>& properties)
    : properties_(properties) {}

PropertyListV2::PropertyListV2(std::initializer_list<PropertyV2> properties)
    : properties_(properties) {}

void PropertyListV2::add(const PropertyV2& property) {
    properties_.push_back(property);
}

const PropertyV2& PropertyListV2::operator[](const std::string& name) const {
    for (const auto& property : properties_) {
        if (property.name() == name) {
            return property;
        }
    }
    throw std::runtime_error("Property not found: " + name);
}

PropertyV2& PropertyListV2::operator[](const std::string& name) {
    for (auto& property : properties_) {
        if (property.name() == name) {
            return property;
        }
    }
    throw std::runtime_error("Property not found: " + name);
}

std::vector<std::string> PropertyListV2::get_required() const {
    std::vector<std::string> required;
    for (const auto& property : properties_) {
        if (!property.has_default_value()) {
            required.push_back(property.name());
        }
    }
    return required;
}

json PropertyListV2::to_json() const {
    json j;
    for (const auto& property : properties_) {
        j[property.name()] = property.to_json();
    }
    return j;
}

// ============================================================================
// McpToolV2 实现
// ============================================================================

McpToolV2::McpToolV2(const std::string& name,
                     const std::string& description,
                     const PropertyListV2& properties,
                     Callback callback)
    : name_(name), description_(description), 
      properties_(properties), callback_(callback) {}

json McpToolV2::to_json() const {
    json j;
    j["name"] = name_;
    j["description"] = description_;
    
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = properties_.to_json();
    
    auto required = properties_.get_required();
    if (!required.empty()) {
        input_schema["required"] = required;
    }
    
    j["inputSchema"] = input_schema;
    
    return j;
}

std::string McpToolV2::call(const PropertyListV2& properties) {
    uint64_t start_time = get_nowus();
    call_count_.fetch_add(1, std::memory_order_relaxed);
    
    try {
        ReturnValue return_value = callback_(properties);
        
        // 更新执行时间
        uint64_t exec_time = get_nowus() - start_time;
        total_exec_time_.fetch_add(exec_time, std::memory_order_relaxed);
        
        LOG_DEBUG("MCP", "Tool '%s' executed in %llu μs", name_.c_str(), exec_time);
        
        // 构建成功响应
        json result;
        json content = json::array();
        json text_item;
        text_item["type"] = "text";
        
        // 转换返回值为字符串
        if (std::holds_alternative<std::string>(return_value)) {
            text_item["text"] = std::get<std::string>(return_value);
        } else if (std::holds_alternative<bool>(return_value)) {
            text_item["text"] = std::get<bool>(return_value) ? "true" : "false";
        } else if (std::holds_alternative<int>(return_value)) {
            text_item["text"] = std::to_string(std::get<int>(return_value));
        } else if (std::holds_alternative<double>(return_value)) {
            text_item["text"] = std::to_string(std::get<double>(return_value));
        } else if (std::holds_alternative<json>(return_value)) {
            text_item["text"] = std::get<json>(return_value).dump();
        }
        
        content.push_back(text_item);
        result["content"] = content;
        result["isError"] = false;
        
        return result.dump();
        
    } catch (const std::exception& e) {
        // 异常转错误响应
        LOG_ERROR("MCP", "Tool '%s' exception: %s", name_.c_str(), e.what());
        
        json result;
        json content = json::array();
        json text_item;
        text_item["type"] = "text";
        text_item["text"] = std::string("Error: ") + e.what();
        content.push_back(text_item);
        result["content"] = content;
        result["isError"] = true;
        
        return result.dump();
    }
}

uint64_t McpToolV2::getAverageExecutionTime() const {
    uint64_t count = call_count_.load(std::memory_order_relaxed);
    if (count == 0) {
        return 0;
    }
    return total_exec_time_.load(std::memory_order_relaxed) / count;
}

// ============================================================================
// McpServerV2::Impl 内部实现
// ============================================================================

class McpServerV2::Impl {
public:
    // 配置
    McpConfig config;
    
    // 工具存储（哈希表 + 智能指针）
    std::unordered_map<std::string, std::unique_ptr<McpToolV2>> tools_map;
    mutable std::mutex tools_mutex;
    
    // 统计信息
    McpServerV2::Stats stats;
    
    explicit Impl(const McpConfig& cfg)
        : config(cfg) {
        LOG_DEBUG("MCPV2", "Impl created");
    }
    
    ~Impl() {
        LOG_DEBUG("MCPV2", "Impl destroyed (tools auto-released by smart pointers)");
    }
    
    // ========================================================================
    // 工具管理（线程安全）
    // ========================================================================
    
    McpError addTool(std::unique_ptr<McpToolV2> tool) {
        if (!tool) {
            return McpError::INVALID_PARAMS;
        }
        
        std::string name = tool->name();
        
        std::lock_guard<std::mutex> lock(tools_mutex);
        
        // O(1)检查重复
        if (tools_map.find(name) != tools_map.end()) {
            LOG_WARN("MCPV2", "Tool already exists: %s", name.c_str());
            return McpError::INVALID_PARAMS;
        }
        
        LOG_INFO("MCPV2", "✓ Tool added: %s", name.c_str());
        tools_map[name] = std::move(tool);  // 所有权转移
        
        return McpError::NONE;
    }
    
    bool removeTool(const std::string& name) {
        std::lock_guard<std::mutex> lock(tools_mutex);
        
        auto it = tools_map.find(name);
        if (it == tools_map.end()) {
            return false;
        }
        
        tools_map.erase(it);
        LOG_INFO("MCPV2", "Tool removed: %s", name.c_str());
        return true;
    }
    
    bool hasTool(const std::string& name) const {
        std::lock_guard<std::mutex> lock(tools_mutex);
        return tools_map.find(name) != tools_map.end();
    }
    
    size_t toolCount() const {
        std::lock_guard<std::mutex> lock(tools_mutex);
        return tools_map.size();
    }
    
    std::vector<std::string> getToolNames() const {
        std::lock_guard<std::mutex> lock(tools_mutex);
        
        std::vector<std::string> names;
        names.reserve(tools_map.size());
        
        for (const auto& [name, tool] : tools_map) {
            names.push_back(name);
        }
        
        return names;
    }
    
    void clearTools() {
        std::lock_guard<std::mutex> lock(tools_mutex);
        tools_map.clear();  // 智能指针自动释放
        LOG_INFO("MCPV2", "All tools cleared");
    }
    
    // ========================================================================
    // 消息处理
    // ========================================================================
    
    std::string handleMessage(const json& mcp_payload) {
        try {
            // 验证JSONRPC版本
            if (!mcp_payload.contains("jsonrpc") || mcp_payload["jsonrpc"] != "2.0") {
                LOG_ERROR("MCPV2", "Invalid JSONRPC version");
                stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                return "";
            }
            
            // 验证方法字段
            if (!mcp_payload.contains("method") || !mcp_payload["method"].is_string()) {
                LOG_ERROR("MCPV2", "Missing or invalid 'method' field");
                stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                return "";
            }
            
            std::string method = mcp_payload["method"];
            
            // 忽略通知消息（无需响应）
            if (method.find("notifications/") == 0) {
                LOG_DEBUG("MCPV2", "Ignoring notification: %s", method.c_str());
                return "";
            }
            
            // 验证ID字段（请求必须有ID）
            if (!mcp_payload.contains("id") || !mcp_payload["id"].is_number_integer()) {
                LOG_ERROR("MCPV2", "Missing or invalid 'id' field for method: %s", method.c_str());
                stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                return "";
            }
            
            int id = mcp_payload["id"];
            json params = mcp_payload.value("params", json::object());
            
            LOG_DEBUG("MCPV2", "← MCP request: method=%s, id=%d", method.c_str(), id);
            
            // 路由到具体处理函数
            if (method == "initialize") {
                return handleInitialize(id, params);
            } else if (method == "tools/list") {
                return handleToolsList(id, params);
            } else if (method == "tools/call") {
                return handleToolsCall(id, params);
            } else {
                LOG_WARN("MCPV2", "Method not implemented: %s", method.c_str());
                stats.method_not_found.fetch_add(1, std::memory_order_relaxed);
                return replyError(id, "Method not implemented: " + method);
            }
            
        } catch (const json::exception& e) {
            LOG_ERROR("MCPV2", "JSON exception: %s", e.what());
            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
            return "";
        } catch (const std::exception& e) {
            LOG_ERROR("MCPV2", "Exception handling message: %s", e.what());
            return "";
        }
    }
    
    std::string handleInitialize(int id, const json& params) {
        LOG_INFO("MCPV2", "→ Initialize request");
        stats.initialize_requests.fetch_add(1, std::memory_order_relaxed);
        
        // 解析客户端能力
        if (params.contains("capabilities")) {
            LOG_DEBUG("MCPV2", "Client capabilities: %s", params["capabilities"].dump().c_str());
        }
        
        // 构建响应
        json result;
        result["protocolVersion"] = config.protocol_version;
        result["capabilities"] = json::object({{"tools", json::object()}});
        result["serverInfo"] = json::object({
            {"name", config.server_name},
            {"version", config.server_version}
        });
        
        LOG_INFO("MCPV2", "✓ Initialize success");
        return replyResult(id, result);
    }
    
    std::string handleToolsList(int id, const json& params) {
        LOG_DEBUG("MCPV2", "→ Tools list request");
        stats.tools_list_requests.fetch_add(1, std::memory_order_relaxed);
        
        std::string cursor = params.value("cursor", "");
        
        std::lock_guard<std::mutex> lock(tools_mutex);
        
        json tools_array = json::array();
        bool found_cursor = cursor.empty();
        
        // 遍历工具（有序）
        std::vector<std::string> sorted_names;
        sorted_names.reserve(tools_map.size());
        for (const auto& [name, tool] : tools_map) {
            sorted_names.push_back(name);
        }
        std::sort(sorted_names.begin(), sorted_names.end());
        
        for (const auto& name : sorted_names) {
            // 如果还没找到起始位置，继续搜索
            if (!found_cursor) {
                if (name == cursor) {
                    found_cursor = true;
                } else {
                    continue;
                }
            }
            
            tools_array.push_back(tools_map[name]->to_json());
        }
        
        json result;
        result["tools"] = tools_array;
        // TODO: 分页支持（如果工具数量很大）
        
        LOG_INFO("MCPV2", "✓ Tools list: %zu tools", tools_array.size());
        return replyResult(id, result);
    }
    
    std::string handleToolsCall(int id, const json& params) {
        uint64_t start_time = get_nowus();
        stats.tools_call_requests.fetch_add(1, std::memory_order_relaxed);
        
        try {
            // 验证工具名称
            if (!params.contains("name") || !params["name"].is_string()) {
                LOG_ERROR("MCPV2", "Missing tool name");
                stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                return replyError(id, "Missing tool name");
            }
            
            std::string tool_name = params["name"];
            json arguments = params.value("arguments", json::object());
            
            LOG_INFO("MCPV2", "→ Calling tool: %s", tool_name.c_str());
            
            // O(1)查找工具
            std::unique_lock<std::mutex> lock(tools_mutex);
            auto it = tools_map.find(tool_name);
            
            if (it == tools_map.end()) {
                lock.unlock();
                LOG_ERROR("MCPV2", "Tool not found: %s", tool_name.c_str());
                stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                return replyError(id, "Tool not found: " + tool_name);
            }
            
            McpToolV2* tool = it->second.get();
            lock.unlock();  // 释放锁，避免工具执行时持有锁
            
            // 准备参数
            PropertyListV2 call_properties = tool->properties();
            
            for (auto& property : call_properties) {
                if (arguments.contains(property.name())) {
                    const auto& value = arguments[property.name()];
                    
                    switch (property.type()) {
                        case PropertyType::Boolean:
                            if (value.is_boolean()) {
                                property.set_value<bool>(value.get<bool>());
                            }
                            break;
                            
                        case PropertyType::Integer:
                            if (value.is_number_integer()) {
                                property.set_value<int>(value.get<int>());
                            }
                            break;
                            
                        case PropertyType::Number:
                            if (value.is_number()) {
                                property.set_value<double>(value.get<double>());
                            }
                            break;
                            
                        case PropertyType::String:
                            if (value.is_string()) {
                                property.set_value<std::string>(value.get<std::string>());
                            }
                            break;
                    }
                } else if (!property.has_default_value()) {
                    LOG_ERROR("MCPV2", "Missing required argument: %s", property.name().c_str());
                    stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                    return replyError(id, "Missing required argument: " + property.name());
                }
            }
            
            // 调用工具
            std::string result_str = tool->call(call_properties);
            json result = json::parse(result_str);
            
            // 检查工具执行是否成功
            if (result.value("isError", false)) {
                stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                LOG_WARN("MCPV2", "Tool '%s' returned error", tool_name.c_str());
            } else {
                stats.tools_call_success.fetch_add(1, std::memory_order_relaxed);
                LOG_DEBUG("MCPV2", "✓ Tool '%s' executed successfully", tool_name.c_str());
            }
            
            // 更新总执行时间统计
            uint64_t total_time = get_nowus() - start_time;
            stats.total_exec_time_us.fetch_add(total_time, std::memory_order_relaxed);
            
            uint64_t call_count = stats.tools_call_requests.load(std::memory_order_relaxed);
            if (call_count > 0) {
                stats.avg_exec_time_us.store(
                    stats.total_exec_time_us.load() / call_count,
                    std::memory_order_relaxed
                );
            }
            
            return replyResult(id, result);
            
        } catch (const std::exception& e) {
            LOG_ERROR("MCPV2", "Tool call exception: %s", e.what());
            stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
            return replyError(id, std::string("Tool call error: ") + e.what());
        }
    }
    
    // ========================================================================
    // 响应生成
    // ========================================================================
    
    std::string replyResult(int id, const json& result) {
        json response;
        response["jsonrpc"] = "2.0";
        response["id"] = id;
        response["result"] = result;
        return response.dump();
    }
    
    std::string replyError(int id, const std::string& message) {
        json response;
        response["jsonrpc"] = "2.0";
        response["id"] = id;
        response["error"] = json::object({{"message", message}});
        return response.dump();
    }
};

// ============================================================================
// McpServerV2 公共接口实现
// ============================================================================

McpServerV2::McpServerV2(const McpConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
    LOG_INFO("MCPV2", "MCP Server V2 created");
}

McpServerV2::~McpServerV2() {
    LOG_INFO("MCPV2", "MCP Server V2 destroying...");
    
    // 输出统计
    logStats();
    
    // RAII自动清理
    LOG_INFO("MCPV2", "MCP Server V2 destroyed");
}

// ========================================================================
// 工具管理
// ========================================================================

McpError McpServerV2::add_tool(std::unique_ptr<McpToolV2> tool) {
    return pImpl_->addTool(std::move(tool));
}

McpError McpServerV2::add_tool(const std::string& name,
                               const std::string& description,
                               const PropertyListV2& properties,
                               std::function<ReturnValue(const PropertyListV2&)> callback) {
    auto tool = std::make_unique<McpToolV2>(name, description, properties, callback);
    return add_tool(std::move(tool));
}

bool McpServerV2::remove_tool(const std::string& name) {
    return pImpl_->removeTool(name);
}

bool McpServerV2::has_tool(const std::string& name) const {
    return pImpl_->hasTool(name);
}

size_t McpServerV2::tool_count() const {
    return pImpl_->toolCount();
}

std::vector<std::string> McpServerV2::get_tool_names() const {
    return pImpl_->getToolNames();
}

void McpServerV2::clear_tools() {
    pImpl_->clearTools();
}

// ========================================================================
// 消息处理
// ========================================================================

std::string McpServerV2::handle_message(const json& mcp_payload) {
    return pImpl_->handleMessage(mcp_payload);
}

std::string McpServerV2::handle_message(const std::string& mcp_payload_str) {
    try {
        json mcp_payload = json::parse(mcp_payload_str);
        return handle_message(mcp_payload);
    } catch (const json::parse_error& e) {
        LOG_ERROR("MCPV2", "Failed to parse MCP message: %s", e.what());
        pImpl_->stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
        return "";
    }
}

// ========================================================================
// 统计信息
// ========================================================================

void McpServerV2::getStats(Stats& out_stats) const {
    out_stats.initialize_requests.store(pImpl_->stats.initialize_requests.load());
    out_stats.tools_list_requests.store(pImpl_->stats.tools_list_requests.load());
    out_stats.tools_call_requests.store(pImpl_->stats.tools_call_requests.load());
    out_stats.tools_call_success.store(pImpl_->stats.tools_call_success.load());
    out_stats.tools_call_errors.store(pImpl_->stats.tools_call_errors.load());
    out_stats.parse_errors.store(pImpl_->stats.parse_errors.load());
    out_stats.method_not_found.store(pImpl_->stats.method_not_found.load());
    out_stats.total_exec_time_us.store(pImpl_->stats.total_exec_time_us.load());
    out_stats.avg_exec_time_us.store(pImpl_->stats.avg_exec_time_us.load());
}

void McpServerV2::resetStats() {
    pImpl_->stats.initialize_requests.store(0);
    pImpl_->stats.tools_list_requests.store(0);
    pImpl_->stats.tools_call_requests.store(0);
    pImpl_->stats.tools_call_success.store(0);
    pImpl_->stats.tools_call_errors.store(0);
    pImpl_->stats.parse_errors.store(0);
    pImpl_->stats.method_not_found.store(0);
    pImpl_->stats.total_exec_time_us.store(0);
    pImpl_->stats.avg_exec_time_us.store(0);
    
    LOG_INFO("MCPV2", "Stats reset");
}

void McpServerV2::logStats() const {
    uint64_t init_req = pImpl_->stats.initialize_requests.load();
    uint64_t list_req = pImpl_->stats.tools_list_requests.load();
    uint64_t call_req = pImpl_->stats.tools_call_requests.load();
    uint64_t call_success = pImpl_->stats.tools_call_success.load();
    uint64_t call_errors = pImpl_->stats.tools_call_errors.load();
    uint64_t parse_err = pImpl_->stats.parse_errors.load();
    uint64_t not_found = pImpl_->stats.method_not_found.load();
    
    LOG_INFO("MCPV2", "=== MCP Server V2 Statistics ===");
    LOG_INFO("MCPV2", "  Initialize requests: %llu", init_req);
    LOG_INFO("MCPV2", "  Tools/list requests: %llu", list_req);
    LOG_INFO("MCPV2", "  Tools/call requests: %llu", call_req);
    LOG_INFO("MCPV2", "    - Success:         %llu", call_success);
    LOG_INFO("MCPV2", "    - Errors:          %llu", call_errors);
    LOG_INFO("MCPV2", "  Parse errors:        %llu", parse_err);
    LOG_INFO("MCPV2", "  Method not found:    %llu", not_found);
    
    if (call_req > 0) {
        double success_rate = (double)call_success / call_req * 100.0;
        uint64_t avg_time = pImpl_->stats.avg_exec_time_us.load();
        
        LOG_INFO("MCPV2", "  Tool success rate:   %.2f%%", success_rate);
        LOG_INFO("MCPV2", "  Avg execution time:  %llu μs", avg_time);
        
        if (success_rate < 95.0) {
            LOG_WARN("MCPV2", "Low tool success rate, review tool implementations");
        }
    }
    
    LOG_INFO("MCPV2", "  Total tools:         %zu", pImpl_->toolCount());
}

std::map<std::string, uint64_t> McpServerV2::getToolUsageStats() const {
    std::lock_guard<std::mutex> lock(pImpl_->tools_mutex);
    
    std::map<std::string, uint64_t> usage;
    
    for (const auto& [name, tool] : pImpl_->tools_map) {
        usage[name] = tool->getCallCount();
    }
    
    return usage;
}

} // namespace mcp
} // namespace chatbot
} // namespace app

