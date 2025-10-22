/**
 * @file uuid.cc
 * @brief UUID生成和管理工具实现
 * @details 支持UUID持久化存储，确保UUID不重复生成
 */

#include "uuid.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <fstream>

namespace app {
namespace tool {
namespace uuid {

// 内部辅助函数：生成随机UUID
static std::string generateRandomUUID() {
    // 使用静态变量确保 random_device 和 mt19937 只被初始化一次
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    
    // UUID格式：xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    // 其中4表示版本4（随机UUID）
    // y的高两位固定为10（二进制），所以y可以是8,9,a,b
    
    // 第一段：8个十六进制数字
    for (int i = 0; i < 8; i++) {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    
    // 第二段：4个十六进制数字
    for (int i = 0; i < 4; i++) {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    
    // 第三段：4开头（版本4），后面3个随机数字
    ss << "4";
    for (int i = 0; i < 3; i++) {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    
    // 第四段：y开头（8,9,a,b），后面3个随机数字
    ss << std::hex << dis2(gen);
    for (int i = 0; i < 3; i++) {
        ss << std::hex << dis(gen);
    }
    ss << "-";
    
    // 第五段：12个十六进制数字
    for (int i = 0; i < 12; i++) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

std::string readUUIDFromConfig(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        // 文件不存在或无法打开，返回空字符串
        return "";
    }

    std::string line;
    std::string uuid_value;
    
    // 逐行读取文件
    while (std::getline(file, line)) {
        // 去除前后空格
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        
        if (start == std::string::npos) {
            continue; // 空行
        }
        
        line = line.substr(start, end - start + 1);
        
        // 跳过注释行
        if (line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // 查找 uuid = 或 uuid=
        if (line.find("uuid") == 0) {
            size_t equal_pos = line.find('=');
            if (equal_pos != std::string::npos) {
                // 提取等号后面的值
                std::string value_part = line.substr(equal_pos + 1);
                
                // 去除前后空格
                size_t value_start = value_part.find_first_not_of(" \t");
                size_t value_end = value_part.find_last_not_of(" \t");
                
                if (value_start != std::string::npos) {
                    uuid_value = value_part.substr(value_start, value_end - value_start + 1);
                    
                    // 验证是否为有效UUID
                    if (isValidUUID(uuid_value)) {
                        break;
                    }
                }
            }
        }
    }
    
    file.close();
    
    if (!uuid_value.empty()) {
        std::cout << "[UUID] Read UUID from config: " << uuid_value << std::endl;
    }
    
    return uuid_value;
}

bool writeUUIDToConfig(const std::string& uuid, const std::string& config_file) {
    if (!isValidUUID(uuid)) {
        std::cerr << "[UUID] ERROR: Invalid UUID format: " << uuid << std::endl;
        return false;
    }

    // 提取目录路径并创建目录（如果不存在）
    size_t last_slash = config_file.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string dir_path = config_file.substr(0, last_slash);
        
        // 使用 mkdir -p 创建目录（包括父目录）
        std::string mkdir_cmd = "mkdir -p " + dir_path + " 2>/dev/null";
        int ret = system(mkdir_cmd.c_str());
        (void)ret; // 忽略返回值
    }

    // 打开文件进行写入（覆盖模式）
    std::ofstream file(config_file, std::ios::trunc);
    if (!file.is_open()) {
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

std::string generateUUID(const std::string& config_file) {
    // 尝试从配置文件读取现有UUID
    std::string existing_uuid = readUUIDFromConfig(config_file);
    
    if (!existing_uuid.empty()) {
        std::cout << "[UUID] Using existing UUID from config" << std::endl;
        return existing_uuid;
    }
    
    // 配置文件中没有UUID，生成新的UUID
    std::string new_uuid = generateRandomUUID();
    std::cout << "[UUID] Generated new UUID: " << new_uuid << std::endl;
    
    // 将新生成的UUID保存到配置文件
    if (!writeUUIDToConfig(new_uuid, config_file)) {
        std::cerr << "[UUID] WARNING: Failed to save UUID to config file, "
                  << "UUID will not persist across restarts" << std::endl;
    }
    
    return new_uuid;
}

std::string generateNewUUID() {
    std::string uuid = generateRandomUUID();
    std::cout << "[UUID] Generated new random UUID: " << uuid << std::endl;
    return uuid;
}

bool isValidUUID(const std::string& uuid) {
    // UUID标准格式长度：36个字符（32个十六进制数字 + 4个短横线）
    if (uuid.length() != 36) {
        return false;
    }
    
    // 检查短横线位置（位置8, 13, 18, 23）
    if (uuid[8] != '-' || uuid[13] != '-' || 
        uuid[18] != '-' || uuid[23] != '-') {
        return false;
    }
    
    // 检查每个字符（除短横线外）是否为十六进制数字
    for (size_t i = 0; i < uuid.length(); i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue; // 跳过短横线
        }
        
        char c = uuid[i];
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    
    // 检查版本号（第15个字符应该是4，表示UUID v4）
    // 注意：这里只检查是否为有效的十六进制，不强制要求版本4
    
    return true;
}

std::string formatUUID(const std::string& uuid) {
    if (uuid.empty()) {
        return "";
    }
    
    // 移除所有非十六进制字符
    std::string clean_uuid;
    for (char c : uuid) {
        if (std::isxdigit(c)) {
            clean_uuid += std::tolower(c);
        }
    }
    
    // UUID应该有32个十六进制字符
    if (clean_uuid.length() != 32) {
        std::cerr << "[UUID] Invalid UUID format: expected 32 hex digits, got " 
                  << clean_uuid.length() << std::endl;
        return "";
    }
    
    // 格式化为标准UUID格式：xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::stringstream ss;
    ss << clean_uuid.substr(0, 8) << "-"
       << clean_uuid.substr(8, 4) << "-"
       << clean_uuid.substr(12, 4) << "-"
       << clean_uuid.substr(16, 4) << "-"
       << clean_uuid.substr(20, 12);
    
    return ss.str();
}

} // namespace uuid
} // namespace tool
} // namespace app

