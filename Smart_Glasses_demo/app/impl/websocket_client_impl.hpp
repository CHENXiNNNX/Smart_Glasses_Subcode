/*
 * websocket_client_impl.hpp - WebSocket 客户端实现
 */

#pragma once

#include "../interfaces/iwebsocket_client.hpp"
#include <memory>

namespace app::protocol::websocket
{
    class WebSocketClient;
}

namespace app
{

    class WebSocketClientImpl : public IWebSocketClient
    {
    public:
        explicit WebSocketClientImpl(const WsConfig& config);
        ~WebSocketClientImpl() override;

        WsError connect() override;
        void    disconnect() override;
        WsError sendText(const std::string& msg) override;
        WsError sendBinary(const char* data, size_t size) override;
        bool    isHandshaked() const override;

        void setTextCallback(WsTextCb cb) override;
        void setBinaryCallback(WsBinaryCb cb) override;
        void setStateCallback(WsStateCb cb) override;
        void setErrorCallback(WsErrorCb cb) override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app
