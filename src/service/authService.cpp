#include "service/authService.h"
#include "base/log.h"
#include "base/util.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>

#ifdef USE_REDIS
#include "base/redisClient.h"
#endif

AuthService::AuthService()
	: m_sessionExpire(86400)
	, m_useRedis(false)
{
}

AuthService::~AuthService()
{
}

void AuthService::setRedisClient(std::shared_ptr<RedisClient> redisClient)
{
#ifdef USE_REDIS
	m_redisClient = redisClient;
	m_useRedis = (redisClient != nullptr && redisClient->isConnected());
	if (m_useRedis)
	{
		LOG_INFO("AuthService: Redis enabled for session storage");
	}
#else
	LOG_WARN("AuthService: Redis support not compiled in");
#endif
}

void AuthService::setSessionRepository(std::shared_ptr<storage::SessionRepository> sessionRepository)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionRepository = std::move(sessionRepository);
}

void AuthService::setNodeId(const std::string& nodeId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeId = nodeId;
}

bool AuthService::bindSessionToNode(const std::string& sessionId, const std::string& nodeId)
{
#ifdef USE_REDIS
    if (m_useRedis && m_redisClient)
    {
        std::string sessionKey = "session:" + sessionId;
        std::string sessionData = m_redisClient->get(sessionKey);
        if (!sessionData.empty())
        {
            auto context = std::make_shared<UserContext>();
            if (deserializeSession(sessionData, context))
            {
                context->m_nodeId = nodeId;
                return m_redisClient->setex(sessionKey, serializeSession(context), m_sessionExpire);
            }
        }
    }
#endif

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_connToUser)
    {
        if (pair.second->m_sessionId == sessionId)
        {
            pair.second->m_nodeId = nodeId;
            return true;
        }
    }
    return false;
}

std::optional<std::string> AuthService::getSessionNode(const std::string& sessionId)
{
#ifdef USE_REDIS
    if (m_useRedis && m_redisClient)
    {
        std::string sessionKey = "session:" + sessionId;
        std::string sessionData = m_redisClient->get(sessionKey);
        if (!sessionData.empty())
        {
            auto context = std::make_shared<UserContext>();
            if (deserializeSession(sessionData, context) && !context->m_nodeId.empty())
            {
                return context->m_nodeId;
            }
        }
    }
#endif

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& pair : m_connToUser)
    {
        if (pair.second->m_sessionId == sessionId && !pair.second->m_nodeId.empty())
        {
            return pair.second->m_nodeId;
        }
    }
    return std::nullopt;
}

bool AuthService::validateToken(const std::string& userId, const std::string& token)
{
	if (userId.empty() || token.empty())
	{
		return false;
	}
	return token == "valid_token_" + userId;
}

std::string AuthService::generateSessionId()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint64_t> dis;

	uint64_t randNum = dis(gen);
	int64_t timestamp = util::GetTimestampMs();

	std::stringstream ss;
	ss << std::hex << timestamp << "-" << randNum;
	return ss.str();
}

std::string AuthService::serializeSession(const std::shared_ptr<UserContext>& context)
{
	nlohmann::json j;
	j["userId"] = context->m_userId;
	j["sessionId"] = context->m_sessionId;
	j["deviceId"] = context->m_deviceId;
	j["nodeId"] = context->m_nodeId;
	j["loginTime"] = context->m_loginTime;
	j["authenticated"] = context->m_authenticated;
	return j.dump();
}

bool AuthService::deserializeSession(const std::string& data, std::shared_ptr<UserContext>& context)
{
	try
	{
		nlohmann::json j = nlohmann::json::parse(data);
		context->m_userId = j.value("userId", "");
		context->m_sessionId = j.value("sessionId", "");
		context->m_deviceId = j.value("deviceId", "");
		context->m_nodeId = j.value("nodeId", "");
		context->m_loginTime = j.value("loginTime", (int64_t)0);
		context->m_authenticated = j.value("authenticated", false);
		return true;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("Failed to deserialize session: {}", e.what());
		return false;
	}
}

void AuthService::login(const std::shared_ptr<TcpConnection>& conn,
						const std::string& userId,
						const std::string& token,
						const std::string& deviceId,
						AuthCallback callback)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	LOG_INFO("AuthService::login - userId: {}, deviceId: {}", userId, deviceId);

	if (!validateToken(userId, token))
	{
		callback(false, "", "Invalid token");
		return;
	}

	std::string connId = std::to_string(conn->fd());

	auto context = std::make_shared<UserContext>();
	context->m_userId = userId;
	context->m_deviceId = deviceId;
	context->m_sessionId = generateSessionId();
	context->m_loginTime = util::GetTimestampMs();
	context->m_nodeId = m_nodeId;
	context->m_authenticated = true;

	m_connToUser[connId] = context;

	auto it = m_userToContext.find(userId);
	if (it != m_userToContext.end())
	{
		LOG_WARN("AuthService::login - user {} already logged in, replacing", userId);
	}
	m_userToContext[userId] = context;

    if (m_sessionRepository)
    {
        storage::SessionRecord record;
        record.sessionId = context->m_sessionId;
        record.userId = context->m_userId;
        record.deviceId = context->m_deviceId;
        record.createdAtMs = context->m_loginTime;
        record.expiresAtMs = context->m_loginTime + static_cast<int64_t>(m_sessionExpire) * 1000;
        std::string repoError;
        if (!m_sessionRepository->createSession(record, repoError))
        {
            LOG_WARN("AuthService::login - failed to persist session to MySQL: {}", repoError);
        }
    }

#ifdef USE_REDIS
	if (m_useRedis && m_redisClient)
	{
		std::string sessionKey = "session:" + context->m_sessionId;
		std::string sessionData = serializeSession(context);
		if (m_redisClient->setex(sessionKey, sessionData, m_sessionExpire))
		{
			LOG_INFO("AuthService::login - session stored in Redis: {}", context->m_sessionId);
		}
		else
		{
			LOG_ERROR("AuthService::login - failed to store session in Redis");
		}

		std::string userKey = "user:session:" + userId;
		m_redisClient->setex(userKey, context->m_sessionId, m_sessionExpire);
	}
#endif

    bindSessionToNode(context->m_sessionId, context->m_nodeId);

	LOG_INFO("AuthService::login - success, userId: {}, sessionId: {}",
			 userId, context->m_sessionId);

	callback(true, context->m_sessionId, "");
}

void AuthService::logout(const std::shared_ptr<TcpConnection>& conn)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::string connId = std::to_string(conn->fd());
	auto it = m_connToUser.find(connId);
	if (it != m_connToUser.end())
	{
		std::string userId = it->second->m_userId;
		std::string sessionId = it->second->m_sessionId;
		LOG_INFO("AuthService::logout - userId: {}", userId);

#ifdef USE_REDIS
		if (m_useRedis && m_redisClient)
		{
			std::string sessionKey = "session:" + sessionId;
			m_redisClient->del(sessionKey);

			std::string userKey = "user:session:" + userId;
			m_redisClient->del(userKey);

			LOG_INFO("AuthService::logout - session removed from Redis");
		}
#endif

        if (m_sessionRepository)
        {
            std::string repoError;
            if (!m_sessionRepository->deleteSession(sessionId, repoError))
            {
                LOG_WARN("AuthService::logout - failed to delete session from MySQL: {}", repoError);
            }
        }

		m_userToContext.erase(userId);
		m_connToUser.erase(it);
	}
}

bool AuthService::isAuthenticated(const std::shared_ptr<TcpConnection>& conn)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::string connId = std::to_string(conn->fd());
	auto it = m_connToUser.find(connId);
	if (it != m_connToUser.end())
	{
		return it->second->m_authenticated;
	}
	return false;
}

std::shared_ptr<UserContext> AuthService::getUserContext(const std::shared_ptr<TcpConnection>& conn)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::string connId = std::to_string(conn->fd());
	auto it = m_connToUser.find(connId);
	if (it != m_connToUser.end())
	{
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<UserContext> AuthService::getUserById(const std::string& userId)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto it = m_userToContext.find(userId);
	if (it != m_userToContext.end())
	{
		return it->second;
	}
	return nullptr;
}

bool AuthService::validateSession(const std::string& sessionId, std::string& outUserId)
{
    if (m_sessionRepository)
    {
        std::string repoError;
        const auto record = m_sessionRepository->findBySessionId(sessionId, repoError);
        if (record)
        {
            outUserId = record->userId;
            return true;
        }
        if (!repoError.empty())
        {
            LOG_WARN("AuthService::validateSession - mysql lookup failed: {}", repoError);
        }
    }

#ifdef USE_REDIS
	if (m_useRedis && m_redisClient)
	{
		std::string sessionKey = "session:" + sessionId;
		std::string sessionData = m_redisClient->get(sessionKey);
		if (!sessionData.empty())
		{
			auto context = std::make_shared<UserContext>();
			if (deserializeSession(sessionData, context))
			{
				outUserId = context->m_userId;
				return true;
			}
		}
		return false;
	}
#endif

	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& pair : m_connToUser)
	{
		if (pair.second->m_sessionId == sessionId)
		{
			outUserId = pair.second->m_userId;
			return true;
		}
	}
	return false;
}
