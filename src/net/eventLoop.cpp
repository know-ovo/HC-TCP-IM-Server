// EventLoop.cpp
#include "net/eventLoop.h"
#include "net/channel.h"
#include "net/epollPoller.h"
#include "base/log.h"
#include "net/currentThread.h"
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <assert.h>

__thread EventLoop* t_loop_in_this_thread = nullptr;

EventLoop::EventLoop()
    : m_looping(false),
      m_quit(false),
      m_callingPendingFunctors(false),
      m_threadId(CurrentThread::tid()),
      m_poller(new EpollPoller(this)),
      m_currentActiveChannel(nullptr)
{
    LOG_DEBUG("EventLoop created {} in thread {}", static_cast<const void*>(this), m_threadId);
    if (t_loop_in_this_thread)
    {
        LOG_FATAL("Another EventLoop {} exists in this thread {}",
                  static_cast<const void*>(t_loop_in_this_thread), m_threadId);
    }
    else
    {
        t_loop_in_this_thread = this;
    }
    m_wakeupFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeupFd < 0)
    {
        LOG_FATAL("EventLoop::EventLoop eventfd failed");
    }
    m_wakeupChannel.reset(new Channel(this, m_wakeupFd));
    m_wakeupChannel->setReadCallback(std::bind(&EventLoop::handleRead, this));
    m_wakeupChannel->enableReading();
}

EventLoop::~EventLoop()
{
    LOG_DEBUG("EventLoop {} destructing in thread {}", static_cast<const void*>(this), m_threadId);
    assert(!m_looping);
    t_loop_in_this_thread = nullptr;
}

void EventLoop::loop()
{
    assert(!m_looping);
    assertInLoopThread();
    m_looping = true;
    m_quit = false;
    LOG_INFO("EventLoop {} start looping", static_cast<const void*>(this));
    while (!m_quit)
    {
        m_activeChannels.clear();
        m_pollReturnTime = m_poller->poll(kPollTimeoutMs, &m_activeChannels);
        for (Channel* channel : m_activeChannels)
        {
            m_currentActiveChannel = channel;
            m_currentActiveChannel->handleEvent(m_pollReturnTime);
        }
        m_currentActiveChannel = nullptr;
        doPendingFunctors();
    }
    LOG_INFO("EventLoop {} stop looping", static_cast<const void*>(this));
    m_looping = false;
}

void EventLoop::quit()
{
    m_quit = true;
    if (!isInLoopThread())
    {
        wakeup();
    }
}

void EventLoop::updateChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    m_poller->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    if (m_currentActiveChannel == channel)
    {
        m_currentActiveChannel = nullptr;
    }
    m_poller->removeChannel(channel);
}

void EventLoop::assertInLoopThread() const
{
    if (!isInLoopThread())
    {
        abortNotInLoopThread();
    }
}

bool EventLoop::isInLoopThread() const
{
    return m_threadId == CurrentThread::tid();
}

void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())
    {
        cb();
    }
    else
    {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingFunctors.push_back(std::move(cb));
    }
    if (!isInLoopThread() || m_callingPendingFunctors)
    {
        wakeup();
    }
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(m_wakeupFd, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup writes {} bytes instead of 8", n);
    }
}

void EventLoop::setUseEdgeTrigger(bool enabled)
{
    m_poller->setUseEdgeTrigger(enabled);
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = ::read(m_wakeupFd, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead reads {} bytes instead of 8", n);
    }
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    m_callingPendingFunctors = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        functors.swap(m_pendingFunctors);
    }
    for (const Functor& functor : functors)
    {
        functor();
    }
    m_callingPendingFunctors = false;
}

void EventLoop::abortNotInLoopThread() const
{
    LOG_FATAL("EventLoop::abortNotInLoopThread - EventLoop {} was created in thread {}, current thread {}",
              static_cast<const void*>(this), m_threadId, CurrentThread::tid());
}

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
    return t_loop_in_this_thread;
}
