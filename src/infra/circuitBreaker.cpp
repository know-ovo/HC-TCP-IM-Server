#include "infra/circuitBreaker.h"

#include "base/util.h"

namespace infra
{

CircuitBreaker::CircuitBreaker(bool enabled, uint32_t failureThreshold, int halfOpenAfterMs)
    : m_enabled(enabled)
    , m_failureThreshold(failureThreshold)
    , m_halfOpenAfterMs(halfOpenAfterMs)
    , m_state(State::Closed)
    , m_consecutiveFailures(0)
    , m_openedAtMs(0)
{
}

bool CircuitBreaker::allowRequest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled)
    {
        return true;
    }

    if (m_state == State::Closed)
    {
        return true;
    }

    const int64_t nowMs = util::GetTimestampMs();
    if (m_state == State::Open && nowMs - m_openedAtMs >= m_halfOpenAfterMs)
    {
        const_cast<CircuitBreaker*>(this)->m_state = State::HalfOpen;
        return true;
    }

    return m_state == State::HalfOpen;
}

void CircuitBreaker::recordSuccess()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consecutiveFailures = 0;
    m_state = State::Closed;
    m_openedAtMs = 0;
}

void CircuitBreaker::recordFailure()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled)
    {
        return;
    }

    ++m_consecutiveFailures;
    if (m_consecutiveFailures >= m_failureThreshold)
    {
        m_state = State::Open;
        m_openedAtMs = util::GetTimestampMs();
    }
}

void CircuitBreaker::configure(bool enabled, uint32_t failureThreshold, int halfOpenAfterMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
    m_failureThreshold = failureThreshold;
    m_halfOpenAfterMs = halfOpenAfterMs;
    m_state = State::Closed;
    m_consecutiveFailures = 0;
    m_openedAtMs = 0;
}

bool CircuitBreaker::isOpen() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state == State::Open;
}

} // namespace infra
