/*
 * iwebsocket_factory.hpp - WebSocket 工厂接口
 */

#pragma once

#include "iwebsocket_client.hpp"
#include <memory>

namespace app
{

    class IWebSocketClientFactory
    {
    public:
        virtual ~IWebSocketClientFactory() = default;

        virtual std::unique_ptr<IWebSocketClient> create(const WsConfig& config) = 0;
    };

} // namespace app
