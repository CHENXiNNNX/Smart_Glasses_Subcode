/*
 * test_main.cpp - 统一测试入口，可选测试内容
 *
 * 用法:
 *   ./test_main [选项]
 *   ./test_main          # 交互式选择
 *
 * 选项:
 *   -b, --basic   主程序启动测试 (App init/run)
 *   -w, --webrtc  WebRTC 通话测试
 *   -s, --stream  实时流上传 (JPEG+Opus, WebSocket)
 *   -h, --help    显示帮助
 */

#include "app/app.hpp"
#include "app/media/audio/audio.hpp"
#include "app/media/camera/camera.hpp"
#include "app/media/media_config.hpp"
#include "app/media/sync.hpp"
#include "app/network/wifi/wifi.hpp"
#include "app/protocol/websocket/websocket.hpp"
#include "app/protocol/webrtc/signaling.hpp"
#include "app/protocol/webrtc/webrtc.hpp"
#include "app/tool/log/log.hpp"
#include "app/tool/time/time.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "TEST";
    app::App*             g_app   = nullptr;

    void signal_handler(int sig)
    {
        (void)sig;
        if (g_app)
            g_app->stop();
    }

    void print_help(const char* prog)
    {
        std::cout << "用法: " << prog << " [选项]\n"
                  << "\n选项:\n"
                  << "  -b, --basic   主程序启动测试 (App init/run)\n"
                  << "  -w, --webrtc  WebRTC 通话测试\n"
                  << "  -s, --stream  实时流上传 (JPEG每5秒+Opus按1秒分段, "
                     "wss://tools.colorpets.cn/t5/ws)\n"
                  << "  -h, --help    显示帮助\n"
                  << "\n无参数时进入交互式选择\n";
    }

    /* StreamUploader - WebSocket 二进制协议，大端序 */
    class StreamUploader
    {
    public:
        explicit StreamUploader(app::protocol::websocket::WebSocketClient* ws) : ws_(ws) {}

        bool uploadJpeg(const uint8_t* data, size_t size)
        {
            if (!ws_ || !data || size == 0 || !ws_->isConnected())
                return false;
            uint64_t session_id = static_cast<uint64_t>(app::tool::time::unix_timestamp_ms());
            if (!sendFileStart(session_id, 0x02, 0x02, size))
                return false;
            size_t offset = 0;
            while (offset < size)
            {
                size_t chunk_len = (size - offset < 1024) ? (size - offset) : 1024;
                if (!sendChunk(session_id, data + offset, chunk_len))
                    return false;
                offset += chunk_len;
            }
            if (!sendFileEnd(session_id))
                return false;
            LOG_INFO(LOG_TAG, "JPEG 上传完成 session=%llu size=%zu", session_id, size);
            return true;
        }

        bool uploadOpusSegment(const uint8_t* data, size_t size)
        {
            if (!ws_ || !data || size == 0 || !ws_->isConnected())
                return false;
            uint64_t session_id = static_cast<uint64_t>(app::tool::time::unix_timestamp_ms());
            if (!sendFileStart(session_id, 0x01, 0x01, size))
                return false;
            size_t offset = 0;
            while (offset < size)
            {
                size_t chunk_len = (size - offset < 1024) ? (size - offset) : 1024;
                if (!sendChunk(session_id, data + offset, chunk_len))
                    return false;
                offset += chunk_len;
            }
            if (!sendFileEnd(session_id))
                return false;
            LOG_INFO(LOG_TAG, "Opus 段上传完成 session=%llu size=%zu", session_id, size);
            return true;
        }

    private:
        static void write_u64_be(uint8_t* buf, uint64_t v)
        {
            for (int i = 0; i < 8; i++)
                buf[i] = static_cast<uint8_t>((v >> (56 - i * 8)) & 0xFF);
        }
        static void write_u32_be(uint8_t* buf, uint32_t v)
        {
            buf[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
            buf[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
            buf[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
            buf[3] = static_cast<uint8_t>(v & 0xFF);
        }
        static void write_u16_be(uint8_t* buf, uint16_t v)
        {
            buf[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
            buf[1] = static_cast<uint8_t>(v & 0xFF);
        }
        bool sendFileStart(uint64_t sid, uint8_t file_type, uint8_t encoding, uint32_t total)
        {
            uint8_t msg[17];
            msg[0] = 0x20;
            write_u64_be(msg + 1, sid);
            msg[9]  = file_type;
            msg[10] = encoding;
            write_u32_be(msg + 11, total);
            write_u16_be(msg + 15, 0);
            return ws_->sendBinary(reinterpret_cast<const char*>(msg), 17) ==
                   app::protocol::websocket::WebSocketError::NONE;
        }
        bool sendChunk(uint64_t sid, const uint8_t* data, size_t len)
        {
            std::vector<uint8_t> msg(9 + len);
            msg[0] = 0x21;
            write_u64_be(msg.data() + 1, sid);
            std::memcpy(msg.data() + 9, data, len);
            return ws_->sendBinary(reinterpret_cast<const char*>(msg.data()), msg.size()) ==
                   app::protocol::websocket::WebSocketError::NONE;
        }
        bool sendFileEnd(uint64_t sid)
        {
            uint8_t msg[17];
            msg[0] = 0x22;
            write_u64_be(msg + 1, sid);
            std::memset(msg + 9, 0, 8);
            return ws_->sendBinary(reinterpret_cast<const char*>(msg), 17) ==
                   app::protocol::websocket::WebSocketError::NONE;
        }
        app::protocol::websocket::WebSocketClient* ws_;
    };

    int run_basic()
    {
        LOG_INFO(LOG_TAG, "========== 主程序启动测试 ==========");
        app::App app;
        g_app = &app;

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        if (!app.init())
        {
            LOG_ERROR(LOG_TAG, "初始化失败");
            return 1;
        }

        app.run();
        g_app = nullptr;

        LOG_INFO(LOG_TAG, "主程序测试完成");
        return 0;
    }

    int run_webrtc()
    {
        LOG_INFO(LOG_TAG, "========== WebRTC 通话测试 ==========");

        const std::string device_id  = "glasses_123456";
        const std::string server_url = "wss://tools.colorpets.cn/t5/ws";

        auto sync_ctx = std::make_shared<sync_context_t>();
        if (sync_init(sync_ctx.get()) != 0)
            LOG_WARN(LOG_TAG, "同步初始化失败，继续");

        app::protocol::webrtc::SignalingConfig sig_config;
        sig_config.device_id  = device_id;
        sig_config.server_url = server_url;

        app::protocol::webrtc::WebRTCConfig webrtc_config;
        webrtc_config.ice.stun_servers = {
            "stun:stun.l.google.com:19302",
            "stun:stun.miwifi.com:3478",
            "stun:stun.chat.bilibili.com:3478",
            "stun.12voip.com:3478",
            "stun:stun.aa.net.uk:3478",
            "stun:stun.actionvoip.com:3478"
        };
        webrtc_config.ice.turn_servers = {
            "turn:turnuser1:turnpass123@116.62.24.66:3478",
            "turn:turnuser1:turnpass123@116.62.24.66:3478?transport=tcp",
            "turn:user:mypwd@43.138.235.180:9002"
        };
        // webrtc_config.ice.use_relay_only = true;

        auto signaling = std::make_shared<app::protocol::webrtc::Signaling>(sig_config);
        auto webrtc    = std::make_shared<app::protocol::webrtc::WebRTCSystem>(webrtc_config);

        app::media::audio::AudioCfg audio_cfg;
        audio_cfg.capture.rate      = AUDIO_SAMPLE_RATE;
        audio_cfg.capture.channels  = AUDIO_CHANNELS;
        audio_cfg.capture.frame_ms  = AUDIO_FRAME_DURATION_MS;
        audio_cfg.playback.rate     = AUDIO_SAMPLE_RATE;
        audio_cfg.playback.channels = AUDIO_CHANNELS;
        audio_cfg.playback.frame_ms = AUDIO_FRAME_DURATION_MS;
        audio_cfg.playback.volume   = 50;
        audio_cfg.opus.rate         = AUDIO_SAMPLE_RATE;
        audio_cfg.opus.channels     = AUDIO_CHANNELS;
        audio_cfg.opus.bitrate      = AUDIO_BIT_RATE;
        audio_cfg.opus.vbr          = true;
        audio_cfg.enable_capture    = true;
        audio_cfg.enable_playback   = true;
        audio_cfg.enable_opus       = true;
        audio_cfg.enable_proc       = true;

        auto audio_drv = std::make_unique<app::media::audio::AudioDrv>();
        if (audio_drv->init(audio_cfg) != app::media::audio::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "音频初始化失败");
            return 1;
        }

        audio_drv->set_capture_cb(
            [webrtc, audio_drv = audio_drv.get()](const app::media::audio::FramePtr& pcm)
            {
                if (!pcm || pcm->size == 0 || !webrtc->isConnected())
                    return;
                auto opus_frame = audio_drv->opus().encode(pcm);
                if (opus_frame && opus_frame->size > 0)
                    webrtc->sendAudioData(opus_frame->data, opus_frame->size,
                                          opus_frame->timestamp);
            });

        if (audio_drv->start() != app::media::audio::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "音频启动失败");
            return 1;
        }
        LOG_INFO(LOG_TAG, "音频就绪");

        std::unique_ptr<app::media::camera::CameraDrv> camera_drv;
        app::media::camera::CameraCfg                  camera_cfg;
        camera_cfg.h264.width   = CAMERA_WIDTH;
        camera_cfg.h264.height  = CAMERA_HEIGHT;
        camera_cfg.h264.fps     = CAMERA_FPS;
        camera_cfg.h264.bitrate = H264_Default_Bitrate;
        camera_cfg.iq_file_dir  = ISP_PATH;

        camera_drv = std::make_unique<app::media::camera::CameraDrv>();
        if (camera_drv->init(camera_cfg, sync_ctx) == app::media::camera::Error::OK &&
            camera_drv->start() == app::media::camera::Error::OK)
        {
            camera_drv->set_webrtc_sink(
                [webrtc, sync_ctx](const uint8_t* data, size_t size, uint64_t pts, bool keyframe)
                {
                    if (!data || size == 0 || !webrtc->isConnected())
                        return;
                    uint64_t ts = sync_ctx ? sync_get_timestamp(sync_ctx.get(), pts, false) : pts;
                    webrtc->sendVideoData(data, size, ts, keyframe);
                });
            LOG_INFO(LOG_TAG, "视频就绪");
        }
        else
        {
            LOG_WARN(LOG_TAG, "摄像头初始化失败，仅音频模式");
            camera_drv.reset();
        }

        webrtc->onStateChanged(
            [](app::protocol::webrtc::WebRTCState state)
            {
                LOG_INFO(LOG_TAG, "WebRTC 状态: %s",
                         app::protocol::webrtc::WebRTCSystem::stateToString(state));
            });

        webrtc->onAudioData(
            [audio_drv = audio_drv.get()](const uint8_t* data, size_t size)
            {
                if (!data || size == 0 || !audio_drv)
                    return;
                auto pcm = audio_drv->opus().decode(data, size);
                if (pcm)
                    audio_drv->playback().push(pcm);
            });

        signaling->onStatusChanged(
            [](app::protocol::webrtc::SignalingStatus status)
            {
                LOG_INFO(LOG_TAG, "信令: %s",
                         app::protocol::webrtc::Signaling::statusToString(status).c_str());
            });
        signaling->onError([](app::protocol::webrtc::SignalingError /* e */, const std::string& msg)
                           { LOG_ERROR(LOG_TAG, "信令错误: %s", msg.c_str()); });
        signaling->onRoomInfoChanged(
            [](const app::protocol::webrtc::RoomInfo& info)
            { LOG_INFO(LOG_TAG, "房间 %s 人数=%d", info.room_id.c_str(), info.num); });

        if (!signaling->connect())
        {
            LOG_ERROR(LOG_TAG, "信令连接失败");
            return 1;
        }

        int wait_cnt = 0;
        while (signaling->getStatus() != app::protocol::webrtc::SignalingStatus::CONNECTED)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (++wait_cnt > 100)
            {
                LOG_ERROR(LOG_TAG, "信令连接超时");
                return 1;
            }
        }
        LOG_INFO(LOG_TAG, "信令已连接");

        if (webrtc->init(signaling) != app::protocol::webrtc::WebRTCError::NONE)
        {
            LOG_ERROR(LOG_TAG, "WebRTC 初始化失败");
            return 1;
        }
        LOG_INFO(LOG_TAG, "WebRTC 就绪");

        LOG_INFO(LOG_TAG, "命令: q 退出, j 加入房间, l 离开, c 发送连接请求");
        std::string input;
        while (std::cin >> input)
        {
            if (input == "q" || input == "Q")
                break;
            if (input == "j" || input == "J")
            {
                if (signaling->joinRoom())
                    LOG_INFO(LOG_TAG, "已加入房间");
                else
                    LOG_ERROR(LOG_TAG, "加入房间失败");
            }
            else if (input == "l" || input == "L")
            {
                if (signaling->leaveRoom())
                    LOG_INFO(LOG_TAG, "已离开房间");
                else
                    LOG_ERROR(LOG_TAG, "离开房间失败");
            }
            else if (input == "c" || input == "C")
            {
                std::string peer = signaling->getPeerDeviceId();
                if (peer.empty())
                    LOG_ERROR(LOG_TAG, "未配对");
                else if (webrtc->sendConnectionRequest(peer, true, true, true))
                    LOG_INFO(LOG_TAG, "连接请求已发往 %s", peer.c_str());
                else
                    LOG_ERROR(LOG_TAG, "发送连接请求失败");
            }
        }

        webrtc->deinit();
        signaling->disconnect();
        if (audio_drv)
        {
            audio_drv->stop();
            audio_drv->deinit();
        }
        if (camera_drv)
        {
            camera_drv->stop();
            camera_drv->deinit();
        }
        if (sync_ctx)
            sync_deinit(sync_ctx.get());

        LOG_INFO(LOG_TAG, "WebRTC 测试完成");
        return 0;
    }

    std::atomic<bool> g_stream_running{true};
    void              stream_signal_handler(int)
    {
        g_stream_running.store(false);
    }

    int run_stream_upload()
    {
        using namespace app;
        using namespace app::protocol::websocket;

        LOG_INFO(LOG_TAG, "========== 实时流上传测试 (JPEG+Opus) ==========");

        g_stream_running.store(true);
        std::signal(SIGINT, stream_signal_handler);
        std::signal(SIGTERM, stream_signal_handler);

        auto sync_ctx = std::make_shared<sync_context_t>();
        if (sync_init(sync_ctx.get()) != 0)
        {
            LOG_ERROR(LOG_TAG, "同步初始化失败");
            return 1;
        }

        /* 音频 */
        media::audio::AudioCfg audio_cfg;
        audio_cfg.capture.rate      = AUDIO_SAMPLE_RATE;
        audio_cfg.capture.channels  = AUDIO_CHANNELS;
        audio_cfg.capture.frame_ms  = AUDIO_FRAME_DURATION_MS;
        audio_cfg.playback.rate     = AUDIO_SAMPLE_RATE;
        audio_cfg.playback.channels = AUDIO_CHANNELS;
        audio_cfg.playback.frame_ms = AUDIO_FRAME_DURATION_MS;
        audio_cfg.opus.rate         = AUDIO_SAMPLE_RATE;
        audio_cfg.opus.channels     = AUDIO_CHANNELS;
        audio_cfg.opus.bitrate      = AUDIO_BIT_RATE;
        audio_cfg.opus.vbr          = true;
        audio_cfg.enable_capture    = true;
        audio_cfg.enable_playback   = false;
        audio_cfg.enable_opus       = true;
        audio_cfg.enable_proc       = true;

        auto audio_drv = std::make_unique<media::audio::AudioDrv>();
        if (audio_drv->init(audio_cfg) != media::audio::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "音频初始化失败");
            return 1;
        }

        /* 摄像头：仅 JPEG */
        media::camera::CameraCfg cam_cfg;
        cam_cfg.h264.width   = CAMERA_WIDTH;
        cam_cfg.h264.height  = CAMERA_HEIGHT;
        cam_cfg.h264.fps     = CAMERA_FPS;
        cam_cfg.h264.bitrate = H264_Default_Bitrate;
        cam_cfg.jpeg.width   = 640;
        cam_cfg.jpeg.height  = 480;
        cam_cfg.jpeg.quality = 80;
        cam_cfg.iq_file_dir  = ISP_PATH;
        cam_cfg.enable_h264  = false;
        cam_cfg.enable_jpeg  = true;
        cam_cfg.jpeg_dst_fps = 1;

        auto camera_drv = std::make_unique<media::camera::CameraDrv>();
        if (camera_drv->init(cam_cfg, sync_ctx) != media::camera::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "摄像头初始化失败");
            return 1;
        }

        /* WiFi */
        network::wifi::WifiConfig wifi_cfg;
        wifi_cfg.auto_connect_on_init = true;
        auto wifi_mgr                 = std::make_unique<network::wifi::WifiManager>(wifi_cfg);
        if (wifi_mgr->init() != network::wifi::WifiError::NONE)
        {
            LOG_ERROR(LOG_TAG, "WiFi 初始化失败");
            return 1;
        }
        wifi_mgr->connectSavedNetwork();
        std::this_thread::sleep_for(std::chrono::seconds(3));

        /* WebSocket */
        WebSocketConfig ws_cfg;
        ws_cfg.url                    = "wss://tools.colorpets.cn/t5/ws";
        ws_cfg.auto_reconnect         = true;
        ws_cfg.reconnect_interval_ms  = 5000;
        ws_cfg.max_reconnect_attempts = 10;

        auto ws_client = std::make_unique<WebSocketClient>(ws_cfg);
        auto uploader  = std::make_unique<StreamUploader>(ws_client.get());

        std::vector<uint8_t> audio_buffer;
        std::mutex           audio_mtx;
        std::atomic<int64_t> last_jpeg_upload_ms{0};
        constexpr int        JPEG_INTERVAL_MS = 5000;

        camera_drv->set_jpeg_cb(
            [&](const media::camera::FramePtr& f)
            {
                if (!f || !f->data || f->size == 0)
                    return;
                int64_t now = tool::time::uptime_ms();
                if (now - last_jpeg_upload_ms.load() < JPEG_INTERVAL_MS)
                    return;
                last_jpeg_upload_ms.store(now);
                uploader->uploadJpeg(f->data, f->size);
            });

        audio_drv->set_capture_cb(
            [&](const media::audio::FramePtr& frame)
            {
                if (!frame || !frame->data || frame->size == 0)
                    return;
                auto opus_frame = audio_drv->encodeCaptureForAI(frame);
                if (!opus_frame || !opus_frame->data || opus_frame->size == 0)
                    return;
                std::vector<uint8_t> to_send;
                {
                    std::lock_guard<std::mutex> lock(audio_mtx);
                    audio_buffer.insert(audio_buffer.end(), opus_frame->data,
                                        opus_frame->data + opus_frame->size);
                    if (audio_buffer.size() >= 4096)
                        to_send.swap(audio_buffer);
                }
                if (!to_send.empty() && ws_client->isConnected())
                    uploader->uploadOpusSegment(to_send.data(), to_send.size());
            });

        if (audio_drv->start() != media::audio::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "音频启动失败");
            return 1;
        }
        if (camera_drv->start() != media::camera::Error::OK)
        {
            LOG_ERROR(LOG_TAG, "摄像头启动失败");
            return 1;
        }

        ws_client->connect();
        for (int i = 0; i < 50; i++)
        {
            if (ws_client->isHandshaked())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (ws_client->isConnected())
            LOG_INFO(LOG_TAG, "WebSocket 已连接，开始上传");
        else
            LOG_WARN(LOG_TAG, "WebSocket 未连接，流上传将等待连接");

        LOG_INFO(LOG_TAG, "按 Ctrl+C 退出");
        int loop_count = 0;
        while (g_stream_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (++loop_count % 40 == 0 && !ws_client->isConnected())
                LOG_INFO(LOG_TAG, "WebSocket 仍未连接，继续等待自动重连...");
            std::vector<uint8_t> to_send;
            {
                std::lock_guard<std::mutex> lock(audio_mtx);
                if (audio_buffer.size() >= 512)
                    to_send.swap(audio_buffer);
            }
            if (!to_send.empty() && ws_client->isConnected())
                uploader->uploadOpusSegment(to_send.data(), to_send.size());
        }

        camera_drv->stop();
        audio_drv->stop();
        ws_client->disconnect();
        sync_deinit(sync_ctx.get());

        LOG_INFO(LOG_TAG, "流上传测试完成");
        return 0;
    }
} // namespace

int main(int argc, char* argv[])
{
    Logger::inst().init(LogConfig());
    LOG_INFO(LOG_TAG, "测试程序启动");

    int mode = -1;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_help(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--basic") == 0)
        {
            mode = 1;
            break;
        }
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--webrtc") == 0)
        {
            mode = 2;
            break;
        }
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--stream") == 0)
        {
            mode = 3;
            break;
        }
    }

    if (mode < 0)
    {
        std::cout << "\n选择测试内容:\n"
                  << "  1. 主程序启动 (App init/run)\n"
                  << "  2. WebRTC 通话\n"
                  << "  3. 流上传 (JPEG+Opus)\n"
                  << "  0. 退出\n"
                  << "请选择 [0-3]: ";
        int choice = 0;
        if (!(std::cin >> choice))
        {
            LOG_ERROR(LOG_TAG, "输入无效");
            return 1;
        }
        if (choice == 0)
            return 0;
        if (choice == 1)
            mode = 1;
        else if (choice == 2)
            mode = 2;
        else if (choice == 3)
            mode = 3;
        else
        {
            LOG_ERROR(LOG_TAG, "无效选项");
            return 1;
        }
    }

    int ret = 0;
    if (mode == 1)
        ret = run_basic();
    else if (mode == 2)
        ret = run_webrtc();
    else if (mode == 3)
        ret = run_stream_upload();
    else
        ret = 1;

    LOG_INFO(LOG_TAG, "退出 (ret=%d)", ret);
    return ret;
}
