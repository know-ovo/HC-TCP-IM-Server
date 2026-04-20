/**
 * @file threadpool.h
 * @brief 线程池实现
 * 
 * 提供固定大小的线程池，用于异步任务处理。
 * 支持任务提交和结果获取，适用于耗时操作的并行处理。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include "base/blockingQueue.h"

/**
 * @brief 线程池类
 * 
 * 固定大小的线程池实现，使用阻塞队列管理任务。
 * 
 * 特性：
 * - 固定线程数量
 * - 支持任意返回类型的任务
 * - 支持 future 获取任务结果
 * - 线程安全
 * 
 * 使用示例：
 * @code
 * ThreadPool pool(4);
 * pool.start();
 * 
 * auto future = pool.submit([](int a, int b) { return a + b; }, 1, 2);
 * int result = future.get();
 * @endcode
 */
class ThreadPool
{
public:
    /**
     * @brief 构造函数
     * @param threadCount 线程数量（默认 4）
     */
    explicit ThreadPool(size_t threadCount = 4);

    /**
     * @brief 析构函数，自动停止所有线程
     */
    ~ThreadPool();

    /**
     * @brief 启动线程池
     */
    void start();

    /**
     * @brief 停止线程池
     */
    void stop(bool drain = true);

    /**
     * @brief 提交任务到线程池
     * @tparam F 任务函数类型
     * @tparam Args 任务函数参数类型
     * @param f 任务函数
     * @param args 任务函数参数
     * @return std::future<ReturnType> 任务结果的 future
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

    /**
     * @brief 获取线程数量
     * @return size_t 线程数量
     */
    size_t size() const { return m_workers.size(); }
    size_t queueSize() const { return m_taskQueue.size(); }

    /**
     * @brief 检查线程池是否正在运行
     * @return true 正在运行
     * @return false 已停止
     */
    bool isRunning() const { return m_runningFlag; }

    void setMaxQueueSize(size_t size) { m_maxQueueSize = size; }
    void setRejectCallback(std::function<void()> cb) { m_rejectCallback = std::move(cb); }

private:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 工作线程函数
     */
    void workerThread();

    size_t m_threadCount;
    std::vector<std::thread> m_workers;
    BlockingQueue<std::function<void()>> m_taskQueue;
    std::atomic<bool> m_runningFlag;
    size_t m_maxQueueSize { 0 };
    std::function<void()> m_rejectCallback;
};

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    if (m_runningFlag)
    {
        if (m_maxQueueSize > 0 && m_taskQueue.size() >= m_maxQueueSize)
        {
            if (m_rejectCallback)
            {
                m_rejectCallback();
            }
            throw std::runtime_error("thread pool queue is full");
        }
        m_taskQueue.put([task]() { (*task)(); });
    }

    return result;
}
