/*
 * websocket_client_impl.cc - WebSocket 客户端实现
 */

#include "websocket_client_impl.hpp"
#include "../protocol/websocket/websocket.hpp"

namespace app
{

    static WsConnectionState toWsState(protocol::websocket::ConnectionState s)
    {
        return static_cast<WsConnectionState>(static_cast<int>(s));
    }

    static WsError toWsError(protocol::websocket::WebSocketError e)
    {
        return static_cast<WsError>(static_cast<int>(e));
    }

    struct WebSocketClientImpl::Impl
    {
        std::unique_ptr<protocol::websocket::WebSocketClient> client;
    };

    WebSocketClientImpl::WebSocketClientImpl(const WsConfig& config)
    {
        protocol::websocket::WebSocketConfig ws_cfg;
        ws_cfg.url                = config.url;
        ws_cfg.headers            = config.headers;
        ws_cfg.hello_message      = config.hello_message;
        ws_cfg.auto_reconnect     = config.auto_reconnect;
        ws_cfg.connect_timeout_ms = config.connect_timeout_ms;
        ws_cfg.verify_ssl         = config.verify_ssl;

        impl_         = std::make_unique<Impl>();
        impl_->client = std::make_unique<protocol::websocket::WebSocketClient>(ws_cfg);
    }

    WebSocketClientImpl::~WebSocketClientImpl()
    {
        if (impl_ && impl_->client)
        {
            impl_->client->disconnect();
        }
    }

    WsError WebSocketClientImpl::connect()
    {
        return toWsError(impl_->client->connect());
    }

    void WebSocketClientImpl::disconnect()
    {
        impl_->client->disconnect();
    }

    WsError WebSocketClientImpl::sendText(const std::string& msg)
    {
        return toWsError(impl_->client->sendText(msg));
    }

    WsError WebSocketClientImpl::sendBinary(const char* data, size_t size)
    {
        return toWsError(impl_->client->sendBinary(data, size));
    }

    bool WebSocketClientImpl::isHandshaked() const
    {
        return impl_->client->isHandshaked();
    }

    void WebSocketClientImpl::setTextCallback(WsTextCb cb)
    {
        impl_->client->setTextCallback([cb](const char* d, size_t s) -> bool
                                       { return cb ? cb(d, s) : true; });
    }

    void WebSocketClientImpl::setBinaryCallback(WsBinaryCb cb)
    {
        impl_->client->setBinaryCallback([cb](const char* d, size_t s) -> bool
                                         { return cb ? cb(d, s) : true; });
    }

    void WebSocketClientImpl::setStateCallback(WsStateCb cb)
    {
        impl_->client->setStateCallback(
            [cb](protocol::websocket::ConnectionState o, protocol::websocket::ConnectionState n)
            {
                if (cb)
                    cb(toWsState(o), toWsState(n));
            });
    }

    void WebSocketClientImpl::setErrorCallback(WsErrorCb cb)
    {
        impl_->client->setErrorCallback(
            [cb](protocol::websocket::WebSocketError e, const std::string& m)
            {
                if (cb)
                    cb(toWsError(e), m);
            });
    }

} // namespace app
