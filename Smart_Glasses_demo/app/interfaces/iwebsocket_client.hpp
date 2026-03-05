/*
 * iwebsocket_client.hpp - WebSocket 客户端接口
 */

#pragma once

#include <string>
#include <functional>
#include <map>

namespace app
{

    enum class WsConnectionState
    {
        DISCONNECTED = 0,
        CONNECTING,
        CONNECTED,
        HANDSHAKED,
        CLOSING,
        CLOSED,
        ERROR
    };

    enum class WsError
    {
        NONE = 0,
        CONNECTION_FAILED,
        SEND_FAILED,
        RECEIVE_FAILED,
        TLS_INIT_FAILED,
        ALREADY_CONNECTED,
        NOT_CONNECTED,
        INVALID_URL,
        TIMEOUT,
        UNKNOWN
    };

    struct WsConfig
    {
        std::string                        url;
        std::map<std::string, std::string> headers;
        std::string                        hello_message;
        bool                               auto_reconnect     = false;
        int                                connect_timeout_ms = 10000;
        bool                               verify_ssl         = false;
    };

    using WsTextCb   = std::function<bool(const char* data, size_t size)>;
    using WsBinaryCb = std::function<bool(const char* data, size_t size)>;
    using WsStateCb  = std::function<void(WsConnectionState old_s, WsConnectionState new_s)>;
    using WsErrorCb  = std::function<void(WsError err, const std::string& msg)>;

    class IWebSocketClient
    {
    public:
        virtual ~IWebSocketClient() = default;

        virtual WsError connect()                                 = 0;
        virtual void    disconnect()                              = 0;
        virtual WsError sendText(const std::string& msg)          = 0;
        virtual WsError sendBinary(const char* data, size_t size) = 0;
        virtual bool    isHandshaked() const                      = 0;

        virtual void setTextCallback(WsTextCb cb)     = 0;
        virtual void setBinaryCallback(WsBinaryCb cb) = 0;
        virtual void setStateCallback(WsStateCb cb)   = 0;
        virtual void setErrorCallback(WsErrorCb cb)   = 0;
    };

} // namespace app
