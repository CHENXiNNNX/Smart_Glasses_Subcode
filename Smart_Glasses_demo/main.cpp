/*
 * main.cpp - WebSocket 图片+音频上传
 *
 * 功能：
 * - 连接 WebSocket 服务器
 * - 每 5 秒拍照并上传 JPEG 图片
 * - 实时录音并上传 Opus 格式音频
 *
 * 消息格式（服务端可据此保存）：
 * - JPEG: [0x01][4字节长度大端][JPEG数据]
 * - Opus: [0x02][4字节长度大端][Opus帧数据]
 */

#include "app/media/audio/audio.hpp"
#include "app/media/camera/camera.hpp"
#include "app/protocol/websocket/websocket.hpp"
#include "app/tool/log/log.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    constexpr const char* TAG = "WsUpload";

    /* 可配置常量 */
    constexpr const char* WS_SERVER_URL = "ws://192.168.50.68:8000";
    constexpr int        PHOTO_INTERVAL_SEC = 5;

    /* 消息类型 */
    constexpr uint8_t MSG_TYPE_JPEG = 0x01;
    constexpr uint8_t MSG_TYPE_OPUS = 0x02;

    std::atomic<bool> g_running{true};
} // namespace

using namespace app::media::audio;
using namespace app::media::camera;
using namespace app::protocol::websocket;
using namespace app::tool::log;

static void signal_handler(int)
{
    g_running.store(false, std::memory_order_relaxed);
}

/* 构建带类型前缀的消息: [type][4-byte length BE][payload] */
static std::vector<uint8_t> buildMessage(uint8_t type, const uint8_t* data, size_t size)
{
    std::vector<uint8_t> buf(5 + size);
    buf[0] = type;
    buf[1] = static_cast<uint8_t>((size >> 24) & 0xFF);
    buf[2] = static_cast<uint8_t>((size >> 16) & 0xFF);
    buf[3] = static_cast<uint8_t>((size >> 8) & 0xFF);
    buf[4] = static_cast<uint8_t>(size & 0xFF);
    if (data && size > 0)
        memcpy(buf.data() + 5, data, size);
    return buf;
}

int main()
{
    LogConfig log_cfg;
    log_cfg.min_level    = LogLevel::INFO;
    log_cfg.enable_color = true;
    Logger::inst().init(log_cfg);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG_INFO(TAG, "WebSocket 图片+音频上传 启动");
    LOG_INFO(TAG, "服务器: %s  拍照间隔: %ds", WS_SERVER_URL, PHOTO_INTERVAL_SEC);

    /* 1. WebSocket 客户端 */
    WebSocketConfig ws_cfg;
    ws_cfg.url               = WS_SERVER_URL;
    ws_cfg.auto_reconnect     = true;
    ws_cfg.reconnect_interval_ms = 5000;
    ws_cfg.max_reconnect_attempts = 10;
    ws_cfg.connect_timeout_ms = 10000;
    ws_cfg.verify_ssl         = false;

    auto ws_client = std::make_unique<WebSocketClient>(ws_cfg);

    ws_client->setErrorCallback(
        [](WebSocketError /*err*/, const std::string& msg)
        {
            LOG_ERROR(TAG, "WebSocket 错误: %s", msg.c_str());
        });

    ws_client->setStateCallback(
        [](ConnectionState /*old_s*/, ConnectionState new_s)
        {
            if (new_s == ConnectionState::HANDSHAKED || new_s == ConnectionState::CONNECTED)
                LOG_INFO(TAG, "WebSocket 已连接");
            else if (new_s == ConnectionState::CLOSED || new_s == ConnectionState::DISCONNECTED)
                LOG_WARN(TAG, "WebSocket 已断开");
        });

    WebSocketError ws_err = ws_client->connect();
    if (ws_err != WebSocketError::NONE)
    {
        LOG_WARN(TAG, "WebSocket 首次连接失败，将重试。错误码: %d", static_cast<int>(ws_err));
    }

    /* 2. 相机 - 仅 JPEG 流 */
    CameraCfg cam_cfg;
    cam_cfg.jpeg.width   = 640;
    cam_cfg.jpeg.height  = 480;
    cam_cfg.jpeg.quality = 80;
    cam_cfg.enable_h264  = false;
    cam_cfg.enable_jpeg  = true;

    CameraDrv cam;
    if (cam.init(cam_cfg, nullptr) != app::media::camera::Error::OK)
    {
        LOG_ERROR(TAG, "相机初始化失败");
        return 1;
    }

    auto last_photo_time = std::chrono::steady_clock::now();
    std::mutex ws_mutex;

    cam.set_jpeg_cb(
        [&](const app::media::camera::FramePtr& frame)
        {
            if (!frame || !frame->data || frame->size == 0)
                return;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_photo_time).count();
            if (elapsed < PHOTO_INTERVAL_SEC)
                return;

            if (!ws_client->isConnected() && !ws_client->isHandshaked())
                return;

            auto msg = buildMessage(MSG_TYPE_JPEG, frame->data, frame->size);
            std::lock_guard<std::mutex> lk(ws_mutex);
            if (ws_client->sendBinary(reinterpret_cast<const char*>(msg.data()), msg.size()) == WebSocketError::NONE)
            {
                last_photo_time = now;
                LOG_INFO(TAG, "上传 JPEG %zu bytes", frame->size);
            }
        });

    if (cam.start() != app::media::camera::Error::OK)
    {
        LOG_ERROR(TAG, "相机启动失败");
        cam.deinit();
        return 1;
    }
    LOG_INFO(TAG, "相机已启动 JPEG %ux%u", cam_cfg.jpeg.width, cam_cfg.jpeg.height);

    /* 3. 音频 - 采集 + Opus 编码，无播放 */
    AudioCfg audio_cfg;
    audio_cfg.capture.rate     = 48000;
    audio_cfg.capture.channels = 1;
    audio_cfg.capture.frame_ms = 20;
    audio_cfg.opus.rate       = 48000;
    audio_cfg.opus.channels   = 1;
    audio_cfg.opus.bitrate    = 32000;
    audio_cfg.opus.vbr        = true;
    audio_cfg.enable_capture  = true;
    audio_cfg.enable_playback = false;
    audio_cfg.enable_opus     = true;
    audio_cfg.enable_proc     = false; /* 仅上传，可关闭 3A 节省资源 */

    AudioDrv audio;
    if (audio.init(audio_cfg) != app::media::audio::Error::OK)
    {
        LOG_ERROR(TAG, "音频初始化失败");
        cam.stop();
        cam.deinit();
        return 1;
    }

    audio.set_capture_cb(
        [&](const app::media::audio::FramePtr& pcm)
        {
            if (!pcm || !pcm->data || pcm->samples == 0)
                return;

            if (!ws_client->isConnected() && !ws_client->isHandshaked())
                return;

            auto opus = audio.opus().encode(pcm);
            if (!opus || !opus->data || opus->size == 0)
                return;

            auto msg = buildMessage(MSG_TYPE_OPUS, opus->data, opus->size);
            std::lock_guard<std::mutex> lk(ws_mutex);
            ws_client->sendBinary(reinterpret_cast<const char*>(msg.data()), msg.size());
        });

    if (audio.start() != app::media::audio::Error::OK)
    {
        LOG_ERROR(TAG, "音频启动失败");
        audio.deinit();
        cam.stop();
        cam.deinit();
        return 1;
    }
    LOG_INFO(TAG, "音频已启动 48kHz 1ch Opus 32kbps");

    /* 4. 主循环 */
    LOG_INFO(TAG, "运行中 Ctrl+C 退出");

    int elapsed = 0;
    while (g_running.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed++;

        /* 定期尝试重连 */
        if (!ws_client->isConnected() && !ws_client->isHandshaked() && (elapsed % 10 == 0))
        {
            LOG_INFO(TAG, "尝试重连 WebSocket...");
            ws_client->reconnect();
        }
    }

    /* 5. 清理 */
    audio.stop();
    audio.deinit();
    cam.stop();
    cam.deinit();
    ws_client->disconnect();

    LOG_INFO(TAG, "已退出");
    return 0;
}
