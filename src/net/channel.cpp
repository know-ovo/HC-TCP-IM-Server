// Channel.cpp
#include "net/channel.h"
#include "net/eventLoop.h"
#include "base/log.h"
#include <poll.h>
#include <assert.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : m_loop(loop), m_fd(fd), m_events(0), m_revents(0), m_index(-1),
      m_eventHandling(false), m_addedToLoop(false)
{
}

Channel::~Channel()
{
    assert(!m_eventHandling);
    assert(!m_addedToLoop);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    m_eventHandling = true;
    if (m_revents & POLLNVAL)
    {
        LOG_WARN("Channel::handleEvent() POLLNVAL");
    }
    if ((m_revents & POLLHUP) && !(m_revents & POLLIN))
    {
        if (m_closeCallback)
            m_closeCallback();
    }
    if (m_revents & (POLLERR | POLLNVAL))
    {
        if (m_errorCallback)
            m_errorCallback();
    }
    if (m_revents & (POLLIN | POLLPRI | POLLRDHUP))
    {
        if (m_readCallback)
            m_readCallback(receiveTime);
    }
    if (m_revents & POLLOUT)
    {
        if (m_writeCallback)
            m_writeCallback();
    }
    m_eventHandling = false;
}

void Channel::update()
{
    m_addedToLoop = true;
    m_loop->updateChannel(this);
}

void Channel::remove()
{
    assert(isNoneEvent());
    m_addedToLoop = false;
    m_loop->removeChannel(this);
}
