/* uuid.cc - UUID生成和管理 */

#include "uuid.hpp"
#include "log/log.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace app
{
    namespace tool
    {
        namespace uuid
        {

            using namespace log;

            namespace
            {
                constexpr const char* LOG_TAG = "UUID";
            } // namespace

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
                    return "";

                std::string line;
                std::string uuid_value;

                while (std::getline(file, line))
                {
                    size_t start = line.find_first_not_of(" \t\r\n");
                    size_t end   = line.find_last_not_of(" \t\r\n");

                    if (start == std::string::npos)
                        continue;

                    line = line.substr(start, end - start + 1);

                    if (line[0] == '#' || line[0] == ';')
                    {
                        continue;
                    }

                    if (line.find("uuid") == 0)
                    {
                        size_t equal_pos = line.find('=');
                        if (equal_pos != std::string::npos)
                        {
                            std::string value_part  = line.substr(equal_pos + 1);
                            size_t      value_start = value_part.find_first_not_of(" \t");
                            size_t      value_end   = value_part.find_last_not_of(" \t");

                            if (value_start != std::string::npos)
                            {
                                uuid_value =
                                    value_part.substr(value_start, value_end - value_start + 1);
                                if (isValidUUID(uuid_value))
                                {
                                    break;
                                }
                            }
                        }
                    }
                }

                file.close();
                return uuid_value;
            }

            bool writeUUIDToConfig(const std::string& uuid, const std::string& config_file)
            {
                if (!isValidUUID(uuid))
                {
                    LOG_ERROR(LOG_TAG, "无效的UUID格式: %s", uuid.c_str());
                    return false;
                }

                size_t last_slash = config_file.find_last_of('/');
                if (last_slash != std::string::npos)
                {
                    std::string dir_path  = config_file.substr(0, last_slash);
                    std::string mkdir_cmd = "mkdir -p " + dir_path + " 2>/dev/null";
                    int         ret       = system(mkdir_cmd.c_str());
                    (void)ret;
                }

                std::ofstream file(config_file, std::ios::trunc);
                if (!file.is_open())
                {
                    LOG_ERROR(LOG_TAG, "打开配置文件写入失败: %s", config_file.c_str());
                    return false;
                }

                file << "uuid = " << uuid << std::endl;
                file.close();
                return true;
            }

            std::string generateUUID(const std::string& config_file)
            {
                std::string existing_uuid = readUUIDFromConfig(config_file);
                if (!existing_uuid.empty())
                    return existing_uuid;

                std::string new_uuid = s_generate_random_uuid();
                if (!writeUUIDToConfig(new_uuid, config_file))
                {
                    LOG_WARN(LOG_TAG, "保存UUID到配置文件失败，UUID将在重启后丢失");
                }

                return new_uuid;
            }

            std::string generateNewUUID()
            {
                return s_generate_random_uuid();
            }

            bool isValidUUID(const std::string& uuid)
            {
                if (uuid.length() != UUID_TOTAL_LENGTH)
                {
                    return false;
                }

                for (std::size_t separator_position : UUID_SEPARATOR_POSITIONS)
                {
                    if (uuid[separator_position] != UUID_SEPARATOR)
                    {
                        return false;
                    }
                }

                for (std::size_t index = 0; index < uuid.length(); ++index)
                {
                    if (std::find(UUID_SEPARATOR_POSITIONS.begin(), UUID_SEPARATOR_POSITIONS.end(),
                                  index) != UUID_SEPARATOR_POSITIONS.end())
                        continue;

                    char current_char = uuid[index];
                    if (!std::isxdigit(current_char))
                    {
                        return false;
                    }
                }
                return true;
            }

            std::string formatUUID(const std::string& uuid)
            {
                if (uuid.empty())
                {
                    return "";
                }

                std::string clean_uuid;
                for (char current_char : uuid)
                {
                    if (std::isxdigit(current_char))
                    {
                        clean_uuid += static_cast<char>(std::tolower(current_char));
                    }
                }

                if (clean_uuid.length() != UUID_HEX_ONLY_LENGTH)
                {
                    LOG_ERROR(LOG_TAG, "无效的UUID格式: 期望 %zu 个十六进制数字，实际得到 %zu",
                              UUID_HEX_ONLY_LENGTH, clean_uuid.length());
                    return "";
                }

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
