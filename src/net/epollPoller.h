/**
 * @file epollPoller.h
 * @brief epoll I/O 多路复用封装
 * 
 * 封装 Linux epoll 系统调用，提供高效的 I/O 事件多路复用功能。
 * 是 EventLoop 的底层实现，负责监控多个文件描述符的 I/O 事件。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

 /*
 EpollPoller 是对 Linux epoll 的 C++ 封装，
 负责管理所有 Channel 对应的 fd，通过 epoll_wait 等待 IO 事件，
 并将就绪事件转化为 Channel 列表返回给 EventLoop 处理。
 */

#pragma once

#include <vector>
#include <map>
#include <sys/epoll.h>
#include "net/nonCopyable.h"
#include "net/timestamp.h"

class Channel;
class EventLoop;

/**
 * @brief epoll 封装类
 * 
 * EpollPoller 封装了 epoll 的创建、事件添加/修改/删除、事件等待等操作。
 * 
 * 特性：
 * - 使用 epoll LT（水平触发）模式
 * - 支持动态调整事件列表大小
 * - 维护 fd 到 Channel 的映射关系
 * 
 * 使用示例：
 * @code
 * EpollPoller poller(&loop);
 * poller.updateChannel(channel);  // 添加或更新 Channel
 * Timestamp now = poller.poll(10000, &activeChannels);  // 等待事件
 * @endcode
 */
class EpollPoller : NonCopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    /**
     * @brief 构造函数
     * @param loop 所属的事件循环
     */
    explicit EpollPoller(EventLoop* loop);

    /**
     * @brief 析构函数
     */
    ~EpollPoller();

    /**
     * @brief 等待 I/O 事件
     * @param timeoutMs 超时时间（毫秒）
     * @param activeChannels 输出参数，活跃的 Channel 列表
     * @return Timestamp 事件发生的时间戳
     */
    Timestamp poll(int timeoutMs, ChannelList* activeChannels);

    /**
     * @brief 更新 Channel 的事件关注
     * @param channel 要更新的 Channel
     */
    void updateChannel(Channel* channel);

    /**
     * @brief 移除 Channel
     * @param channel 要移除的 Channel
     */
    void removeChannel(Channel* channel);

    /**
     * @brief 断言当前在事件循环线程
     */
    void assertInLoopThread() const;

    /**
     * @brief 设置是否启用 ET 模式
     */
    void setUseEdgeTrigger(bool enabled) { m_useEdgeTrigger = enabled; }

private:
    static const int kInitEventListSize = 16;
    static const int kNew = -1;
    static const int kAdded = 1;
    static const int kDeleted = 2;

    /**
     * @brief 填充活跃的 Channel 列表
     * @param numEvents 活跃事件数量
     * @param activeChannels 输出参数，活跃的 Channel 列表
     */
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

    /**
     * @brief 执行 epoll_ctl 操作
     * @param operation epoll 操作（EPOLL_CTL_ADD/MOD/DEL）
     * @param channel 目标 Channel
     */
    void update(int operation, Channel* channel);

    using EventList = std::vector<struct epoll_event>;
    using ChannelMap = std::map<int, Channel*>;

    EventLoop* m_ownerLoop;
    int m_epollfd;
    EventList m_events;
    ChannelMap m_channels;
    bool m_useEdgeTrigger;
};
