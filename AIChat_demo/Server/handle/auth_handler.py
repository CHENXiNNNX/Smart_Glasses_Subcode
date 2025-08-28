from tools.logger import logger

class AuthHandler:
    def __init__(self, access_token: str, device_id: str = None, protocol_version: int = 2):
        """
        初始化 AuthHandler

        :param access_token: 用于验证的访问令牌
        :param device_id: 设备 ID (可选)
        :param protocol_version: 协议版本
        """
        self.access_token = access_token
        self.device_id = device_id
        self.protocol_version = protocol_version

    def authenticate(self, headers: dict) -> bool:
        """
        验证客户端的身份信息

        :param headers: 客户端请求头
        :return: 是否通过验证
        """
        auth_token = headers.get("Authorization", "").replace("Bearer ", "")
        client_device_id = headers.get("Device-Id")
        client_protocol_version = headers.get("Protocol-Version")

        # 验证访问令牌
        if auth_token != self.access_token:
            logger.error("Authentication failed: Invalid access token")
            return False

        # 验证设备 ID（如果指定了设备 ID）
        if self.device_id and client_device_id != self.device_id:
            logger.error("Authentication failed: Invalid device ID")
            return False

        # 验证协议版本
        if client_protocol_version != str(self.protocol_version):
            logger.error("Authentication failed: Invalid protocol version")
            return False

        logger.info("Authentication successful")
        return True
