/**
 * @file uuid.cc
 * @brief UUID生成和管理工具实现
 * @details 支持UUID持久化存储，确保UUID不重复生成
 */

#include "uuid.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace app
{
    namespace tool
    {
        namespace uuid
        {

            // ============================================================================
            // 常量定义
            // ============================================================================

            constexpr char                       UUID_SEPARATOR                 = '-';
            constexpr char                       UUID_VERSION_HEX               = '4';
            constexpr int                        UUID_RANDOM_HEX_MIN            = 0;
            constexpr int                        UUID_RANDOM_HEX_MAX            = 15;
            constexpr int                        UUID_VARIANT_HEX_MIN           = 8;
            constexpr int                        UUID_VARIANT_HEX_MAX           = 11;
            constexpr std::size_t                UUID_SECTION1_HEX_COUNT        = 8;
            constexpr std::size_t                UUID_SECTION2_HEX_COUNT        = 4;
            constexpr std::size_t                UUID_SECTION3_RANDOM_HEX_COUNT = 3;
            constexpr std::size_t                UUID_SECTION4_RANDOM_HEX_COUNT = 3;
            constexpr std::size_t                UUID_SECTION5_HEX_COUNT        = 12;
            constexpr std::size_t                UUID_TOTAL_LENGTH              = 36;
            constexpr std::size_t                UUID_HEX_ONLY_LENGTH           = 32;
            constexpr std::array<std::size_t, 4> UUID_SEPARATOR_POSITIONS       = {8, 13, 18, 23};
            constexpr std::array<std::size_t, 5> UUID_SECTION_LENGTHS           = {
                          UUID_SECTION1_HEX_COUNT, UUID_SECTION2_HEX_COUNT, UUID_SECTION2_HEX_COUNT,
                          UUID_SECTION2_HEX_COUNT, UUID_SECTION5_HEX_COUNT};

            // ============================================================================
            // 内部辅助函数
            // ============================================================================

            static std::string s_generate_random_uuid()
            {
                static std::random_device                 random_device;
                static std::mt19937                       random_generator(random_device());
                static std::uniform_int_distribution<int> random_hex_distribution(
                    UUID_RANDOM_HEX_MIN, UUID_RANDOM_HEX_MAX);
                static std::uniform_int_distribution<int> variant_distribution(
                    UUID_VARIANT_HEX_MIN, UUID_VARIANT_HEX_MAX);

                std::stringstream uuid_stream;
                uuid_stream << std::hex;

                auto append_random_hex_digits = [&](std::size_t count)
                {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        uuid_stream << random_hex_distribution(random_generator);
                    }
                };

                append_random_hex_digits(UUID_SECTION1_HEX_COUNT);
                uuid_stream << UUID_SEPARATOR;

                append_random_hex_digits(UUID_SECTION2_HEX_COUNT);
                uuid_stream << UUID_SEPARATOR;

                uuid_stream << UUID_VERSION_HEX;
                append_random_hex_digits(UUID_SECTION3_RANDOM_HEX_COUNT);
                uuid_stream << UUID_SEPARATOR;

                uuid_stream << variant_distribution(random_generator);
                append_random_hex_digits(UUID_SECTION4_RANDOM_HEX_COUNT);
                uuid_stream << UUID_SEPARATOR;

                append_random_hex_digits(UUID_SECTION5_HEX_COUNT);

                return uuid_stream.str();
            }

            std::string readUUIDFromConfig(const std::string& config_file)
            {
                std::ifstream file(config_file);
                if (!file.is_open())
                {
                    // 文件不存在或无法打开，返回空字符串
                    return "";
                }

                std::string line;
                std::string uuid_value;

                // 逐行读取文件
                while (std::getline(file, line))
                {
                    // 去除前后空格
                    size_t start = line.find_first_not_of(" \t\r\n");
                    size_t end   = line.find_last_not_of(" \t\r\n");

                    if (start == std::string::npos)
                    {
                        continue; // 空行
                    }

                    line = line.substr(start, end - start + 1);

                    // 跳过注释行
                    if (line[0] == '#' || line[0] == ';')
                    {
                        continue;
                    }

                    // 查找 uuid = 或 uuid=
                    if (line.find("uuid") == 0)
                    {
                        size_t equal_pos = line.find('=');
                        if (equal_pos != std::string::npos)
                        {
                            // 提取等号后面的值
                            std::string value_part = line.substr(equal_pos + 1);

                            // 去除前后空格
                            size_t value_start = value_part.find_first_not_of(" \t");
                            size_t value_end   = value_part.find_last_not_of(" \t");

                            if (value_start != std::string::npos)
                            {
                                uuid_value =
                                    value_part.substr(value_start, value_end - value_start + 1);

                                // 验证是否为有效UUID
                                if (isValidUUID(uuid_value))
                                {
                                    break;
                                }
                            }
                        }
                    }
                }

                file.close();

                if (!uuid_value.empty())
                {
                    std::cout << "[UUID] Read UUID from config: " << uuid_value << std::endl;
                }

                return uuid_value;
            }

            bool writeUUIDToConfig(const std::string& uuid, const std::string& config_file)
            {
                if (!isValidUUID(uuid))
                {
                    std::cerr << "[UUID] ERROR: Invalid UUID format: " << uuid << std::endl;
                    return false;
                }

                // 提取目录路径并创建目录（如果不存在）
                size_t last_slash = config_file.find_last_of('/');
                if (last_slash != std::string::npos)
                {
                    std::string dir_path = config_file.substr(0, last_slash);

                    // 使用 mkdir -p 创建目录（包括父目录）
                    std::string mkdir_cmd = "mkdir -p " + dir_path + " 2>/dev/null";
                    int         ret       = system(mkdir_cmd.c_str());
                    (void)ret; // 忽略返回值
                }

                // 打开文件进行写入（覆盖模式）
                std::ofstream file(config_file, std::ios::trunc);
                if (!file.is_open())
                {
                    std::cerr << "[UUID] ERROR: Failed to open config file for writing: "
                              << config_file << std::endl;
                    return false;
                }

                // 写入简单的键值对格式
                file << "uuid = " << uuid << std::endl;

                file.close();

                std::cout << "[UUID] Saved UUID to config: " << config_file << std::endl;

                return true;
            }

            std::string generateUUID(const std::string& config_file)
            {
                // 尝试从配置文件读取现有UUID
                std::string existing_uuid = readUUIDFromConfig(config_file);

                if (!existing_uuid.empty())
                {
                    std::cout << "[UUID] Using existing UUID from config" << std::endl;
                    return existing_uuid;
                }

                // 配置文件中没有UUID，生成新的UUID
                std::string new_uuid = s_generate_random_uuid();
                std::cout << "[UUID] Generated new UUID: " << new_uuid << std::endl;

                // 将新生成的UUID保存到配置文件
                if (!writeUUIDToConfig(new_uuid, config_file))
                {
                    std::cerr << "[UUID] WARNING: Failed to save UUID to config file, "
                              << "UUID will not persist across restarts" << std::endl;
                }

                return new_uuid;
            }

            std::string generateNewUUID()
            {
                std::string uuid = s_generate_random_uuid();
                std::cout << "[UUID] Generated new random UUID: " << uuid << std::endl;
                return uuid;
            }

            bool isValidUUID(const std::string& uuid)
            {
                // UUID标准格式长度：36个字符（32个十六进制数字 + 4个短横线）
                if (uuid.length() != UUID_TOTAL_LENGTH)
                {
                    return false;
                }

                // 检查短横线位置（位置8, 13, 18, 23）
                for (std::size_t separator_position : UUID_SEPARATOR_POSITIONS)
                {
                    if (uuid[separator_position] != UUID_SEPARATOR)
                    {
                        return false;
                    }
                }

                // 检查每个字符（除短横线外）是否为十六进制数字
                for (std::size_t index = 0; index < uuid.length(); ++index)
                {
                    if (std::find(UUID_SEPARATOR_POSITIONS.begin(), UUID_SEPARATOR_POSITIONS.end(),
                                  index) != UUID_SEPARATOR_POSITIONS.end())
                    {
                        continue; // 跳过短横线
                    }

                    char current_char = uuid[index];
                    if (!std::isxdigit(current_char))
                    {
                        return false;
                    }
                }

                // 检查版本号（第15个字符应该是4，表示UUID v4）
                // 注意：这里只检查是否为有效的十六进制，不强制要求版本4

                return true;
            }

            std::string formatUUID(const std::string& uuid)
            {
                if (uuid.empty())
                {
                    return "";
                }

                // 移除所有非十六进制字符
                std::string clean_uuid;
                for (char current_char : uuid)
                {
                    if (std::isxdigit(current_char))
                    {
                        clean_uuid += static_cast<char>(std::tolower(current_char));
                    }
                }

                // UUID应该有32个十六进制字符
                if (clean_uuid.length() != UUID_HEX_ONLY_LENGTH)
                {
                    std::cerr << "[UUID] Invalid UUID format: expected " << UUID_HEX_ONLY_LENGTH
                              << " hex digits, got " << clean_uuid.length() << std::endl;
                    return "";
                }

                // 格式化为标准UUID格式：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
                std::stringstream formatted_stream;

                std::size_t offset           = 0;
                bool        is_first_section = true;
                for (std::size_t length : UUID_SECTION_LENGTHS)
                {
                    if (!is_first_section)
                    {
                        formatted_stream << UUID_SEPARATOR;
                    }
                    formatted_stream << clean_uuid.substr(offset, length);
                    offset += length;
                    is_first_section = false;
                }

                return formatted_stream.str();
            }

        } // namespace uuid
    }     // namespace tool
} // namespace app
