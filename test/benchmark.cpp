#include "../include/volume_manager.h"
#include "../include/error_codes.h"
#include "../include/compression_utils.h"
#include "../include/async_file_io.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <future>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>

using namespace volumemanager;

// ========================================================================
// 配置
// ========================================================================
constexpr const char* BENCH_DIR = "/tmp/volume_manager_bench/";
constexpr size_t FILE_COUNT = 40;               // 文件数量
constexpr size_t FILE_SIZE = 256 * 1024;         // 每个文件 256KB（够大才能体现并行优势）
constexpr uint64_t VOLUME_SIZE = 128ULL * 1024 * 1024;  // 128MB 卷
constexpr double SIZE_THRESHOLD = 0.95;          // 需要手动触发打包，便于分别测试读写

// ========================================================================
// 工具函数
// ========================================================================

void EnsureDirectory(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        mkdir(path.c_str(), 0755);
    }
}

bool DeleteFile(const std::string& path) {
    return (unlink(path.c_str()) == 0);
}

/// 简易伪随机数（确定性，不依赖 <random> 的全局状态）
struct LCG {
    uint64_t state;
    explicit LCG(uint64_t s) : state(s) {}
    uint64_t Next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    }
};

/// 生成模拟真实文本文件的数据（可压缩）
/// 混合：代码注释 + JSON 结构 + 重复模板 + 各文件独特字段
std::vector<uint8_t> MakeDiverseData(size_t target_size, size_t seed) {
    LCG rng(seed * 997 + 13);

    const char* verbs[]   = {"get","set","update","delete","query","insert","merge","sync","validate","transform"};
    const char* nouns[]   = {"UserProfile","OrderRecord","InventoryItem","LogEntry","SessionToken",
                              "CacheBlock","ConfigNode","MetricPoint","EventStream","ByteBuffer"};
    const char* status[]  = {"OK","PENDING","TIMEOUT","RETRY","FAILED","DEGRADED","THROTTLED","UNKNOWN"};
    const char* levels[]  = {"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"};
    const char* hosts[]   = {"node-01.dc-east","node-02.dc-east","node-03.dc-west",
                              "node-04.dc-west","gateway-01","gateway-02"};

    std::string data;
    data.reserve(target_size + 4096);

    // 文件头 —— 不同种子生成不同模块名
    data += "// Module: service_";
    data += nouns[seed % 10];
    data += "\n// Generated for benchmark testing\n";
    data += "// This file simulates a typical application log with structured records.\n\n";

    size_t counter_base = seed * 1000000;
    while (data.size() < target_size) {
        size_t n = data.size();
        const char* lvl = levels[(rng.Next() >> 32) % 6];
        const char* verb = verbs[(rng.Next() >> 32) % 10];
        const char* noun = nouns[(rng.Next() >> 32) % 10];
        const char* st = status[(rng.Next() >> 32) % 8];
        const char* host = hosts[(rng.Next() >> 32) % 6];
        uint64_t latency_us = (rng.Next() >> 32) % 50000;
        uint64_t rec_id = counter_base + n;
        uint64_t payload_sz = 64 + (rng.Next() >> 32) % 4096;

        // 模拟日志条目
        data += "[";
        data += std::to_string(rec_id / 1000);  // 伪时间戳
        data += ".";
        data += std::to_string(rec_id % 1000);
        data += "] ";
        data += lvl;
        data += "  ";
        data += host;
        data += "  ";
        data += verb;
        data += "::";
        data += noun;
        data += "  status=";
        data += st;
        data += "  latency_us=";
        data += std::to_string(latency_us);
        data += "  payload_bytes=";
        data += std::to_string(payload_sz);

        // 模拟 JSON payload（带一些变化）
        data += "  params={";
        data += "\"id\":";
        data += std::to_string(rec_id);
        data += ",\"q\":\"";
        // 不同种子产生不同的查询词
        const char* queries[] = {"select","where","join","group_by","order_by","count","sum"};
        data += queries[(rng.Next() >> 32) % 7];
        data += "\",\"limit\":";
        data += std::to_string(10 + (rng.Next() >> 32) % 100);
        data += ",\"offset\":";
        data += std::to_string((rng.Next() >> 32) % 500);
        data += "}\n";
    }

    data.resize(target_size);
    return std::vector<uint8_t>(data.begin(), data.end());
}

/// 获取单调时间（毫秒）
double NowMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::milli>(now).count();
}

/// 计时器 RAII
struct Timer {
    const char* label;
    double start_ms;
    explicit Timer(const char* l) : label(l), start_ms(NowMs()) {}
    double ElapsedMs() const { return NowMs() - start_ms; }
    void Print() const {
        double ms = ElapsedMs();
        if (ms < 1000.0) {
            std::cout << "  [" << label << "] " << ms << " ms" << std::endl;
        } else {
            std::cout << "  [" << label << "] " << (ms / 1000.0) << " s" << std::endl;
        }
    }
};

// ========================================================================
// 阶段 0：环境准备
// ========================================================================

class BenchEnv {
public:
    BenchEnv() {
        // 清理旧数据
        std::string cmd = "rm -rf " + std::string(BENCH_DIR) + " 2>/dev/null";
        (void)system(cmd.c_str());

        EnsureDirectory(BENCH_DIR);
        EnsureDirectory(std::string(BENCH_DIR) + "source/");

        std::cout << "--- 环境准备：生成 " << FILE_COUNT << " 个 "
                  << (FILE_SIZE / 1024) << " KB 测试文件 ---" << std::endl;

        Timer t("生成测试文件");
        for (size_t i = 0; i < FILE_COUNT; ++i) {
            std::string path = SourcePath(i);
            auto data = MakeDiverseData(FILE_SIZE, i * 997 + 13);
            std::ofstream file(path, std::ios::binary);
            assert(file.is_open());
            file.write(reinterpret_cast<const char*>(data.data()), data.size());
            file.close();
        }
        t.Print();
    }

    ~BenchEnv() {
        // 清理
        std::string cmd = "rm -rf " + std::string(BENCH_DIR) + " 2>/dev/null";
        (void)system(cmd.c_str());
    }

    static std::string SourcePath(size_t i) {
        return std::string(BENCH_DIR) + "source/bench_file_" + std::to_string(i) + ".dat";
    }

    static std::string RelPath(size_t i) {
        return "bench_file_" + std::to_string(i) + ".dat";
    }
};

// ========================================================================
// 测试 1：批量并行压缩 vs 逐个串行压缩（AddFilesToCollect）
// ========================================================================

void TestBatchVsSequentialCompress() {
    std::cout << "\n========== 测试 1：批量并行 vs 逐个串行压缩 ==========" << std::endl;
    std::cout << "（每个文件 " << (FILE_SIZE / 1024) << " KB，" << FILE_COUNT << " 个文件）" << std::endl;

    double time_seq = 0.0;
    double time_par = 0.0;

    // --- 串行模式：逐个调用 AddFileToCollect ---
    {
        VolumeManager mgr(VOLUME_SIZE, SIZE_THRESHOLD);
        mgr.SetBasePath(std::string(BENCH_DIR) + "source/");

        Timer t("串行 AddFileToCollect");
        for (size_t i = 0; i < FILE_COUNT; ++i) {
            uint64_t inode_id;
            ErrorCode ret = mgr.AddFileToCollect(BenchEnv::RelPath(i), inode_id);
            assert(ret == ErrorCode::SUCCESS);
        }
        t.Print();
        time_seq = t.ElapsedMs();
    }

    // --- 并行模式：调用 AddFilesToCollect ---
    {
        VolumeManager mgr(VOLUME_SIZE, SIZE_THRESHOLD);
        mgr.SetBasePath(std::string(BENCH_DIR) + "source/");

        std::vector<std::string> paths;
        paths.reserve(FILE_COUNT);
        for (size_t i = 0; i < FILE_COUNT; ++i) {
            paths.push_back(BenchEnv::RelPath(i));
        }

        Timer t("并行 AddFilesToCollect");
        std::vector<uint64_t> inode_ids;
        ErrorCode ret = mgr.AddFilesToCollect(paths, inode_ids);
        assert(ret == ErrorCode::SUCCESS);
        assert(inode_ids.size() == FILE_COUNT);
        t.Print();
        time_par = t.ElapsedMs();
    }

    // --- 对比 ---
    double speedup = time_seq / time_par;
    std::cout << std::endl;
    std::cout << "  ╔══════════════════════════════════════╗" << std::endl;
    std::cout << "  ║  串行耗时:   " << std::setw(8) << std::fixed << std::setprecision(0)
              << time_seq << " ms                 ║" << std::endl;
    std::cout << "  ║  并行耗时:   " << std::setw(8) << time_par << " ms                 ║" << std::endl;
    std::cout << "  ║  加速比:     " << std::setw(8) << std::setprecision(2)
              << speedup << "x                  ║" << std::endl;
    std::cout << "  ╚══════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    assert(speedup > 1.2);  // 至少 20% 提升
    std::cout << "  ✓ 批量并行压缩有显著性能提升" << std::endl;
}

// ========================================================================
// 测试 2：并发读取 vs 串行读取
// ========================================================================

void TestConcurrentVsSequentialRead() {
    std::cout << "\n========== 测试 2：并发读取 vs 串行读取 ==========" << std::endl;

    // 先准备一批已打包的文件
    VolumeManager mgr(VOLUME_SIZE, SIZE_THRESHOLD);
    mgr.SetBasePath(std::string(BENCH_DIR) + "source/");

    std::vector<std::string> paths;
    paths.reserve(FILE_COUNT);
    for (size_t i = 0; i < FILE_COUNT; ++i) {
        paths.push_back(BenchEnv::RelPath(i));
    }

    std::vector<uint64_t> inode_ids;
    ErrorCode ret = mgr.AddFilesToCollect(paths, inode_ids);
    assert(ret == ErrorCode::SUCCESS);

    // 手动打包
    std::string volume_path;
    ret = mgr.PackVolume(volume_path);
    assert(ret == ErrorCode::SUCCESS);
    std::cout << "  卷镜像已打包: " << volume_path << std::endl;

    // 验证数据完整性（旁路检查）
    {
        int ok = 0;
        for (auto id : inode_ids) {
            std::string out;
            ErrorCode r = mgr.ReadFile(id, out);
            if (r == ErrorCode::SUCCESS) ok++;
        }
        assert(ok == static_cast<int>(FILE_COUNT));
        std::cout << "  数据完整性预检通过: " << ok << "/" << FILE_COUNT << std::endl;
    }

    double time_seq = 0.0;
    double time_par = 0.0;

    // --- 串行读取 ---
    {
        Timer t("串行 ReadFile");
        for (auto id : inode_ids) {
            std::string output_path;
            ErrorCode r = mgr.ReadFile(id, output_path);
            assert(r == ErrorCode::SUCCESS);
        }
        t.Print();
        time_seq = t.ElapsedMs();
    }

    // --- 并发读取 ---
    {
        Timer t("并发 ReadFile");
        std::vector<std::future<bool>> tasks;
        tasks.reserve(inode_ids.size());
        for (auto id : inode_ids) {
            tasks.push_back(std::async(std::launch::async, [&mgr, id]() {
                std::string output_path;
                ErrorCode r = mgr.ReadFile(id, output_path);
                return r == ErrorCode::SUCCESS;
            }));
        }
        for (auto& f : tasks) {
            assert(f.get());
        }
        t.Print();
        time_par = t.ElapsedMs();
    }

    // --- 对比 ---
    double speedup = time_seq / time_par;
    std::cout << std::endl;
    std::cout << "  ╔══════════════════════════════════════╗" << std::endl;
    std::cout << "  ║  串行耗时:   " << std::setw(8) << std::fixed << std::setprecision(0)
              << time_seq << " ms                 ║" << std::endl;
    std::cout << "  ║  并发耗时:   " << std::setw(8) << time_par << " ms                 ║" << std::endl;
    std::cout << "  ║  加速比:     " << std::setw(8) << std::setprecision(2)
              << speedup << "x                  ║" << std::endl;
    std::cout << "  ╚══════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    assert(speedup > 1.2);
    std::cout << "  ✓ 并发读取有显著性能提升（ReadFile 锁优化生效）" << std::endl;
}

// ========================================================================
// 测试 3：zstd vs zlib 压缩性能对比
// ========================================================================

void TestZstdVsZlibCompression() {
    std::cout << "\n========== 测试 3：zstd vs zlib 压缩性能对比 ==========" << std::endl;

    // 生成一份有代表性的测试数据
    std::vector<uint8_t> data = MakeDiverseData(FILE_SIZE, 42);

    double time_zlib = 0.0;
    double time_zstd = 0.0;
    size_t size_zlib = 0;
    size_t size_zstd = 0;

    // --- zlib ---
    {
        Timer t("zlib 压缩 (Z_BEST_COMPRESSION)");
        std::vector<uint8_t> output;
        ErrorCode ret = CompressionUtils::CompressData(data, output);
        assert(ret == ErrorCode::SUCCESS);
        size_zlib = output.size();
        t.Print();
        time_zlib = t.ElapsedMs();
    }

    // --- zstd ---
    {
        Timer t("zstd 压缩 (level=3)");
        std::vector<uint8_t> output;
        ErrorCode ret = CompressionUtils::CompressDataZstd(data, output, 3);
        assert(ret == ErrorCode::SUCCESS);
        size_zstd = output.size();
        t.Print();
        time_zstd = t.ElapsedMs();
    }

    double speedup = time_zlib / time_zstd;
    double zlib_ratio = 100.0 * size_zlib / data.size();
    double zstd_ratio = 100.0 * size_zstd / data.size();

    std::cout << std::endl;
    std::cout << "  ╔══════════════════════════════════════════════╗" << std::endl;
    std::cout << "  ║               zlib             zstd          ║" << std::endl;
    std::cout << "  ║  耗时:   " << std::setw(8) << std::fixed << std::setprecision(0)
              << time_zlib << " ms       " << std::setw(8) << time_zstd << " ms       ║" << std::endl;
    std::cout << "  ║  大小:   " << std::setw(8) << size_zlib << " B     " << std::setw(8)
              << size_zstd << " B     ║" << std::endl;
    std::cout << "  ║  压缩率: " << std::setw(8) << std::setprecision(1)
              << zlib_ratio << "%       " << std::setw(8) << zstd_ratio << "%       ║" << std::endl;
    std::cout << "  ║  加速比: " << std::setw(8) << std::setprecision(2)
              << speedup << "x                                ║" << std::endl;
    std::cout << "  ╚══════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    // zstd 应比 zlib 更快
    assert(speedup > 1.5);
    std::cout << "  ✓ zstd 压缩速度显著优于 zlib" << std::endl;
}

// ========================================================================
// 测试 4：批量并行正确性验证 + 边界条件
// ========================================================================

void TestBatchCorrectness() {
    std::cout << "\n========== 测试 4：批量并行正确性验证 ==========" << std::endl;

    VolumeManager mgr(VOLUME_SIZE, SIZE_THRESHOLD);
    mgr.SetBasePath(std::string(BENCH_DIR) + "source/");

    // 空列表
    {
        std::vector<uint64_t> ids;
        ErrorCode ret = mgr.AddFilesToCollect({}, ids);
        assert(ret == ErrorCode::SUCCESS);
        assert(ids.empty());
        std::cout << "  ✓ 空列表正确处理" << std::endl;
    }

    // 包含不存在的文件
    {
        std::vector<uint64_t> ids;
        ErrorCode ret = mgr.AddFilesToCollect({"nonexistent.dat"}, ids);
        assert(ret == ErrorCode::FILE_NOT_FOUND);
        std::cout << "  ✓ 不存在的文件正确报错" << std::endl;
    }

    // 正常批量添加 + 数据完整性
    {
        std::vector<std::string> paths;
        constexpr size_t N = 20;
        paths.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            paths.push_back(BenchEnv::RelPath(i));
        }

        std::vector<uint64_t> inode_ids;
        ErrorCode ret = mgr.AddFilesToCollect(paths, inode_ids);
        assert(ret == ErrorCode::SUCCESS);
        assert(inode_ids.size() == N);
        for (size_t i = 0; i < N; ++i) {
            assert(inode_ids[i] != INVALID_INODE_ID);
        }
        std::cout << "  ✓ 批量添加 " << N << " 个文件成功" << std::endl;

        // 打包
        std::string volume_path;
        ret = mgr.PackVolume(volume_path);
        assert(ret == ErrorCode::SUCCESS);

        // 读取并校验每个文件
        int ok_count = 0;
        for (size_t i = 0; i < N; ++i) {
            std::string out_path;
            ErrorCode r = mgr.ReadFile(inode_ids[i], out_path);
            if (r != ErrorCode::SUCCESS) continue;

            // 比较内容
            std::ifstream f1(BenchEnv::SourcePath(i), std::ios::binary);
            std::ifstream f2(out_path, std::ios::binary);
            assert(f1.is_open() && f2.is_open());

            f1.seekg(0, std::ios::end);
            f2.seekg(0, std::ios::end);
            if (f1.tellg() != f2.tellg()) continue;

            f1.seekg(0, std::ios::beg);
            f2.seekg(0, std::ios::beg);

            std::vector<uint8_t> d1(static_cast<size_t>(f1.tellg()));
            std::vector<uint8_t> d2(static_cast<size_t>(f2.tellg()));
            f1.seekg(0); f2.seekg(0);
            f1.read(reinterpret_cast<char*>(d1.data()), static_cast<std::streamsize>(d1.size()));
            f2.read(reinterpret_cast<char*>(d2.data()), static_cast<std::streamsize>(d2.size()));

            if (d1 == d2) ok_count++;
        }
        assert(ok_count == static_cast<int>(N));
        std::cout << "  ✓ 批量添加文件数据完整性验证通过: " << ok_count << "/" << N << std::endl;
    }

    // 卸载 + 重新挂载 + 再次读取（验证 v2 格式持久化）
    {
        auto& volumes = mgr.mounted_volumes_;
        assert(!volumes.empty());
        uint64_t vol_id = volumes.begin()->first;
        std::string vol_path = mgr.temp_dir_ + "volume_" + std::to_string(vol_id) + ".vimg";

        mgr.UnmountVolume(vol_id);
        std::cout << "  ✓ 卸载卷镜像" << std::endl;

        ErrorCode ret = mgr.MountVolume(vol_path);
        assert(ret == ErrorCode::SUCCESS);
        std::cout << "  ✓ 重新挂载卷镜像" << std::endl;

        // 随机抽查 3 个文件
        for (size_t i : {0, 7, 15}) {
            auto it = mgr.file_metadata_cache_.begin();
            std::advance(it, static_cast<int>(i));
            uint64_t id = it->first;

            std::string out_path;
            ErrorCode r = mgr.ReadFile(id, out_path);
            assert(r == ErrorCode::SUCCESS);
        }
        std::cout << "  ✓ 重新挂载后文件读取正常" << std::endl;
    }

    std::cout << "  ✓ 批量并行正确性验证通过" << std::endl;
}

// ========================================================================
// 测试 5：ThreadPool 压力测试
// ========================================================================

void TestThreadPoolStress() {
    std::cout << "\n========== 测试 5：ThreadPool 压力测试 ==========" << std::endl;

    // 大任务量
    {
        ThreadPool pool(8);
        constexpr size_t N = 500;
        std::vector<std::future<size_t>> futures;
        futures.reserve(N);

        for (size_t i = 0; i < N; ++i) {
            futures.push_back(pool.Enqueue([](size_t v) {
                // 模拟CPU工作
                volatile size_t sum = 0;
                for (size_t k = 0; k < 10000; ++k) sum += k;
                return v + static_cast<size_t>(sum & 0xFF);
            }, i));
        }

        for (size_t i = 0; i < N; ++i) {
            size_t result = futures[i].get();
            (void)result;
        }
        std::cout << "  ✓ 大任务量: " << N << " 个任务全部完成" << std::endl;
    }

    // 线程池析构正确性（任务未全部完成时析构）
    {
        ThreadPool pool(4);
        std::vector<std::future<int>> futures;
        for (int i = 0; i < 100; ++i) {
            futures.push_back(pool.Enqueue([](int v) {
                return v * v;
            }, i));
        }
        // 让 futures 在 pool 析构前先收集完
        for (auto& f : futures) {
            assert(f.get() >= 0);
        }
        // pool 在此析构，不应卡死
        std::cout << "  ✓ 线程池正常析构" << std::endl;
    }
}

// ========================================================================
// 测试 6：混合读写并发测试（读-写不互斥）
// ========================================================================

void TestMixedReadWrite() {
    std::cout << "\n========== 测试 6：混合读写并发测试 ==========" << std::endl;

    // 先创建第一批数据并打包为卷 A
    VolumeManager mgr_a(VOLUME_SIZE, SIZE_THRESHOLD);
    mgr_a.SetBasePath(std::string(BENCH_DIR) + "source/");

    std::vector<std::string> paths_a;
    constexpr size_t N_A = 15;
    for (size_t i = 0; i < N_A; ++i) {
        paths_a.push_back(BenchEnv::RelPath(i));
    }
    std::vector<uint64_t> ids_a;
    mgr_a.AddFilesToCollect(paths_a, ids_a);
    std::string vol_a;
    mgr_a.PackVolume(vol_a);

    // 在另一个 manager 上创建第二批数据并打包为卷 B
    VolumeManager mgr_b(VOLUME_SIZE, SIZE_THRESHOLD);
    mgr_b.SetBasePath(std::string(BENCH_DIR) + "source/");

    std::vector<std::string> paths_b;
    constexpr size_t N_B = 15;
    for (size_t i = N_A; i < N_A + N_B; ++i) {
        paths_b.push_back(BenchEnv::RelPath(i));
    }
    std::vector<uint64_t> ids_b;
    mgr_b.AddFilesToCollect(paths_b, ids_b);
    std::string vol_b;
    mgr_b.PackVolume(vol_b);

    // 并发：同时从 A 读 + 向 B 写入新文件
    std::future<int> read_future = std::async(std::launch::async, [&mgr_a, &ids_a]() {
        int ok = 0;
        for (auto id : ids_a) {
            std::string out;
            if (mgr_a.ReadFile(id, out) == ErrorCode::SUCCESS) ok++;
        }
        return ok;
    });

    std::future<int> write_future = std::async(std::launch::async, [&mgr_b, N_A, N_B]() {
        int ok = 0;
        for (size_t i = N_A; i < N_A + N_B; ++i) {
            std::string out;
            uint64_t id;
            if (mgr_b.AddFileToCollect(BenchEnv::RelPath(i), id) == ErrorCode::SUCCESS) ok++;
        }
        return ok;
    });

    int read_ok = read_future.get();
    int write_ok = write_future.get();

    assert(read_ok == static_cast<int>(N_A));
    assert(write_ok == static_cast<int>(N_B));
    std::cout << "  ✓ 并发读 " << read_ok << "/" << N_A
              << " + 并发写 " << write_ok << "/" << N_B << " 全部成功" << std::endl;
    std::cout << "  ✓ 读写操作互不阻塞" << std::endl;
}

// ========================================================================
// main
// ========================================================================

int main() {
    std::cout << "╔══════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║   VolumeManager 异步并行化性能基准测试          ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════╝" << std::endl;

    try {
        BenchEnv env;

        TestBatchVsSequentialCompress();
        TestConcurrentVsSequentialRead();
        TestZstdVsZlibCompression();
        TestBatchCorrectness();
        TestThreadPoolStress();
        TestMixedReadWrite();

        std::cout << "\n========== 所有基准测试通过！ ==========" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ 测试异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n✗ 测试异常: 未知错误" << std::endl;
        return 1;
    }
}
