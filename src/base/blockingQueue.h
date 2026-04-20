/**
 * @file blockingQueue.h
 * @brief 线程安全的阻塞队列实现
 * 
 * 提供一个线程安全的阻塞队列，用于生产者-消费者模式。
 * 支持阻塞式获取和超时获取操作，适用于线程池任务队列等场景。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

/**
 * @brief 线程安全的阻塞队列模板类
 * @tparam T 队列元素类型
 * 
 * 特性：
 * - 线程安全：所有操作都通过互斥锁保护
 * - 阻塞获取：当队列为空时，take() 会阻塞等待
 * - 超时获取：支持带超时的获取操作
 * - 禁止拷贝：防止意外复制导致的数据竞争
 */
template<typename T>
class BlockingQueue
{
public:
    BlockingQueue() = default;
    ~BlockingQueue() = default;

    /**
     * @brief 向队列中添加元素（拷贝语义）
     * @param task 要添加的元素
     */
    void put(const T& task);

    /**
     * @brief 向队列中添加元素（移动语义）
     * @param task 要添加的元素（右值引用）
     */
    void put(T&& task);

    /**
     * @brief 从队列中获取元素（阻塞式）
     * @return 队列头部的元素
     * @note 如果队列为空，将阻塞直到有元素可用
     */
    T take();

    /**
     * @brief 从队列中获取元素（带超时）
     * @param task 输出参数，存储获取的元素
     * @param timeoutMs 超时时间（毫秒）
     * @return true 成功获取元素
     * @return false 超时未获取到元素
     */
    bool take(T& task, int timeoutMs);

    /**
     * @brief 获取队列大小
     * @return 队列中元素的数量
     */
    size_t size() const;

    /**
     * @brief 检查队列是否为空
     * @return true 队列为空
     * @return false 队列不为空
     */
    bool empty() const;

    /**
     * @brief 清空队列
     */
    void clear();

private:
    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
};

template<typename T>
void BlockingQueue<T>::put(const T& task)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(task);
    m_notEmpty.notify_one();
}

template<typename T>
void BlockingQueue<T>::put(T&& task)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(std::move(task));
    m_notEmpty.notify_one();
}

template<typename T>
T BlockingQueue<T>::take()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_notEmpty.wait(lock, [this] { return !m_queue.empty(); });
    T task = std::move(m_queue.front());
    m_queue.pop();
    return task;
}

template<typename T>
bool BlockingQueue<T>::take(T& task, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_notEmpty.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this] { return !m_queue.empty(); }))
    {
        return false;
    }
    task = std::move(m_queue.front());
    m_queue.pop();
    return true;
}

template<typename T>
size_t BlockingQueue<T>::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

template<typename T>
bool BlockingQueue<T>::empty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.empty();
}

template<typename T>
void BlockingQueue<T>::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::queue<T> empty;
    std::swap(m_queue, empty);
}
