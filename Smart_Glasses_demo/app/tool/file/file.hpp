/*
 * file.hpp - 文件操作
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app::tool::file
{

    /* 错误码 */
    enum class FileError
    {
        NONE = 0,
        INVALID_PARAM,
        FILE_NOT_FOUND,
        FILE_OPEN_FAILED,
        FILE_READ_FAILED,
        FILE_WRITE_FAILED,
        FILE_CLOSE_FAILED,
        DIR_CREATE_FAILED,
        PERMISSION_DENIED,
        DISK_FULL,
        UNKNOWN
    };

    /* 打开模式 */
    enum class FileMode
    {
        READ,      // rb
        WRITE,     // wb
        APPEND,    // ab
        READ_WRITE // r+b
    };

    /* 文件包装器 (RAII) */
    class FileWrapper
    {
    public:
        explicit FileWrapper(const std::string& filename, FileMode mode = FileMode::WRITE);
        ~FileWrapper();

        FileWrapper(const FileWrapper&)            = delete;
        FileWrapper& operator=(const FileWrapper&) = delete;
        FileWrapper(FileWrapper&&) noexcept;
        FileWrapper& operator=(FileWrapper&&) noexcept;

        bool valid() const
        {
            return valid_;
        }
        FileError get_last_error() const
        {
            return last_error_;
        }

        bool    write(const void* data, size_t size);
        size_t  read(void* data, size_t size);
        void    flush();
        int64_t get_size() const;
        int64_t get_position() const;
        bool    seek(int64_t offset, int whence = 0);
        bool    rewind();
        bool    seek_to_end();

        std::string get_filename() const
        {
            return filename_;
        }
        FileMode get_mode() const
        {
            return mode_;
        }

    private:
        std::string filename_;
        FILE*       file_;
        bool        valid_;
        FileMode    mode_;
        FileError   last_error_;

        void open_file();
        void close_file();
    };

    /*------------------------------------------------------------------------
     * 文件工具函数
     *------------------------------------------------------------------------*/

    bool    exists(const std::string& filename);
    bool    is_directory(const std::string& path);
    bool    is_file(const std::string& path);
    int64_t get_file_size(const std::string& filename);

    /* 目录操作 */
    bool create_directory(const std::string& dir_path, mode_t mode);
    bool create_directory(const std::string& dir_path);
    bool create_directory_single(const std::string& dir_path, mode_t mode);
    bool create_directory_single(const std::string& dir_path);
    bool remove_directory(const std::string& dir_path);

    /* 文件操作 */
    bool remove_file(const std::string& filename);
    bool rename(const std::string& old_path, const std::string& new_path);
    bool copy_file(const std::string& src_path, const std::string& dst_path);

    /* 整体读写 */
    bool read_all(const std::string& filename, std::vector<uint8_t>& data);
    bool read_all(const std::string& filename, std::string& content);
    bool write_all(const std::string& filename, const void* data, size_t size);
    bool write_all(const std::string& filename, const std::string& content);

    /* 路径处理 */
    std::string get_directory(const std::string& filepath);
    std::string get_filename(const std::string& filepath);
    std::string get_extension(const std::string& filepath);
    std::string join_path(const std::string& base, const std::string& path);
    std::string normalize_path(const std::string& path);

    /* 常量 */
    constexpr mode_t DIRECTORY_DEFAULT_MODE = 0755;

} // namespace app::tool::file
