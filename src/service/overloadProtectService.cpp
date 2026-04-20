// OverloadProtectService.cpp
#include "service/overloadProtectService.h"
#include "base/util.h"
#include "codec/protocol.h"

OverloadProtectService::OverloadProtectService(size_t maxConnections, size_t maxQps)
    : m_maxConnections(maxConnections),
      m_maxQps(maxQps),
      m_currentConnections(0),
      m_messageCount(0),
      m_lastResetTime(util::GetTimestampMs())
{
}

bool OverloadProtectService::canAcceptConnection()
{
    return m_currentConnections.load() < m_maxConnections.load();
}

bool OverloadProtectService::canAcceptConnection(const std::string& ip)
{
    if (!canAcceptConnection())
    {
        return false;
    }
    if (!m_rateLimitEnabled.load() || m_ipConnectRatePerSecond <= 0.0 || ip.empty())
    {
        return true;
    }

    auto limiter = getOrCreateLimiter(m_ipLimiters, ip, m_ipConnectRatePerSecond, m_ipConnectRatePerSecond);
    return limiter->allow();
}

void OverloadProtectService::onConnectionAccepted()
{
    m_currentConnections++;
}

void OverloadProtectService::onConnectionClosed()
{
    m_currentConnections--;
}

bool OverloadProtectService::canProcessMessage()
{
    int64_t now = util::GetTimestampMs();
    int64_t last = m_lastResetTime.load();
    
    if (now - last >= 1000)
    {
        m_messageCount.store(0);
        m_lastResetTime.store(now);
    }
    
    return m_messageCount.load() < m_maxQps.load();
}

bool OverloadProtectService::canProcessCommand(uint16_t command)
{
    if (!canProcessMessage())
    {
        return false;
    }
    if (!m_rateLimitEnabled.load())
    {
        return true;
    }

    if (command == protocol::CmdLoginReq && m_loginLimiter)
    {
        return m_loginLimiter->allow();
    }
    if (command == protocol::CmdP2pMsgReq && m_p2pLimiter)
    {
        return m_p2pLimiter->allow();
    }
    if (command == protocol::CmdBroadcastMsgReq && m_broadcastLimiter)
    {
        return m_broadcastLimiter->allow();
    }
    return true;
}

bool OverloadProtectService::canSendMessage(const std::string& userId)
{
    if (!m_rateLimitEnabled.load() || m_userSendRatePerSecond <= 0.0 || userId.empty())
    {
        return true;
    }

    auto limiter = getOrCreateLimiter(m_userLimiters, userId, m_userSendRatePerSecond, m_userSendRatePerSecond);
    return limiter->allow();
}

void OverloadProtectService::onMessageProcessed()
{
    m_messageCount++;
}

size_t OverloadProtectService::getCurrentQps() const
{
    return m_messageCount.load();
}

void OverloadProtectService::configureIpConnectRate(double perSecond)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ipConnectRatePerSecond = perSecond;
    m_ipLimiters.clear();
}

void OverloadProtectService::configureCommandRates(double loginQps, double p2pQps, double broadcastQps)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_loginLimiter = loginQps > 0 ? std::make_shared<infra::TokenBucketRateLimiter>(loginQps, loginQps) : nullptr;
    m_p2pLimiter = p2pQps > 0 ? std::make_shared<infra::TokenBucketRateLimiter>(p2pQps, p2pQps) : nullptr;
    m_broadcastLimiter = broadcastQps > 0 ? std::make_shared<infra::TokenBucketRateLimiter>(broadcastQps, broadcastQps) : nullptr;
}

void OverloadProtectService::configureUserSendRate(double perSecond)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userSendRatePerSecond = perSecond;
    m_userLimiters.clear();
}

std::shared_ptr<infra::TokenBucketRateLimiter> OverloadProtectService::getOrCreateLimiter(
    std::unordered_map<std::string, std::shared_ptr<infra::TokenBucketRateLimiter>>& pool,
    const std::string& key,
    double rate,
    double burst)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = pool.find(key);
    if (it != pool.end())
    {
        return it->second;
    }
    auto limiter = std::make_shared<infra::TokenBucketRateLimiter>(rate, burst);
    pool[key] = limiter;
    return limiter;
}
