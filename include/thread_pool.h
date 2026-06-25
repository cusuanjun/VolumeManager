#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

/**
 * @file thread_pool.h
 * @brief C++11 线程池，用于替换散落的 std::async，统一管理并发度
 *
 * 使用方式：
 *   ThreadPool pool(8);
 *   auto future = pool.Enqueue([](int x) { return x * 2; }, 42);
 *   int result = future.get();  // 84
 *
 * 线程池在析构时自动等待所有任务完成并 join 所有 worker 线程。
 */

namespace volumemanager {

class ThreadPool {
public:
    /// @brief 构造线程池
    /// @param num_threads worker 线程数，0 表示自动取 hardware_concurrency()
    explicit ThreadPool(size_t num_threads = 0)
        : stop_(false)
    {
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) {
                num_threads = 4;  // hardware_concurrency 不可用时的兜底值
            }
        }

        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });

                        if (stop_ && tasks_.empty()) {
                            return;
                        }

                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }

                    task();
                }
            });
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    /// @brief 向线程池提交一个任务
    /// @tparam F 可调用对象类型
    /// @tparam Args 参数类型
    /// @param f 可调用对象
    /// @param args 参数
    /// @return std::future 持有任务返回值的 future
    template <typename F, typename... Args>
    auto Enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        // 用 packaged_task 包装任务，以便获取 future
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            if (stop_) {
                throw std::runtime_error("ThreadPool: Enqueue on stopped pool");
            }

            tasks_.emplace([task]() { (*task)(); });
        }

        condition_.notify_one();
        return result;
    }

    /// @brief 返回当前 worker 线程数
    size_t Size() const
    {
        return workers_.size();
    }

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

} // namespace volumemanager
