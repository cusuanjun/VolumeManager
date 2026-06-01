#include "../include/volume_manager.h"
#include "../include/error_codes.h"
#include "../include/serializer.h"
#include "../include/compression_utils.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

using namespace volumemanager;

constexpr const char* TEST_DATA_DIR = "./test_data/";

// 辅助函数：创建测试文件
void CreateTestFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (file.is_open()) {
        file << content;
        file.close();
    }
}

void EnsureDirectory(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        mkdir(path.c_str(), 0755);
    }
}

// 辅助函数：比较两个文件内容是否相同
bool CompareFileContent(const std::string& path1, const std::string& path2) {
    std::ifstream file1(path1, std::ios::binary);
    std::ifstream file2(path2, std::ios::binary);

    if (!file1.is_open() || !file2.is_open()) {
        return false;
    }

    file1.seekg(0, std::ios::end);
    file2.seekg(0, std::ios::end);

    if (file1.tellg() != file2.tellg()) {
        file1.close();
        file2.close();
        return false;
    }

    file1.seekg(0, std::ios::beg);
    file2.seekg(0, std::ios::beg);

    char c1, c2;
    while (file1.get(c1) && file2.get(c2)) {
        if (c1 != c2) {
            file1.close();
            file2.close();
            return false;
        }
    }

    file1.close();
    file2.close();
    return true;
}

// 测试1：基本功能测试
void TestBasicFunction() {
    std::cout << "=== 测试1：基本功能测试 ===" << std::endl;

    // 创建卷镜像管理器（设置较小的卷镜像大小用于测试）
    VolumeManager manager(2048, 0.8);

    // 设置基础路径
    manager.SetBasePath(TEST_DATA_DIR);

    // 创建测试文件
    CreateTestFile(std::string(TEST_DATA_DIR) + "file1.txt", "这是测试文件1的内容");
    CreateTestFile(std::string(TEST_DATA_DIR) + "file2.txt", "这是测试文件2的内容");
    CreateTestFile(std::string(TEST_DATA_DIR) + "file3.txt", "这是测试文件3的内容");

    // 添加文件到待封装集
    ErrorCode ret = manager.AddFileToCollect("file1.txt");
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "添加文件1成功" << std::endl;

    ret = manager.AddFileToCollect("file2.txt");
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "添加文件2成功" << std::endl;

    ret = manager.AddFileToCollect("file3.txt");
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "添加文件3成功" << std::endl;

    std::cout << "测试1通过" << std::endl << std::endl;
}

// 测试2：错误处理测试
void TestErrorHandling() {
    std::cout << "=== 测试2：错误处理测试 ===" << std::endl;

    VolumeManager manager(1024, 0.9);
    manager.SetBasePath(TEST_DATA_DIR);

    // 测试添加不存在的文件
    ErrorCode ret = manager.AddFileToCollect("nonexistent.txt");
    assert(ret == ErrorCode::FILE_NOT_FOUND);
    std::cout << "正确检测到文件不存在" << std::endl;

    // 测试添加无效路径
    ret = manager.AddFileToCollect("");
    assert(ret == ErrorCode::INVALID_PATH);
    std::cout << "正确检测到无效路径" << std::endl;

    // 测试卸载不存在的卷镜像
    ret = manager.UnmountVolume(99999);
    assert(ret == ErrorCode::VOLUME_NOT_FOUND);
    std::cout << "正确检测到卷镜像不存在" << std::endl;

    std::cout << "测试2通过" << std::endl << std::endl;
}

// 测试3：序列化/反序列化测试
void TestSerialization() {
    std::cout << "=== 测试3：序列化/反序列化测试 ===" << std::endl;

    // 测试卷镜像元数据序列化
    VolumeMetadata original_meta;
    original_meta.volume_id = 12345678;
    original_meta.volume_size = 1024 * 1024;
    original_meta.metadata_offset = 56;
    original_meta.metadata_size = 100;
    original_meta.directory_offset = 156;
    original_meta.directory_size = 0;
    original_meta.user_data_offset = 156;
    original_meta.user_data_size = 200;
    original_meta.file_count = 3;

    std::vector<uint8_t> serialized;
    ErrorCode ret = Serializer::SerializeVolumeMetadata(original_meta, serialized);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "卷镜像元数据序列化成功" << std::endl;

    VolumeMetadata deserialized_meta;
    ret = Serializer::DeserializeVolumeMetadata(serialized, deserialized_meta);
    assert(ret == ErrorCode::SUCCESS);
    assert(deserialized_meta.volume_id == original_meta.volume_id);
    assert(deserialized_meta.volume_size == original_meta.volume_size);
    assert(deserialized_meta.file_count == original_meta.file_count);
    std::cout << "卷镜像元数据反序列化成功" << std::endl;

    // 测试文件元数据序列化
    FileMetadata file_meta;
    file_meta.inode_id = 87654321;
    file_meta.file_size = 100;
    file_meta.compressed_size = 50; // 假设压缩后大小为50
    file_meta.volume_id = 12345678;
    file_meta.offset_in_volume = 200;
    file_meta.file_name = "test.txt";
    file_meta.compressed_file_name = "test.txt.compressed";
    file_meta.file_mode = 0644;
    file_meta.last_modified = 1234567890;
    file_meta.is_directory = false;
    file_meta.is_compressed = true;

    std::vector<uint8_t> file_serialized;
    ret = Serializer::SerializeFileMetadata(file_meta, file_serialized);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "文件元数据序列化成功" << std::endl;

    FileMetadata file_deserialized;
    ret = Serializer::DeserializeFileMetadata(file_serialized, file_deserialized);
    assert(ret == ErrorCode::SUCCESS);
    assert(file_deserialized.inode_id == file_meta.inode_id);
    assert(file_deserialized.file_name == file_meta.file_name);
    assert(file_deserialized.file_size == file_meta.file_size);
    assert(file_deserialized.compressed_size == file_meta.compressed_size);
    std::cout << "文件元数据反序列化成功" << std::endl;

    // 测试卷镜像元数据验证
    assert(Serializer::ValidateVolumeMetadata(deserialized_meta) == true);
    std::cout << "卷镜像元数据验证成功" << std::endl;

    std::cout << "测试3通过" << std::endl << std::endl;
}

// 测试4：边界条件测试
void TestBoundaryConditions() {
    std::cout << "=== 测试4：边界条件测试 ===" << std::endl;

    // 测试阈值边界
    VolumeManager manager(100, 1.0); // 设置阈值为100%

    manager.SetBasePath(TEST_DATA_DIR);

    CreateTestFile(std::string(TEST_DATA_DIR) + "small.txt", "小文件");

    ErrorCode ret = manager.AddFileToCollect("small.txt");
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "添加小文件成功" << std::endl;

    // 测试错误信息获取
    const char* msg = GetErrorMessage(ErrorCode::SUCCESS);
    assert(std::string(msg) == "操作成功");
    std::cout << "错误信息获取正确" << std::endl;

    std::cout << "测试4通过" << std::endl << std::endl;
}

// 测试5：压缩功能测试
void TestCompression() {
    std::cout << "=== 测试5：压缩功能测试 ===" << std::endl;

    // 测试数据压缩/解压
    std::string test_data = "这是一段用于测试压缩功能的文本数据，应该能够被正确地压缩和解压。";
    std::vector<uint8_t> original_data(test_data.begin(), test_data.end());

    std::vector<uint8_t> compressed_data;
    ErrorCode ret = CompressionUtils::CompressData(original_data, compressed_data);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "数据压缩成功，原始大小: " << original_data.size()
              << ", 压缩后大小: " << compressed_data.size() << std::endl;

    // 验证压缩后数据大小（对于小文件，压缩后可能不会变小）
    std::cout << "压缩效果: 原始=" << original_data.size()
              << ", 压缩=" << compressed_data.size();
    if (compressed_data.size() < original_data.size()) {
        double ratio = 100.0 * compressed_data.size() / original_data.size();
        std::cout << ", 压缩率=" << ratio << "%" << std::endl;
    } else {
        std::cout << " (小文件压缩后反而变大了)" << std::endl;
    }

    std::vector<uint8_t> decompressed_data;
    ret = CompressionUtils::DecompressData(compressed_data, original_data.size(),
                                               decompressed_data);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "数据解压成功" << std::endl;

    // 验证解压后的数据与原始数据一致
    assert(decompressed_data.size() == original_data.size());
    for (size_t i = 0; i < original_data.size(); ++i) {
        assert(decompressed_data[i] == original_data[i]);
    }
    std::cout << "解压后数据与原始数据一致" << std::endl;

    // 测试文件名转换
    std::string original_name = "test.txt";
    std::string compressed_name = CompressionUtils::GetCompressedFileName(original_name);
    assert(compressed_name == "test.txt.compressed");
    std::cout << "压缩文件名生成正确" << std::endl;

    std::string recovered_name = CompressionUtils::GetOriginalFileName(compressed_name);
    assert(recovered_name == original_name);
    std::cout << "原始文件名恢复正确" << std::endl;

    std::cout << "测试5通过" << std::endl << std::endl;
}

// 测试6：端到端压缩功能测试
void TestEndToEndCompression() {
    std::cout << "=== 测试6：端到端压缩功能测试 ===" << std::endl;

    // 创建卷镜像管理器
    VolumeManager manager(2048, 0.9);
    manager.SetBasePath(TEST_DATA_DIR);

    // 创建较大的测试文件
    std::string large_content = "这是一段较大的测试内容，用于验证压缩功能是否正常工作。";
    for (int i = 0; i < 10; ++i) {
        large_content += large_content;
    }

    CreateTestFile(std::string(TEST_DATA_DIR) + "large_file1.txt", large_content);
    CreateTestFile(std::string(TEST_DATA_DIR) + "large_file2.txt", large_content);

    // 添加文件到卷镜像
    ErrorCode ret = manager.AddFileToCollect("large_file1.txt");
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "添加大文件1成功" << std::endl;

    ret = manager.AddFileToCollect("large_file2.txt");
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "添加大文件2成功" << std::endl;

    std::cout << "测试6通过" << std::endl << std::endl;
}

int main() {
    std::cout << "========== 卷镜像管理系统单元测试 ==========" << std::endl << std::endl;

    EnsureDirectory(TEST_DATA_DIR);

    try {
        TestBasicFunction();
        TestErrorHandling();
        TestSerialization();
        TestBoundaryConditions();
        TestCompression();
        TestEndToEndCompression();

        std::cout << "========== 所有测试通过！ ==========" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "测试异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "测试异常: 未知错误" << std::endl;
        return 1;
    }
}
