/* mcp.cc - MCP协议服务器 */

#include "mcp.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/time/time.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace app
{
    namespace chatbot
    {
        namespace mcp
        {

            using namespace tool::log;

            namespace
            {
                constexpr const char* LOG_TAG                        = "MCP";
                constexpr double      SUCCESS_RATE_WARNING_THRESHOLD = 95.0;
            } // namespace

            Property::Property(std::string name, PropertyType type)
                : name_(std::move(name)), type_(type), has_default_value_(false)
            {
            }

            Property::Property(std::string name, PropertyType type, int min_value, int max_value)
                : name_(std::move(name)), type_(type), has_default_value_(false),
                  min_value_(min_value), max_value_(max_value)
            {
                if (type != PropertyType::Integer)
                {
                    throw std::invalid_argument("Range limits only apply to integer properties");
                }
            }

            Property::Property(std::string name, PropertyType type, int default_value,
                               int min_value, int max_value)
                : name_(std::move(name)), type_(type), has_default_value_(true),
                  min_value_(min_value), max_value_(max_value)
            {
                if (type != PropertyType::Integer)
                {
                    throw std::invalid_argument("Range limits only apply to integer properties");
                }
                if (default_value < min_value || default_value > max_value)
                {
                    throw std::invalid_argument("Default value must be within the specified range");
                }
                value_ = default_value;
            }

            json Property::to_json() const
            {
                json j;

                switch (type_)
                {
                case PropertyType::Boolean:
                    j["type"] = "boolean";
                    if (has_default_value_)
                    {
                        j["default"] = value<bool>();
                    }
                    break;

                case PropertyType::Integer:
                    j["type"] = "integer";
                    if (has_default_value_)
                    {
                        j["default"] = value<int>();
                    }
                    if (min_value_.has_value())
                    {
                        j["minimum"] = min_value_.value();
                    }
                    if (max_value_.has_value())
                    {
                        j["maximum"] = max_value_.value();
                    }
                    break;

                case PropertyType::Number:
                    j["type"] = "number";
                    if (has_default_value_)
                    {
                        j["default"] = value<double>();
                    }
                    break;

                case PropertyType::String:
                    j["type"] = "string";
                    if (has_default_value_)
                    {
                        j["default"] = value<std::string>();
                    }
                    break;
                }

                return j;
            }

            PropertyList::PropertyList(std::vector<Property> properties)
                : properties_(std::move(properties))
            {
            }

            PropertyList::PropertyList(std::initializer_list<Property> properties)
                : properties_(properties)
            {
            }

            void PropertyList::add(const Property& property)
            {
                properties_.push_back(property);
            }

            const Property& PropertyList::operator[](const std::string& name) const
            {
                for (const auto& property : properties_)
                {
                    if (property.name() == name)
                    {
                        return property;
                    }
                }
                throw std::runtime_error("Property not found: " + name);
            }

            Property& PropertyList::operator[](const std::string& name)
            {
                for (auto& property : properties_)
                {
                    if (property.name() == name)
                    {
                        return property;
                    }
                }
                throw std::runtime_error("Property not found: " + name);
            }

            std::vector<std::string> PropertyList::get_required() const
            {
                std::vector<std::string> required;
                for (const auto& property : properties_)
                {
                    if (!property.has_default_value())
                    {
                        required.push_back(property.name());
                    }
                }
                return required;
            }

            json PropertyList::to_json() const
            {
                json j;
                for (const auto& property : properties_)
                {
                    j[property.name()] = property.to_json();
                }
                return j;
            }

            McpTool::McpTool(std::string name, std::string description, PropertyList properties,
                             Callback callback)
                : name_(std::move(name)), description_(std::move(description)),
                  properties_(std::move(properties)), callback_(std::move(callback))
            {
            }

            json McpTool::to_json() const
            {
                json j;
                j["name"]        = name_;
                j["description"] = description_;

                json input_schema;
                input_schema["type"]       = "object";
                input_schema["properties"] = properties_.to_json();

                auto required = properties_.get_required();
                if (!required.empty())
                {
                    input_schema["required"] = required;
                }

                j["inputSchema"] = input_schema;

                return j;
            }

            std::string McpTool::call(const PropertyList& properties)
            {
                uint64_t start_time = static_cast<uint64_t>(app::tool::time::uptime_us());
                call_count_.fetch_add(1, std::memory_order_relaxed);

                try
                {
                    ReturnValue return_value = callback_(properties);

                    uint64_t exec_time =
                        static_cast<uint64_t>(app::tool::time::uptime_us()) - start_time;
                    total_exec_time_.fetch_add(exec_time, std::memory_order_relaxed);

                    // LOG_DEBUG(LOG_TAG, "工具 '%s' 执行耗时 %llu μs", name_.c_str(), exec_time);

                    json result;
                    json content = json::array();
                    json text_item;
                    text_item["type"] = "text";

                    if (std::holds_alternative<std::string>(return_value))
                    {
                        text_item["text"] = std::get<std::string>(return_value);
                    }
                    else if (std::holds_alternative<bool>(return_value))
                    {
                        text_item["text"] = std::get<bool>(return_value) ? "true" : "false";
                    }
                    else if (std::holds_alternative<int>(return_value))
                    {
                        text_item["text"] = std::to_string(std::get<int>(return_value));
                    }
                    else if (std::holds_alternative<double>(return_value))
                    {
                        text_item["text"] = std::to_string(std::get<double>(return_value));
                    }
                    else if (std::holds_alternative<json>(return_value))
                    {
                        text_item["text"] = std::get<json>(return_value).dump();
                    }

                    content.push_back(text_item);
                    result["content"] = content;
                    result["isError"] = false;

                    return result.dump();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR(LOG_TAG, "工具 '%s' 异常: %s", name_.c_str(), e.what());

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

            uint64_t McpTool::getAverageExecutionTime() const
            {
                uint64_t count = call_count_.load(std::memory_order_relaxed);
                if (count == 0)
                {
                    return 0;
                }
                return total_exec_time_.load(std::memory_order_relaxed) / count;
            }

            class McpServer::Impl
            {
            public:
                // 配置
                McpConfig config;

                // 工具存储
                std::unordered_map<std::string, std::unique_ptr<McpTool>> tools_map;
                mutable std::mutex                                        tools_mutex;

                // 统计信息
                McpServer::Stats stats;

                // Vision配置回调
                McpServer::VisionConfigCallback vision_config_callback_;

                explicit Impl(McpConfig cfg) : config(std::move(cfg)) {}

                ~Impl() {}

                static std::optional<std::string>
                parseToolCallParams(const json& params, std::string& tool_name, json& arguments)
                {
                    if (!params.contains("name") || !params["name"].is_string())
                    {
                        return std::string("Missing tool name");
                    }

                    tool_name = params["name"];
                    arguments = params.value("arguments", json::object());
                    return std::nullopt;
                }

                McpTool* findToolByName(const std::string& tool_name)
                {
                    std::lock_guard<std::mutex> lock(tools_mutex);
                    auto                        it = tools_map.find(tool_name);
                    if (it == tools_map.end())
                    {
                        return nullptr;
                    }
                    return it->second.get();
                }

                void parseServerResponse(const json& response)
                {
                    try
                    {
                        // 检查是否有result字段
                        if (!response.contains("result") || !response["result"].is_object())
                        {
                            return;
                        }

                        const json& result = response["result"];

                        // 检查是否有capabilities字段
                        if (!result.contains("capabilities") || !result["capabilities"].is_object())
                        {
                            return;
                        }

                        const json& capabilities = result["capabilities"];

                        // 检查是否有vision字段
                        if (!capabilities.contains("vision") || !capabilities["vision"].is_object())
                        {
                            LOG_DEBUG(LOG_TAG, "服务器响应中未包含vision配置");
                            return;
                        }

                        const json& vision = capabilities["vision"];

                        // 提取url字段（必需）
                        if (!vision.contains("url") || !vision["url"].is_string())
                        {
                            LOG_WARN(LOG_TAG, "vision配置中缺少url字段");
                            return;
                        }

                        std::string url = vision["url"].get<std::string>();
                        if (url.empty())
                        {
                            LOG_WARN(LOG_TAG, "vision配置中的url为空");
                            return;
                        }

                        // 提取token字段（可选）
                        std::string token;
                        if (vision.contains("token") && vision["token"].is_string())
                        {
                            token = vision["token"].get<std::string>();
                        }

                        LOG_INFO(LOG_TAG, "vision配置 url=%s", url.c_str());

                        if (vision_config_callback_)
                        {
                            vision_config_callback_(url, token);
                        }
                        else
                            LOG_WARN(LOG_TAG, "vision回调未设置");
                    }
                    catch (const json::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "解析服务器响应失败: %s", e.what());
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "解析服务器响应异常: %s", e.what());
                    }
                }

                static std::optional<std::string> assignPropertyValue(Property&   property,
                                                                      const json& value)
                {
                    switch (property.type())
                    {
                    case PropertyType::Boolean:
                        if (value.is_boolean())
                        {
                            property.set_value<bool>(value.get<bool>());
                            return std::nullopt;
                        }
                        break;

                    case PropertyType::Integer:
                        if (value.is_number_integer())
                        {
                            property.set_value<int>(value.get<int>());
                            return std::nullopt;
                        }
                        break;

                    case PropertyType::Number:
                        if (value.is_number())
                        {
                            property.set_value<double>(value.get<double>());
                            return std::nullopt;
                        }
                        break;

                    case PropertyType::String:
                        if (value.is_string())
                        {
                            property.set_value<std::string>(value.get<std::string>());
                            return std::nullopt;
                        }
                        break;
                    }

                    return std::string("Invalid argument type for property: ") + property.name();
                }

                static std::optional<std::string>
                populateArgumentValues(PropertyList& call_properties, const json& arguments)
                {
                    for (auto& property : call_properties)
                    {
                        if (arguments.contains(property.name()))
                        {
                            const auto& value = arguments[property.name()];
                            if (auto assign_error = assignPropertyValue(property, value);
                                assign_error.has_value())
                            {
                                return assign_error;
                            }
                        }
                        else if (!property.has_default_value())
                        {
                            return std::string("Missing required argument: ") + property.name();
                        }
                    }

                    return std::nullopt;
                }

                McpError addTool(std::unique_ptr<McpTool> tool)
                {
                    if (!tool)
                    {
                        return McpError::INVALID_PARAMS;
                    }

                    std::string name = tool->name();

                    std::lock_guard<std::mutex> lock(tools_mutex);

                    if (tools_map.find(name) != tools_map.end())
                    {
                        LOG_WARN(LOG_TAG, "工具已存在: %s", name.c_str());
                        return McpError::INVALID_PARAMS;
                    }

                    LOG_DEBUG(LOG_TAG, "添加工具: %s", name.c_str());
                    tools_map[name] = std::move(tool);

                    return McpError::NONE;
                }

                bool removeTool(const std::string& name)
                {
                    std::lock_guard<std::mutex> lock(tools_mutex);

                    auto it = tools_map.find(name);
                    if (it == tools_map.end())
                    {
                        return false;
                    }

                    tools_map.erase(it);
                    LOG_DEBUG(LOG_TAG, "移除工具: %s", name.c_str());
                    return true;
                }

                bool hasTool(const std::string& name) const
                {
                    std::lock_guard<std::mutex> lock(tools_mutex);
                    return tools_map.find(name) != tools_map.end();
                }

                size_t toolCount() const
                {
                    std::lock_guard<std::mutex> lock(tools_mutex);
                    return tools_map.size();
                }

                std::vector<std::string> getToolNames() const
                {
                    std::lock_guard<std::mutex> lock(tools_mutex);

                    std::vector<std::string> names;
                    names.reserve(tools_map.size());

                    for (const auto& [name, tool] : tools_map)
                    {
                        names.push_back(name);
                    }

                    return names;
                }

                void clearTools()
                {
                    std::lock_guard<std::mutex> lock(tools_mutex);
                    tools_map.clear();
                    // LOG_INFO(LOG_TAG, "所有工具已清空");
                }

                std::string handleMessage(const json& mcp_payload)
                {
                    try
                    {
                        if (!mcp_payload.contains("jsonrpc") || mcp_payload["jsonrpc"] != "2.0")
                        {
                            LOG_ERROR(LOG_TAG, "无效的JSONRPC版本");
                            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                            return "";
                        }

                        // 检查是否是服务器响应（包含result字段）
                        if (mcp_payload.contains("result") && mcp_payload["result"].is_object())
                        {
                            parseServerResponse(mcp_payload);
                            return "";
                        }

                        if (!mcp_payload.contains("method") || !mcp_payload["method"].is_string())
                        {
                            LOG_ERROR(LOG_TAG, "缺少或无效的'method'字段");
                            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                            return "";
                        }

                        std::string method = mcp_payload["method"];

                        if (method.find("notifications/") == 0)
                        {
                            LOG_DEBUG(LOG_TAG, "忽略通知: %s", method.c_str());
                            return "";
                        }

                        if (!mcp_payload.contains("id") || !mcp_payload["id"].is_number_integer())
                        {
                            LOG_ERROR(LOG_TAG, "方法 %s 缺少或无效的'id'字段", method.c_str());
                            stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                            return "";
                        }

                        int  id     = mcp_payload["id"];
                        json params = mcp_payload.value("params", json::object());

                        LOG_DEBUG(LOG_TAG, "<- MCP请求: method=%s, id=%d", method.c_str(), id);

                        if (method == "initialize")
                        {
                            return handleInitialize(id, params);
                        }
                        if (method == "tools/list")
                        {
                            return handleToolsList(id, params);
                        }
                        if (method == "tools/call")
                        {
                            return handleToolsCall(id, params);
                        }

                        LOG_WARN(LOG_TAG, "方法未实现: %s", method.c_str());
                        stats.method_not_found.fetch_add(1, std::memory_order_relaxed);
                        return replyError(id, "Method not implemented: " + method);
                    }
                    catch (const json::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "JSON异常: %s", e.what());
                        stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                        return "";
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "处理消息异常: %s", e.what());
                        return "";
                    }
                }

                std::string handleInitialize(int id, const json& params)
                {
                    LOG_DEBUG(LOG_TAG, "Initialize");
                    stats.initialize_requests.fetch_add(1, std::memory_order_relaxed);

                    // 解析客户端能力
                    if (params.contains("capabilities") && params["capabilities"].is_object())
                    {
                        const json& capabilities = params["capabilities"];
                        LOG_DEBUG(LOG_TAG, "客户端能力: %s", capabilities.dump().c_str());

                        // 解析vision配置
                        if (capabilities.contains("vision") && capabilities["vision"].is_object())
                        {
                            const json& vision = capabilities["vision"];
                            if (vision.contains("url") && vision["url"].is_string())
                            {
                                std::string url = vision["url"].get<std::string>();
                                std::string token;
                                if (vision.contains("token") && vision["token"].is_string())
                                {
                                    token = vision["token"].get<std::string>();
                                }
                                if (vision_config_callback_)
                                {
                                    vision_config_callback_(url, token);
                                }
                            }
                        }
                    }

                    // 构建响应
                    json result;
                    result["protocolVersion"] = config.protocol_version;
                    result["capabilities"]    = json::object({{"tools", json::object()}});
                    result["serverInfo"]      = json::object(
                             {{"name", config.server_name}, {"version", config.server_version}});

                    LOG_DEBUG(LOG_TAG, "Initialize完成");
                    return replyResult(id, result);
                }

                std::string handleToolsList(int id, const json& params)
                {
                    LOG_DEBUG(LOG_TAG, "Tools/list");
                    stats.tools_list_requests.fetch_add(1, std::memory_order_relaxed);

                    std::string cursor = params.value("cursor", "");

                    std::lock_guard<std::mutex> lock(tools_mutex);

                    json tools_array  = json::array();
                    bool found_cursor = cursor.empty();

                    // 遍历工具（有序）
                    std::vector<std::string> sorted_names;
                    sorted_names.reserve(tools_map.size());
                    for (const auto& [name, tool] : tools_map)
                    {
                        sorted_names.push_back(name);
                    }
                    std::sort(sorted_names.begin(), sorted_names.end());

                    for (const auto& name : sorted_names)
                    {
                        // 如果还没找到起始位置，继续搜索
                        if (!found_cursor)
                        {
                            if (name == cursor)
                            {
                                found_cursor = true;
                            }
                            else
                            {
                                continue;
                            }
                        }

                        tools_array.push_back(tools_map[name]->to_json());
                    }

                    json result;
                    result["tools"] = tools_array;

                    // LOG_INFO(LOG_TAG, "Tools/list: %zu个工具", tools_array.size());
                    return replyResult(id, result);
                }

                std::string handleToolsCall(int id, const json& params)
                {
                    uint64_t start_time = static_cast<uint64_t>(app::tool::time::uptime_us());
                    stats.tools_call_requests.fetch_add(1, std::memory_order_relaxed);

                    try
                    {
                        std::string tool_name;
                        json        arguments;
                        if (auto parse_error = parseToolCallParams(params, tool_name, arguments);
                            parse_error.has_value())
                        {
                            LOG_ERROR(LOG_TAG, "%s", parse_error->c_str());
                            stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                            return replyError(id, *parse_error);
                        }

                        LOG_DEBUG(LOG_TAG, "调用工具: %s", tool_name.c_str());

                        McpTool* tool = findToolByName(tool_name);
                        if (tool == nullptr)
                        {
                            LOG_ERROR(LOG_TAG, "工具未找到: %s", tool_name.c_str());
                            stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                            return replyError(id, "Tool not found: " + tool_name);
                        }

                        PropertyList call_properties = tool->properties();
                        if (auto populate_error =
                                populateArgumentValues(call_properties, arguments);
                            populate_error.has_value())
                        {
                            LOG_ERROR(LOG_TAG, "%s", populate_error->c_str());
                            stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                            return replyError(id, *populate_error);
                        }

                        std::string result_str = tool->call(call_properties);
                        json        result     = json::parse(result_str);

                        if (result.value("isError", false))
                        {
                            stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                        }
                        else
                        {
                            stats.tools_call_success.fetch_add(1, std::memory_order_relaxed);
                        }

                        uint64_t total_time =
                            static_cast<uint64_t>(app::tool::time::uptime_us()) - start_time;
                        stats.total_exec_time_us.fetch_add(total_time, std::memory_order_relaxed);

                        uint64_t call_count =
                            stats.tools_call_requests.load(std::memory_order_relaxed);
                        if (call_count > 0)
                        {
                            stats.avg_exec_time_us.store(stats.total_exec_time_us.load() /
                                                             call_count,
                                                         std::memory_order_relaxed);
                        }

                        return replyResult(id, result);
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "工具调用异常: %s", e.what());
                        stats.tools_call_errors.fetch_add(1, std::memory_order_relaxed);
                        return replyError(id, std::string("Tool call error: ") + e.what());
                    }
                }

                std::string replyResult(int id, const json& result)
                {
                    json response;
                    response["jsonrpc"] = "2.0";
                    response["id"]      = id;
                    response["result"]  = result;
                    return response.dump();
                }

                std::string replyError(int id, const std::string& message)
                {
                    json response;
                    response["jsonrpc"] = "2.0";
                    response["id"]      = id;
                    response["error"]   = json::object({{"message", message}});
                    return response.dump();
                }
            };

            McpServer::McpServer(McpConfig config)
                : pImpl_(std::make_unique<Impl>(std::move(config)))
            {
                LOG_DEBUG(LOG_TAG, "启动");
            }

            McpServer::~McpServer()
            {
                // LOG_INFO(LOG_TAG, "MCP服务器销毁中...");
                logStats();
                // LOG_INFO(LOG_TAG, "MCP服务器已销毁");
            }

            McpError McpServer::add_tool(std::unique_ptr<McpTool> tool)
            {
                return pImpl_->addTool(std::move(tool));
            }

            McpError McpServer::add_tool(std::string name, std::string description,
                                         PropertyList                                    properties,
                                         std::function<ReturnValue(const PropertyList&)> callback)
            {
                auto tool = std::make_unique<McpTool>(std::move(name), std::move(description),
                                                      std::move(properties), std::move(callback));
                return add_tool(std::move(tool));
            }

            bool McpServer::remove_tool(const std::string& name)
            {
                return pImpl_->removeTool(name);
            }

            bool McpServer::has_tool(const std::string& name) const
            {
                return pImpl_->hasTool(name);
            }

            size_t McpServer::tool_count() const
            {
                return pImpl_->toolCount();
            }

            std::vector<std::string> McpServer::get_tool_names() const
            {
                return pImpl_->getToolNames();
            }

            void McpServer::clear_tools()
            {
                pImpl_->clearTools();
            }

            std::string McpServer::handle_message(const json& mcp_payload)
            {
                return pImpl_->handleMessage(mcp_payload);
            }

            std::string McpServer::handle_message(const std::string& mcp_payload_str)
            {
                try
                {
                    json mcp_payload = json::parse(mcp_payload_str);
                    return handle_message(mcp_payload);
                }
                catch (const json::parse_error& e)
                {
                    LOG_ERROR(LOG_TAG, "解析MCP消息失败: %s", e.what());
                    pImpl_->stats.parse_errors.fetch_add(1, std::memory_order_relaxed);
                    return "";
                }
            }

            void McpServer::setVisionConfigCallback(VisionConfigCallback callback)
            {
                pImpl_->vision_config_callback_ = std::move(callback);
            }

            void McpServer::getStats(Stats& out_stats) const
            {
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

            void McpServer::resetStats()
            {
                pImpl_->stats.initialize_requests.store(0);
                pImpl_->stats.tools_list_requests.store(0);
                pImpl_->stats.tools_call_requests.store(0);
                pImpl_->stats.tools_call_success.store(0);
                pImpl_->stats.tools_call_errors.store(0);
                pImpl_->stats.parse_errors.store(0);
                pImpl_->stats.method_not_found.store(0);
                pImpl_->stats.total_exec_time_us.store(0);
                pImpl_->stats.avg_exec_time_us.store(0);

                LOG_DEBUG(LOG_TAG, "统计已重置");
            }

            void McpServer::logStats() const
            {
                uint64_t init_req     = pImpl_->stats.initialize_requests.load();
                uint64_t list_req     = pImpl_->stats.tools_list_requests.load();
                uint64_t call_req     = pImpl_->stats.tools_call_requests.load();
                uint64_t call_success = pImpl_->stats.tools_call_success.load();
                uint64_t call_errors  = pImpl_->stats.tools_call_errors.load();
                uint64_t parse_err    = pImpl_->stats.parse_errors.load();
                uint64_t not_found    = pImpl_->stats.method_not_found.load();

                LOG_INFO(LOG_TAG,
                         "统计 init=%llu list=%llu call=%llu 成功=%llu 错误=%llu 解析=%llu 未找到=%llu",
                         init_req, list_req, call_req, call_success, call_errors, parse_err,
                         not_found);

                if (call_req > 0)
                {
                    double   success_rate = (double)call_success / call_req * 100.0;
                    uint64_t avg_time     = pImpl_->stats.avg_exec_time_us.load();
                    LOG_INFO(LOG_TAG, "成功率 %.1f%% 均时 %lluμs 工具数 %zu", success_rate,
                             avg_time, pImpl_->toolCount());
                    if (success_rate < SUCCESS_RATE_WARNING_THRESHOLD)
                        LOG_WARN(LOG_TAG, "成功率偏低");
                }
            }

            std::map<std::string, uint64_t> McpServer::getToolUsageStats() const
            {
                std::lock_guard<std::mutex> lock(pImpl_->tools_mutex);

                std::map<std::string, uint64_t> usage;

                for (const auto& [name, tool] : pImpl_->tools_map)
                {
                    usage[name] = tool->getCallCount();
                }

                return usage;
            }

        } // namespace mcp
    }     // namespace chatbot
} // namespace app
