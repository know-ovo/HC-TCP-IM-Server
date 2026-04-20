#pragma once

#include <cstdint>
#include <mutex>

namespace infra
{

class CircuitBreaker
{
public:
    CircuitBreaker(bool enabled = false, uint32_t failureThreshold = 10, int halfOpenAfterMs = 5000);

    bool allowRequest() const;
    void recordSuccess();
    void recordFailure();

    void configure(bool enabled, uint32_t failureThreshold, int halfOpenAfterMs);
    bool isOpen() const;

private:
    enum class State
    {
        Closed,
        Open,
        HalfOpen
    };

    mutable std::mutex m_mutex;
    bool m_enabled;
    uint32_t m_failureThreshold;
    int m_halfOpenAfterMs;
    State m_state;
    uint32_t m_consecutiveFailures;
    int64_t m_openedAtMs;
};

} // namespace infra
