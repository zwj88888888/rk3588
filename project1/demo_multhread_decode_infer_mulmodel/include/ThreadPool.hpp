/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cassert>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <stdexcept>

namespace dpool
{

    class ThreadPool
    {
    public:
        using MutexGuard = std::lock_guard<std::mutex>;
        using UniqueLock = std::unique_lock<std::mutex>;
        using Thread = std::thread;
        using ThreadID = std::thread::id;
        using Task = std::function<void()>;

        // 构造函数：自动检测硬件线程数
        ThreadPool() : ThreadPool(std::thread::hardware_concurrency()) {}

        // 构造函数：指定最大线程数
        explicit ThreadPool(size_t maxThreads) 
            : quit_(false),
              currentThreads_(0),
              idleThreads_(0),
              maxThreads_(maxThreads > 0 ? maxThreads : 1),
              taskCount_(0),
              completedTaskCount_(0)
        {
            if (maxThreads == 0) {
                throw std::invalid_argument("Thread count cannot be zero");
            }
        }

        // 禁用拷贝操作
        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        // 析构函数：优雅关闭
        ~ThreadPool()
        {
            shutdown();
        }

        // 提交任务到线程池
        template <typename Func, typename... Ts>
        auto submit(Func &&func, Ts &&...params)
            -> std::future<typename std::result_of<Func(Ts...)>::type>
        {
            if (quit_.load()) {
                throw std::runtime_error("ThreadPool is shutdown");
            }

            auto execute = std::bind(std::forward<Func>(func), std::forward<Ts>(params)...);
            using ReturnType = typename std::result_of<Func(Ts...)>::type;
            using PackagedTask = std::packaged_task<ReturnType()>;

            auto task = std::make_shared<PackagedTask>(std::move(execute));
            auto result = task->get_future();

            {
                MutexGuard guard(mutex_);
                tasks_.emplace([task]() { (*task)(); });
                ++taskCount_;
            }

            // 通知工作线程
            if (idleThreads_ > 0) {
                cv_.notify_one();
            } else if (currentThreads_ < maxThreads_) {
                createWorkerThread();
            }

            return result;
        }

        // 获取当前线程数
        size_t getCurrentThreadCount() const
        {
            MutexGuard guard(mutex_);
            return currentThreads_;
        }

        // 获取空闲线程数
        size_t getIdleThreadCount() const
        {
            MutexGuard guard(mutex_);
            return idleThreads_;
        }

        // 获取最大线程数
        size_t getMaxThreadCount() const
        {
            return maxThreads_;
        }

        // 获取待处理任务数
        size_t getPendingTaskCount() const
        {
            MutexGuard guard(mutex_);
            return tasks_.size();
        }

        // 获取已完成任务数
        size_t getCompletedTaskCount() const
        {
            return completedTaskCount_.load();
        }

        // 获取总任务数
        size_t getTotalTaskCount() const
        {
            return taskCount_.load();
        }

        // 检查是否已关闭
        bool isShutdown() const
        {
            return quit_.load();
        }

        // 优雅关闭线程池
        void shutdown()
        {
            {
                MutexGuard guard(mutex_);
                quit_ = true;
            }
            cv_.notify_all();

            // 等待所有工作线程完成
            for (auto &elem : threads_) {
                if (elem.second.joinable()) {
                    elem.second.join();
                }
            }
            threads_.clear();
            currentThreads_ = 0;
        }

        // 等待所有任务完成
        void waitForAll()
        {
            UniqueLock lock(mutex_);
            cv_.wait(lock, [this]() { return tasks_.empty() && (idleThreads_ == currentThreads_); });
        }

        // 清空待处理任务队列
        void clearPendingTasks()
        {
            MutexGuard guard(mutex_);
            std::queue<Task> empty;
            std::swap(tasks_, empty);
        }

    private:
        // 创建工作线程
        void createWorkerThread()
        {
            Thread t(&ThreadPool::worker, this);
            threads_[t.get_id()] = std::move(t);
            ++currentThreads_;
        }

        // 工作线程函数
        void worker()
        {
            while (true)
            {
                Task task;
                {
                    UniqueLock uniqueLock(mutex_);
                    ++idleThreads_;
                    
                    // 等待任务或关闭信号
                    auto hasTimedout = !cv_.wait_for(uniqueLock,
                                                     std::chrono::seconds(WAIT_SECONDS),
                                                     [this]() {
                                                         return quit_ || !tasks_.empty();
                                                     });
                    --idleThreads_;
                    
                    if (tasks_.empty()) {
                        if (quit_) {
                            --currentThreads_;
                            return;
                        }
                        if (hasTimedout) {
                            --currentThreads_;
                            joinFinishedThreads();
                            finishedThreadIDs_.emplace(std::this_thread::get_id());
                            return;
                        }
                    }
                    
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                
                // 执行任务
                try {
                    task();
                    ++completedTaskCount_;
                } catch (...) {
                    // 任务执行异常，记录日志但不中断线程
                    // 这里可以添加日志记录
                }
            }
        }

        // 清理已完成的线程
        void joinFinishedThreads()
        {
            while (!finishedThreadIDs_.empty()) {
                auto id = std::move(finishedThreadIDs_.front());
                finishedThreadIDs_.pop();
                auto iter = threads_.find(id);

                if (iter != threads_.end() && iter->second.joinable()) {
                    iter->second.join();
                    threads_.erase(iter);
                }
            }
        }

        static constexpr size_t WAIT_SECONDS = 2;

        std::atomic<bool> quit_;                    // 关闭标志
        std::atomic<size_t> currentThreads_;        // 当前线程数
        std::atomic<size_t> idleThreads_;           // 空闲线程数
        const size_t maxThreads_;                   // 最大线程数
        std::atomic<size_t> taskCount_;             // 总任务数
        std::atomic<size_t> completedTaskCount_;    // 已完成任务数

        mutable std::mutex mutex_;                  // 互斥锁
        std::condition_variable cv_;                // 条件变量
        std::queue<Task> tasks_;                    // 任务队列
        std::queue<ThreadID> finishedThreadIDs_;    // 已完成线程ID队列
        std::unordered_map<ThreadID, Thread> threads_; // 线程映射表
    };

    constexpr size_t ThreadPool::WAIT_SECONDS;

} // namespace dpool

#endif /* THREADPOOL_H */