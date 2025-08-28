import asyncio
from ws_server import WebSocketServer
from threads.tts_thread import TTSGenerateThread
from threads.audio_send_thread import AudioSendThread
from tools.logger import logger
from service_manager import ServiceManager
import sys
sys.path.append("..")

async def main():

    # 初始化vad asr chat intent tts服务
    service_manager = ServiceManager()

    # 启动 TTS 生成线程
    tts_generate_thread = TTSGenerateThread(service_manager)
    # tts_generate_thread.start()

    # 启动 audio 数据发送线程
    tts_send_thread = AudioSendThread(service_manager)
    tts_send_thread.start()

    # 启动 WebSocket 服务器
    server = WebSocketServer(host="0.0.0.0", port=8000, access_token="123456", service_manager=service_manager)
    try:
        await server.start_server()
    except KeyboardInterrupt:
        logger.info("\n服务器正在关闭...")
    finally:
        # 停止线程
        service_manager.stop_event.set()  # 设置停止事件
        # tts_generate_thread.join()
        tts_send_thread.join()
        logger.info("服务器已关闭。")

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("程序已被用户中断")
    finally:
        # 确保事件循环关闭
        try:
            loop = asyncio.get_event_loop()
            if loop.is_running():
                loop.stop()
        except RuntimeError:
            pass
        logger.info("事件循环已关闭")
