/*
 * websocket_factory_impl.hpp - WebSocket 工厂实现
 */

#pragma once

#include "../interfaces/iwebsocket_factory.hpp"
#include "websocket_client_impl.hpp"

namespace app
{

    class WebSocketClientFactoryImpl : public IWebSocketClientFactory
    {
    public:
        std::unique_ptr<IWebSocketClient> create(const WsConfig& config) override;
    };

} // namespace app
