// /**
//  * @file websocket.cc
//  * @brief WebSocket客户端模块实现
//  * @details 基于websocketpp的WebSocket实现
//  */

// #include "websocket.h"
// #include <websocketpp/config/asio_client.hpp>
// #include <websocketpp/client.hpp>
// #include <nlohmann/json.hpp>

// #include <iostream>
// #include <thread>
// #include <chrono>
// #include <atomic>
// #include <mutex>
// #include <memory>

// namespace glasses {
// namespace protocol {
// namespace websocket {

// // WebSocket客户端类型定义
// typedef websocketpp::client<websocketpp::config::asio_tls_client> ws_client;
// typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;
// typedef websocketpp::connection_hdl connection_hdl;

// using json = nlohmann::json;

// /**
//  * @brief WebSocket客户端的内部实现（Pimpl惯用法）
//  */
// class WebSocketClient::Impl {
// public:
//     WebSocketConfig config;
//     std::unique_ptr<ws_client> client;
//     connection_hdl hdl;
    
//     MessageCallback binary_callback;
//     MessageCallback text_callback;
    
//     std::atomic<ConnectionState> state;
//     std::atomic<bool> handshaked;
//     std::atomic<bool> should_reconnect;
//     std::atomic<bool> is_running;
    
//     std::mutex mutex;
//     std::thread ws_thread;
//     std::thread reconnect_thread;

//     Impl(const WebSocketConfig& cfg)
//         : config(cfg)
//         , client(nullptr)
//         , binary_callback(nullptr)
//         , text_callback(nullptr)
//         , state(ConnectionState::DISCONNECTED)
//         , handshaked(false)
//         , should_reconnect(false)
//         , is_running(false) {
//     }

//     ~Impl() {
//         cleanup();
//     }

//     void cleanup() {
//         should_reconnect = false;
//         is_running = false;
        
//         if (client) {
//             try {
//                 client->stop();
//             } catch(...) {}
//         }

//         if (reconnect_thread.joinable()) {
//             reconnect_thread.join();
//         }

//         if (ws_thread.joinable()) {
//             ws_thread.join();
//         }
//     }

//     // TLS验证相关函数
//     static bool verify_subject_alternative_name(const char* hostname, X509* cert) {
//         STACK_OF(GENERAL_NAME)* san_names = nullptr;
        
//         san_names = (STACK_OF(GENERAL_NAME)*) X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr);
//         if (san_names == nullptr) {
//             return false;
//         }
        
//         int san_names_count = sk_GENERAL_NAME_num(san_names);
//         bool result = false;
        
//         for (int i = 0; i < san_names_count; i++) {
//             const GENERAL_NAME* current_name = sk_GENERAL_NAME_value(san_names, i);
            
//             if (current_name->type != GEN_DNS) {
//                 continue;
//             }
            
//             const char* dns_name = (const char*) ASN1_STRING_get0_data(current_name->d.dNSName);
            
//             if (ASN1_STRING_length(current_name->d.dNSName) != strlen(dns_name)) {
//                 break;
//             }
            
//             result = (strcasecmp(hostname, dns_name) == 0);
//         }
//         sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
        
//         return result;
//     }

//     static bool verify_common_name(const char* hostname, X509* cert) {
//         int common_name_loc = X509_NAME_get_index_by_NID(X509_get_subject_name(cert), NID_commonName, -1);
//         if (common_name_loc < 0) {
//             return false;
//         }
        
//         X509_NAME_ENTRY* common_name_entry = X509_NAME_get_entry(X509_get_subject_name(cert), common_name_loc);
//         if (common_name_entry == nullptr) {
//             return false;
//         }
        
//         ASN1_STRING* common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
//         if (common_name_asn1 == nullptr) {
//             return false;
//         }
        
//         const char* common_name_str = (const char*) ASN1_STRING_get0_data(common_name_asn1);
        
//         if (ASN1_STRING_length(common_name_asn1) != strlen(common_name_str)) {
//             return false;
//         }
        
//         return (strcasecmp(hostname, common_name_str) == 0);
//     }

//     static bool verify_certificate(const char* hostname, bool preverified, boost::asio::ssl::verify_context& ctx) {
//         int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());

//         if (depth == 0 && preverified) {
//             X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            
//             if (verify_subject_alternative_name(hostname, cert)) {
//                 return true;
//             } else if (verify_common_name(hostname, cert)) {
//                 return true;
//             } else {
//                 return false;
//             }
//         }
     
//         return preverified;
//     }

//     context_ptr on_tls_init(const char* hostname, websocketpp::connection_hdl) {
//         context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);
     
//         try {
//             ctx->set_options(boost::asio::ssl::context::default_workarounds |
//                              boost::asio::ssl::context::no_sslv2 |
//                              boost::asio::ssl::context::no_sslv3 |
//                              boost::asio::ssl::context::single_dh_use);
     
//             // 注释掉TLS验证，避免证书问题
//             // ctx->set_verify_mode(boost::asio::ssl::verify_peer);
//             ctx->set_verify_callback(std::bind(&Impl::verify_certificate, hostname, 
//                                               std::placeholders::_1, std::placeholders::_2));
//         } catch (std::exception& e) {
//             std::cerr << "[WebSocket] TLS init error: " << e.what() << std::endl;
//         }
//         return ctx;
//     }
// };

// // ============================================================================
// // WebSocketClient 公共接口实现
// // ============================================================================

// WebSocketClient::WebSocketClient(const WebSocketConfig& config)
//     : pimpl_(new Impl(config)) {
//     std::cout << "[WebSocket] Client created" << std::endl;
// }

// WebSocketClient::~WebSocketClient() {
//     if (pimpl_) {
//         delete pimpl_;
//         pimpl_ = nullptr;
//     }
// }

// void WebSocketClient::setCallbacks(MessageCallback binary_callback,
//                                    MessageCallback text_callback,
//                                    void* user_data) {
//     pimpl_->binary_callback = binary_callback;
//     pimpl_->text_callback = text_callback;
//     if (user_data) {
//         pimpl_->config.user_data = user_data;
//     }
// }

// bool WebSocketClient::connect() {
//     std::lock_guard<std::mutex> lock(pimpl_->mutex);

//     if (pimpl_->state != ConnectionState::DISCONNECTED &&
//         pimpl_->state != ConnectionState::CLOSED) {
//         std::cout << "[WebSocket] Already connecting or connected" << std::endl;
//         return false;
//     }

//     try {
//         // 创建WebSocket客户端
//         pimpl_->client = std::make_unique<ws_client>();
        
//         // 设置日志级别
//         pimpl_->client->clear_access_channels(websocketpp::log::alevel::all);
//         pimpl_->client->set_access_channels(websocketpp::log::alevel::app);
//         pimpl_->client->set_error_channels(websocketpp::log::elevel::all);

//         // 初始化ASIO
//         pimpl_->client->init_asio();

//         // 解析URL提取hostname
//         std::string hostname = pimpl_->config.url;
//         size_t start = hostname.find("://");
//         if (start != std::string::npos) {
//             hostname = hostname.substr(start + 3);
//         }
//         size_t end = hostname.find("/");
//         if (end != std::string::npos) {
//             hostname = hostname.substr(0, end);
//         }
//         end = hostname.find(":");
//         if (end != std::string::npos) {
//             hostname = hostname.substr(0, end);
//         }

//         // 设置消息处理器
//         pimpl_->client->set_message_handler(
//             [this](connection_hdl hdl, ws_client::message_ptr msg) {
//                 auto opcode = msg->get_opcode();
//                 std::string payload = msg->get_payload();

//                 if (opcode == websocketpp::frame::opcode::binary) {
//                     // 二进制消息（TTS音频）
//                     if (pimpl_->binary_callback) {
//                         pimpl_->binary_callback(payload.data(), payload.size(), 
//                                                pimpl_->config.user_data);
//                     }
//                 } else {
//                     // 文本消息（JSON协议）
//                     std::cout << "[WebSocket] Received: " << payload << std::endl;
//                     if (pimpl_->text_callback) {
//                         pimpl_->text_callback(payload.data(), payload.size(), 
//                                              pimpl_->config.user_data);
//                     }
//                 }
//             });

//         // 设置TLS初始化处理器
//         pimpl_->client->set_tls_init_handler(
//             [this, hostname](websocketpp::connection_hdl hdl) {
//                 return pimpl_->on_tls_init(hostname.c_str(), hdl);
//             });

//         // 设置连接打开处理器
//         pimpl_->client->set_open_handler(
//             [this](connection_hdl hdl) {
//                 pimpl_->hdl = hdl;
//                 pimpl_->state = ConnectionState::CONNECTED;
//                 std::cout << "[WebSocket] Connection opened" << std::endl;

//                 // 发送Hello消息
//                 if (!pimpl_->config.hello_message.empty()) {
//                     sendText(pimpl_->config.hello_message.c_str(), 
//                             pimpl_->config.hello_message.length());
//                     pimpl_->handshaked = true;
//                     std::cout << "[WebSocket] Hello message sent" << std::endl;
//                 }
//             });

//         // 设置连接关闭处理器
//         pimpl_->client->set_close_handler(
//             [this](connection_hdl hdl) {
//                 pimpl_->state = ConnectionState::CLOSED;
//                 pimpl_->handshaked = false;
//                 pimpl_->is_running = false;

//                 ws_client::connection_ptr con = pimpl_->client->get_con_from_hdl(hdl);
//                 std::cout << "[WebSocket] Connection closed. Code: " 
//                          << con->get_remote_close_code() << ", Reason: " 
//                          << con->get_remote_close_reason() << std::endl;

//                 // 自动重连逻辑
//                 if (pimpl_->config.auto_reconnect && pimpl_->should_reconnect) {
//                     std::cout << "[WebSocket] Will reconnect in " 
//                              << pimpl_->config.reconnect_interval_ms << "ms" << std::endl;
                    
//                     if (pimpl_->reconnect_thread.joinable()) {
//                         pimpl_->reconnect_thread.join();
//                     }
                    
//                     pimpl_->reconnect_thread = std::thread([this]() {
//                         std::this_thread::sleep_for(
//                             std::chrono::milliseconds(pimpl_->config.reconnect_interval_ms));
//                         if (pimpl_->should_reconnect) {
//                             connect();
//                         }
//                     });
//                 }
//             });

//         // 获取连接
//         websocketpp::lib::error_code ec;
//         ws_client::connection_ptr con = pimpl_->client->get_connection(pimpl_->config.url, ec);
//         if (ec) {
//             std::cerr << "[WebSocket] Could not create connection: " << ec.message() << std::endl;
//             return false;
//         }

//         // 设置HTTP headers
//         for (const auto& [key, value] : pimpl_->config.headers) {
//             con->append_header(key, value);
//             std::cout << "[WebSocket] Header: " << key << " = " << value << std::endl;
//         }

//         // 请求连接
//         pimpl_->client->connect(con);

//         // 启动WebSocket线程
//         pimpl_->state = ConnectionState::CONNECTING;
//         pimpl_->should_reconnect = true;
//         pimpl_->is_running = true;
        
//         std::cout << "[WebSocket] Connecting to " << pimpl_->config.url << std::endl;

//         // 在新线程中运行WebSocket
//         if (pimpl_->ws_thread.joinable()) {
//             pimpl_->ws_thread.join();
//         }
        
//         pimpl_->ws_thread = std::thread([this]() {
//             try {
//                 pimpl_->client->run();
//             } catch (const std::exception& e) {
//                 std::cerr << "[WebSocket] Run error: " << e.what() << std::endl;
//             }
//         });

//         return true;

//     } catch (const std::exception& e) {
//         std::cerr << "[WebSocket] Connect failed: " << e.what() << std::endl;
//         pimpl_->state = ConnectionState::CLOSED;
//         return false;
//     }
// }

// void WebSocketClient::disconnect() {
//     std::lock_guard<std::mutex> lock(pimpl_->mutex);
    
//     pimpl_->should_reconnect = false;
//     pimpl_->state = ConnectionState::CLOSING;

//     if (pimpl_->client) {
//         try {
//             pimpl_->client->close(pimpl_->hdl, websocketpp::close::status::normal, "disconnect");
//             std::cout << "[WebSocket] Disconnecting..." << std::endl;
//         } catch (const std::exception& e) {
//             std::cerr << "[WebSocket] Disconnect error: " << e.what() << std::endl;
//         }
//     }

//     pimpl_->state = ConnectionState::DISCONNECTED;
//     pimpl_->handshaked = false;
// }

// bool WebSocketClient::sendBinary(const char* data, size_t size) {
//     if (!pimpl_->client || pimpl_->state != ConnectionState::CONNECTED) {
//         std::cerr << "[WebSocket] Not connected, cannot send binary" << std::endl;
//         return false;
//     }

//     if (!pimpl_->handshaked) {
//         std::cerr << "[WebSocket] Not handshaked yet, cannot send binary" << std::endl;
//         return false;
//     }

//     try {
//         pimpl_->client->send(pimpl_->hdl, data, size, websocketpp::frame::opcode::binary);
//         return true;
//     } catch (const std::exception& e) {
//         std::cerr << "[WebSocket] Send binary failed: " << e.what() << std::endl;
//         return false;
//     }
// }

// bool WebSocketClient::sendText(const char* data, size_t size) {
//     if (!pimpl_->client || pimpl_->state != ConnectionState::CONNECTED) {
//         std::cerr << "[WebSocket] Not connected, cannot send text" << std::endl;
//         return false;
//     }

//     try {
//         pimpl_->client->send(pimpl_->hdl, data, size, websocketpp::frame::opcode::text);
//         return true;
//     } catch (const std::exception& e) {
//         std::cerr << "[WebSocket] Send text failed: " << e.what() << std::endl;
//         return false;
//     }
// }

// ConnectionState WebSocketClient::getState() const {
//     return pimpl_->state;
// }

// bool WebSocketClient::isConnected() const {
//     return pimpl_->state == ConnectionState::CONNECTED;
// }

// bool WebSocketClient::isHandshaked() const {
//     return pimpl_->handshaked;
// }

// void WebSocketClient::setUrl(const std::string& url) {
//     pimpl_->config.url = url;
// }

// void WebSocketClient::setHeaders(const std::map<std::string, std::string>& headers) {
//     pimpl_->config.headers = headers;
// }

// void WebSocketClient::addHeader(const std::string& key, const std::string& value) {
//     pimpl_->config.headers[key] = value;
// }

// void WebSocketClient::setHelloMessage(const std::string& hello_msg) {
//     pimpl_->config.hello_message = hello_msg;
// }

// void WebSocketClient::setAutoReconnect(bool enable) {
//     pimpl_->config.auto_reconnect = enable;
// }

// // ============================================================================
// // 辅助函数
// // ============================================================================

// WebSocketClient* createXiaozhiClient(
//     const std::string& device_id,
//     const std::string& client_id,
//     MessageCallback binary_cb,
//     MessageCallback text_cb,
//     void* user_data) {
    
//     WebSocketConfig config;
//     config.url = "wss://api.tenclass.net/xiaozhi/v1/";
//     config.auto_reconnect = true;
//     config.reconnect_interval_ms = 5000;
//     config.user_data = user_data;

//     // 设置HTTP headers
//     config.headers["Device-Id"] = device_id;
//     config.headers["Client-Id"] = client_id;
//     config.headers["Protocol-Version"] = "1";
//     config.headers["Authorization"] = "Bearer test-token";

//     // 设置Hello消息
//     config.hello_message = R"({
//         "type": "hello",
//         "version": 1,
//         "transport": "websocket",
//         "features": {
//             "aec": true,
//             "mcp": true
//         },
//         "audio_params": {
//             "format": "opus",
//             "sample_rate": 16000,
//             "channels": 1,
//             "frame_duration": 20
//         }
//     })";

//     WebSocketClient* client = new WebSocketClient(config);
//     client->setCallbacks(binary_cb, text_cb, user_data);

//     std::cout << "[WebSocket] Xiaozhi client created" << std::endl;
//     std::cout << "[WebSocket]   Device-Id: " << device_id << std::endl;
//     std::cout << "[WebSocket]   Client-Id: " << client_id << std::endl;

//     return client;
// }

// } // namespace websocket
// } // namespace protocol
// } // namespace glasses
