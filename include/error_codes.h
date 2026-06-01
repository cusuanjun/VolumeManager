#pragma once

/**
 * @file error_codes.h
 * @brief 错误码定义
 */

namespace volumemanager {

/**
 * @brief 错误码枚举
 */
enum class ErrorCode {
    SUCCESS = 0,           // 成功
    FILE_NOT_FOUND,        // 文件未找到
    INVALID_PATH,          // 无效路径
    VOLUME_FULL,           // 卷镜像已满
    VOLUME_NOT_FOUND,      // 卷镜像未找到
    INVALID_VOLUME_FORMAT, // 无效的卷镜像格式
    INODE_NOT_FOUND,       // inode未找到
    SERIALIZATION_ERROR,   // 序列化/反序列化错误
    IO_ERROR,              // IO错误
    OUT_OF_MEMORY,         // 内存不足
    INVALID_VOLUME_ID,     // 无效的卷镜像ID
    INVALID_PARAMETER      // 无效参数
};

/**
 * @brief 将错误码转换为描述字符串
 * @param code 错误码
 * @return 错误描述字符串
 */
const char* GetErrorMessage(ErrorCode code);

} // namespace volumemanager
