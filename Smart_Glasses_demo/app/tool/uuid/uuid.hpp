/**
 * @file uuid.h
 * @brief UUID生成和管理工具
 * @details 用于生成符合RFC 4122标准的UUID（通用唯一识别码）
 *          支持UUID持久化存储到配置文件，确保不重复生成
 */

#ifndef UUID_HPP
#define UUID_HPP

#include <string>

namespace app {
namespace tool {
namespace uuid {

// 默认配置文件路径
const std::string DEFAULT_CONFIG_FILE = "./system_para.conf";

/**
 * @brief 生成UUID（智能模式）
 * 
 * @details 智能生成UUID：
 *          1. 首先尝试从配置文件中读取已有的UUID
 *          2. 如果配置文件不存在或没有UUID，则生成新的UUID并保存
 *          3. 确保UUID只生成一次，后续调用都返回相同的UUID
 * 
 *          生成的UUID符合RFC 4122标准的UUID v4（随机UUID）
 *          格式：xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 *          其中：
 *          - x是任意十六进制数字
 *          - 4表示UUID版本4
 *          - y是8、9、a或b中的一个
 * 
 * @param config_file 配置文件路径（默认为./system_para.conf）
 * @return std::string 生成或读取的UUID字符串
 * 
 * @example
 *   std::string uuid = generateUUID();  // 使用默认配置文件
 *   // 首次调用：生成并保存 -> a1b2c3d4-e5f6-4890-abcd-ef1234567890
 *   // 后续调用：从文件读取 -> a1b2c3d4-e5f6-4890-abcd-ef1234567890
 */
std::string generateUUID(const std::string& config_file = DEFAULT_CONFIG_FILE);

/**
 * @brief 生成新的UUID（强制生成）
 * 
 * @details 总是生成新的随机UUID，不检查配置文件
 *          适用于需要临时UUID或测试场景
 * 
 * @return std::string 新生成的UUID字符串
 * 
 * @example
 *   std::string uuid = generateNewUUID();
 *   // 每次调用都会生成不同的UUID
 */
std::string generateNewUUID();

/**
 * @brief 验证UUID格式是否正确
 * 
 * @param uuid 待验证的UUID字符串
 * @return true UUID格式正确
 * @return false UUID格式错误
 * 
 * @example
 *   bool valid = isValidUUID("a1b2c3d4-e5f6-4890-abcd-ef1234567890");
 *   // 结果: true
 */
bool isValidUUID(const std::string& uuid);

/**
 * @brief 格式化UUID（转换为标准格式）
 * 
 * @param uuid 原始UUID字符串（可以包含或不包含短横线）
 * @return std::string 格式化后的UUID（带短横线，小写）
 * 
 * @example
 *   std::string formatted = formatUUID("A1B2C3D4E5F648ABCDEF1234567890");
 *   // 结果: a1b2c3d4-e5f6-48ab-cdef-1234567890
 */
std::string formatUUID(const std::string& uuid);

/**
 * @brief 从配置文件读取UUID
 * 
 * @param config_file 配置文件路径
 * @return std::string 读取到的UUID字符串，失败返回空字符串
 * 
 * @note 配置文件格式：uuid = xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
 */
std::string readUUIDFromConfig(const std::string& config_file);

/**
 * @brief 将UUID写入配置文件
 * 
 * @param uuid 要写入的UUID字符串
 * @param config_file 配置文件路径
 * @return true 写入成功
 * @return false 写入失败
 * 
 * @note 配置文件格式：uuid = xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
 */
bool writeUUIDToConfig(const std::string& uuid, const std::string& config_file);

} // namespace uuid
} // namespace tool
} // namespace app

#endif // UUID_HPP

