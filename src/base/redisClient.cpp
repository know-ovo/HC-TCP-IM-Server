#include "base/redisClient.h"
#include "base/log.h"
#include <cstdarg>
#include <cstring>

RedisClient::RedisClient()
	: m_context(nullptr)
{
}

RedisClient::~RedisClient()
{
	disconnect();
}

bool RedisClient::connect(const std::string& host, int port, int timeoutMs)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	struct timeval timeout;
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_usec = (timeoutMs % 1000) * 1000;

	m_context = redisConnectWithTimeout(host.c_str(), port, timeout);
	if (m_context == nullptr || m_context->err)
	{
		if (m_context)
		{
			m_lastError = m_context->errstr;
			LOG_ERROR("Redis connect failed: {}", m_lastError);
			redisFree(m_context);
			m_context = nullptr;
		}
		else
		{
			m_lastError = "Failed to allocate redis context";
			LOG_ERROR("Redis connect failed: {}", m_lastError);
		}
		return false;
	}

	LOG_INFO("Redis connected: {}:{}", host, port);
	return true;
}

void RedisClient::disconnect()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_context)
	{
		redisFree(m_context);
		m_context = nullptr;
		LOG_INFO("Redis disconnected");
	}
}

bool RedisClient::isConnected() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_context != nullptr && m_context->err == 0;
}

bool RedisClient::set(const std::string& key, const std::string& value)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "SET %s %s", key.c_str(), value.c_str());
	bool success = checkReply(reply);
	if (success)
	{
		LOG_DEBUG("Redis SET {} = {}", key, value);
	}
	freeReply(reply);
	return success;
}

bool RedisClient::setex(const std::string& key, const std::string& value, int expireSeconds)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "SETEX %s %d %s", 
		key.c_str(), expireSeconds, value.c_str());
	bool success = checkReply(reply);
	if (success)
	{
		LOG_DEBUG("Redis SETEX {} = {} (expire: {}s)", key, value, expireSeconds);
	}
	freeReply(reply);
	return success;
}

std::string RedisClient::get(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "GET %s", key.c_str());
	std::string result;
	if (checkReply(reply) && reply->type == REDIS_REPLY_STRING)
	{
		result = std::string(reply->str, reply->len);
		LOG_DEBUG("Redis GET {} = {}", key, result);
	}
	freeReply(reply);
	return result;
}

bool RedisClient::del(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "DEL %s", key.c_str());
	bool success = checkReply(reply);
	freeReply(reply);
	return success;
}

bool RedisClient::exists(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "EXISTS %s", key.c_str());
	bool result = false;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = reply->integer == 1;
	}
	freeReply(reply);
	return result;
}

bool RedisClient::expire(const std::string& key, int seconds)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "EXPIRE %s %d", key.c_str(), seconds);
	bool success = checkReply(reply);
	freeReply(reply);
	return success;
}

int RedisClient::ttl(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "TTL %s", key.c_str());
	int result = -1;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = static_cast<int>(reply->integer);
	}
	freeReply(reply);
	return result;
}

long long RedisClient::incr(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "INCR %s", key.c_str());
	long long result = 0;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = reply->integer;
	}
	freeReply(reply);
	return result;
}

long long RedisClient::decr(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "DECR %s", key.c_str());
	long long result = 0;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = reply->integer;
	}
	freeReply(reply);
	return result;
}

long long RedisClient::incrBy(const std::string& key, long long increment)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "INCRBY %s %lld", key.c_str(), increment);
	long long result = 0;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = reply->integer;
	}
	freeReply(reply);
	return result;
}

bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "HSET %s %s %s",
		key.c_str(), field.c_str(), value.c_str());
	bool success = checkReply(reply);
	freeReply(reply);
	return success;
}

std::string RedisClient::hget(const std::string& key, const std::string& field)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "HGET %s %s",
		key.c_str(), field.c_str());
	std::string result;
	if (checkReply(reply) && reply->type == REDIS_REPLY_STRING)
	{
		result = std::string(reply->str, reply->len);
	}
	freeReply(reply);
	return result;
}

bool RedisClient::hdel(const std::string& key, const std::string& field)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "HDEL %s %s",
		key.c_str(), field.c_str());
	bool success = checkReply(reply);
	freeReply(reply);
	return success;
}

std::unordered_map<std::string, std::string> RedisClient::hgetall(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::unordered_map<std::string, std::string> result;
	redisReply* reply = (redisReply*)redisCommand(m_context, "HGETALL %s", key.c_str());
	if (checkReply(reply) && reply->type == REDIS_REPLY_ARRAY)
	{
		for (size_t i = 0; i + 1 < reply->elements; i += 2)
		{
			std::string field(reply->element[i]->str, reply->element[i]->len);
			std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);
			result[field] = value;
		}
	}
	freeReply(reply);
	return result;
}

long long RedisClient::lpush(const std::string& key, const std::string& value)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "LPUSH %s %s",
		key.c_str(), value.c_str());
	long long result = 0;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = reply->integer;
	}
	freeReply(reply);
	return result;
}

long long RedisClient::rpush(const std::string& key, const std::string& value)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "RPUSH %s %s",
		key.c_str(), value.c_str());
	long long result = 0;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = reply->integer;
	}
	freeReply(reply);
	return result;
}

std::string RedisClient::lpop(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "LPOP %s", key.c_str());
	std::string result;
	if (checkReply(reply) && reply->type == REDIS_REPLY_STRING)
	{
		result = std::string(reply->str, reply->len);
	}
	freeReply(reply);
	return result;
}

std::string RedisClient::rpop(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "RPOP %s", key.c_str());
	std::string result;
	if (checkReply(reply) && reply->type == REDIS_REPLY_STRING)
	{
		result = std::string(reply->str, reply->len);
	}
	freeReply(reply);
	return result;
}

std::vector<std::string> RedisClient::lrange(const std::string& key, long long start, long long stop)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<std::string> result;
	redisReply* reply = (redisReply*)redisCommand(m_context, "LRANGE %s %lld %lld",
		key.c_str(), start, stop);
	if (checkReply(reply) && reply->type == REDIS_REPLY_ARRAY)
	{
		for (size_t i = 0; i < reply->elements; ++i)
		{
			result.emplace_back(reply->element[i]->str, reply->element[i]->len);
		}
	}
	freeReply(reply);
	return result;
}

bool RedisClient::sadd(const std::string& key, const std::string& member)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "SADD %s %s",
		key.c_str(), member.c_str());
	bool success = checkReply(reply);
	freeReply(reply);
	return success;
}

bool RedisClient::srem(const std::string& key, const std::string& member)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "SREM %s %s",
		key.c_str(), member.c_str());
	bool success = checkReply(reply);
	freeReply(reply);
	return success;
}

bool RedisClient::sismember(const std::string& key, const std::string& member)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	redisReply* reply = (redisReply*)redisCommand(m_context, "SISMEMBER %s %s",
		key.c_str(), member.c_str());
	bool result = false;
	if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
	{
		result = reply->integer == 1;
	}
	freeReply(reply);
	return result;
}

std::vector<std::string> RedisClient::smembers(const std::string& key)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<std::string> result;
	redisReply* reply = (redisReply*)redisCommand(m_context, "SMEMBERS %s", key.c_str());
	if (checkReply(reply) && reply->type == REDIS_REPLY_ARRAY)
	{
		for (size_t i = 0; i < reply->elements; ++i)
		{
			result.emplace_back(reply->element[i]->str, reply->element[i]->len);
		}
	}
	freeReply(reply);
	return result;
}

redisReply* RedisClient::executeCommand(const char* format, ...)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	va_list ap;
	va_start(ap, format);
	redisReply* reply = (redisReply*)redisvCommand(m_context, format, ap);
	va_end(ap);
	return reply;
}

void RedisClient::freeReply(redisReply* reply)
{
	if (reply)
	{
		freeReplyObject(reply);
	}
}

bool RedisClient::checkReply(redisReply* reply)
{
	if (reply == nullptr)
	{
		m_lastError = "Redis reply is null";
		LOG_ERROR("Redis error: {}", m_lastError);
		return false;
	}
	if (reply->type == REDIS_REPLY_ERROR)
	{
		m_lastError = reply->str ? reply->str : "Unknown error";
		LOG_ERROR("Redis error: {}", m_lastError);
		return false;
	}
	return true;
}
