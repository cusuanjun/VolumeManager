#pragma once

#include "volume_metadata.h"
#include "error_codes.h"
#include "async_file_io.h"
#include "thread_pool.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

/**
 * @file volume_manager.h
 * @brief 卷镜像管理器主接口
 */

namespace volumemanager {

enum class PackTrigger {
    Manual,
    AutoThreshold,
    AutoOverflow
};

struct PackReport {
    PackTrigger trigger;
    ErrorCode result;
    uint64_t volume_id;
    std::string volume_path;
    uint32_t file_count;
    uint64_t user_data_size;
};

class IPackReporter {
public:
    virtual ~IPackReporter() = default;
    virtual void OnPackFinished(const PackReport& report) = 0;
};

/**
 * @brief 卷镜像管理器类
 */
class VolumeManager {
public:
    /**
     * @brief 构造函数
     * @param volume_size 卷镜像大小限制（字节），默认1GB
     * @param size_threshold 触发镜像封装的大小阈值（百分比0.0-1.0），默认0.9（90%）
     */
    explicit VolumeManager(uint64_t volume_size = 1024 * 1024 * 1024,
                           double size_threshold = 0.9);

    /**
     * @brief 析构函数
     */
    ~VolumeManager();

    // 禁止拷贝构造和拷贝赋值
    VolumeManager(const VolumeManager&) = delete;
    VolumeManager& operator=(const VolumeManager&) = delete;

    /**
     * @brief 将文件纳入待封装文件集
     * @param file_path 文件相对路径
     * @return ErrorCode 操作结果
     */
    ErrorCode AddFileToCollect(const std::string& file_path);
    ErrorCode AddFileToCollect(const std::string& file_path, uint64_t& inode_id);

    /**
     * @brief 批量将文件纳入待封装文件集（并行处理，利用线程池加速）
     * @param file_paths 文件相对路径列表
     * @param out_inode_ids 输出每个文件对应的 inode_id
     * @return ErrorCode 操作结果（首个失败即中止）
     */
    ErrorCode AddFilesToCollect(const std::vector<std::string>& file_paths,
                                std::vector<uint64_t>& out_inode_ids);

    /**
     * @brief 根据inode_id读取文件内容
     * @param inode_id 文件的全局唯一标识
     * @param output_path 输出中间文件的相对路径
     * @return ErrorCode 操作结果
     */
    ErrorCode ReadFile(uint64_t inode_id, std::string& output_path);

    /**
     * @brief 读取卷镜像的所有inode和目录文件数据
     * @param volume_id 卷镜像ID
     * @param output_path 输出中间文件的相对路径
     * @return ErrorCode 操作结果
     */
    ErrorCode ReadAllInodesAndDirectories(uint64_t volume_id,
                                           std::string& output_path);

    /**
     * @brief 卸载卷镜像，释放相关资源
     * @param volume_id 卷镜像ID
     * @return ErrorCode 操作结果
     */
    ErrorCode UnmountVolume(uint64_t volume_id);

    /**
     * @brief 挂载卷镜像，重建管理资源
     * @param volume_path 卷镜像文件路径
     * @return ErrorCode 操作结果
     */
    ErrorCode MountVolume(const std::string& volume_path);

    /**
     * @brief 设置基础路径
     * @param base_path 基础路径
     * @return ErrorCode 操作结果
     */
    ErrorCode SetBasePath(const std::string& base_path);

public:
    std::string base_path_;                          // 基础路径
    std::string temp_dir_;                           // 临时目录
    std::unordered_map<uint64_t, VolumeMetadata> mounted_volumes_;  // 已挂载的卷镜像
    std::unordered_map<uint64_t, FileMetadata> file_metadata_cache_;   // 文件元数据缓存
    std::vector<FileMetadata> pending_files_;        // 待封装文件集
    uint64_t pending_size_;                          // 待封装文件总大小
    uint64_t volume_size_;                          // 卷镜像大小限制
    double size_threshold_;                         // 触发镜像封装的大小阈值（百分比）

    IPackReporter* pack_reporter_ = nullptr;

    ErrorCode PackVolumeInternal(PackTrigger trigger, std::string& output_path);
    ErrorCode FlushPendingFiles(std::string& output_path);

    /**
     * @brief 初始化临时目录
     * @return ErrorCode 操作结果
     */
    ErrorCode InitializeTempDir();

    /**
     * @brief 生成唯一的卷镜像ID
     * @return volume_id
     */
    uint64_t GenerateVolumeId();

    /**
     * @brief 生成唯一的inode ID
     * @return inode_id
     */
    uint64_t GenerateInodeId();

    /**
     * @brief 封装卷镜像
     * @param output_path 输出卷镜像文件路径
     * @return ErrorCode 操作结果
     */
    ErrorCode PackVolume(std::string& output_path);

    /**
     * @brief 检查是否需要封装卷镜像
     * @return true 如果需要封装
     */
    bool ShouldPackVolume() const;

    void SetPackReporter(IPackReporter* reporter);

private:
    mutable std::mutex state_mutex_;   // 保护共享元数据和待打包集合
    mutable std::mutex pack_mutex_;    // 保护打包流程，避免并发封装同一个 pending 集合
    ThreadPool thread_pool_;           // 线程池，用于压缩/I/O 并行化
};

} // namespace volumemanager
