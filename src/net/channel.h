/**
 * @file channel.h
 * @brief I/O 事件通道
 * 
 * Channel 是 Reactor 模式中的核心组件，封装了文件描述符及其关注的事件。
 * 它是 selectable I/O 事件的分发器，将 I/O 事件分发给对应的回调函数处理。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <functional>
#include <memory>
#include "net/nonCopyable.h"
#include "net/timestamp.h"

class EventLoop;

/**
 * @brief I/O 事件通道类
 * 
 * Channel 负责管理一个文件描述符的 I/O 事件。
 * 每个 Channel 对象属于一个 EventLoop，负责该文件描述符的事件分发。
 * 
 * 事件类型：
 * - 读事件（EPOLLIN）：有数据可读
 * - 写事件（EPOLLOUT）：可以写入数据
 * - 关闭事件：连接关闭
 * - 错误事件：发生错误
 * 
 * 使用示例：
 * @code
 * Channel channel(&loop, sockfd);
 * channel.setReadCallback([](Timestamp) { });  // 处理读事件
 * channel.enableReading();
 * @endcode
 */
class Channel : NonCopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    /**
     * @brief 构造函数
     * @param loop 所属的事件循环
     * @param fd 文件描述符
     */
    Channel(EventLoop* loop, int fd);
    ~Channel();

    /**
     * @brief 处理事件
     * @param receiveTime 事件发生的时间戳
     */
    void handleEvent(Timestamp receiveTime);

    /**
     * @name 回调函数设置
     * @{
     */

    /**
     * @brief 设置读事件回调
     * @param cb 回调函数
     */
    void setReadCallback(ReadEventCallback cb) { m_readCallback = std::move(cb); }

    /**
     * @brief 设置写事件回调
     * @param cb 回调函数
     */
    void setWriteCallback(EventCallback cb) { m_writeCallback = std::move(cb); }

    /**
     * @brief 设置关闭事件回调
     * @param cb 回调函数
     */
    void setCloseCallback(EventCallback cb) { m_closeCallback = std::move(cb); }

    /**
     * @brief 设置错误事件回调
     * @param cb 回调函数
     */
    void setErrorCallback(EventCallback cb) { m_errorCallback = std::move(cb); }

    /** @} */

    /**
     * @name 文件描述符和事件信息
     * @{
     */

    /**
     * @brief 获取文件描述符
     * @return int 文件描述符
     */
    int fd() const { return m_fd; }

    /**
     * @brief 获取关注的事件
     * @return int 事件掩码
     */
    int events() const { return m_events; }

    /**
     * @brief 设置发生的事件
     * @param revt 事件掩码
     */
    void setRevents(int revt) { m_revents = revt; }

    /**
     * @brief 检查是否没有关注任何事件
     * @return true 没有关注任何事件
     * @return false 有关注的事件
     */
    bool isNoneEvent() const { return m_events == kNoneEvent; }

    /** @} */

    /**
     * @name 事件控制
     * @{
     */

    /**
     * @brief 启用读事件
     */
    void enableReading() { m_events |= kReadEvent; update(); }

    /**
     * @brief 禁用读事件
     */
    void disableReading() { m_events &= ~kReadEvent; update(); }

    /**
     * @brief 启用写事件
     */
    void enableWriting() { m_events |= kWriteEvent; update(); }

    /**
     * @brief 禁用写事件
     */
    void disableWriting() { m_events &= ~kWriteEvent; update(); }

    /**
     * @brief 禁用所有事件
     */
    void disableAll() { m_events = kNoneEvent; update(); }

    /**
     * @brief 检查是否关注写事件
     * @return true 关注写事件
     * @return false 不关注写事件
     */
    bool isWriting() const { return m_events & kWriteEvent; }

    /**
     * @brief 检查是否关注读事件
     * @return true 关注读事件
     * @return false 不关注读事件
     */
    bool isReading() const { return m_events & kReadEvent; }

    /** @} */

    /**
     * @name Poller 相关
     * @{
     */

    /**
     * @brief 获取在 Poller 中的索引
     * @return int 索引值
     */
    int index() { return m_index; }

    /**
     * @brief 设置在 Poller 中的索引
     * @param idx 索引值
     */
    void setIndex(int idx) { m_index = idx; }

    /** @} */

    /**
     * @brief 获取所属的事件循环
     * @return EventLoop* 事件循环指针
     */
    EventLoop* ownerLoop() { return m_loop; }

    /**
     * @brief 从事件循环中移除自己
     */
    void remove();

private:
    /**
     * @brief 更新事件关注状态
     */
    void update();

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* m_loop;
    const int m_fd;
    int m_events;
    int m_revents;
    int m_index;

    ReadEventCallback m_readCallback;
    EventCallback m_writeCallback;
    EventCallback m_closeCallback;
    EventCallback m_errorCallback;

    bool m_eventHandling;
    bool m_addedToLoop;
};
