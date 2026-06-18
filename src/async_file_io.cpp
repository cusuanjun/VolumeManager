#include "async_file_io.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <new>

namespace volumemanager {

// 复用 utils.cpp 里的目录创建逻辑，避免把临时目录创建逻辑重复写一遍。
ErrorCode CreateDirectories(const std::string& path);

namespace {

bool EnsureParentDirectory(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return true;
    }

    std::string parent = path.substr(0, pos);
    if (parent.empty()) {
        return true;
    }

    return CreateDirectories(parent) == ErrorCode::SUCCESS;
}

ErrorCode ReadExact(int fd, uint64_t offset, uint8_t* buffer, uint64_t size) {
    uint64_t total = 0;
    while (total < size) {
        ssize_t ret = pread(fd, buffer + total, static_cast<size_t>(size - total),
                            static_cast<off_t>(offset + total));
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ErrorCode::IO_ERROR;
        }
        if (ret == 0) {
            return ErrorCode::IO_ERROR;
        }
        total += static_cast<uint64_t>(ret);
    }
    return ErrorCode::SUCCESS;
}

ErrorCode WriteExact(int fd, uint64_t offset, const uint8_t* buffer, uint64_t size) {
    uint64_t total = 0;
    while (total < size) {
        ssize_t ret = pwrite(fd, buffer + total, static_cast<size_t>(size - total),
                             static_cast<off_t>(offset + total));
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ErrorCode::IO_ERROR;
        }
        total += static_cast<uint64_t>(ret);
    }
    return ErrorCode::SUCCESS;
}

} // namespace

ErrorCode AsyncReadAll(const std::string& path, std::vector<uint8_t>& content) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return ErrorCode::FILE_NOT_FOUND;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return ErrorCode::IO_ERROR;
    }

    uint64_t size = static_cast<uint64_t>(st.st_size);
    content.clear();
    if (size == 0) {
        close(fd);
        return ErrorCode::SUCCESS;
    }

    try {
        content.resize(static_cast<size_t>(size));
    } catch (const std::bad_alloc&) {
        close(fd);
        return ErrorCode::OUT_OF_MEMORY;
    }

    ErrorCode ret = ReadExact(fd, 0, content.data(), size);
    close(fd);
    return ret;
}

ErrorCode AsyncReadAt(const std::string& path,
                      uint64_t offset,
                      uint64_t size,
                      std::vector<uint8_t>& content) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return ErrorCode::FILE_NOT_FOUND;
    }

    content.clear();
    if (size == 0) {
        close(fd);
        return ErrorCode::SUCCESS;
    }

    try {
        content.resize(static_cast<size_t>(size));
    } catch (const std::bad_alloc&) {
        close(fd);
        return ErrorCode::OUT_OF_MEMORY;
    }

    ErrorCode ret = ReadExact(fd, offset, content.data(), size);
    close(fd);
    return ret;
}

ErrorCode AsyncWriteAll(const std::string& path, const std::vector<uint8_t>& content) {
    if (!EnsureParentDirectory(path)) {
        return ErrorCode::IO_ERROR;
    }

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return ErrorCode::IO_ERROR;
    }

    ErrorCode ret = ErrorCode::SUCCESS;
    if (!content.empty()) {
        ret = WriteExact(fd, 0, content.data(), static_cast<uint64_t>(content.size()));
    }

    close(fd);
    return ret;
}

ErrorCode AsyncWriteAt(const std::string& path,
                       uint64_t offset,
                       const std::vector<uint8_t>& content) {
    if (!EnsureParentDirectory(path)) {
        return ErrorCode::IO_ERROR;
    }

    int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        return ErrorCode::IO_ERROR;
    }

    ErrorCode ret = ErrorCode::SUCCESS;
    if (!content.empty()) {
        ret = WriteExact(fd, offset, content.data(), static_cast<uint64_t>(content.size()));
    }

    close(fd);
    return ret;
}

ErrorCode AsyncDeleteFile(const std::string& path) {
    if (unlink(path.c_str()) != 0) {
        if (errno == ENOENT) {
            return ErrorCode::SUCCESS;
        }
        return ErrorCode::IO_ERROR;
    }
    return ErrorCode::SUCCESS;
}

} // namespace volumemanager
