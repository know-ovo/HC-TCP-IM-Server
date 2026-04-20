#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class RedisClient;

class RoutingService
{
public:
    void setRedisClient(std::shared_ptr<RedisClient> client);
    void setNodeId(const std::string& nodeId);
    void setRouteTtlSeconds(int routeTtlSeconds);

    bool bindUserRoute(const std::string& userId, const std::string& nodeId);
    bool unbindUserRoute(const std::string& userId);
    std::optional<std::string> lookupUserRoute(const std::string& userId);

    const std::string& nodeId() const { return m_nodeId; }

private:
    std::string makeRouteKey(const std::string& userId) const;

    mutable std::mutex m_mutex;
    std::shared_ptr<RedisClient> m_redisClient;
    std::string m_nodeId { "single-node" };
    int m_routeTtlSeconds { 30 };
    std::unordered_map<std::string, std::string> m_localRoutes;
};
