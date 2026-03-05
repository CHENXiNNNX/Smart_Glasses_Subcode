/*
 * websocket_factory_impl.cc - WebSocket 工厂
 */

#include "websocket_factory_impl.hpp"

namespace app
{

    std::unique_ptr<IWebSocketClient> WebSocketClientFactoryImpl::create(const WsConfig& config)
    {
        return std::make_unique<WebSocketClientImpl>(config);
    }

} // namespace app
