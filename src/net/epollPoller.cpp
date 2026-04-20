// EpollPoller.cpp
#include "net/epollPoller.h"
#include "net/channel.h"
#include "net/eventLoop.h"
#include "base/log.h"
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

EpollPoller::EpollPoller(EventLoop* loop)
    : m_ownerLoop(loop),
      m_epollfd(::epoll_create1(EPOLL_CLOEXEC)),
      m_events(kInitEventListSize),
      m_useEdgeTrigger(false)
{
    if (m_epollfd < 0)
    {
        LOG_FATAL("EpollPoller::EpollPoller epoll_create1 failed");
    }
}

EpollPoller::~EpollPoller()
{
    ::close(m_epollfd);
}

Timestamp EpollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    int numEvents = ::epoll_wait(m_epollfd, m_events.data(),
                                   static_cast<int>(m_events.size()), timeoutMs);
    int savedErrno = errno;
    Timestamp now(Timestamp::now());
    if (numEvents > 0)
    {
        LOG_DEBUG("%d events happened", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        if (static_cast<size_t>(numEvents) == m_events.size())
        {
            m_events.resize(m_events.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("nothing happened");
    }
    else
    {
        if (savedErrno != EINTR)
        {
            errno = savedErrno;
            LOG_ERROR("EpollPoller::poll() error: %s", strerror(errno));
        }
    }
    return now;
}

void EpollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    assert(static_cast<size_t>(numEvents) <= m_events.size());
    for (int i = 0; i < numEvents; ++i)
    {
        //核心2：channel = m_events.data.ptr 将channel从内核中拿回来
        Channel* channel = static_cast<Channel*>(m_events[i].data.ptr);
        channel->setRevents(m_events[i].events);
        activeChannels->push_back(channel);
    }
}

void EpollPoller::updateChannel(Channel* channel)
{
    assertInLoopThread();
    const int index = channel->index();
    LOG_DEBUG("fd = %d events = %d index = %d", channel->fd(), channel->events(), index);
    if (index == kNew || index == kDeleted)
    {
        int fd = channel->fd();
        if (index == kNew)
        {
            assert(m_channels.find(fd) == m_channels.end());
            m_channels[fd] = channel;
        }
        else
        {
            assert(m_channels.find(fd) != m_channels.end());
            assert(m_channels[fd] == channel);
        }
        channel->setIndex(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    else
    {
        int fd = channel->fd();
        assert(m_channels.find(fd) != m_channels.end());
        assert(m_channels[fd] == channel);
        assert(index == kAdded);
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->setIndex(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EpollPoller::removeChannel(Channel* channel)
{
    assertInLoopThread();
    int fd = channel->fd();
    assert(m_channels.find(fd) != m_channels.end());
    assert(m_channels[fd] == channel);
    assert(channel->isNoneEvent());
    int index = channel->index();
    assert(index == kAdded || index == kDeleted);
    size_t n = m_channels.erase(fd);
    assert(n == 1);
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->setIndex(kNew);
}

void EpollPoller::update(int operation, Channel* channel)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->events();
    if (m_useEdgeTrigger && operation != EPOLL_CTL_DEL)
    {
        event.events |= EPOLLET;
    }

    //核心1：将channel指针存储到epoll_event中，并监控事件，方便后续从内核中拿回来
    event.data.ptr = channel;
    int fd = channel->fd();
    LOG_DEBUG("epoll_ctl op = %s, fd = %d",
              (operation == EPOLL_CTL_ADD ? "ADD" : (operation == EPOLL_CTL_MOD ? "MOD" : "DEL")),
              fd);
    if (::epoll_ctl(m_epollfd, operation, fd, &event) < 0)
    {
        LOG_ERROR("epoll_ctl op=%d fd=%d failed: %s", operation, fd, strerror(errno));
    }
}

void EpollPoller::assertInLoopThread() const
{
    m_ownerLoop->assertInLoopThread();
}
