#include "service/heartbeatService.h"
#include "base/log.h"
#include "base/util.h"
#include "base/threadpool.h"
#include "infra/metricsRegistry.h"
#include <tuple>
#include <thread>
#include <chrono>

#ifdef USE_REDIS
#include "base/redisClient.h"
#endif

HeartbeatService::HeartbeatService(EventLoop* loop, int timeoutSeconds, int checkIntervalSeconds)
	: m_loop(loop)
	, m_timeoutSeconds(timeoutSeconds)
	, m_checkIntervalSeconds(checkIntervalSeconds)
	, m_threadPool(nullptr)
	, m_useRedis(false)
	, m_running(false)
{
}

HeartbeatService::~HeartbeatService()
{
	stop();
}

void HeartbeatService::setRedisClient(std::shared_ptr<RedisClient> redisClient)
{
#ifdef USE_REDIS
	m_redisClient = redisClient;
	m_useRedis = (redisClient != nullptr && redisClient->isConnected());
	if (m_useRedis)
	{
		LOG_INFO("HeartbeatService: Redis enabled for online status");
	}
#else
	LOG_WARN("HeartbeatService: Redis support not compiled in");
#endif
}

void HeartbeatService::setThreadPool(ThreadPool* pool)
{
	m_threadPool = pool;
	if (pool)
	{
		LOG_INFO("HeartbeatService: Thread pool set for async Redis operations");
	}
}

void HeartbeatService::start()
{
	if (m_running.exchange(true))
	{
		return;
	}
	LOG_INFO("HeartbeatService started, timeout: {}s, check interval: {}s",
			 m_timeoutSeconds, m_checkIntervalSeconds);

	std::thread([this]() {
		while (m_running)
		{
			std::this_thread::sleep_for(std::chrono::seconds(m_checkIntervalSeconds));
			if (m_running)
			{
                reportNodeAlive();
				checkHeartbeat();
			}
		}
	}).detach();
}

void HeartbeatService::stop()
{
	if (!m_running.exchange(false))
	{
		return;
	}
	LOG_INFO("HeartbeatService stopped");
}

void HeartbeatService::onHeartbeat(const std::shared_ptr<TcpConnection>& conn)
{
	std::string connId = std::to_string(conn->fd());
	
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_lastHeartbeatTime[connId] = util::GetTimestampMs();
        auto healthIt = m_connectionHealth.find(connId);
        if (healthIt != m_connectionHealth.end())
        {
            healthIt->second.lastHeartbeatAtMs = util::GetTimestampMs();
            if (healthIt->second.authenticated && healthIt->second.state != ConnectionHealthState::BackPressured)
            {
                healthIt->second.state = ConnectionHealthState::Active;
            }
        }
	}

#ifdef USE_REDIS
	std::string userId;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_connToUser.find(connId);
		if (it != m_connToUser.end())
		{
			userId = it->second;
		}
	}
	
	if (!userId.empty())
	{
		refreshOnlineStatusAsync(userId);
	}
#endif

	LOG_DEBUG("HeartbeatService::onHeartbeat - connId: {}", connId);
}

void HeartbeatService::onConnection(const std::shared_ptr<TcpConnection>& conn, bool connected)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::string connId = std::to_string(conn->fd());

	if (connected)
	{
		m_connections[connId] = conn;
		m_lastHeartbeatTime[connId] = util::GetTimestampMs();
        ConnectionHealth health;
        health.state = ConnectionHealthState::AuthPending;
        health.createdAtMs = util::GetTimestampMs();
        health.lastHeartbeatAtMs = health.createdAtMs;
        m_connectionHealth[connId] = health;
		LOG_INFO("HeartbeatService::onConnection - added, connId: {}", connId);
	}
	else
	{
#ifdef USE_REDIS
		auto it = m_connToUser.find(connId);
		if (it != m_connToUser.end())
		{
			updateOnlineStatusAsync(it->second, false);
		}
#endif
		m_connections.erase(connId);
		m_lastHeartbeatTime.erase(connId);
		m_connToUser.erase(connId);
        m_connectionHealth.erase(connId);
		LOG_INFO("HeartbeatService::onConnection - removed, connId: {}", connId);
	}
}

void HeartbeatService::registerUserId(const std::string& connId, const std::string& userId)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_connToUser[connId] = userId;
	}
	updateOnlineStatusAsync(userId, true);
	LOG_INFO("HeartbeatService::registerUserId - connId: {}, userId: {}", connId, userId);
}

void HeartbeatService::onAuthenticated(const std::shared_ptr<TcpConnection>& conn, const std::string& userId)
{
    const std::string connId = std::to_string(conn->fd());
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connToUser[connId] = userId;
        auto& health = m_connectionHealth[connId];
        if (health.createdAtMs == 0)
        {
            health.createdAtMs = util::GetTimestampMs();
        }
        health.lastHeartbeatAtMs = util::GetTimestampMs();
        health.authenticated = true;
        health.state = ConnectionHealthState::Active;
    }
    updateOnlineStatusAsync(userId, true);
    reportNodeAlive();
}

void HeartbeatService::onWriteBlocked(const std::shared_ptr<TcpConnection>& conn)
{
    const std::string connId = std::to_string(conn->fd());
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connectionHealth.find(connId);
    if (it != m_connectionHealth.end())
    {
        it->second.state = ConnectionHealthState::BackPressured;
        it->second.lastBlockedAtMs = util::GetTimestampMs();
    }
}

void HeartbeatService::onBackPressureRecovered(const std::shared_ptr<TcpConnection>& conn)
{
    const std::string connId = std::to_string(conn->fd());
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connectionHealth.find(connId);
    if (it != m_connectionHealth.end())
    {
        it->second.state = it->second.authenticated ? ConnectionHealthState::Active : ConnectionHealthState::AuthPending;
        it->second.lastBlockedAtMs = 0;
    }
}

void HeartbeatService::unregisterUserId(const std::string& connId)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto it = m_connToUser.find(connId);
	if (it != m_connToUser.end())
	{
		updateOnlineStatusAsync(it->second, false);
		m_connToUser.erase(it);
        auto healthIt = m_connectionHealth.find(connId);
        if (healthIt != m_connectionHealth.end())
        {
            healthIt->second.authenticated = false;
            healthIt->second.state = ConnectionHealthState::Closing;
        }
		LOG_INFO("HeartbeatService::unregisterUserId - connId: {}", connId);
	}
}

void HeartbeatService::setNodeId(const std::string& nodeId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeId = nodeId;
}

void HeartbeatService::setNodeAliveTtlSeconds(int ttlSeconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeAliveTtlSeconds = ttlSeconds;
}

void HeartbeatService::setConnectionGovernance(int authTimeoutSeconds, int idleTimeoutSeconds, int slowConsumerKickSeconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_authTimeoutSeconds = authTimeoutSeconds;
    m_idleTimeoutSeconds = idleTimeoutSeconds;
    m_slowConsumerKickSeconds = slowConsumerKickSeconds;
}

void HeartbeatService::reportNodeAlive()
{
#ifdef USE_REDIS
    if (!m_useRedis || !m_redisClient)
    {
        return;
    }

    std::string nodeId;
    int nodeAliveTtlSeconds = 15;
    size_t connectionCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        nodeId = m_nodeId;
        nodeAliveTtlSeconds = m_nodeAliveTtlSeconds;
        connectionCount = m_connections.size();
    }

    auto report = [redisClient = m_redisClient, nodeId, nodeAliveTtlSeconds, connectionCount]() {
        redisClient->setex("node:alive:" + nodeId, "1", nodeAliveTtlSeconds);
        redisClient->setex("node:connections:" + nodeId, std::to_string(connectionCount), nodeAliveTtlSeconds);
    };

    if (m_threadPool)
    {
        m_threadPool->submit(std::move(report));
    }
    else
    {
        report();
    }
#endif
}

bool HeartbeatService::isUserOnline(const std::string& userId)
{
#ifdef USE_REDIS
	if (m_useRedis && m_redisClient)
	{
		std::string key = "user:online:" + userId;
		return m_redisClient->exists(key);
	}
#endif

	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& pair : m_connToUser)
	{
		if (pair.second == userId)
		{
			return true;
		}
	}
	return false;
}

void HeartbeatService::updateOnlineStatus(const std::string& userId, bool online)
{
#ifdef USE_REDIS
	if (m_useRedis && m_redisClient)
	{
		std::string key = "user:online:" + userId;
		if (online)
		{
			m_redisClient->setex(key, "1", m_timeoutSeconds);
			LOG_DEBUG("HeartbeatService: user {} online in Redis", userId);
		}
		else
		{
			m_redisClient->del(key);
			LOG_DEBUG("HeartbeatService: user {} offline in Redis", userId);
		}
	}
#endif
}

void HeartbeatService::refreshOnlineStatus(const std::string& userId)
{
#ifdef USE_REDIS
	if (m_useRedis && m_redisClient)
	{
		std::string key = "user:online:" + userId;
		m_redisClient->setex(key, "1", m_timeoutSeconds);
		LOG_DEBUG("HeartbeatService: refreshed user {} online status", userId);
	}
#endif
}

void HeartbeatService::updateOnlineStatusAsync(const std::string& userId, bool online)
{
#ifdef USE_REDIS
	if (!m_useRedis || !m_redisClient)
	{
		return;
	}

	if (m_threadPool)
	{
		auto redisClient = m_redisClient;
		int timeout = m_timeoutSeconds;
		m_threadPool->submit([redisClient, userId, online, timeout]() {
			std::string key = "user:online:" + userId;
			if (online)
			{
				redisClient->setex(key, "1", timeout);
			}
			else
			{
				redisClient->del(key);
			}
		});
	}
	else
	{
		updateOnlineStatus(userId, online);
	}
#endif
}

void HeartbeatService::refreshOnlineStatusAsync(const std::string& userId)
{
#ifdef USE_REDIS
	if (!m_useRedis || !m_redisClient)
	{
		return;
	}

	if (m_threadPool)
	{
		auto redisClient = m_redisClient;
		int timeout = m_timeoutSeconds;
		m_threadPool->submit([redisClient, userId, timeout]() {
			std::string key = "user:online:" + userId;
			redisClient->setex(key, "1", timeout);
		});
	}
	else
	{
		refreshOnlineStatus(userId);
	}
#endif
}

void HeartbeatService::checkHeartbeat()
{
    int64_t now = util::GetTimestampMs();
    int64_t heartbeatTimeoutMs = static_cast<int64_t>(m_timeoutSeconds) * 1000;
    int64_t authTimeoutMs = static_cast<int64_t>(m_authTimeoutSeconds) * 1000;
    int64_t idleTimeoutMs = static_cast<int64_t>(m_idleTimeoutSeconds) * 1000;
    int64_t slowConsumerKickMs = static_cast<int64_t>(m_slowConsumerKickSeconds) * 1000;

    std::vector<std::tuple<std::string, std::shared_ptr<TcpConnection>, std::string, std::string>> toKick;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& pair : m_connections)
        {
            const std::string& connId = pair.first;
            const auto& conn = pair.second;
            const auto healthIt = m_connectionHealth.find(connId);
            if (healthIt == m_connectionHealth.end())
            {
                continue;
            }

            const ConnectionHealth& health = healthIt->second;
            std::string reason;
            if (!health.authenticated && now - health.createdAtMs > authTimeoutMs)
            {
                reason = "auth timeout";
            }
            else if (health.state == ConnectionHealthState::BackPressured &&
                     health.lastBlockedAtMs > 0 &&
                     now - health.lastBlockedAtMs > slowConsumerKickMs)
            {
                reason = "slow consumer";
            }
            else if (now - health.lastHeartbeatAtMs > heartbeatTimeoutMs)
            {
                reason = "heartbeat timeout";
            }
            else if (health.authenticated && idleTimeoutMs > heartbeatTimeoutMs &&
                     now - health.lastHeartbeatAtMs > idleTimeoutMs)
            {
                reason = "idle timeout";
            }

            if (!reason.empty())
            {
                std::string userId;
                auto userIt = m_connToUser.find(connId);
                if (userIt != m_connToUser.end())
                {
                    userId = userIt->second;
                }
                toKick.emplace_back(connId, conn, userId, reason);
            }
        }

        for (const auto& item : toKick)
        {
            m_connections.erase(std::get<0>(item));
            m_lastHeartbeatTime.erase(std::get<0>(item));
            m_connToUser.erase(std::get<0>(item));
            m_connectionHealth.erase(std::get<0>(item));
        }
    }

    for (const auto& item : toKick)
    {
        const std::string& connId = std::get<0>(item);
        const auto& conn = std::get<1>(item);
        const std::string& userId = std::get<2>(item);
        const std::string& reason = std::get<3>(item);

        LOG_WARN("HeartbeatService::checkHeartbeat - kicking connId: {}, reason={}", connId, reason);
        if (reason == "heartbeat timeout")
        {
            infra::MetricsRegistry::instance().incCounter("im_heartbeat_timeout_total",
                                                          1,
                                                          "Connections kicked due to heartbeat timeout");
        }
#ifdef USE_REDIS
        if (!userId.empty())
        {
            updateOnlineStatusAsync(userId, false);
        }
#endif
        if (m_kickCallback)
        {
            m_kickCallback(conn, reason);
        }
    }
}
