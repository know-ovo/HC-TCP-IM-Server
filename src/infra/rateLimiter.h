#pragma once

#include <mutex>

namespace infra
{

class TokenBucketRateLimiter
{
public:
    TokenBucketRateLimiter(double rate = 1.0, double burst = 1.0);

    bool allow(double tokens = 1.0);
    void reset(double rate, double burst);

private:
    void refill();

    mutable std::mutex m_mutex;
    double m_rate;
    double m_burst;
    double m_tokens;
    double m_lastRefillMs;
};

} // namespace infra
