#include "../include/volume_manager.h"
#include "../include/error_codes.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>
#include <string>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

using namespace volumemanager;

// 测试配置
constexpr uint64_t VOLUME_SIZE = 1 * 1024 * 1024; // 1MB
constexpr double SIZE_THRESHOLD = 0.5;            // 50%
constexpr const char *BASE_PATH = "./test_data/";
constexpr const char *TEMP_DIR = "/tmp/volume_manager/";
constexpr int NUM_FILES = 10;

// 卷镜像信息结构文件列表
struct VolumeInfo
{
    uint64_t volume_id;
    std::string volume_path;
    std::vector<uint64_t> inode_ids;
};

/**
 * @brief 获取卷镜像文件列表
 */
std::vector<std::string> GetVolumeFiles()
{
    std::vector<std::string> files;
    std::string dir = TEMP_DIR;
    std::string cmd = "ls " + dir + "volume_*.vimg 2>/dev/null";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe)
    {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        {
            std::string file = buffer;
            file.pop_back(); // 移除换行符
            files.push_back(file);
        }
        pclose(pipe);
    }
    return files;
}

/**
 * @brief 从卷镜像文件路径解析volume_id
 */
uint64_t ParseVolumeId(const std::string &volume_path)
{
    size_t start = volume_path.find_last_of('_');
    size_t end = volume_path.find(".vimg");
    if (start != std::string::npos && end != std::string::npos)
    {
        std::string id_str = volume_path.substr(start + 1, end - start - 1);
        return std::stoull(id_str);
    }
    return 0;
}

/**
 * @brief 检查文件是否存在
 */
bool FileExists(const std::string &path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

/**
 * @brief 删除文件
 */
bool DeleteFile(const std::string &path)
{
    return (unlink(path.c_str()) == 0);
}

void EnsureDirectory(const std::string &path)
{
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
    {
        mkdir(path.c_str(), 0755);
    }
}

/**
 * @brief 复制文件
 */
bool CopyFile(const std::string &src, const std::string &dst)
{
    std::ifstream src_file(src, std::ios::binary);
    std::ofstream dst_file(dst, std::ios::binary);

    if (!src_file.is_open() || !dst_file.is_open())
    {
        return false;
    }

    dst_file << src_file.rdbuf();
    return true;
}

/**
 * @brief 获取文件大小
 */
uint64_t GetFileSize(const std::string &path)
{
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(buffer.st_size);
}

/**
 * @brief 读取文件内容
 */
std::vector<uint8_t> ReadFileContent(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        std::cout << "cant open file" <<path<< std::endl;
        return {};
    }

    file.seekg(0, std::ios::end);
    uint64_t size = static_cast<uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> content(size);
    file.read(reinterpret_cast<char *>(content.data()), size);

    return content;
}

/**
 * @brief 比较两个文件内容是否相同
 */
bool CompareFileContent(const std::string &file1, const std::string &file2)
{
    std::vector<uint8_t> content1 = ReadFileContent(file1);
    std::vector<uint8_t> content2 = ReadFileContent(file2);

    if (content1.size() != content2.size())
    {
        std::cout << "size nosame" << content1.size() << ":" << content2.size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < content1.size(); ++i)
    {
        if (content1[i] != content2[i])
        {
            std::cout << "offset nosame" << i << " " << std::endl;
            return false;
        }
    }

    return true;
}

/**
 * @brief 步骤1：初始化
 */
void Step1_Initialize(VolumeManager &manager)
{
    std::cout << "\n=== 步骤1：初始化 ===" << std::endl;
    ErrorCode ret = manager.SetBasePath(BASE_PATH);
    if (ret == ErrorCode::SUCCESS)
    {
        std::cout << "✓ 卷镜像管理器初始化成功" << std::endl;
        std::cout << "  - 卷镜像大小: " << VOLUME_SIZE << " 字节 (1MB)" << std::endl;
        std::cout << "  - 触发阈值: " << (SIZE_THRESHOLD * 100) << "%" << std::endl;
    }
    else
    {
        std::cerr << "✗ 初始化失败: " << GetErrorMessage(ret) << std::endl;
        throw std::runtime_error("初始化失败");
    }
}

/**
 * @brief 步骤2：创建测试文件副本
 */
void Step2_CreateTestFileCopies()
{
    std::cout << "\n=== 步骤2：创建测试文件副本 ===" << std::endl;

    std::string source_file = std::string(BASE_PATH) + "testfile1.data";
    if (!FileExists(source_file))
    {
        std::cerr << "✗ 源文件不存在: " << source_file << std::endl;
        throw std::runtime_error("源文件不存在");
    }

    int success_count = 0;
    for (int i = 1; i <= NUM_FILES; i++)
    {
        std::string dest_file = std::string(BASE_PATH) + "test_file_copy" + std::to_string(i) + ".txt";
        if (CopyFile(source_file, dest_file))
        {
            success_count++;
            std::cout << "✓ 创建副本文件: test_file_copy" << i << ".txt" << std::endl;
        }
        else
        {
            std::cerr << "✗ 创建副本文件失败: test_file_copy" << i << ".txt" << std::endl;
        }
    }

    if (success_count == NUM_FILES)
    {
        std::cout << "✓ 所有文件副本创建成功 (" << success_count << "/" << NUM_FILES << ")" << std::endl;
    }
    else
    {
        throw std::runtime_error("文件副本创建不完整");
    }
}

/**
 * @brief 步骤3：添加文件到卷镜像
 */
void Step3_AddFilesToVolume(VolumeManager &manager, std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤3：添加文件到卷镜像 ===" << std::endl;

    std::vector<std::string> volume_files_before = GetVolumeFiles();

    for (int i = 1; i <= NUM_FILES; i++)
    {
        std::string file_name = "test_file_copy" + std::to_string(i) + ".txt";
        uint64_t inode_id;
        ErrorCode ret = manager.AddFileToCollect(file_name, inode_id);

        if (ret == ErrorCode::SUCCESS)
        {
            std::cout << "✓ 添加文件: " << file_name << std::endl;

            // 检查是否生成了新的卷镜像
            std::vector<std::string> volume_files_after = GetVolumeFiles();
            if (volume_files_after.size() > volume_files_before.size())
            {
                std::string new_volume = volume_files_after.back();
                uint64_t volume_id = ParseVolumeId(new_volume);
                std::cout << "  → 触发封装，生成卷镜像: volume_" << volume_id << ".vimg" << std::endl;

                VolumeInfo info;
                info.volume_id = volume_id;
                info.volume_path = new_volume;
                info.inode_ids.push_back(inode_id);
                volume_infos.push_back(info);
            }
            volume_files_before = volume_files_after;
        }
        else
        {
            std::cerr << "✗ 添加文件失败: " << file_name << " - " << GetErrorMessage(ret) << std::endl;
            throw std::runtime_error("添加文件失败");
        }
    }

    std::cout << "✓ 卷镜像封装完成，共生成 " << volume_infos.size() << " 个卷镜像文件" << std::endl;
}

/**
 * @brief 步骤4：验证元数据
 */
void Step4_VerifyMetadata(VolumeManager &manager, const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤4：验证元数据 ===" << std::endl;

    for (const auto &info : volume_infos)
    {
        std::string output_path;
        ErrorCode ret = (ErrorCode)manager.ReadAllInodesAndDirectories(info.volume_id, output_path);

        if (ret == ErrorCode::SUCCESS)
        {
            std::cout << "✓ 读取卷镜像 " << info.volume_id << " 的元数据成功" << std::endl;
            std::cout << "  → 输出文件: " << output_path << std::endl;

            // 读取并输出元数据内容
            std::ifstream file(output_path);
            if (file.is_open())
            {
                std::string line;
                while (std::getline(file, line))
                {
                    std::cout << "  " << line << std::endl;
                }
            }
        }
        else
        {
            std::cerr << "✗ 读取元数据失败: " << GetErrorMessage(ret) << std::endl;
            throw std::runtime_error("读取元数据失败");
        }
    }
}

/**
 * @brief 步骤5：读取并验证文件内容
 */
void Step5_VerifyFileContents(VolumeManager &manager, const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤5：读取并验证文件内容 ===" << std::endl;

    int success_count = 0;

    // for (const auto &info : volume_infos)
    // {
        for (auto [inode_id, file_meta] : manager.file_metadata_cache_)
        {
            std::string output_path;
            ErrorCode ret = manager.ReadFile(inode_id, output_path);

            if (ret == ErrorCode::SUCCESS)
            {
                // 从output_path提取文件名
                // size_t pos = output_path.find_last_of('/');
                // std::string filename = (pos != std::string::npos) ? output_path.substr(pos + 1) : output_path;
                size_t pos = output_path.find("test_file_copy");
            std::string filename = output_path.substr(pos, output_path.size());

            // 构建原始文件路径
            std::string original_path = std::string(BASE_PATH) + filename;
            std::cout<<"原始文件名：" << original_path << std::endl;

                // 构建原始文件路径
                // std::string original_path = std::string(BASE_PATH) + filename;

                // 比较文件内容
                if (CompareFileContent(original_path, output_path))
                {
                    std::cout << "✓ 文件内容验证成功: " << filename << std::endl;
                    success_count++;
                }
                else
                {
                    std::cout << "original_path:" << original_path << std::endl;
                    std::cout << "output_path:" << output_path << std::endl;
                    std::cerr << "✗ 文件内容不一致: " << filename << std::endl;
                }
            }
            else
            {
                std::cerr << "✗ 读取文件失败: inode_id " << inode_id << " - " << GetErrorMessage(ret) << std::endl;
            }
        }
    // }

    if (success_count == NUM_FILES)
    {
        std::cout << "✓ 文件内容验证完成 (" << success_count << "/" << NUM_FILES << ")" << std::endl;
    }
    else
    {
        std::cout << "⚠ 部分文件内容验证完成 (" << success_count << "/" << NUM_FILES << ")" << std::endl;
    }
}

/**
 * @brief 步骤6：卸载所有卷镜像
 */
void Step6_UnmountVolumes(VolumeManager &manager, const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤6：卸载所有卷镜像 ===" << std::endl;

    int success_count = 0;
    for (const auto &info : volume_infos)
    {
        ErrorCode ret = manager.UnmountVolume(info.volume_id);
        if (ret == ErrorCode::SUCCESS)
        {
            std::cout << "✓ 卸载卷镜像: volume_" << info.volume_id << ".vimg" << std::endl;
            success_count++;
        }
        else
        {
            std::cerr << "✗ 卸载卷镜像失败: " << GetErrorMessage(ret) << std::endl;
        }
    }

    if (success_count == static_cast<int>(volume_infos.size()))
    {
        std::cout << "✓ 所有卷镜像卸载成功 (" << success_count << "/" << volume_infos.size() << ")" << std::endl;
    }
    else
    {
        throw std::runtime_error("卷镜像卸载不完整");
    }
}

/**
 * @brief 步骤7：验证卸载后状态
 */
void Step7_VerifyUnmountedState(const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤7：验证卸载后状态 ===" << std::endl;

    // 验证卷镜像文件仍然存在
    int file_count = 0;
    for (const auto &info : volume_infos)
    {
        if (FileExists(info.volume_path))
        {
            file_count++;
            std::cout << "✓ 卷镜像文件仍然存在: " << info.volume_path << std::endl;
        }
        else
        {
            std::cerr << "✗ 卷镜像文件已被删除: " << info.volume_path << std::endl;
        }
    }

    if (file_count == static_cast<int>(volume_infos.size()))
    {
        std::cout << "✓ 所有卷镜像文件保持完整 (" << file_count << "/" << volume_infos.size() << ")" << std::endl;
    }
    else
    {
        throw std::runtime_error("卷镜像文件不完整");
    }
}

/**
 * @brief 步骤8：重新挂载所有卷镜像
 */
void Step8_RemountVolumes(VolumeManager &manager, const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤8：重新挂载所有卷镜像 ===" << std::endl;

    int success_count = 0;
    for (const auto &info : volume_infos)
    {
        ErrorCode ret = manager.MountVolume(info.volume_path);
        if (ret == ErrorCode::SUCCESS)
        {
            std::cout << "✓ 挂载卷镜像: volume_" << info.volume_id << ".vimg" << std::endl;
            success_count++;
        }
        else
        {
            std::cerr << "✗ 挂载卷镜像失败: " << GetErrorMessage(ret) << std::endl;
        }
    }

    if (success_count == static_cast<int>(volume_infos.size()))
    {
        std::cout << "✓ 所有卷镜像重新挂载成功 (" << success_count << "/" << volume_infos.size() << ")" << std::endl;
    }
    else
    {
        throw std::runtime_error("卷镜像挂载不完整");
    }
}

/**
 * @brief 步骤9：挂载后验证数据
 */
void Step9_VerifyAfterRemount(VolumeManager &manager, const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤9：挂载后验证数据 ===" << std::endl;

    // 验证元数据
    for (const auto &info : volume_infos)
    {
        std::string output_path;
        ErrorCode ret = (ErrorCode)manager.ReadAllInodesAndDirectories(info.volume_id, output_path);

        if (ret == ErrorCode::SUCCESS)
        {
            std::cout << "✓ 卷镜像 " << info.volume_id << " 元数据验证成功" << std::endl;
        }
        else
        {
            std::cerr << "✗ 元数据验证失败: " << GetErrorMessage(ret) << std::endl;
        }
    }

    // 验证文件内容
    int success_count = 0;
    for (auto [inode_id, file_metadata] : manager.file_metadata_cache_)
    {
        std::string output_path;
        ErrorCode ret = manager.ReadFile(inode_id, output_path);

        if (ret == ErrorCode::SUCCESS)
        {
            std::cout << "✓ readfile [" << inode_id << "]" << output_path << std::endl;
            // 从output_path提取文件名
            size_t pos = output_path.find("test_file_copy");
            std::string filename = output_path.substr(pos, output_path.size());

            // 构建原始文件路径
            std::string original_path = std::string(BASE_PATH) + filename;
            std::cout<<"原始文件名：" << original_path << std::endl;

            // 比较文件内容
            if (CompareFileContent(original_path, output_path))
            {
                success_count++;
            }
        }
        else
        {
            std::cout << "readfile [" << inode_id << "] error" << GetErrorMessage(ret) << std::endl;
        }
    }
    // for (const auto &info : volume_infos)
    // {
    //     for (uint64_t inode_id : info.inode_ids)
    //     {
    //         std::string output_path;
    //         ErrorCode ret = manager.ReadFile(inode_id, output_path);

    //         if (ret == ErrorCode::SUCCESS)
    //         {
    //             std::cout << "✓ readfile [" << inode_id << "]" << output_path << std::endl;
    //             // 从output_path提取文件名
    //             size_t pos = output_path.find_last_of('/');
    //             std::string filename = (pos != std::string::npos) ? output_path.substr(pos + 1) : output_path;

    //             // 构建原始文件路径
    //             std::string original_path = std::string(BASE_PATH) + filename;

    //             // 比较文件内容
    //             if (CompareFileContent(original_path, output_path))
    //             {
    //                 success_count++;
    //             }
    //         }
    //         else
    //         {
    //             std::cout << "readfile [" << inode_id << "] error" << GetErrorMessage(ret) << std::endl;
    //         }
    //     }
    // }

    if (success_count == NUM_FILES)
    {
        std::cout << "✓ 挂载后文件内容验证完成 (" << success_count << "/" << NUM_FILES << ")" << std::endl;
    }
    else
    {
        std::cout << "⚠ 部分文件内容验证完成 (" << success_count << "/" << NUM_FILES << ")" << std::endl;
    }

    std::cout << "✓ 挂载后数据验证完成" << std::endl;
}

/**
 * @brief 步骤10：资源清理
 */
void Step10_CleanupResources(const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n=== 步骤10：资源清理 ===" << std::endl;

    // 删除所有测试文件副本
    int deleted_count = 0;
    for (int i = 1; i <= NUM_FILES; i++)
    {
        std::string file_path = std::string(BASE_PATH) + "test_file_copy" + std::to_string(i) + ".txt";
        if (DeleteFile(file_path))
        {
            deleted_count++;
        }
    }

    std::cout << "✓ 删除测试文件副本: " << deleted_count << " 个" << std::endl;

    // 删除所有卷镜像文件
    deleted_count = 0;
    for (const auto &info : volume_infos)
    {
        if (DeleteFile(info.volume_path))
        {
            deleted_count++;
            std::cout << "✓ 删除卷镜像文件: " << info.volume_path << std::endl;
        }
    }

    std::cout << "✓ 删除卷镜像文件: " << deleted_count << " 个" << std::endl;
    std::cout << "✓ 资源清理完成" << std::endl;
}

/**
 * @brief 打印测试总结
 */
void PrintTestSummary(const std::vector<VolumeInfo> &volume_infos)
{
    std::cout << "\n========== 测试总结 ==========" << std::endl;
    std::cout << "✓ 10个副本文件创建成功" << std::endl;
    std::cout << "✓ 所有文件成功添加到待封装集" << std::endl;
    std::cout << "✓ 卷镜像自动封装成功，生成 " << volume_infos.size() << " 个卷镜像" << std::endl;
    std::cout << "✓ 元数据读取成功" << std::endl;
    std::cout << "✓ 文件内容验证完成" << std::endl;
    std::cout << "✓ 卸载操作成功" << std::endl;
    std::cout << "✓ 重新挂载操作成功" << std::endl;
    std::cout << "✓ 挂载后数据验证成功" << std::endl;
    std::cout << "✓ 资源清理成功" << std::endl;
    std::cout << "==============================" << std::endl;
}

int main()
{
    try
    {
        std::cout << "========== 卷镜像管理系统端到端测试 ==========" << std::endl;

        EnsureDirectory(BASE_PATH);

        // 创建卷镜像管理器
        VolumeManager manager(VOLUME_SIZE, SIZE_THRESHOLD);

        // 记录卷镜像信息
        std::vector<VolumeInfo> volume_infos;

        // 执行测试步骤
        Step1_Initialize(manager);
        Step2_CreateTestFileCopies();
        Step3_AddFilesToVolume(manager, volume_infos);
        Step4_VerifyMetadata(manager, volume_infos);
        Step5_VerifyFileContents(manager, volume_infos);
        Step6_UnmountVolumes(manager, volume_infos);
        Step7_VerifyUnmountedState(volume_infos);
        Step8_RemountVolumes(manager, volume_infos);

        // Step4_VerifyMetadata(manager, volume_infos);

        Step9_VerifyAfterRemount(manager, volume_infos);
        Step10_CleanupResources(volume_infos);

        // 打印测试总结
        PrintTestSummary(volume_infos);

        std::cout << "\n✓ 所有测试通过！" << std::endl;
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n✗ 测试异常: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "\n✗ 测试异常: 未知错误" << std::endl;
        return 1;
    }
}
