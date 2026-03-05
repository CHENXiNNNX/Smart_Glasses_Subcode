/*
 * file.cc - 文件操作
 */

#include "file.hpp"
#include "../log/log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace app::tool::file
{

    using namespace tool::log;

    namespace
    {
        constexpr const char* LOG_TAG = "FILE";
    }

    FileWrapper::FileWrapper(const std::string& filename, FileMode mode)
        : filename_(filename), file_(nullptr), valid_(false), mode_(mode),
          last_error_(FileError::NONE)
    {
        open_file();
    }

    FileWrapper::~FileWrapper()
    {
        close_file();
    }

    FileWrapper::FileWrapper(FileWrapper&& other) noexcept
        : filename_(std::move(other.filename_)), file_(other.file_), valid_(other.valid_),
          mode_(other.mode_), last_error_(other.last_error_)
    {
        other.file_  = nullptr;
        other.valid_ = false;
    }

    FileWrapper& FileWrapper::operator=(FileWrapper&& other) noexcept
    {
        if (this != &other)
        {
            close_file();

            filename_   = std::move(other.filename_);
            file_       = other.file_;
            valid_      = other.valid_;
            mode_       = other.mode_;
            last_error_ = other.last_error_;

            other.file_  = nullptr;
            other.valid_ = false;
        }
        return *this;
    }

    void FileWrapper::open_file()
    {
        const char* mode_str = nullptr;
        switch (mode_)
        {
        case FileMode::READ:
            mode_str = "rb";
            break;
        case FileMode::WRITE:
            mode_str = "wb";
            break;
        case FileMode::APPEND:
            mode_str = "ab";
            break;
        case FileMode::READ_WRITE:
            mode_str = "r+b";
            break;
        }

        if (!mode_str)
        {
            last_error_ = FileError::INVALID_PARAM;
            LOG_ERROR(LOG_TAG, "无效的文件模式");
            return;
        }

        file_ = fopen(filename_.c_str(), mode_str);

        if (file_)
        {
            valid_      = true;
            last_error_ = FileError::NONE;
        }
        else
        {
            valid_      = false;
            last_error_ = FileError::FILE_OPEN_FAILED;
            LOG_ERROR(LOG_TAG, "打开文件失败: %s (模式: %s)", filename_.c_str(), mode_str);
        }
    }

    void FileWrapper::close_file()
    {
        if (file_)
        {
            fclose(file_);
            file_  = nullptr;
            valid_ = false;
        }
    }

    bool FileWrapper::write(const void* data, size_t size)
    {
        if (!valid_ || !file_)
        {
            last_error_ = FileError::FILE_WRITE_FAILED;
            return false;
        }

        size_t written = fwrite(data, 1, size, file_);
        if (written != size)
        {
            last_error_ = FileError::FILE_WRITE_FAILED;
            LOG_ERROR(LOG_TAG, "写入文件失败: %s", filename_.c_str());
            return false;
        }

        last_error_ = FileError::NONE;
        return true;
    }

    size_t FileWrapper::read(void* data, size_t size)
    {
        if (!valid_ || !file_)
        {
            last_error_ = FileError::FILE_READ_FAILED;
            return 0;
        }

        size_t read_count = fread(data, 1, size, file_);
        if (read_count == 0 && ferror(file_))
        {
            last_error_ = FileError::FILE_READ_FAILED;
            LOG_ERROR(LOG_TAG, "读取文件失败: %s", filename_.c_str());
            return 0;
        }

        last_error_ = FileError::NONE;
        return read_count;
    }

    void FileWrapper::flush()
    {
        if (file_)
            fflush(file_);
    }

    int64_t FileWrapper::get_size() const
    {
        if (!file_)
            return -1;

        int64_t current_pos = ftell(file_);
        if (current_pos < 0)
            return -1;

        if (fseek(file_, 0, SEEK_END) != 0)
            return -1;

        int64_t size = ftell(file_);
        fseek(file_, current_pos, SEEK_SET);
        return size;
    }

    int64_t FileWrapper::get_position() const
    {
        if (!file_)
            return -1;
        return ftell(file_);
    }

    bool FileWrapper::seek(int64_t offset, int whence)
    {
        if (!file_)
            return false;
        return fseek(file_, static_cast<long>(offset), whence) == 0;
    }

    bool FileWrapper::rewind()
    {
        if (!file_)
            return false;
        return fseek(file_, 0, SEEK_SET) == 0;
    }

    bool FileWrapper::seek_to_end()
    {
        return seek(0, SEEK_END);
    }

    bool exists(const std::string& filename)
    {
        struct stat st;
        return stat(filename.c_str(), &st) == 0;
    }

    bool is_directory(const std::string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            return false;
        return S_ISDIR(st.st_mode);
    }

    bool is_file(const std::string& path)
    {
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            return false;
        return S_ISREG(st.st_mode);
    }

    int64_t get_file_size(const std::string& filename)
    {
        struct stat st;
        if (stat(filename.c_str(), &st) != 0)
            return -1;
        return static_cast<int64_t>(st.st_size);
    }

    bool create_directory_single(const std::string& dir_path, mode_t mode)
    {
        if (dir_path.empty())
            return false;
        if (is_directory(dir_path))
            return true;
        if (mkdir(dir_path.c_str(), mode) == 0)
            return true;
        LOG_ERROR(LOG_TAG, "创建目录失败: %s", dir_path.c_str());
        return false;
    }

    bool create_directory_single(const std::string& dir_path)
    {
        return create_directory_single(dir_path, DIRECTORY_DEFAULT_MODE);
    }

    bool create_directory(const std::string& dir_path, mode_t mode)
    {
        if (dir_path.empty())
            return false;
        if (is_directory(dir_path))
            return true;

        std::string              path = dir_path;
        std::vector<std::string> dirs;

        while (!path.empty() && path != "/")
        {
            dirs.push_back(path);
            size_t pos = path.find_last_of('/');
            if (pos == std::string::npos)
                break;
            path = path.substr(0, pos);
        }

        std::reverse(dirs.begin(), dirs.end());

        return std::all_of(dirs.begin(), dirs.end(),
                           [mode](const std::string& dir)
                           { return create_directory_single(dir, mode); });
    }

    bool create_directory(const std::string& dir_path)
    {
        return create_directory(dir_path, DIRECTORY_DEFAULT_MODE);
    }

    bool remove_file(const std::string& filename)
    {
        if (unlink(filename.c_str()) == 0)
            return true;
        LOG_ERROR(LOG_TAG, "删除文件失败: %s", filename.c_str());
        return false;
    }

    bool remove_directory(const std::string& dir_path)
    {
        DIR* dir = opendir(dir_path.c_str());
        if (!dir)
            return false;

        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            std::string full_path = join_path(dir_path, entry->d_name);

            if (is_directory(full_path))
            {
                if (!remove_directory(full_path))
                {
                    closedir(dir);
                    return false;
                }
            }
            else
            {
                if (!remove_file(full_path))
                {
                    closedir(dir);
                    return false;
                }
            }
        }

        closedir(dir);

        if (rmdir(dir_path.c_str()) == 0)
            return true;
        LOG_ERROR(LOG_TAG, "删除目录失败: %s", dir_path.c_str());
        return false;
    }

    bool rename(const std::string& old_path, const std::string& new_path)
    {
        if (::rename(old_path.c_str(), new_path.c_str()) == 0)
            return true;
        LOG_ERROR(LOG_TAG, "重命名失败: %s -> %s", old_path.c_str(), new_path.c_str());
        return false;
    }

    bool copy_file(const std::string& src_path, const std::string& dst_path)
    {
        FileWrapper src(src_path, FileMode::READ);
        if (!src.valid())
            return false;

        FileWrapper dst(dst_path, FileMode::WRITE);
        if (!dst.valid())
            return false;

        constexpr size_t     BUFFER_SIZE = 64 * 1024;
        std::vector<uint8_t> buffer(BUFFER_SIZE);

        size_t read_count = 0;
        while ((read_count = src.read(buffer.data(), BUFFER_SIZE)) > 0)
        {
            if (!dst.write(buffer.data(), read_count))
                return false;
        }

        dst.flush();
        return true;
    }

    bool read_all(const std::string& filename, std::vector<uint8_t>& data)
    {
        FileWrapper file(filename, FileMode::READ);
        if (!file.valid())
            return false;

        int64_t file_size = file.get_size();
        if (file_size < 0)
            return false;

        data.resize(static_cast<size_t>(file_size));
        size_t read_count = file.read(data.data(), static_cast<size_t>(file_size));
        return read_count == static_cast<size_t>(file_size);
    }

    bool read_all(const std::string& filename, std::string& content)
    {
        std::vector<uint8_t> data;
        if (!read_all(filename, data))
            return false;
        content.assign(data.begin(), data.end());
        return true;
    }

    bool write_all(const std::string& filename, const void* data, size_t size)
    {
        FileWrapper file(filename, FileMode::WRITE);
        if (!file.valid())
            return false;
        if (!file.write(data, size))
            return false;
        file.flush();
        return true;
    }

    bool write_all(const std::string& filename, const std::string& content)
    {
        return write_all(filename, content.data(), content.size());
    }

    std::string get_directory(const std::string& filepath)
    {
        size_t pos = filepath.find_last_of('/');
        if (pos == std::string::npos)
            return ".";
        if (pos == 0)
            return "/";
        return filepath.substr(0, pos);
    }

    std::string get_filename(const std::string& filepath)
    {
        size_t pos = filepath.find_last_of('/');
        if (pos == std::string::npos)
            return filepath;
        return filepath.substr(pos + 1);
    }

    std::string get_extension(const std::string& filepath)
    {
        std::string fname = get_filename(filepath);
        size_t      pos   = fname.find_last_of('.');
        if (pos == std::string::npos || pos == fname.length() - 1)
            return "";
        return fname.substr(pos + 1);
    }

    std::string join_path(const std::string& base, const std::string& path)
    {
        if (base.empty())
            return path;
        if (path.empty())
            return base;

        if (base.back() == '/')
            return path.front() == '/' ? base + path.substr(1) : base + path;

        if (path.front() == '/')
            return base + path;
        return base + "/" + path;
    }

    std::string normalize_path(const std::string& path)
    {
        if (path.empty())
            return ".";

        std::string result = path;
        std::replace(result.begin(), result.end(), '\\', '/');

        std::string normalized;
        bool        last_was_slash = false;
        for (char c : result)
        {
            if (c == '/')
            {
                if (!last_was_slash)
                {
                    normalized += c;
                    last_was_slash = true;
                }
            }
            else
            {
                normalized += c;
                last_was_slash = false;
            }
        }

        if (normalized.length() > 1 && normalized.back() == '/')
            normalized.pop_back();

        return normalized;
    }

} // namespace app::tool::file
