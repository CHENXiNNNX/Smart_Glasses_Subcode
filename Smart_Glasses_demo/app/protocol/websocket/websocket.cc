#include "websocket.hpp"
#include "../../tool/log/log.hpp"
#include "../../tool/time/time.hpp"
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <thread>
#include <condition_variable>
#include <variant>
#include <cstring>
#include <exception>
#include <string>
#include <utility>

namespace app
{
    namespace protocol
    {
        namespace websocket
        {

            using namespace tool::log;

            namespace
            {
                constexpr const char* LOG_TAG = "WEBSOCKET";
            } // namespace

            using ws_client_type        = websocketpp::client<websocketpp::config::asio_tls_client>;
            using ws_client_nontls_type = websocketpp::client<websocketpp::config::asio_client>;
            using context_ptr = websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;
            using connection_hdl = websocketpp::connection_hdl;

            class WebSocketClient::Impl
            {
            public:
                WebSocketConfig config;
                std::variant<std::unique_ptr<ws_client_type>,
                             std::unique_ptr<ws_client_nontls_type>>
                               client;
                connection_hdl connection_handle;
                bool           use_tls = false;

                std::atomic<ConnectionState> state{ConnectionState::DISCONNECTED};
                std::atomic<bool>            handshaked{false};
                std::atomic<int>             reconnect_count{0};
                uint64_t                     connected_timestamp{0};

                std::unique_ptr<std::thread> ws_thread;
                std::unique_ptr<std::thread> reconnect_thread;
                std::atomic<bool>            should_stop{false};
                std::atomic<bool>            should_reconnect{false};
                std::condition_variable      reconnect_cv;
                std::mutex                   reconnect_mutex;

                MessageCallback         binary_callback;
                MessageCallback         text_callback;
                ConnectionStateCallback state_callback;
                WebSocketErrorCallback  error_callback;
                mutable std::mutex      callback_mutex;

                WebSocketClient::Stats stats;
                mutable std::mutex     mutex;

                explicit Impl(WebSocketConfig cfg) : config(std::move(cfg)) {}

                ~Impl()
                {
                    cleanup();
                }

                void cleanup()
                {

                    should_stop.store(true, std::memory_order_release);
                    should_reconnect.store(false, std::memory_order_release);
                    reconnect_cv.notify_all();

                    try
                    {
                        if (use_tls)
                        {
                            if (auto* tls_client =
                                    std::get_if<std::unique_ptr<ws_client_type>>(&client);
                                tls_client && *tls_client)
                            {
                                (*tls_client)->stop();
                            }
                        }
                        else
                        {
                            if (auto* nontls_client =
                                    std::get_if<std::unique_ptr<ws_client_nontls_type>>(&client);
                                nontls_client && *nontls_client)
                            {
                                (*nontls_client)->stop();
                            }
                        }
                    }
                    catch (const std::exception& e)
                    {
                        LOG_WARN(LOG_TAG, "客户端停止: %s", e.what());
                    }
                    catch (...)
                    {
                        LOG_WARN(LOG_TAG, "客户端停止异常");
                    }

                    if (reconnect_thread)
                    {
                        try
                        {
                            if (reconnect_thread->joinable())
                            {
                                reconnect_thread->join();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_WARN(LOG_TAG, "重连线程join: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_WARN(LOG_TAG, "重连线程join异常");
                        }
                        reconnect_thread.reset();
                    }

                    if (ws_thread)
                    {
                        try
                        {
                            if (ws_thread->joinable())
                            {
                                ws_thread->join();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_WARN(LOG_TAG, "WS线程join: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_WARN(LOG_TAG, "WS线程join异常");
                        }
                        ws_thread.reset();
                    }
                }

                void setState(ConnectionState new_state)
                {
                    ConnectionState old_state =
                        state.exchange(new_state, std::memory_order_acq_rel);

                    if (old_state != new_state)
                    {
                        LOG_INFO(LOG_TAG, "状态 %s -> %s", stateToString(old_state).c_str(),
                                 stateToString(new_state).c_str());

                        if (old_state == ConnectionState::CONNECTED &&
                            new_state != ConnectionState::CONNECTED)
                        {
                            uint64_t uptime = static_cast<uint64_t>(app::tool::time::uptime_us()) -
                                              connected_timestamp;
                            stats.total_uptime_us.fetch_add(uptime, std::memory_order_relaxed);
                        }

                        if (new_state == ConnectionState::CONNECTED)
                        {
                            connected_timestamp =
                                static_cast<uint64_t>(app::tool::time::uptime_us());
                        }

                        invokeStateCallback(old_state, new_state);
                    }
                }

                static std::string stateToString(ConnectionState state)
                {
                    switch (state)
                    {
                    case ConnectionState::DISCONNECTED:
                        return "DISCONNECTED";
                    case ConnectionState::CONNECTING:
                        return "CONNECTING";
                    case ConnectionState::CONNECTED:
                        return "CONNECTED";
                    case ConnectionState::HANDSHAKED:
                        return "HANDSHAKED";
                    case ConnectionState::CLOSING:
                        return "CLOSING";
                    case ConnectionState::CLOSED:
                        return "CLOSED";
                    case ConnectionState::ERROR:
                        return "ERROR";
                    default:
                        return "UNKNOWN";
                    }
                }

                context_ptr onTlsInit(const std::string&          hostname,
                                      websocketpp::connection_hdl hdl) const
                {
                    static_cast<void>(hostname);
                    static_cast<void>(hdl);

                    context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(
                        boost::asio::ssl::context::sslv23);

                    try
                    {
                        ctx->set_options(boost::asio::ssl::context::default_workarounds |
                                         boost::asio::ssl::context::no_sslv2 |
                                         boost::asio::ssl::context::no_sslv3 |
                                         boost::asio::ssl::context::single_dh_use);

                        if (!config.verify_ssl)
                        {
                            ctx->set_verify_mode(boost::asio::ssl::verify_none);
                        }
                        else
                        {
                            ctx->set_verify_mode(boost::asio::ssl::verify_peer);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "TLS初始化错误: %s", e.what());
                    }

                    return ctx;
                }

                // ========================================================================
                // 回调调用
                // ========================================================================

                void invokeBinaryCallback(const char* data, size_t size)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (binary_callback)
                    {
                        try
                        {
                            bool success = binary_callback(data, size);
                            if (!success)
                            {
                                LOG_WARN(LOG_TAG, "二进制回调失败");
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "二进制回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                void invokeTextCallback(const char* data, size_t size)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (text_callback)
                    {
                        try
                        {
                            bool success = text_callback(data, size);
                            if (!success)
                            {
                                LOG_WARN(LOG_TAG, "文本回调失败");
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "文本回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                void invokeStateCallback(ConnectionState old_state, ConnectionState new_state)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (state_callback)
                    {
                        try
                        {
                            state_callback(old_state, new_state);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "状态回调异常: %s", e.what());
                            stats.callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                void invokeErrorCallback(WebSocketError error, const std::string& message)
                {
                    std::lock_guard<std::mutex> lock(callback_mutex);

                    if (error_callback)
                    {
                        try
                        {
                            error_callback(error, message);
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "错误回调异常: %s", e.what());
                        }
                    }
                }

                // ========================================================================
                // 连接和重连逻辑
                // ========================================================================

                WebSocketError connectInternal()
                {
                    if (should_stop.load(std::memory_order_acquire))
                    {
                        LOG_DEBUG(LOG_TAG, "对象正在销毁，取消连接");
                        return WebSocketError::CONNECTION_FAILED;
                    }

                    std::lock_guard<std::mutex> lock(mutex);

                    if (should_stop.load(std::memory_order_acquire))
                    {
                        LOG_DEBUG(LOG_TAG, "对象正在销毁，取消连接（锁内）");
                        return WebSocketError::CONNECTION_FAILED;
                    }

                    ConnectionState current = state.load(std::memory_order_acquire);
                    if (current != ConnectionState::DISCONNECTED &&
                        current != ConnectionState::CLOSED && current != ConnectionState::ERROR)
                    {
                        LOG_WARN(LOG_TAG, "已经在连接或已连接");
                        return WebSocketError::ALREADY_CONNECTED;
                    }

                    if (ws_thread)
                    {
                        try
                        {
                            if (ws_thread->joinable())
                            {
                                LOG_DEBUG(LOG_TAG, "清理旧的WebSocket线程");
                                ws_thread->join();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_WARN(LOG_TAG, "旧WebSocket线程join异常: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_WARN(LOG_TAG, "旧WebSocket线程join未知异常");
                        }
                        ws_thread.reset();
                    }

                    setState(ConnectionState::CONNECTING);
                    stats.connection_attempts.fetch_add(1, std::memory_order_relaxed);

                    use_tls = (config.url.find("wss://") == 0);

                    try
                    {
                        if (use_tls)
                        {
                            LOG_DEBUG(LOG_TAG, "创建TLS客户端 (wss://)");

                            auto tls_client = std::make_unique<ws_client_type>();

                            tls_client->clear_access_channels(websocketpp::log::alevel::all);
                            if (config.enable_detailed_logging)
                            {
                                tls_client->set_access_channels(websocketpp::log::alevel::app);
                            }
                            tls_client->set_error_channels(websocketpp::log::elevel::all);

                            tls_client->init_asio();

                            std::string hostname = extractHostname(config.url);
                            tls_client->set_tls_init_handler(
                                [this, hostname](websocketpp::connection_hdl hdl)
                                { return onTlsInit(hostname, hdl); });

                            tls_client->set_message_handler(
                                [this](connection_hdl hdl, ws_client_type::message_ptr msg)
                                {
                                    (void)hdl;
                                    auto        opcode  = msg->get_opcode();
                                    std::string payload = msg->get_payload();

                                    stats.messages_received.fetch_add(1, std::memory_order_relaxed);
                                    stats.bytes_received.fetch_add(payload.size(),
                                                                   std::memory_order_relaxed);

                                    if (opcode == websocketpp::frame::opcode::binary)
                                    {
                                        invokeBinaryCallback(payload.data(), payload.size());
                                    }
                                    else
                                    {
                                        if (config.enable_detailed_logging)
                                        {
                                            LOG_DEBUG(LOG_TAG, "<- 文本消息: %s", payload.c_str());
                                        }
                                        invokeTextCallback(payload.data(), payload.size());
                                    }
                                });

                            tls_client->set_open_handler(
                                [this](connection_hdl hdl)
                                {
                                    connection_handle = hdl;
                                    setState(ConnectionState::CONNECTED);
                                    LOG_INFO(LOG_TAG, "已连接");

                                    if (!config.hello_message.empty())
                                    {
                                        WebSocketError err = sendTextInternal(config.hello_message);
                                        if (err == WebSocketError::NONE)
                                        {
                                            handshaked.store(true, std::memory_order_release);
                                            reconnect_count.store(0, std::memory_order_release);
                                            setState(ConnectionState::HANDSHAKED);
                                            LOG_INFO(LOG_TAG, "握手完成");
                                        }
                                        else
                                        {
                                            LOG_ERROR(LOG_TAG, "Hello发送失败");
                                        }
                                    }
                                    else
                                    {
                                        handshaked.store(true, std::memory_order_release);
                                        reconnect_count.store(0, std::memory_order_release);
                                        setState(ConnectionState::HANDSHAKED);
                                        LOG_INFO(LOG_TAG, "连接就绪（无 Hello）");
                                    }
                                });

                            tls_client->set_close_handler(
                                [this](connection_hdl hdl)
                                {
                                    setState(ConnectionState::CLOSED);
                                    handshaked.store(false, std::memory_order_release);

                                    if (auto* tls_client_ptr =
                                            std::get_if<std::unique_ptr<ws_client_type>>(&client);
                                        tls_client_ptr && *tls_client_ptr)
                                    {
                                        ws_client_type::connection_ptr con =
                                            (*tls_client_ptr)->get_con_from_hdl(hdl);
                                        LOG_INFO(LOG_TAG, "关闭 code=%d %s",
                                                 con->get_remote_close_code(),
                                                 con->get_remote_close_reason().c_str());
                                    }

                                    scheduleReconnect();
                                });

                            tls_client->set_fail_handler(
                                [this](connection_hdl hdl)
                                {
                                    setState(ConnectionState::ERROR);
                                    stats.connection_failures.fetch_add(1,
                                                                        std::memory_order_relaxed);

                                    if (auto* tls_client_ptr =
                                            std::get_if<std::unique_ptr<ws_client_type>>(&client);
                                        tls_client_ptr && *tls_client_ptr)
                                    {
                                        ws_client_type::connection_ptr con =
                                            (*tls_client_ptr)->get_con_from_hdl(hdl);
                                        std::string error_msg = con->get_ec().message();
                                        LOG_ERROR(LOG_TAG, "连接失败: %s", error_msg.c_str());
                                        invokeErrorCallback(WebSocketError::CONNECTION_FAILED,
                                                            error_msg);
                                    }

                                    scheduleReconnect();
                                });

                            websocketpp::lib::error_code   ec;
                            ws_client_type::connection_ptr con =
                                tls_client->get_connection(config.url, ec);

                            if (ec)
                            {
                                LOG_ERROR(LOG_TAG, "创建连接失败: %s", ec.message().c_str());
                                setState(ConnectionState::ERROR);
                                stats.connection_failures.fetch_add(1, std::memory_order_relaxed);
                                return WebSocketError::CONNECTION_FAILED;
                            }

                            for (const auto& [key, value] : config.headers)
                            {
                                if (should_stop.load(std::memory_order_acquire))
                                {
                                    LOG_DEBUG(LOG_TAG, "对象正在销毁，取消header设置");
                                    return WebSocketError::CONNECTION_FAILED;
                                }
                                con->append_header(key, value);
                                LOG_DEBUG(LOG_TAG, "Header: %s = %s", key.c_str(), value.c_str());
                            }

                            client = std::move(tls_client);

                            if (auto* tls_client_ptr =
                                    std::get_if<std::unique_ptr<ws_client_type>>(&client);
                                tls_client_ptr && *tls_client_ptr)
                            {
                                (*tls_client_ptr)->connect(con);
                            }
                        }
                        else
                        {
                            LOG_DEBUG(LOG_TAG, "创建非TLS客户端 (ws://)");

                            auto nontls_client = std::make_unique<ws_client_nontls_type>();

                            nontls_client->clear_access_channels(websocketpp::log::alevel::all);
                            if (config.enable_detailed_logging)
                            {
                                nontls_client->set_access_channels(websocketpp::log::alevel::app);
                            }
                            nontls_client->set_error_channels(websocketpp::log::elevel::all);

                            nontls_client->init_asio();

                            nontls_client->set_message_handler(
                                [this](connection_hdl hdl, ws_client_nontls_type::message_ptr msg)
                                {
                                    (void)hdl;
                                    auto        opcode  = msg->get_opcode();
                                    std::string payload = msg->get_payload();

                                    stats.messages_received.fetch_add(1, std::memory_order_relaxed);
                                    stats.bytes_received.fetch_add(payload.size(),
                                                                   std::memory_order_relaxed);

                                    if (opcode == websocketpp::frame::opcode::binary)
                                    {
                                        invokeBinaryCallback(payload.data(), payload.size());
                                    }
                                    else
                                    {
                                        if (config.enable_detailed_logging)
                                        {
                                            LOG_DEBUG(LOG_TAG, "<- 文本消息: %s", payload.c_str());
                                        }
                                        invokeTextCallback(payload.data(), payload.size());
                                    }
                                });

                            nontls_client->set_open_handler(
                                [this](connection_hdl hdl)
                                {
                                    connection_handle = hdl;
                                    setState(ConnectionState::CONNECTED);
                                    LOG_INFO(LOG_TAG, "已连接");

                                    if (!config.hello_message.empty())
                                    {
                                        WebSocketError err = sendTextInternal(config.hello_message);
                                        if (err == WebSocketError::NONE)
                                        {
                                            handshaked.store(true, std::memory_order_release);
                                            reconnect_count.store(0, std::memory_order_release);
                                            setState(ConnectionState::HANDSHAKED);
                                            LOG_INFO(LOG_TAG, "握手完成");
                                        }
                                        else
                                        {
                                            LOG_ERROR(LOG_TAG, "Hello发送失败");
                                        }
                                    }
                                    else
                                    {
                                        handshaked.store(true, std::memory_order_release);
                                        reconnect_count.store(0, std::memory_order_release);
                                        setState(ConnectionState::HANDSHAKED);
                                        LOG_INFO(LOG_TAG, "连接就绪（无 Hello）");
                                    }
                                });

                            nontls_client->set_close_handler(
                                [this](connection_hdl hdl)
                                {
                                    setState(ConnectionState::CLOSED);
                                    handshaked.store(false, std::memory_order_release);

                                    if (auto* nontls_client_ptr =
                                            std::get_if<std::unique_ptr<ws_client_nontls_type>>(
                                                &client);
                                        nontls_client_ptr && *nontls_client_ptr)
                                    {
                                        ws_client_nontls_type::connection_ptr con =
                                            (*nontls_client_ptr)->get_con_from_hdl(hdl);
                                        LOG_INFO(LOG_TAG, "关闭 code=%d %s",
                                                 con->get_remote_close_code(),
                                                 con->get_remote_close_reason().c_str());
                                    }

                                    scheduleReconnect();
                                });

                            nontls_client->set_fail_handler(
                                [this](connection_hdl hdl)
                                {
                                    setState(ConnectionState::ERROR);
                                    stats.connection_failures.fetch_add(1,
                                                                        std::memory_order_relaxed);

                                    if (auto* nontls_client_ptr =
                                            std::get_if<std::unique_ptr<ws_client_nontls_type>>(
                                                &client);
                                        nontls_client_ptr && *nontls_client_ptr)
                                    {
                                        ws_client_nontls_type::connection_ptr con =
                                            (*nontls_client_ptr)->get_con_from_hdl(hdl);
                                        std::string error_msg = con->get_ec().message();
                                        LOG_ERROR(LOG_TAG, "连接失败: %s", error_msg.c_str());
                                        invokeErrorCallback(WebSocketError::CONNECTION_FAILED,
                                                            error_msg);
                                    }

                                    scheduleReconnect();
                                });

                            websocketpp::lib::error_code          ec;
                            ws_client_nontls_type::connection_ptr con =
                                nontls_client->get_connection(config.url, ec);

                            if (ec)
                            {
                                LOG_ERROR(LOG_TAG, "创建连接失败: %s", ec.message().c_str());
                                setState(ConnectionState::ERROR);
                                stats.connection_failures.fetch_add(1, std::memory_order_relaxed);
                                return WebSocketError::CONNECTION_FAILED;
                            }

                            for (const auto& [key, value] : config.headers)
                            {
                                if (should_stop.load(std::memory_order_acquire))
                                {
                                    LOG_DEBUG(LOG_TAG, "对象正在销毁，取消header设置");
                                    return WebSocketError::CONNECTION_FAILED;
                                }
                                con->append_header(key, value);
                                LOG_DEBUG(LOG_TAG, "Header: %s = %s", key.c_str(), value.c_str());
                            }

                            client = std::move(nontls_client);

                            if (auto* nontls_client_ptr =
                                    std::get_if<std::unique_ptr<ws_client_nontls_type>>(&client);
                                nontls_client_ptr && *nontls_client_ptr)
                            {
                                (*nontls_client_ptr)->connect(con);
                            }
                        }

                        try
                        {
                            if (should_stop.load(std::memory_order_acquire))
                            {
                                LOG_DEBUG(LOG_TAG, "对象正在销毁，取消WebSocket线程创建");
                                setState(ConnectionState::ERROR);
                                return WebSocketError::CONNECTION_FAILED;
                            }

                            ws_thread = std::make_unique<std::thread>(
                                [this]()
                                {
                                    std::set_terminate(
                                        []() { LOG_ERROR(LOG_TAG, "WebSocket线程意外终止"); });

                                    try
                                    {
                                        LOG_DEBUG(LOG_TAG, "WebSocket线程已启动");

                                        if (should_stop.load(std::memory_order_acquire))
                                        {
                                            LOG_DEBUG(LOG_TAG, "对象正在销毁，跳过client run");
                                            return;
                                        }

                                        if (use_tls)
                                        {
                                            auto* tls_client_ptr =
                                                std::get_if<std::unique_ptr<ws_client_type>>(
                                                    &client);
                                            if (tls_client_ptr && *tls_client_ptr)
                                            {
                                                (*tls_client_ptr)->run();
                                            }
                                        }
                                        else
                                        {
                                            auto* nontls_client_ptr =
                                                std::get_if<std::unique_ptr<ws_client_nontls_type>>(
                                                    &client);
                                            if (nontls_client_ptr && *nontls_client_ptr)
                                            {
                                                (*nontls_client_ptr)->run();
                                            }
                                        }
                                    }
                                    catch (const std::exception& e)
                                    {
                                        LOG_ERROR(LOG_TAG, "WebSocket线程异常: %s", e.what());
                                    }
                                    catch (...)
                                    {
                                        LOG_ERROR(LOG_TAG, "WebSocket线程未知异常");
                                    }

                                    LOG_DEBUG(LOG_TAG, "WebSocket线程已停止");
                                });
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "创建WebSocket线程失败: %s", e.what());
                            setState(ConnectionState::ERROR);
                            return WebSocketError::CONNECTION_FAILED;
                        }
                        catch (...)
                        {
                            LOG_ERROR(LOG_TAG, "创建WebSocket线程未知异常");
                            setState(ConnectionState::ERROR);
                            return WebSocketError::CONNECTION_FAILED;
                        }

                        LOG_INFO(LOG_TAG, "连接 %s", config.url.c_str());
                        return WebSocketError::NONE;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "连接异常: %s", e.what());
                        setState(ConnectionState::ERROR);
                        stats.connection_failures.fetch_add(1, std::memory_order_relaxed);
                        return WebSocketError::CONNECTION_FAILED;
                    }
                }

                void disconnectInternal()
                {
                    std::lock_guard<std::mutex> lock(mutex);

                    should_reconnect.store(false, std::memory_order_release);
                    should_stop.store(true, std::memory_order_release);
                    reconnect_cv.notify_all();

                    if (reconnect_thread)
                    {
                        try
                        {
                            if (reconnect_thread->joinable())
                            {
                                reconnect_thread->join();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_WARN(LOG_TAG, "重连线程join: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_WARN(LOG_TAG, "重连线程join异常");
                        }
                        reconnect_thread.reset();
                    }

                    ConnectionState current = state.load(std::memory_order_acquire);

                    if (current == ConnectionState::CONNECTED ||
                        current == ConnectionState::HANDSHAKED)
                    {

                        setState(ConnectionState::CLOSING);

                        try
                        {
                            websocketpp::lib::error_code ec;
                            if (use_tls)
                            {
                                auto* tls_client_ptr =
                                    std::get_if<std::unique_ptr<ws_client_type>>(&client);
                                if (tls_client_ptr && *tls_client_ptr)
                                {
                                    (*tls_client_ptr)
                                        ->close(connection_handle,
                                                websocketpp::close::status::normal,
                                                "Client disconnect", ec);
                                }
                            }
                            else
                            {
                                auto* nontls_client_ptr =
                                    std::get_if<std::unique_ptr<ws_client_nontls_type>>(&client);
                                if (nontls_client_ptr && *nontls_client_ptr)
                                {
                                    (*nontls_client_ptr)
                                        ->close(connection_handle,
                                                websocketpp::close::status::normal,
                                                "Client disconnect", ec);
                                }
                            }

                            if (ec)
                            {
                                LOG_WARN(LOG_TAG, "关闭错误: %s", ec.message().c_str());
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERROR(LOG_TAG, "断开连接异常: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_ERROR(LOG_TAG, "断开连接未知异常");
                        }
                    }

                    if (ws_thread)
                    {
                        try
                        {
                            if (ws_thread->joinable())
                            {
                                ws_thread->join();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_WARN(LOG_TAG, "断开连接时WebSocket线程join异常: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_WARN(LOG_TAG, "断开连接时WebSocket线程join未知异常");
                        }
                        ws_thread.reset();
                    }

                    setState(ConnectionState::DISCONNECTED);
                    handshaked.store(false, std::memory_order_release);

                    LOG_INFO(LOG_TAG, "已断开");
                }

                // ========================================================================
                // 消息发送
                // ========================================================================

                WebSocketError sendBinaryInternal(const char* data, size_t size)
                {
                    std::lock_guard<std::mutex> lock(mutex);

                    ConnectionState current = state.load(std::memory_order_acquire);
                    if (current != ConnectionState::CONNECTED &&
                        current != ConnectionState::HANDSHAKED)
                    {
                        LOG_WARN(LOG_TAG, "无法发送，未连接 (状态: %s)",
                                 stateToString(current).c_str());
                        return WebSocketError::NOT_CONNECTED;
                    }

                    try
                    {
                        websocketpp::lib::error_code ec;
                        if (use_tls)
                        {
                            auto* tls_client_ptr =
                                std::get_if<std::unique_ptr<ws_client_type>>(&client);
                            if (!tls_client_ptr || !*tls_client_ptr)
                            {
                                return WebSocketError::NOT_CONNECTED;
                            }
                            (*tls_client_ptr)
                                ->send(connection_handle, data, size,
                                       websocketpp::frame::opcode::binary, ec);
                        }
                        else
                        {
                            auto* nontls_client_ptr =
                                std::get_if<std::unique_ptr<ws_client_nontls_type>>(&client);
                            if (!nontls_client_ptr || !*nontls_client_ptr)
                            {
                                return WebSocketError::NOT_CONNECTED;
                            }
                            (*nontls_client_ptr)
                                ->send(connection_handle, data, size,
                                       websocketpp::frame::opcode::binary, ec);
                        }

                        if (ec)
                        {
                            /* invalid state 为断开时的正常情况，不按错误处理 */
                            if (ec.message().find("invalid state") == std::string::npos)
                                LOG_ERROR(LOG_TAG, "发送二进制消息失败: %s", ec.message().c_str());
                            else
                                LOG_DEBUG(LOG_TAG, "发送跳过: %s", ec.message().c_str());
                            stats.send_errors.fetch_add(1, std::memory_order_relaxed);
                            return WebSocketError::SEND_FAILED;
                        }

                        stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
                        stats.bytes_sent.fetch_add(size, std::memory_order_relaxed);

                        return WebSocketError::NONE;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "发送二进制消息异常: %s", e.what());
                        stats.send_errors.fetch_add(1, std::memory_order_relaxed);
                        return WebSocketError::SEND_FAILED;
                    }
                }

                WebSocketError sendTextInternal(const std::string& message)
                {
                    std::lock_guard<std::mutex> lock(mutex);

                    ConnectionState current = state.load(std::memory_order_acquire);
                    if (current != ConnectionState::CONNECTED &&
                        current != ConnectionState::HANDSHAKED)
                    {
                        LOG_WARN(LOG_TAG, "无法发送，未连接");
                        return WebSocketError::NOT_CONNECTED;
                    }

                    try
                    {
                        websocketpp::lib::error_code ec;
                        if (use_tls)
                        {
                            auto* tls_client_ptr =
                                std::get_if<std::unique_ptr<ws_client_type>>(&client);
                            if (!tls_client_ptr || !*tls_client_ptr)
                            {
                                return WebSocketError::NOT_CONNECTED;
                            }
                            (*tls_client_ptr)
                                ->send(connection_handle, message, websocketpp::frame::opcode::text,
                                       ec);
                        }
                        else
                        {
                            auto* nontls_client_ptr =
                                std::get_if<std::unique_ptr<ws_client_nontls_type>>(&client);
                            if (!nontls_client_ptr || !*nontls_client_ptr)
                            {
                                return WebSocketError::NOT_CONNECTED;
                            }
                            (*nontls_client_ptr)
                                ->send(connection_handle, message, websocketpp::frame::opcode::text,
                                       ec);
                        }

                        if (ec)
                        {
                            LOG_ERROR(LOG_TAG, "发送文本消息失败: %s", ec.message().c_str());
                            stats.send_errors.fetch_add(1, std::memory_order_relaxed);
                            return WebSocketError::SEND_FAILED;
                        }

                        stats.messages_sent.fetch_add(1, std::memory_order_relaxed);
                        stats.bytes_sent.fetch_add(message.size(), std::memory_order_relaxed);

                        return WebSocketError::NONE;
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "发送文本消息异常: %s", e.what());
                        stats.send_errors.fetch_add(1, std::memory_order_relaxed);
                        return WebSocketError::SEND_FAILED;
                    }
                }

                void scheduleReconnect()
                {
                    if (!config.auto_reconnect)
                    {
                        LOG_DEBUG(LOG_TAG, "重连已禁用");
                        return;
                    }

                    if (should_stop.load(std::memory_order_acquire))
                    {
                        LOG_DEBUG(LOG_TAG, "停止，跳过重连");
                        return;
                    }

                    int current_count = reconnect_count.load(std::memory_order_acquire);
                    if (config.max_reconnect_attempts > 0 &&
                        current_count >= config.max_reconnect_attempts)
                    {
                        LOG_ERROR(LOG_TAG, "重连达上限 %d", current_count);
                        invokeErrorCallback(WebSocketError::RECONNECT_FAILED, "超过最大重连次数");
                        return;
                    }

                    should_reconnect.store(true, std::memory_order_release);

                    if (reconnect_thread)
                    {
                        try
                        {
                            if (reconnect_thread->joinable())
                            {
                                reconnect_thread->join();
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_WARN(LOG_TAG, "旧重连线程join异常: %s", e.what());
                        }
                        catch (...)
                        {
                            LOG_WARN(LOG_TAG, "旧重连线程join未知异常");
                        }
                        reconnect_thread.reset();
                    }

                    try
                    {
                        reconnect_thread = std::make_unique<std::thread>(
                            [this]()
                            {
                                std::set_terminate([]()
                                                   { LOG_ERROR(LOG_TAG, "重连线程意外终止"); });

                                try
                                {
                                    LOG_INFO(LOG_TAG, "%dms后重连", config.reconnect_interval_ms);

                                    std::unique_lock<std::mutex> lock(reconnect_mutex);

                                    bool cancelled = reconnect_cv.wait_for(
                                        lock,
                                        std::chrono::milliseconds(config.reconnect_interval_ms),
                                        [this]()
                                        {
                                            try
                                            {
                                                return !should_reconnect.load(
                                                           std::memory_order_acquire) ||
                                                       should_stop.load(std::memory_order_acquire);
                                            }
                                            catch (...)
                                            {
                                                LOG_ERROR(LOG_TAG, "重连条件检查异常");
                                                return true;
                                            }
                                        });

                                    if (cancelled || should_stop.load(std::memory_order_acquire))
                                    {
                                        LOG_DEBUG(LOG_TAG, "重连已取消");
                                        return;
                                    }

                                    if (should_reconnect.load(std::memory_order_acquire))
                                    {
                                        int count = reconnect_count.fetch_add(
                                                        1, std::memory_order_acq_rel) +
                                                    1;
                                        stats.reconnections.fetch_add(1, std::memory_order_relaxed);

                                        LOG_INFO(LOG_TAG, "重连 #%d", count);

                                        if (should_stop.load(std::memory_order_acquire))
                                        {
                                            LOG_DEBUG(LOG_TAG, "对象正在销毁，取消重连");
                                            should_reconnect.store(false,
                                                                   std::memory_order_release);
                                            return;
                                        }

                                        lock.unlock();
                                        should_reconnect.store(false, std::memory_order_release);

                                        WebSocketError err = connectInternal();
                                        if (err != WebSocketError::NONE)
                                        {
                                            LOG_WARN(LOG_TAG, "重连失败 %d", static_cast<int>(err));
                                        }
                                    }
                                }
                                catch (const std::exception& e)
                                {
                                    LOG_ERROR(LOG_TAG, "重连线程严重异常: %s", e.what());
                                    should_reconnect.store(false, std::memory_order_release);
                                }
                                catch (...)
                                {
                                    LOG_ERROR(LOG_TAG, "重连线程严重未知异常");
                                    should_reconnect.store(false, std::memory_order_release);
                                }
                            });
                    }
                    catch (const std::exception& e)
                    {
                        LOG_ERROR(LOG_TAG, "创建重连线程失败: %s", e.what());
                        should_reconnect.store(false, std::memory_order_release);
                    }
                    catch (...)
                    {
                        LOG_ERROR(LOG_TAG, "创建重连线程未知异常");
                        should_reconnect.store(false, std::memory_order_release);
                    }
                }

                // ========================================================================
                // 工具函数
                // ========================================================================

                std::string extractHostname(const std::string& url) const
                {
                    std::string hostname = url;

                    size_t start = hostname.find("://");
                    if (start != std::string::npos)
                    {
                        hostname = hostname.substr(start + 3);
                    }

                    size_t end = hostname.find("/");
                    if (end != std::string::npos)
                    {
                        hostname = hostname.substr(0, end);
                    }

                    end = hostname.find(":");
                    if (end != std::string::npos)
                    {
                        hostname = hostname.substr(0, end);
                    }

                    return hostname;
                }
            };

            WebSocketClient::WebSocketClient(WebSocketConfig config)
                : pImpl_(std::make_unique<Impl>(std::move(config)))
            {
                LOG_INFO(LOG_TAG, "已创建");
            }

            WebSocketClient::~WebSocketClient()
            {

                try
                {
                    logStats();
                }
                catch (const std::exception& e)
                {
                    LOG_WARN(LOG_TAG, "统计异常: %s", e.what());
                }
                catch (...)
                {
                    LOG_WARN(LOG_TAG, "统计异常");
                }

                try
                {
                    pImpl_.reset();
                }
                catch (const std::exception& e)
                {
                    LOG_WARN(LOG_TAG, "清理异常: %s", e.what());
                }
                catch (...)
                {
                    LOG_WARN(LOG_TAG, "清理未知异常");
                }
            }

            WebSocketError WebSocketClient::connect()
            {
                return pImpl_->connectInternal();
            }

            void WebSocketClient::disconnect()
            {
                pImpl_->disconnectInternal();
            }

            WebSocketError WebSocketClient::reconnect()
            {
                disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                pImpl_->reconnect_count.store(0, std::memory_order_release);
                return connect();
            }

            bool WebSocketClient::shouldReconnect() const
            {
                return pImpl_->should_reconnect.load(std::memory_order_acquire);
            }

            void WebSocketClient::processReconnect()
            {
                if (shouldReconnect())
                {
                    LOG_INFO(LOG_TAG, "执行重连");
                    connect();
                }
            }

            WebSocketError WebSocketClient::sendBinary(const char* data, size_t size)
            {
                return pImpl_->sendBinaryInternal(data, size);
            }

            WebSocketError WebSocketClient::sendText(const char* data, size_t size)
            {
                std::string message(data, size);
                return pImpl_->sendTextInternal(message);
            }

            WebSocketError WebSocketClient::sendText(const std::string& message)
            {
                return pImpl_->sendTextInternal(message);
            }

            void WebSocketClient::setBinaryCallback(MessageCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->binary_callback = callback;
            }

            void WebSocketClient::setTextCallback(MessageCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->text_callback = callback;
            }

            void WebSocketClient::setStateCallback(ConnectionStateCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->state_callback = callback;
            }

            void WebSocketClient::setErrorCallback(WebSocketErrorCallback callback)
            {
                std::lock_guard<std::mutex> lock(pImpl_->callback_mutex);
                pImpl_->error_callback = callback;
            }

            ConnectionState WebSocketClient::getState() const
            {
                return pImpl_->state.load(std::memory_order_acquire);
            }

            bool WebSocketClient::isConnected() const
            {
                ConnectionState current = getState();
                return (current == ConnectionState::CONNECTED ||
                        current == ConnectionState::HANDSHAKED);
            }

            bool WebSocketClient::isHandshaked() const
            {
                return pImpl_->handshaked.load(std::memory_order_acquire);
            }

            int WebSocketClient::getReconnectCount() const
            {
                return pImpl_->reconnect_count.load(std::memory_order_acquire);
            }

            void WebSocketClient::setUrl(const std::string& url)
            {
                pImpl_->config.url = url;
            }

            void WebSocketClient::addHeader(const std::string& key, const std::string& value)
            {
                pImpl_->config.headers[key] = value;
            }

            void WebSocketClient::setHelloMessage(const std::string& hello_msg)
            {
                pImpl_->config.hello_message = hello_msg;
            }

            void WebSocketClient::setAutoReconnect(bool enable)
            {
                pImpl_->config.auto_reconnect = enable;
            }

            const WebSocketConfig& WebSocketClient::getConfig() const
            {
                return pImpl_->config;
            }

            void WebSocketClient::getStats(Stats& out_stats) const
            {
                out_stats.messages_sent.store(pImpl_->stats.messages_sent.load());
                out_stats.messages_received.store(pImpl_->stats.messages_received.load());
                out_stats.bytes_sent.store(pImpl_->stats.bytes_sent.load());
                out_stats.bytes_received.store(pImpl_->stats.bytes_received.load());
                out_stats.send_errors.store(pImpl_->stats.send_errors.load());
                out_stats.connection_attempts.store(pImpl_->stats.connection_attempts.load());
                out_stats.connection_failures.store(pImpl_->stats.connection_failures.load());
                out_stats.reconnections.store(pImpl_->stats.reconnections.load());
                out_stats.callback_exceptions.store(pImpl_->stats.callback_exceptions.load());
                out_stats.total_uptime_us.store(pImpl_->stats.total_uptime_us.load());
            }

            void WebSocketClient::resetStats()
            {
                pImpl_->stats.messages_sent.store(0);
                pImpl_->stats.messages_received.store(0);
                pImpl_->stats.bytes_sent.store(0);
                pImpl_->stats.bytes_received.store(0);
                pImpl_->stats.send_errors.store(0);
                pImpl_->stats.connection_attempts.store(0);
                pImpl_->stats.connection_failures.store(0);
                pImpl_->stats.reconnections.store(0);
                pImpl_->stats.callback_exceptions.store(0);
                pImpl_->stats.total_uptime_us.store(0);
            }

            void WebSocketClient::logStats() const
            {
                uint64_t sent          = pImpl_->stats.messages_sent.load();
                uint64_t received      = pImpl_->stats.messages_received.load();
                uint64_t bytes_s       = pImpl_->stats.bytes_sent.load();
                uint64_t bytes_r       = pImpl_->stats.bytes_received.load();
                uint64_t send_err      = pImpl_->stats.send_errors.load();
                uint64_t conn_attempts = pImpl_->stats.connection_attempts.load();
                uint64_t conn_failures = pImpl_->stats.connection_failures.load();
                uint64_t reconnects    = pImpl_->stats.reconnections.load();
                uint64_t exceptions    = pImpl_->stats.callback_exceptions.load();
                uint64_t uptime_us     = pImpl_->stats.total_uptime_us.load();

                LOG_INFO(LOG_TAG,
                         "统计 tx=%llu rx=%llu 字节 tx=%llu rx=%llu 连接=%llu/%llu 重连=%llu "
                         "异常=%llu 时长=%lluus",
                         sent, received, bytes_s, bytes_r, conn_attempts, conn_failures, reconnects,
                         exceptions, uptime_us);

                if (conn_attempts > 0)
                {
                    double fr = (double)conn_failures / conn_attempts * 100.0;
                    if (fr > 20.0)
                        LOG_WARN(LOG_TAG, "连接失败率 %.1f%%", fr);
                }

                if (sent > 0 && (double)send_err / sent * 100.0 > 1.0)
                {
                    LOG_WARN(LOG_TAG, "发送错误率 %.1f%%", (double)send_err / sent * 100.0);
                }
            }

        } // namespace websocket
    }     // namespace protocol
} // namespace app
