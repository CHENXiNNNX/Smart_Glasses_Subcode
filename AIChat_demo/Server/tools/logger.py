import logging
import os
from datetime import datetime
import sys
sys.path.append("..")
from config.settings import global_settings

class Logger:
    """
    日志记录器类，基于 Python 标准 logging 模块封装
    特点：
    - 支持多级日志（DEBUG/INFO/ERROR）
    - 自动创建日志目录
    - 支持控制台+文件双输出
    - 可通过 settings 配置日志级别和输出路径
    """

    def __init__(self, name="assistant", level=logging.INFO):
        self.logger = logging.getLogger(name)
        self.logger.setLevel(level)

        # 禁用日志传播
        self.logger.propagate = False

        # 创建日志目录（如果不存在）
        log_dir = getattr(global_settings, "LOG_DIR", "./logs")
        if not os.path.exists(log_dir):
            os.makedirs(log_dir)

        # 设置日志格式
        formatter = logging.Formatter(
            "[%(asctime)s][%(levelname)s] %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S"
        )

        # 控制台输出
        console_handler = logging.StreamHandler()
        console_handler.setFormatter(formatter)
        self.logger.addHandler(console_handler)

        # 文件输出（按天分割）
        file_handler = logging.FileHandler(
            filename=os.path.join(log_dir, f"assistant_{datetime.now().strftime('%Y%m%d')}.log"),
            encoding="utf-8"
        )
        file_handler.setFormatter(formatter)
        self.logger.addHandler(file_handler)

    def debug(self, msg: str, *args, **kwargs):
        self.logger.debug(msg, *args, **kwargs)

    def info(self, msg: str, *args, **kwargs):
        self.logger.info(msg, *args, **kwargs)

    def warning(self, msg: str, *args, **kwargs):
        self.logger.warning(msg, *args, **kwargs)

    def error(self, msg: str, *args, **kwargs):
        self.logger.error(msg, *args, **kwargs)

    def critical(self, msg: str, *args, **kwargs):
        self.logger.critical(msg, *args, **kwargs)


# 初始化默认日志记录器（可在其他模块直接导入使用）
logger = Logger(__name__, level=logging.INFO)
