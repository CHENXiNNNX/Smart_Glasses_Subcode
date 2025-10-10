#ifndef ACTIVATION_H
#define ACTIVATION_H

#include <string>

namespace glasses {
namespace chatbot {
namespace activation {

/**
 * @brief 设备激活管理类
 */
class DeviceActivation {
public:
    /**
     * @brief 检查设备激活状态
     * @param mac MAC地址
     * @param uuid UUID
     * @param activation_code 输出激活码（如果未激活）
     * @return 0-已激活, 1-需要激活(返回激活码), -1-失败
     */
    static int checkActivation(const std::string& mac, 
                               const std::string& uuid,
                               std::string& activation_code);
    
    /**
     * @brief 等待用户激活设备（轮询检查）
     * @param mac MAC地址
     * @param uuid UUID
     * @param timeout_seconds 超时时间（秒），默认300秒（5分钟）
     * @return true-激活成功, false-超时或失败
     */
    static bool waitForActivation(const std::string& mac,
                                  const std::string& uuid,
                                  int timeout_seconds = 300);
    
    /**
     * @brief 快速检查是否已激活（不显示详细信息）
     * @param mac MAC地址
     * @param uuid UUID
     * @return true-已激活, false-未激活或失败
     */
    static bool isActivated(const std::string& mac,
                           const std::string& uuid);

private:
    // CURL回调函数
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

} // namespace activation
} // namespace chatbot
} // namespace glasses

#endif // ACTIVATION_H

