/**
 * @file eventLoop.h
 * @brief 事件循环
 * 
 * 事件循环是 Reactor 模式的核心，负责监控 I/O 事件并分发到对应的处理器。
 * 每个 EventLoop 对象管理一组 Channel，通过 EpollPoller 获取活跃事件。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include "net/nonCopyable.h"
#include "net/timestamp.h"

class Channel;
class EpollPoller;

/**
 * @brief 事件循环类
 * 
 * EventLoop 实现了事件驱动模式，核心功能包括：
 * - I/O 事件监控：通过 epoll 等待 I/O 事件
 * - 事件分发：将事件分发给对应的 Channel 处理
 * - 任务队列：支持跨线程提交任务
 * - 唤醒机制：通过 eventfd 实现线程间通信
 * 
 * 线程模型：
 * - 一个 EventLoop 对象属于一个线程
 * - 只能在所属线程中调用 loop()
 * - 其他线程可以通过 runInLoop() 提交任务
 * 
 * 使用示例：
 * @code
 * EventLoop loop;
 * loop.updateChannel(&channel);
 * loop.loop();  // 开始事件循环
 * @endcode
 */
class EventLoop : NonCopyable
{
public:
    using Functor = std::function<void()>;

    /**
     * @brief 构造函数
     */
    EventLoop();

    /**
     * @brief 析构函数
     */
    ~EventLoop();

    /**
     * @brief 开始事件循环
     * @note 这是一个阻塞调用，直到调用 quit()
     */
    void loop();

    /**
     * @brief 退出事件循环
     */
    void quit();

    /**
     * @name Channel 管理
     * @{
     */

    /**
     * @brief 更新 Channel
     * @param channel 要更新的 Channel
     */
    void updateChannel(Channel* channel);

    /**
     * @brief 移除 Channel
     * @param channel 要移除的 Channel
     */
    void removeChannel(Channel* channel);

    /** @} */

    /**
     * @name 线程安全
     * @{
     */

    /**
     * @brief 断言当前在事件循环线程
     */
    void assertInLoopThread() const;

    /**
     * @brief 检查是否在事件循环线程
     * @return true 在事件循环线程
     * @return false 不在事件循环线程
     */
    bool isInLoopThread() const;

    /** @} */

    /**
     * @name 任务提交
     * @{
     */

    /**
     * @brief 在事件循环中执行任务
     * @param cb 任务函数
     * @note 如果在事件循环线程，立即执行；否则加入任务队列
     */
    void runInLoop(Functor cb);

    /**
     * @brief 将任务加入队列
     * @param cb 任务函数
     */
    void queueInLoop(Functor cb);

    /** @} */

    /**
     * @brief 唤醒事件循环
     */
    void wakeup();

    /**
     * @brief 设置是否使用边缘触发
     */
    void setUseEdgeTrigger(bool enabled);

    /**
     * @brief 获取 poll 返回时间
     * @return Timestamp poll 返回时间
     */
    Timestamp pollReturnTime() const { return m_pollReturnTime; }

    /**
     * @brief 获取当前线程的事件循环
     * @return EventLoop* 当前线程的事件循环（可能为 nullptr）
     */
    static EventLoop* getEventLoopOfCurrentThread();

private:
    /**
     * @brief 断言不在事件循环线程时中止
     */
    void abortNotInLoopThread() const;

    /**
     * @brief 处理唤醒事件
     */
    void handleRead();

    /**
     * @brief 执行待处理的任务
     */
    void doPendingFunctors();

    using ChannelList = std::vector<Channel*>;

    std::atomic<bool> m_looping;
    std::atomic<bool> m_quit;
    std::atomic<bool> m_callingPendingFunctors;

    const int m_threadId;
    Timestamp m_pollReturnTime;

    std::unique_ptr<EpollPoller> m_poller;
    ChannelList m_activeChannels;
    Channel* m_currentActiveChannel;

    int m_wakeupFd;
    std::unique_ptr<Channel> m_wakeupChannel;

    std::mutex m_mutex;
    std::vector<Functor> m_pendingFunctors;

    static const int kPollTimeoutMs = 10000;
};
