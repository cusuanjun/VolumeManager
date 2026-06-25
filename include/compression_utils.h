#pragma once

#include "error_codes.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file compression_utils.h
 * @brief 压缩/解压缩工具类
 */

namespace volumemanager {

/// @brief 压缩算法枚举
enum class CompressionAlgorithm : uint8_t {
    ZLIB = 0,  ///< zlib 压缩（旧版卷格式兼容）
    ZSTD = 1,  ///< zstd 压缩（新版默认）
};

/**
 * @brief 压缩/解压缩工具类
 */
class CompressionUtils {
public:
    // ========================================================================
    // 文件级压缩/解压
    // ========================================================================

    /**
     * @brief 压缩文件（zlib，兼容旧版）
     * @param input_path 输入文件路径
     * @param output_path 输出压缩文件路径
     * @return ErrorCode 操作结果
     */
    static ErrorCode CompressFile(const std::string& input_path,
                                   const std::string& output_path);

    /**
     * @brief 解压文件（zlib，兼容旧版）
     * @param input_path 输入压缩文件路径
     * @param output_path 输出解压文件路径
     * @return ErrorCode 操作结果
     */
    static ErrorCode DecompressFile(const std::string& input_path,
                                     const std::string& output_path);

    /**
     * @brief 解压文件（zlib，需要原始大小）
     * @param input_path 输入压缩文件路径
     * @param output_path 输出解压文件路径
     * @param original_size 原始文件大小
     * @return ErrorCode 操作结果
     */
    static ErrorCode DecompressFile(const std::string& input_path,
                                     const std::string& output_path,
                                     uint64_t original_size);

    // ========================================================================
    // 数据级压缩/解压（zlib，向后兼容）
    // ========================================================================

    /**
     * @brief 压缩数据（zlib）
     * @param input 输入数据
     * @param output 输出压缩数据
     * @return ErrorCode 操作结果
     */
    static ErrorCode CompressData(const std::vector<uint8_t>& input,
                                   std::vector<uint8_t>& output);

    /**
     * @brief 解压数据（zlib）
     * @param input 输入压缩数据
     * @param original_size 原始数据大小（用于分配解压缓冲区）
     * @param output 输出解压数据
     * @return ErrorCode 操作结果
     */
    static ErrorCode DecompressData(const std::vector<uint8_t>& input,
                                     uint64_t original_size,
                                     std::vector<uint8_t>& output);

    // ========================================================================
    // zstd 压缩/解压（新版，推荐）
    // ========================================================================

    /**
     * @brief zstd 压缩数据
     * @param input 输入数据
     * @param output 输出压缩数据
     * @param compression_level 压缩级别（1-22，默认3；级别越高压缩率越高但越慢）
     * @return ErrorCode 操作结果
     */
    static ErrorCode CompressDataZstd(const std::vector<uint8_t>& input,
                                       std::vector<uint8_t>& output,
                                       int compression_level = 3);

    /**
     * @brief zstd 解压数据（自动从帧头提取原始大小）
     * @param input 输入压缩数据
     * @param output 输出解压数据
     * @return ErrorCode 操作结果
     */
    static ErrorCode DecompressDataZstd(const std::vector<uint8_t>& input,
                                         std::vector<uint8_t>& output);

    /**
     * @brief zstd 压缩文件
     * @param input_path 输入文件路径
     * @param output_path 输出压缩文件路径
     * @param compression_level 压缩级别
     * @return ErrorCode 操作结果
     */
    static ErrorCode CompressFileZstd(const std::string& input_path,
                                       const std::string& output_path,
                                       int compression_level = 3);

    /**
     * @brief zstd 解压文件
     * @param input_path 输入压缩文件路径
     * @param output_path 输出解压文件路径
     * @return ErrorCode 操作结果
     */
    static ErrorCode DecompressFileZstd(const std::string& input_path,
                                         const std::string& output_path);

    // ========================================================================
    // 文件名后缀工具
    // ========================================================================

    /**
     * @brief 获取压缩文件名
     * @param original_name 原始文件名
     * @param algorithm 压缩算法（默认 zstd）
     * @return 压缩文件名
     */
    static std::string GetCompressedFileName(
        const std::string& original_name,
        CompressionAlgorithm algorithm = CompressionAlgorithm::ZSTD);

    /**
     * @brief 从压缩文件名获取原始文件名
     * @param compressed_name 压缩文件名
     * @return 原始文件名
     */
    static std::string GetOriginalFileName(const std::string& compressed_name);

    /**
     * @brief 检查是否为压缩文件名（识别 .compressed 和 .zst 后缀）
     * @param filename 文件名
     * @return true 如果是压缩文件名
     */
    static bool IsCompressedFileName(const std::string& filename);

    /**
     * @brief 从文件名后缀检测压缩算法
     * @param filename 文件名
     * @return CompressionAlgorithm
     */
    static CompressionAlgorithm GetAlgorithmFromFileName(const std::string& filename);

private:
    // 压缩后缀
    static constexpr const char* COMPRESSED_ZLIB_SUFFIX = ".compressed";
    static constexpr const char* COMPRESSED_ZSTD_SUFFIX  = ".zst";
};

} // namespace volumemanager
