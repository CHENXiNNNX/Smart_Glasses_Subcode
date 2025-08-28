from concurrent.futures import ThreadPoolExecutor
from services.chat_service import ChatService
from tools.logger import logger

# 短生命周期的任务管理器
class TaskManager:
    def __init__(self):
        self.executor = ThreadPoolExecutor(max_workers=5)  # 线程池，最多同时运行5个任务

    def submit_task(self, func, *args, **kwargs):
        """
        提交任务到线程池
        """
        self.executor.submit(func, *args, **kwargs)
        logger.info(f"任务提交成功: {func.__name__}, 参数: {args}, {kwargs}")
