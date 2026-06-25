#include "../include/volume_manager.h"
#include "../include/error_codes.h"
#include "../include/serializer.h"
#include "../include/compression_utils.h"
#include "../include/async_file_io.h"
#include <iostream>
#include <cassert>
#include <algorithm>
#include <fstream>
#include <future>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>

using namespace volumemanager;

constexpr const char* TEST_DATA_DIR = "./test_data/";
constexpr const char* ASYNC_IO_TEST_DIR = "/tmp/volume_manager_async_io";

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

std::vector<uint8_t> MakePatternData(size_t size, uint8_t seed) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>((seed + i * 31) & 0xFF);
    }
    return data;
}

bool CompareVectors(const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] != rhs[i]) {
            return false;
        }
    }
    return true;
}

std::string JoinPath(const std::string& dir, const std::string& name) {
    if (!dir.empty() && dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

void TestAsyncFileIO() {
    std::cout << "=== 测试6：异步文件I/O基础测试 ===" << std::endl;

    EnsureDirectory(ASYNC_IO_TEST_DIR);
    const std::string empty_path = JoinPath(ASYNC_IO_TEST_DIR, "empty.bin");
    const std::string binary_path = JoinPath(ASYNC_IO_TEST_DIR, "binary.bin");
    const std::string patch_path = JoinPath(ASYNC_IO_TEST_DIR, "patch.bin");
    const std::string missing_path = JoinPath(ASYNC_IO_TEST_DIR, "missing.bin");

    std::vector<uint8_t> empty_content;
    ErrorCode ret = AsyncWriteAll(empty_path, empty_content);
    assert(ret == ErrorCode::SUCCESS);

    std::vector<uint8_t> read_back;
    ret = AsyncReadAll(empty_path, read_back);
    assert(ret == ErrorCode::SUCCESS);
    assert(read_back.empty());
    std::cout << "空文件读写成功" << std::endl;

    std::vector<uint8_t> original = MakePatternData(4096, 17);
    ret = AsyncWriteAll(binary_path, original);
    assert(ret == ErrorCode::SUCCESS);

    read_back.clear();
    ret = AsyncReadAll(binary_path, read_back);
    assert(ret == ErrorCode::SUCCESS);
    assert(CompareVectors(original, read_back));
    std::cout << "整文件读写成功" << std::endl;

    std::vector<uint8_t> slice;
    ret = AsyncReadAt(binary_path, 128, 256, slice);
    assert(ret == ErrorCode::SUCCESS);
    assert(slice.size() == 256);
    assert(std::equal(slice.begin(), slice.end(), original.begin() + 128));
    std::cout << "按偏移读取成功" << std::endl;

    std::vector<uint8_t> tail;
    ret = AsyncReadAt(binary_path, original.size() - 1, 1, tail);
    assert(ret == ErrorCode::SUCCESS);
    assert(tail.size() == 1);
    assert(tail[0] == original.back());
    std::cout << "边界偏移读取成功" << std::endl;

    std::vector<uint8_t> out_of_range;
    ret = AsyncReadAt(binary_path, original.size(), 1, out_of_range);
    assert(ret == ErrorCode::IO_ERROR);
    std::cout << "越界读取正确返回错误" << std::endl;

    ret = AsyncReadAll(missing_path, read_back);
    assert(ret == ErrorCode::FILE_NOT_FOUND);
    std::cout << "缺失文件读取正确返回错误" << std::endl;

    std::vector<uint8_t> patch_source = MakePatternData(512, 203);
    ret = AsyncWriteAll(patch_path, original);
    assert(ret == ErrorCode::SUCCESS);

    ret = AsyncWriteAt(patch_path, 1024, patch_source);
    assert(ret == ErrorCode::SUCCESS);

    std::vector<uint8_t> patched;
    ret = AsyncReadAll(patch_path, patched);
    assert(ret == ErrorCode::SUCCESS);
    assert(patched.size() == original.size());
    assert(std::equal(patched.begin(), patched.begin() + 1024, original.begin()));
    assert(std::equal(patched.begin() + 1024,
                      patched.begin() + 1024 + patch_source.size(),
                      patch_source.begin()));
    assert(std::equal(patched.begin() + 1024 + patch_source.size(),
                      patched.end(),
                      original.begin() + 1024 + patch_source.size()));
    std::cout << "按偏移写入成功" << std::endl;

    ret = AsyncDeleteFile(empty_path);
    assert(ret == ErrorCode::SUCCESS);
    ret = AsyncDeleteFile(binary_path);
    assert(ret == ErrorCode::SUCCESS);
    ret = AsyncDeleteFile(patch_path);
    assert(ret == ErrorCode::SUCCESS);
    ret = AsyncDeleteFile(missing_path);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "文件删除成功" << std::endl;

    std::cout << "测试6通过" << std::endl << std::endl;
}

void TestAsyncFileIOConcurrency() {
    std::cout << "=== 测试7：异步文件I/O并发测试 ===" << std::endl;

    EnsureDirectory(ASYNC_IO_TEST_DIR);

    constexpr size_t FILE_COUNT = 12;
    std::vector<std::string> paths;
    std::vector<std::vector<uint8_t>> expected_data;
    paths.reserve(FILE_COUNT);
    expected_data.reserve(FILE_COUNT);

    for (size_t i = 0; i < FILE_COUNT; ++i) {
        paths.push_back(JoinPath(ASYNC_IO_TEST_DIR, "concurrent_" + std::to_string(i) + ".bin"));
        expected_data.push_back(MakePatternData(512 + i * 73, static_cast<uint8_t>(11 + i * 17)));
    }

    std::vector<std::future<ErrorCode>> write_tasks;
    for (size_t i = 0; i < FILE_COUNT; ++i) {
        std::string path = paths[i];
        std::vector<uint8_t> data = expected_data[i];
        write_tasks.push_back(std::async(std::launch::async, [path, data]() {
            return AsyncWriteAll(path, data);
        }));
    }

    for (auto& task : write_tasks) {
        assert(task.get() == ErrorCode::SUCCESS);
    }
    std::cout << "并发写文件成功" << std::endl;

    std::vector<std::future<bool>> read_tasks;
    for (size_t i = 0; i < FILE_COUNT; ++i) {
        std::string path = paths[i];
        std::vector<uint8_t> data = expected_data[i];
        read_tasks.push_back(std::async(std::launch::async, [path, data]() {
            std::vector<uint8_t> content;
            if (AsyncReadAll(path, content) != ErrorCode::SUCCESS) {
                return false;
            }
            return CompareVectors(content, data);
        }));
    }

    for (auto& task : read_tasks) {
        assert(task.get());
    }
    std::cout << "并发读文件成功" << std::endl;

    const std::string shared_path = JoinPath(ASYNC_IO_TEST_DIR, "shared.bin");
    std::vector<uint8_t> shared_data = MakePatternData(4096, 91);
    ErrorCode ret = AsyncWriteAll(shared_path, shared_data);
    assert(ret == ErrorCode::SUCCESS);

    std::vector<std::pair<uint64_t, uint64_t>> ranges = {
        {0, 128},
        {64, 256},
        {1024, 512},
        {2048, 1024},
        {3500, 300}
    };

    std::vector<std::future<bool>> range_tasks;
    for (const auto& range : ranges) {
        const uint64_t offset = range.first;
        const uint64_t size = range.second;
        range_tasks.push_back(std::async(std::launch::async, [shared_path, shared_data, offset, size]() {
            std::vector<uint8_t> chunk;
            if (AsyncReadAt(shared_path, offset, size, chunk) != ErrorCode::SUCCESS) {
                return false;
            }
            return chunk.size() == size &&
                   std::equal(chunk.begin(), chunk.end(), shared_data.begin() + offset);
        }));
    }

    for (auto& task : range_tasks) {
        assert(task.get());
    }
    std::cout << "并发偏移读取成功" << std::endl;

    std::vector<std::future<ErrorCode>> delete_tasks;
    for (const auto& path : paths) {
        delete_tasks.push_back(std::async(std::launch::async, [path]() {
            return AsyncDeleteFile(path);
        }));
    }
    delete_tasks.push_back(std::async(std::launch::async, [shared_path]() {
        return AsyncDeleteFile(shared_path);
    }));

    for (auto& task : delete_tasks) {
        assert(task.get() == ErrorCode::SUCCESS);
    }
    std::cout << "并发删除文件成功" << std::endl;

    std::cout << "测试7通过" << std::endl << std::endl;
}

void TestCompressionFileRoundTrip() {
    std::cout << "=== 测试8：压缩文件I/O回环测试 ===" << std::endl;

    EnsureDirectory(ASYNC_IO_TEST_DIR);
    const std::string source_path = JoinPath(ASYNC_IO_TEST_DIR, "compress_source.txt");
    const std::string compressed_path = JoinPath(ASYNC_IO_TEST_DIR, "compress_source.txt.compressed");
    const std::string restored_path = JoinPath(ASYNC_IO_TEST_DIR, "compress_restored.txt");
    const std::string empty_path = JoinPath(ASYNC_IO_TEST_DIR, "compress_empty.txt");
    const std::string empty_compressed_path = JoinPath(ASYNC_IO_TEST_DIR, "compress_empty.txt.compressed");

    std::string text = "VolumeManager compression file round trip test.\n";
    for (int i = 0; i < 20; ++i) {
        text += "payload_" + std::to_string(i) + "_";
    }
    std::vector<uint8_t> source_data(text.begin(), text.end());

    ErrorCode ret = AsyncWriteAll(source_path, source_data);
    assert(ret == ErrorCode::SUCCESS);

    ret = CompressionUtils::CompressFile(source_path, compressed_path);
    assert(ret == ErrorCode::SUCCESS);

    ret = CompressionUtils::DecompressFile(compressed_path, restored_path, source_data.size());
    assert(ret == ErrorCode::SUCCESS);
    assert(CompareFileContent(source_path, restored_path));
    std::cout << "普通文件压缩/解压回环成功" << std::endl;

    ret = AsyncWriteAll(empty_path, {});
    assert(ret == ErrorCode::SUCCESS);

    ret = CompressionUtils::CompressFile(empty_path, empty_compressed_path);
    assert(ret == ErrorCode::SUCCESS);

    ret = CompressionUtils::DecompressFile(empty_compressed_path, restored_path, 0);
    assert(ret == ErrorCode::INVALID_PARAMETER);
    std::cout << "空文件解压参数校验正确" << std::endl;

    ret = AsyncDeleteFile(source_path);
    assert(ret == ErrorCode::SUCCESS);
    ret = AsyncDeleteFile(compressed_path);
    assert(ret == ErrorCode::SUCCESS);
    ret = AsyncDeleteFile(restored_path);
    assert(ret == ErrorCode::SUCCESS);
    ret = AsyncDeleteFile(empty_path);
    assert(ret == ErrorCode::SUCCESS);
    ret = AsyncDeleteFile(empty_compressed_path);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "压缩文件I/O测试资源清理成功" << std::endl;

    std::cout << "测试8通过" << std::endl << std::endl;
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
    file_meta.compressed_file_name = "test.txt.zst";
    file_meta.file_mode = 0644;
    file_meta.last_modified = 1234567890;
    file_meta.is_directory = false;
    file_meta.is_compressed = true;
    file_meta.compression_algorithm = CompressionAlgorithm::ZSTD;

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
    assert(compressed_name == "test.txt.zst");
    std::cout << "zstd压缩文件名生成正确" << std::endl;

    // 显式指定 zlib
    std::string compressed_zlib = CompressionUtils::GetCompressedFileName(
        original_name, CompressionAlgorithm::ZLIB);
    assert(compressed_zlib == "test.txt.compressed");
    std::cout << "zlib压缩文件名生成正确" << std::endl;

    std::string recovered_name = CompressionUtils::GetOriginalFileName(compressed_name);
    assert(recovered_name == original_name);
    std::cout << "zstd原始文件名恢复正确" << std::endl;

    std::string recovered_zlib = CompressionUtils::GetOriginalFileName(compressed_zlib);
    assert(recovered_zlib == original_name);
    std::cout << "zlib原始文件名恢复正确" << std::endl;

    std::cout << "测试5通过" << std::endl << std::endl;
}

// 测试9：端到端压缩功能测试
void TestEndToEndCompression() {
    std::cout << "=== 测试9：端到端压缩功能测试 ===" << std::endl;

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

    std::cout << "测试9通过" << std::endl << std::endl;
}

// 测试10：ThreadPool 测试
void TestThreadPool() {
    std::cout << "=== 测试10：ThreadPool 基础测试 ===" << std::endl;

    // 基础任务提交
    ThreadPool pool(4);
    std::cout << "线程池大小: " << pool.Size() << std::endl;
    assert(pool.Size() == 4);

    auto future = pool.Enqueue([](int x) { return x * 2; }, 42);
    assert(future.get() == 84);
    std::cout << "基础任务提交成功" << std::endl;

    // 并发任务测试
    constexpr int TASK_COUNT = 20;
    std::vector<std::future<int>> futures;
    futures.reserve(TASK_COUNT);
    for (int i = 0; i < TASK_COUNT; ++i) {
        futures.push_back(pool.Enqueue([](int val) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return val * val;
        }, i));
    }

    for (int i = 0; i < TASK_COUNT; ++i) {
        assert(futures[i].get() == i * i);
    }
    std::cout << "并发任务测试通过 (" << TASK_COUNT << " 个任务)" << std::endl;

    // 默认大小
    ThreadPool default_pool;
    assert(default_pool.Size() > 0);
    std::cout << "默认线程池大小: " << default_pool.Size() << std::endl;

    std::cout << "测试10通过" << std::endl << std::endl;
}

// 测试11：zstd 压缩/解压测试
void TestZstdCompression() {
    std::cout << "=== 测试11：zstd 压缩/解压测试 ===" << std::endl;

    // 回环测试
    std::string test_data = "这是一段用于测试zstd压缩功能的文本数据，应当能够被正确压缩和解压。";
    std::vector<uint8_t> original_data(test_data.begin(), test_data.end());

    std::vector<uint8_t> compressed_data;
    ErrorCode ret = CompressionUtils::CompressDataZstd(original_data, compressed_data);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "zstd数据压缩成功，原始大小: " << original_data.size()
              << ", 压缩后大小: " << compressed_data.size() << std::endl;

    std::vector<uint8_t> decompressed_data;
    ret = CompressionUtils::DecompressDataZstd(compressed_data, decompressed_data);
    assert(ret == ErrorCode::SUCCESS);
    assert(decompressed_data.size() == original_data.size());
    for (size_t i = 0; i < original_data.size(); ++i) {
        assert(decompressed_data[i] == original_data[i]);
    }
    std::cout << "zstd数据解压成功，数据一致" << std::endl;

    // 空输入测试
    std::vector<uint8_t> empty;
    std::vector<uint8_t> empty_compressed;
    ret = CompressionUtils::CompressDataZstd(empty, empty_compressed);
    assert(ret == ErrorCode::SUCCESS);
    assert(empty_compressed.empty());

    std::vector<uint8_t> empty_decompressed;
    ret = CompressionUtils::DecompressDataZstd(empty_compressed, empty_decompressed);
    assert(ret == ErrorCode::SUCCESS);
    assert(empty_decompressed.empty());
    std::cout << "zstd空输入处理正确" << std::endl;

    // 不同压缩级别
    std::vector<uint8_t> pattern = MakePatternData(4096, 42);
    for (int level : {1, 3, 10, 22}) {
        std::vector<uint8_t> compressed;
        ret = CompressionUtils::CompressDataZstd(pattern, compressed, level);
        assert(ret == ErrorCode::SUCCESS);

        std::vector<uint8_t> decompressed;
        ret = CompressionUtils::DecompressDataZstd(compressed, decompressed);
        assert(ret == ErrorCode::SUCCESS);
        assert(CompareVectors(pattern, decompressed));
    }
    std::cout << "zstd多级别压缩测试通过（级别 1, 3, 10, 22）" << std::endl;

    // 文件名后缀检测
    assert(CompressionUtils::GetAlgorithmFromFileName("test.txt.zst") ==
           CompressionAlgorithm::ZSTD);
    assert(CompressionUtils::GetAlgorithmFromFileName("test.txt.compressed") ==
           CompressionAlgorithm::ZLIB);
    assert(CompressionUtils::GetAlgorithmFromFileName("test.txt") ==
           CompressionAlgorithm::ZLIB);  // 未知后缀默认 zlib
    assert(CompressionUtils::IsCompressedFileName("test.txt.zst"));
    assert(CompressionUtils::IsCompressedFileName("test.txt.compressed"));
    assert(!CompressionUtils::IsCompressedFileName("test.txt"));
    std::cout << "文件名后缀检测正确" << std::endl;

    std::cout << "测试11通过" << std::endl << std::endl;
}

int main() {
    std::cout << "========== 卷镜像管理系统单元测试 ==========" << std::endl << std::endl;

    EnsureDirectory(TEST_DATA_DIR);
    EnsureDirectory(ASYNC_IO_TEST_DIR);

    try {
        TestBasicFunction();
        TestErrorHandling();
        TestSerialization();
        TestBoundaryConditions();
        TestCompression();
        TestAsyncFileIO();
        TestAsyncFileIOConcurrency();
        TestCompressionFileRoundTrip();
        TestEndToEndCompression();
        TestThreadPool();
        TestZstdCompression();

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
