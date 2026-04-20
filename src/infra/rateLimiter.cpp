#include "infra/rateLimiter.h"

#include <algorithm>

#include "base/util.h"

namespace infra
{

TokenBucketRateLimiter::TokenBucketRateLimiter(double rate, double burst)
    : m_rate(rate)
    , m_burst(std::max(1.0, burst))
    , m_tokens(std::max(1.0, burst))
    , m_lastRefillMs(static_cast<double>(util::GetTimestampMs()))
{
}

bool TokenBucketRateLimiter::allow(double tokens)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    refill();
    if (m_tokens < tokens)
    {
        return false;
    }
    m_tokens -= tokens;
    return true;
}

void TokenBucketRateLimiter::reset(double rate, double burst)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rate = rate;
    m_burst = std::max(1.0, burst);
    m_tokens = m_burst;
    m_lastRefillMs = static_cast<double>(util::GetTimestampMs());
}

void TokenBucketRateLimiter::refill()
{
    const double nowMs = static_cast<double>(util::GetTimestampMs());
    const double deltaMs = nowMs - m_lastRefillMs;
    if (deltaMs <= 0 || m_rate <= 0)
    {
        m_lastRefillMs = nowMs;
        return;
    }

    const double refillTokens = deltaMs * (m_rate / 1000.0);
    m_tokens = std::min(m_burst, m_tokens + refillTokens);
    m_lastRefillMs = nowMs;
}

} // namespace infra
