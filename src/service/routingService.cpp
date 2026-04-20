#include "service/routingService.h"

#include "base/log.h"

#ifdef USE_REDIS
#include "base/redisClient.h"
#endif

void RoutingService::setRedisClient(std::shared_ptr<RedisClient> client)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_redisClient = std::move(client);
}

void RoutingService::setNodeId(const std::string& nodeId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeId = nodeId;
}

void RoutingService::setRouteTtlSeconds(int routeTtlSeconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_routeTtlSeconds = routeTtlSeconds;
}

bool RoutingService::bindUserRoute(const std::string& userId, const std::string& nodeId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localRoutes[userId] = nodeId;
#ifdef USE_REDIS
    if (m_redisClient && m_redisClient->isConnected())
    {
        return m_redisClient->setex(makeRouteKey(userId), nodeId, m_routeTtlSeconds);
    }
#endif
    return true;
}

bool RoutingService::unbindUserRoute(const std::string& userId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_localRoutes.erase(userId);
#ifdef USE_REDIS
    if (m_redisClient && m_redisClient->isConnected())
    {
        return m_redisClient->del(makeRouteKey(userId));
    }
#endif
    return true;
}

std::optional<std::string> RoutingService::lookupUserRoute(const std::string& userId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto localIt = m_localRoutes.find(userId);
        if (localIt != m_localRoutes.end())
        {
            return localIt->second;
        }
    }

#ifdef USE_REDIS
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_redisClient && m_redisClient->isConnected())
    {
        const std::string route = m_redisClient->get(makeRouteKey(userId));
        if (!route.empty())
        {
            m_localRoutes[userId] = route;
            return route;
        }
    }
#endif
    return std::nullopt;
}

std::string RoutingService::makeRouteKey(const std::string& userId) const
{
    return "route:" + userId;
}
