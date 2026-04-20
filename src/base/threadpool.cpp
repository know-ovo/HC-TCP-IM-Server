#include "base/threadpool.h"
#include "base/log.h"

ThreadPool::ThreadPool(size_t threadCount)
    : m_threadCount(threadCount)
    , m_runningFlag(false)
{
}

ThreadPool::~ThreadPool()
{
    if (isRunning())
    {
        stop();
    }
}

void ThreadPool::start()
{
    if (m_runningFlag.exchange(true))
    {
        return;
    }
    m_workers.reserve(m_threadCount);
    for (size_t i = 0; i < m_threadCount; ++i)
    {
        m_workers.emplace_back(&ThreadPool::workerThread, this);
    }

    LOG_INFO("ThreadPool started with {} threads", m_threadCount);
}

void ThreadPool::stop(bool drain)
{
    if (!m_runningFlag.exchange(false))
    {
        return;
    }
    if (!drain)
    {
        m_taskQueue.clear();
    }
    for (size_t i = 0; i < m_workers.size(); ++i)
    {
        m_taskQueue.put([]() {});
    }
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    m_workers.clear();

    LOG_INFO("ThreadPool stopped");
}

void ThreadPool::workerThread()
{
    while (m_runningFlag || !m_taskQueue.empty())
    {
        std::function<void()> task;
        if (m_taskQueue.take(task, 1000))
        {
            if (task)
            {
                try
                {
                    task();
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("ThreadPool task exception: %s", e.what());
                }
                catch (...)
                {
                    LOG_ERROR("ThreadPool task unknown exception");
                }
            }
        }
    }
}
