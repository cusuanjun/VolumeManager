#include "compression_utils.h"
#include "async_file_io.h"
#include <cstdint>
#include <zlib.h>
#include <cstring>

namespace volumemanager {

// 工具函数声明（在utils.cpp中定义）
ErrorCode ReadFileContent(const std::string& path, std::vector<uint8_t>& content);
ErrorCode WriteFileContent(const std::string& path, const std::vector<uint8_t>& content);

ErrorCode CompressionUtils::CompressData(const std::vector<uint8_t>& input,
                                         std::vector<uint8_t>& output) {
    if (input.empty()) {
        output.clear();
        return ErrorCode::SUCCESS;
    }

    // 计算压缩后的最大长度
    uLongf compressed_size = compressBound(static_cast<uLong>(input.size()));

    try {
        output.resize(compressed_size);
    } catch (const std::bad_alloc&) {
        return ErrorCode::OUT_OF_MEMORY;
    }

    // 执行压缩
    int ret = compress2(output.data(), &compressed_size,
                      input.data(), static_cast<uLong>(input.size()),
                      Z_BEST_COMPRESSION);

    if (ret != Z_OK) {
        return ErrorCode::SERIALIZATION_ERROR;
    }

    // 调整输出缓冲区大小为实际压缩大小
    output.resize(compressed_size);

    return ErrorCode::SUCCESS;
}

ErrorCode CompressionUtils::DecompressData(const std::vector<uint8_t>& input,
                                             uint64_t original_size,
                                             std::vector<uint8_t>& output) {
    if (input.empty()) {
        output.clear();
        return ErrorCode::SUCCESS;
    }

    try {
        output.resize(original_size);
    } catch (const std::bad_alloc&) {
        return ErrorCode::OUT_OF_MEMORY;
    }

    uLongf decompressed_size = static_cast<uLongf>(original_size);

    // 执行解压
    int ret = uncompress(output.data(), &decompressed_size,
                       input.data(), static_cast<uLong>(input.size()));

    if (ret != Z_OK) {
        return ErrorCode::SERIALIZATION_ERROR;
    }

    // 确保解压后的数据大小匹配预期
    if (decompressed_size != original_size) {
        return ErrorCode::SERIALIZATION_ERROR;
    }

    return ErrorCode::SUCCESS;
}

ErrorCode CompressionUtils::CompressFile(const std::string& input_path,
                                         const std::string& output_path) {
    // 读取输入文件
    std::vector<uint8_t> input_data;
    ErrorCode ret = AsyncReadAll(input_path, input_data);
    if (ret != ErrorCode::SUCCESS) {
        return ret;
    }

    // 压缩数据
    std::vector<uint8_t> compressed_data;
    ret = CompressData(input_data, compressed_data);
    if (ret != ErrorCode::SUCCESS) {
        return ret;
    }

    // 写入压缩文件
    return AsyncWriteAll(output_path, compressed_data);
}

ErrorCode CompressionUtils::DecompressFile(const std::string& input_path,
                                           const std::string& output_path) {
    return DecompressFile(input_path, output_path, 0);
}

ErrorCode CompressionUtils::DecompressFile(const std::string& input_path,
                                           const std::string& output_path,
                                           uint64_t original_size) {
    // 读取压缩文件
    std::vector<uint8_t> compressed_data;
    ErrorCode ret = AsyncReadAll(input_path, compressed_data);
    if (ret != ErrorCode::SUCCESS) {
        return ret;
    }

    if (original_size == 0) {
        return ErrorCode::INVALID_PARAMETER;
    }

    std::vector<uint8_t> decompressed_data;
    ret = DecompressData(compressed_data, original_size, decompressed_data);
    if (ret != ErrorCode::SUCCESS) {
        return ret;
    }

    return AsyncWriteAll(output_path, decompressed_data);
}

std::string CompressionUtils::GetCompressedFileName(const std::string& original_name) {
    return original_name + COMPRESSED_SUFFIX;
}

std::string CompressionUtils::GetOriginalFileName(const std::string& compressed_name) {
    size_t suffix_len = strlen(COMPRESSED_SUFFIX);
    if (compressed_name.size() > suffix_len) {
        size_t pos = compressed_name.find(COMPRESSED_SUFFIX);
        if (pos != std::string::npos &&
            pos + suffix_len == compressed_name.size()) {
            return compressed_name.substr(0, pos);
        }
    }
    return compressed_name;
}

bool CompressionUtils::IsCompressedFileName(const std::string& filename) {
    size_t suffix_len = strlen(COMPRESSED_SUFFIX);
    if (filename.size() > suffix_len) {
        size_t pos = filename.find(COMPRESSED_SUFFIX);
        return (pos != std::string::npos &&
                pos + suffix_len == filename.size());
    }
    return false;
}

} // namespace volumemanager
