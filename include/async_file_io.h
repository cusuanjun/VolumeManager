#pragma once

#include "error_codes.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file async_file_io.h
 * @brief 基于随机访问接口的文件 I/O 封装
 *
 * 这里先不强行把所有调用都改成复杂框架，而是先把“可并行的底层文件操作”
 * 包装起来。后续只要把这些接口放进多线程任务里，就能做到多个文件同时读写。
 */

namespace volumemanager {

/**
 * @brief 读取整个文件内容到内存
 * @param path 文件路径
 * @param content 输出内容
 * @return ErrorCode 操作结果
 */
ErrorCode AsyncReadAll(const std::string& path, std::vector<uint8_t>& content);

/**
 * @brief 从指定偏移读取指定大小的数据
 * @param path 文件路径
 * @param offset 读取偏移
 * @param size 读取大小
 * @param content 输出内容
 * @return ErrorCode 操作结果
 */
ErrorCode AsyncReadAt(const std::string& path,
                      uint64_t offset,
                      uint64_t size,
                      std::vector<uint8_t>& content);

/**
 * @brief 将内存数据写入整个文件
 * @param path 文件路径
 * @param content 写入内容
 * @return ErrorCode 操作结果
 */
ErrorCode AsyncWriteAll(const std::string& path, const std::vector<uint8_t>& content);

/**
 * @brief 将内存数据写入文件指定偏移
 * @param path 文件路径
 * @param offset 写入偏移
 * @param content 写入内容
 * @return ErrorCode 操作结果
 */
ErrorCode AsyncWriteAt(const std::string& path,
                       uint64_t offset,
                       const std::vector<uint8_t>& content);

/**
 * @brief 删除文件
 * @param path 文件路径
 * @return ErrorCode 操作结果
 */
ErrorCode AsyncDeleteFile(const std::string& path);

} // namespace volumemanager

