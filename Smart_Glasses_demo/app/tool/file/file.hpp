/**
 * @file file.hpp
 * @brief 文件操作工具类
 * @details 提供统一的文件读写、目录管理等接口，用于数据文件存储
 */

#ifndef FILE_HPP
#define FILE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace app
{
    namespace tool
    {
        namespace file
        {

            // ============================================================================
            // 错误类型枚举
            // ============================================================================

            enum class FileError
            {
                NONE = 0,            // 无错误
                INVALID_PARAM,       // 无效参数
                FILE_NOT_FOUND,      // 文件不存在
                FILE_OPEN_FAILED,    // 文件打开失败
                FILE_READ_FAILED,    // 文件读取失败
                FILE_WRITE_FAILED,   // 文件写入失败
                FILE_CLOSE_FAILED,   // 文件关闭失败
                DIR_CREATE_FAILED,   // 目录创建失败
                PERMISSION_DENIED,   // 权限不足
                DISK_FULL,           // 磁盘已满
                UNKNOWN              // 未知错误
            };

            // ============================================================================
            // 文件打开模式
            // ============================================================================

            enum class FileMode
            {
                READ,        // 只读模式 ("rb")
                WRITE,       // 写入模式，覆盖 ("wb")
                APPEND,      // 追加模式 ("ab")
                READ_WRITE   // 读写模式 ("r+b")
            };

            // ============================================================================
            // FileWrapper 文件包装器类
            // ============================================================================

            /**
             * @brief 文件资源包装器
             * @details 提供文件读写、刷新等基本操作，使用RAII管理文件资源
             */
            class FileWrapper
            {
            public:
                /**
                 * @brief 构造函数
                 * @param filename 文件路径
                 * @param mode 打开模式（默认写入模式）
                 */
                explicit FileWrapper(const std::string& filename, FileMode mode = FileMode::WRITE);

                /**
                 * @brief 析构函数（自动关闭文件）
                 */
                ~FileWrapper();

                // 禁用拷贝构造和赋值
                FileWrapper(const FileWrapper&)            = delete;
                FileWrapper& operator=(const FileWrapper&) = delete;

                // 允许移动构造和赋值
                FileWrapper(FileWrapper&&) noexcept;
                FileWrapper& operator=(FileWrapper&&) noexcept;

                /**
                 * @brief 检查文件是否有效
                 */
                bool isValid() const
                {
                    return valid_;
                }

                /**
                 * @brief 获取最后的错误码
                 */
                FileError getLastError() const
                {
                    return last_error_;
                }

                /**
                 * @brief 写入数据
                 * @param data 数据指针
                 * @param size 数据大小（字节）
                 * @return 是否成功
                 */
                bool write(const void* data, size_t size);

                /**
                 * @brief 读取数据
                 * @param data 数据缓冲区
                 * @param size 要读取的大小（字节）
                 * @return 实际读取的字节数，失败返回0
                 */
                size_t read(void* data, size_t size);

                /**
                 * @brief 刷新缓冲区到磁盘
                 */
                void flush();

                /**
                 * @brief 获取文件大小
                 * @return 文件大小（字节），失败返回-1
                 */
                int64_t getSize() const;

                /**
                 * @brief 获取当前文件位置
                 * @return 当前位置，失败返回-1
                 */
                int64_t getPosition() const;

                /**
                 * @brief 设置文件位置
                 * @param offset 偏移量
                 * @param whence SEEK_SET=0, SEEK_CUR=1, SEEK_END=2
                 * @return 是否成功
                 */
                bool seek(int64_t offset, int whence = 0);

                /**
                 * @brief 移动到文件开头
                 */
                bool rewind();

                /**
                 * @brief 移动到文件末尾
                 */
                bool seekToEnd();

                /**
                 * @brief 获取文件名
                 */
                std::string getFilename() const
                {
                    return filename_;
                }

                /**
                 * @brief 获取文件模式
                 */
                FileMode getMode() const
                {
                    return mode_;
                }

            private:
                std::string filename_;  // 文件路径
                FILE*       file_;      // 文件指针
                bool        valid_;     // 是否有效
                FileMode    mode_;      // 打开模式
                FileError   last_error_; // 最后的错误码

                /**
                 * @brief 打开文件
                 */
                void openFile();

                /**
                 * @brief 关闭文件
                 */
                void closeFile();
            };

            // ============================================================================
            // 文件工具函数
            // ============================================================================

            /**
             * @brief 检查文件是否存在
             * @param filename 文件路径
             * @return 是否存在
             */
            bool exists(const std::string& filename);

            /**
             * @brief 检查路径是否为目录
             * @param path 路径
             * @return 是否为目录
             */
            bool isDirectory(const std::string& path);

            /**
             * @brief 检查路径是否为文件
             * @param path 路径
             * @return 是否为文件
             */
            bool isFile(const std::string& path);

            /**
             * @brief 获取文件大小
             * @param filename 文件路径
             * @return 文件大小（字节），失败返回-1
             */
            int64_t getFileSize(const std::string& filename);

            /**
             * @brief 创建目录（递归创建父目录）
             * @param dir_path 目录路径
             * @param mode 权限模式（默认DIRECTORY_DEFAULT_MODE）
             * @return 是否成功
             */
            bool createDirectory(const std::string& dir_path, mode_t mode);

            /**
             * @brief 创建目录（递归创建父目录，使用默认权限）
             * @param dir_path 目录路径
             * @return 是否成功
             */
            bool createDirectory(const std::string& dir_path);

            /**
             * @brief 创建目录（不递归，只创建最后一级）
             * @param dir_path 目录路径
             * @param mode 权限模式（默认DIRECTORY_DEFAULT_MODE）
             * @return 是否成功
             */
            bool createDirectorySingle(const std::string& dir_path, mode_t mode);

            /**
             * @brief 创建目录（不递归，使用默认权限）
             * @param dir_path 目录路径
             * @return 是否成功
             */
            bool createDirectorySingle(const std::string& dir_path);

            /**
             * @brief 删除文件
             * @param filename 文件路径
             * @return 是否成功
             */
            bool removeFile(const std::string& filename);

            /**
             * @brief 删除目录（递归删除）
             * @param dir_path 目录路径
             * @return 是否成功
             */
            bool removeDirectory(const std::string& dir_path);

            /**
             * @brief 重命名文件或目录
             * @param old_path 旧路径
             * @param new_path 新路径
             * @return 是否成功
             */
            bool rename(const std::string& old_path, const std::string& new_path);

            /**
             * @brief 复制文件
             * @param src_path 源文件路径
             * @param dst_path 目标文件路径
             * @return 是否成功
             */
            bool copyFile(const std::string& src_path, const std::string& dst_path);

            /**
             * @brief 读取整个文件到内存
             * @param filename 文件路径
             * @param data 输出数据缓冲区
             * @return 是否成功
             */
            bool readAll(const std::string& filename, std::vector<uint8_t>& data);

            /**
             * @brief 读取整个文件到字符串
             * @param filename 文件路径
             * @param content 输出字符串
             * @return 是否成功
             */
            bool readAll(const std::string& filename, std::string& content);

            /**
             * @brief 写入整个文件
             * @param filename 文件路径
             * @param data 数据指针
             * @param size 数据大小
             * @return 是否成功
             */
            bool writeAll(const std::string& filename, const void* data, size_t size);

            /**
             * @brief 写入整个文件（字符串）
             * @param filename 文件路径
             * @param content 字符串内容
             * @return 是否成功
             */
            bool writeAll(const std::string& filename, const std::string& content);

            /**
             * @brief 获取文件所在目录
             * @param filepath 文件路径
             * @return 目录路径
             */
            std::string getDirectory(const std::string& filepath);

            /**
             * @brief 获取文件名（不含路径）
             * @param filepath 文件路径
             * @return 文件名
             */
            std::string getFilename(const std::string& filepath);

            /**
             * @brief 获取文件扩展名
             * @param filepath 文件路径
             * @return 扩展名（不含点号）
             */
            std::string getExtension(const std::string& filepath);

            /**
             * @brief 组合路径
             * @param base 基础路径
             * @param path 相对路径
             * @return 组合后的路径
             */
            std::string joinPath(const std::string& base, const std::string& path);

            /**
             * @brief 规范化路径（移除多余的斜杠等）
             * @param path 路径
             * @return 规范化后的路径
             */
            std::string normalizePath(const std::string& path);

            // ============================================================================
            // 常量定义
            // ============================================================================

            constexpr mode_t DIRECTORY_DEFAULT_MODE = 0755; // 默认目录权限

        } // namespace file
    }     // namespace tool
} // namespace app

#endif // FILE_HPP

