# A-阶段六：引入Redis优化

## 目录

- [1. 概述](#1-概述)
- [2. 整体架构设计](#2-整体架构设计)
- [3. RedisClient 详解](#3-redisclient-详解)
- [4. 服务集成详解](#4-服务集成详解)
- [5. 配置与构建](#5-配置与构建)
- [6. 实现难点分析](#6-实现难点分析)
- [7. Redis接入原因与思路](#7-redis接入原因与思路)
- [8. 扩展指南](#8-扩展指南)

---

## 1. 概述

### 1.1 阶段六目标

阶段六的核心任务是引入 Redis 作为分布式缓存和会话存储，主要包括：

1. **RedisClient 组件开发**：封装 hiredis 库，提供简洁的 Redis 操作接口
2. **AuthService 集成**：将会话信息存储到 Redis，支持分布式部署
3. **HeartbeatService 集成**：将用户在线状态存储到 Redis，支持跨服务器查询
4. **配置系统扩展**：增加 Redis 相关配置项
5. **构建系统适配**：条件编译 Redis 支持

### 1.2 新增文件清单

```
src/
├── base/
│   ├── redisClient.h          # Redis 客户端头文件
│   └── redisClient.cpp        # Redis 客户端实现
conf/
└── server.ini                 # 新增 [redis] 配置段
```

### 1.3 修改文件清单

```
src/
├── server.cpp                 # 初始化 Redis 连接
├── service/
│   ├── authService.h          # 新增 Redis 相关接口
│   ├── authService.cpp        # 会话存储到 Redis
│   ├── heartbeatService.h     # 新增 Redis 相关接口
│   └── heartbeatService.cpp   # 在线状态存储到 Redis
├── CMakeLists.txt             # 条件编译 Redis 支持
```

### 1.4 架构总览图

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              应用层 (Application Layer)                          │
│  ┌─────────────────────────────────────────────────────────────────────────┐    │
│  │                           server.cpp                                     │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │    │
│  │  │   Config    │  │  Log/Logger │  │  ThreadPool │  │ RedisClient │    │    │
│  │  │  配置管理   │  │   日志系统  │  │   线程池    │  │  Redis客户端│    │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │    │
│  └─────────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              服务层 (Service Layer)                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐            │
│  │ AuthService │  │HeartbeatSvc │  │ MessageSvc  │  │OverloadSvc  │            │
│  │  认证服务   │  │  心跳服务   │  │  消息服务   │  │  过载保护   │            │
│  │  ┌───────┐  │  │  ┌───────┐  │  └─────────────┘  └─────────────┘            │
│  │  │Redis  │  │  │  │Redis  │  │                                          │
│  │  │会话   │  │  │  │在线态 │  │                                          │
│  │  └───────┘  │  │  └───────┘  │                                          │
│  └─────────────┘  └─────────────┘                                          │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           存储层 (Storage Layer)                                 │
│  ┌─────────────────────────────────────────────────────────────────────────┐    │
│  │                           Redis                                          │    │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐          │    │
│  │  │ session:{sid}   │  │ user:session:{id}│  │ user:online:{id}│          │    │
│  │  │   会话信息      │  │   用户会话映射   │  │   在线状态      │          │    │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────┘          │    │
│  └─────────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 整体架构设计

### 2.1 Redis 在系统中的角色

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Redis 在系统中的定位                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 会话存储 (Session Storage)                                              │
│     ┌─────────────────────────────────────────────────────────────────┐    │
│     │ Key: session:{sessionId}                                        │    │
│     │ Value: JSON {userId, sessionId, deviceId, loginTime, ...}       │    │
│     │ TTL: 86400s (24小时)                                            │    │
│     └─────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  2. 用户会话映射 (User Session Mapping)                                     │
│     ┌─────────────────────────────────────────────────────────────────┐    │
│     │ Key: user:session:{userId}                                      │    │
│     │ Value: sessionId                                                │    │
│     │ TTL: 86400s (24小时)                                            │    │
│     └─────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  3. 在线状态 (Online Status)                                                │
│     ┌─────────────────────────────────────────────────────────────────┐    │
│     │ Key: user:online:{userId}                                       │    │
│     │ Value: "1"                                                      │    │
│     │ TTL: 30s (心跳超时时间)                                          │    │
│     └─────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 数据流向图

```
┌──────────────┐                    ┌──────────────┐                    ┌──────────────┐
│   Client     │                    │    Server    │                    │    Redis     │
│              │                    │              │                    │              │
└──────┬───────┘                    └──────┬───────┘                    └──────┬───────┘
       │                                   │                                   │
       │  1. Login Request                 │                                   │
       │ ─────────────────────────────────>│                                   │
       │                                   │                                   │
       │                                   │  2. SETEX session:{sid}           │
       │                                   │ ─────────────────────────────────>│
       │                                   │                                   │
       │                                   │  3. SETEX user:session:{userId}   │
       │                                   │ ─────────────────────────────────>│
       │                                   │                                   │
       │                                   │  4. SETEX user:online:{userId}    │
       │                                   │ ─────────────────────────────────>│
       │                                   │                                   │
       │  5. Login Response                │                                   │
       │ <─────────────────────────────────│                                   │
       │                                   │                                   │
       │  6. Heartbeat                     │                                   │
       │ ─────────────────────────────────>│                                   │
       │                                   │                                   │
       │                                   │  7. SETEX user:online:{userId}    │
       │                                   │ ─────────────────────────────────>│
       │                                   │                                   │
       │  8. Heartbeat Response            │                                   │
       │ <─────────────────────────────────│                                   │
       │                                   │                                   │
       │  9. Disconnect                    │                                   │
       │ ─────────────────────────────────>│                                   │
       │                                   │                                   │
       │                                   │  10. DEL user:online:{userId}     │
       │                                   │ ─────────────────────────────────>│
       │                                   │                                   │
       │                                   │  11. DEL session:{sid}            │
       │                                   │ ─────────────────────────────────>│
       │                                   │                                   │
       │                                   │  12. DEL user:session:{userId}    │
       │                                   │ ─────────────────────────────────>│
       │                                   │                                   │
```

### 2.3 条件编译设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          条件编译架构                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  CMakeLists.txt:                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ if(HIREDIS_FOUND)                                                │       │
│  │     target_compile_definitions(server_lib PUBLIC USE_REDIS)      │       │
│  │     target_link_libraries(server_lib PUBLIC hiredis)             │       │
│  │ endif()                                                          │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  代码中使用:                                                                │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ #ifdef USE_REDIS                                                 │       │
│  │     // Redis 相关代码                                            │       │
│  │ #endif                                                           │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  优势:                                                                      │
│  1. 没有 hiredis 库时仍可编译运行                                          │
│  2. 方便测试和开发                                                         │
│  3. 支持多环境部署                                                         │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. RedisClient 详解

### 3.1 类设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              RedisClient                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private:                                                                    │
│   - m_context: redisContext*              // hiredis 连接上下文             │
│   - m_lastError: std::string              // 最后一次错误信息               │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public:                                                                     │
│   + RedisClient()                          // 构造函数                      │
│   + ~RedisClient()                         // 析构函数                      │
│   + connect(host, port, timeoutMs) -> bool // 连接 Redis                   │
│   + disconnect()                           // 断开连接                      │
│   + isConnected() -> bool                  // 检查连接状态                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ String 操作:                                                                │
│   + set(key, value) -> bool                // 设置键值                     │
│   + setex(key, value, expire) -> bool      // 设置键值并指定过期时间        │
│   + get(key) -> string                     // 获取值                       │
│   + del(key) -> bool                       // 删除键                       │
│   + exists(key) -> bool                    // 检查键是否存在                │
│   + expire(key, seconds) -> bool           // 设置过期时间                  │
│   + ttl(key) -> int                        // 获取剩余过期时间              │
├─────────────────────────────────────────────────────────────────────────────┤
│ 计数器操作:                                                                 │
│   + incr(key) -> long long                 // 自增                         │
│   + decr(key) -> long long                 // 自减                         │
│   + incrBy(key, increment) -> long long    // 增加指定值                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ Hash 操作:                                                                  │
│   + hset(key, field, value) -> bool        // 设置哈希字段                 │
│   + hget(key, field) -> string             // 获取哈希字段                 │
│   + hdel(key, field) -> bool               // 删除哈希字段                 │
│   + hgetall(key) -> map                    // 获取所有字段                 │
├─────────────────────────────────────────────────────────────────────────────┤
│ List 操作:                                                                  │
│   + lpush(key, value) -> long long         // 左侧插入                     │
│   + rpush(key, value) -> long long         // 右侧插入                     │
│   + lpop(key) -> string                    // 左侧弹出                     │
│   + rpop(key) -> string                    // 右侧弹出                     │
│   + lrange(key, start, stop) -> vector     // 获取范围                     │
├─────────────────────────────────────────────────────────────────────────────┤
│ Set 操作:                                                                   │
│   + sadd(key, member) -> bool              // 添加成员                     │
│   + srem(key, member) -> bool              // 删除成员                     │
│   + sismember(key, member) -> bool         // 检查成员                     │
│   + smembers(key) -> vector                // 获取所有成员                 │
├─────────────────────────────────────────────────────────────────────────────┤
│ 其他:                                                                       │
│   + executeCommand(format, ...) -> redisReply*  // 执行原始命令            │
│   + getLastError() -> string               // 获取最后错误                 │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 核心实现

#### 3.2.1 连接管理

```cpp
bool RedisClient::connect(const std::string& host, int port, int timeoutMs)
{
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
    if (m_context)
    {
        redisFree(m_context);
        m_context = nullptr;
        LOG_INFO("Redis disconnected");
    }
}

bool RedisClient::isConnected() const
{
    return m_context != nullptr && m_context->err == 0;
}
```

#### 3.2.2 String 操作实现

```cpp
bool RedisClient::set(const std::string& key, const std::string& value)
{
    redisReply* reply = (redisReply*)redisCommand(m_context, "SET %s %s", 
        key.c_str(), value.c_str());
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
    redisReply* reply = (redisReply*)redisCommand(m_context, "DEL %s", key.c_str());
    bool success = checkReply(reply);
    freeReply(reply);
    return success;
}

bool RedisClient::exists(const std::string& key)
{
    redisReply* reply = (redisReply*)redisCommand(m_context, "EXISTS %s", key.c_str());
    bool result = false;
    if (checkReply(reply) && reply->type == REDIS_REPLY_INTEGER)
    {
        result = reply->integer == 1;
    }
    freeReply(reply);
    return result;
}
```

#### 3.2.3 错误处理

```cpp
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
```

---

## 4. 服务集成详解

### 4.1 AuthService 集成

#### 4.1.1 新增成员变量

```cpp
class AuthService
{
private:
    std::shared_ptr<RedisClient> m_redisClient;    // Redis 客户端
    int m_sessionExpire;                            // 会话过期时间（秒）
    bool m_useRedis;                                // 是否启用 Redis
};
```

#### 4.1.2 登录流程修改

```cpp
void AuthService::login(const std::shared_ptr<TcpConnection>& conn,
                        const std::string& userId,
                        const std::string& token,
                        const std::string& deviceId,
                        AuthCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

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
    context->m_authenticated = true;

    m_connToUser[connId] = context;
    m_userToContext[userId] = context;

#ifdef USE_REDIS
    if (m_useRedis && m_redisClient)
    {
        std::string sessionKey = "session:" + context->m_sessionId;
        std::string sessionData = serializeSession(context);
        if (m_redisClient->setex(sessionKey, sessionData, m_sessionExpire))
        {
            LOG_INFO("AuthService::login - session stored in Redis: {}", context->m_sessionId);
        }

        std::string userKey = "user:session:" + userId;
        m_redisClient->setex(userKey, context->m_sessionId, m_sessionExpire);
    }
#endif

    callback(true, context->m_sessionId, "");
}
```

#### 4.1.3 登出流程修改

```cpp
void AuthService::logout(const std::shared_ptr<TcpConnection>& conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string connId = std::to_string(conn->fd());
    auto it = m_connToUser.find(connId);
    if (it != m_connToUser.end())
    {
        std::string userId = it->second->m_userId;
        std::string sessionId = it->second->m_sessionId;

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

        m_userToContext.erase(userId);
        m_connToUser.erase(it);
    }
}
```

#### 4.1.4 会话验证修改

```cpp
bool AuthService::validateSession(const std::string& sessionId, std::string& outUserId)
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
```

#### 4.1.5 会话序列化

```cpp
std::string AuthService::serializeSession(const std::shared_ptr<UserContext>& context)
{
    nlohmann::json j;
    j["userId"] = context->m_userId;
    j["sessionId"] = context->m_sessionId;
    j["deviceId"] = context->m_deviceId;
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
```

### 4.2 HeartbeatService 集成

#### 4.2.1 新增成员变量

```cpp
class HeartbeatService
{
private:
    std::shared_ptr<RedisClient> m_redisClient;           // Redis 客户端
    ThreadPool* m_threadPool;                              // 线程池（异步操作）
    bool m_useRedis;                                       // 是否启用 Redis
    std::unordered_map<std::string, std::string> m_connToUser;  // connId -> userId
};
```

#### 4.2.2 在线状态管理

```cpp
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
```

#### 4.2.3 异步 Redis 操作

```cpp
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
```

#### 4.2.4 心跳处理修改

```cpp
void HeartbeatService::onHeartbeat(const std::shared_ptr<TcpConnection>& conn)
{
    std::string connId = std::to_string(conn->fd());
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastHeartbeatTime[connId] = util::GetTimestampMs();
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
```

#### 4.2.5 用户注册与注销

```cpp
void HeartbeatService::registerUserId(const std::string& connId, const std::string& userId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connToUser[connId] = userId;
    }
    updateOnlineStatusAsync(userId, true);
    LOG_INFO("HeartbeatService::registerUserId - connId: {}, userId: {}", connId, userId);
}

void HeartbeatService::unregisterUserId(const std::string& connId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connToUser.find(connId);
    if (it != m_connToUser.end())
    {
        updateOnlineStatusAsync(it->second, false);
        m_connToUser.erase(it);
        LOG_INFO("HeartbeatService::unregisterUserId - connId: {}", connId);
    }
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
```

---

## 5. 配置与构建

### 5.1 配置文件

```ini
[server]
port = 8888
worker_threads = 4

[log]
level = info
path = ../logs/server.log
max_file_size = 104857600
max_files = 3

[heartbeat]
timeout = 30
interval = 10

[overload]
max_connections = 10000
max_qps = 100000

[redis]
enabled = true              # 是否启用 Redis
host = 127.0.0.1           # Redis 服务器地址
port = 6379                # Redis 端口
timeout_ms = 2000          # 连接超时（毫秒）
session_expire = 86400     # 会话过期时间（秒）
```

### 5.2 CMakeLists.txt 修改

```cmake
# 查找 hiredis 库
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HIREDIS QUIET hiredis)
endif()

# 条件编译 Redis 支持
if(HIREDIS_FOUND)
    message(STATUS "Found hiredis: ${HIREDIS_LIBRARIES}")
    
    # 添加 Redis 客户端源文件
    list(APPEND BASE_SOURCES base/redisClient.cpp)
    
    # 添加编译定义
    target_compile_definitions(server_lib PUBLIC USE_REDIS)
    
    # 添加头文件路径和链接库
    target_include_directories(server_lib PUBLIC ${HIREDIS_INCLUDE_DIRS})
    target_link_libraries(server_lib PUBLIC ${HIREDIS_LIBRARIES})
else()
    message(STATUS "hiredis not found, Redis support disabled")
endif()
```

### 5.3 server.cpp 初始化

```cpp
#ifdef USE_REDIS
#include "base/redisClient.h"
#endif

int main(int argc, char* argv[])
{
    // ... 其他初始化 ...

#ifdef USE_REDIS
    bool redisEnabled = config.getBool("redis", "enabled", false);
    shared_ptr<RedisClient> redisClient;

    if (redisEnabled)
    {
        string redisHost = config.getString("redis", "host", "127.0.0.1");
        int redisPort = config.getInt("redis", "port", 6379);
        int redisTimeout = config.getInt("redis", "timeout_ms", 2000);
        int sessionExpire = config.getInt("redis", "session_expire", 86400);

        redisClient = make_shared<RedisClient>();
        if (redisClient->connect(redisHost, redisPort, redisTimeout))
        {
            LOG_INFO("Redis connected: {}:{}", redisHost, redisPort);
            authService->setRedisClient(redisClient);
            authService->setSessionExpire(sessionExpire);
        }
        else
        {
            LOG_ERROR("Failed to connect Redis: {}", redisClient->getLastError());
            redisClient.reset();
        }
    }
#else
    LOG_INFO("Redis support not compiled in");
#endif

    // ... 创建 HeartbeatService ...

#ifdef USE_REDIS
    if (redisClient)
    {
        heartbeatService->setRedisClient(redisClient);
        heartbeatService->setThreadPool(&threadPool);
    }
#endif

    // ... 服务器启动 ...

#ifdef USE_REDIS
    if (redisClient)
    {
        redisClient->disconnect();
    }
#endif

    return 0;
}
```

---

## 6. 实现难点分析

### 6.1 条件编译的复杂性

**问题描述：**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          条件编译的复杂性                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  问题：大量 #ifdef USE_REDIS 导致代码可读性下降                             │
│                                                                             │
│  示例：                                                                     │
│  void login(...) {                                                          │
│      // 本地存储                                                            │
│      m_connToUser[connId] = context;                                       │
│                                                                             │
│ #ifdef USE_REDIS                                                           │
│      // Redis 存储                                                          │
│      if (m_useRedis && m_redisClient) {                                    │
│          m_redisClient->setex(...);                                        │
│      }                                                                      │
│ #endif                                                                     │
│                                                                             │
│      callback(...);                                                         │
│  }                                                                          │
│                                                                             │
│  解决方案：                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. 使用辅助函数封装 Redis 操作                                   │       │
│  │ 2. 运行时检查 m_useRedis 标志                                   │       │
│  │ 3. 保持代码结构清晰                                             │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 异步操作与线程安全

**问题描述：**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          异步操作与线程安全                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  问题 1：Redis 操作在线程池中执行，如何保证 RedisClient 生命周期？          │
│                                                                             │
│  错误示例：                                                                 │
│  void onHeartbeat() {                                                       │
│      m_threadPool->submit([this]() {                                       │
│          m_redisClient->setex(...);  // this 可能已销毁！                   │
│      });                                                                    │
│  }                                                                          │
│                                                                             │
│  正确做法：                                                                 │
│  void onHeartbeat() {                                                       │
│      auto redisClient = m_redisClient;  // 捕获 shared_ptr                 │
│      m_threadPool->submit([redisClient]() {                                │
│          redisClient->setex(...);  // 安全                                  │
│      });                                                                    │
│  }                                                                          │
│                                                                             │
│  问题 2：RedisClient 本身是否线程安全？                                     │
│                                                                             │
│  分析：                                                                     │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ hiredis 的 redisContext 不是线程安全的！                         │       │
│  │ 多线程同时使用同一个 redisContext 会导致数据混乱。               │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  解决方案：                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 方案 1：每个线程一个 Redis 连接（连接池）                         │       │
│  │ 方案 2：使用 Redis 异步 API（hiredis 的 async 接口）             │       │
│  │ 方案 3：加锁保护 Redis 操作（简单但性能差）                       │       │
│  │                                                                 │       │
│  │ 本项目采用方案 3（简单实现），生产环境建议方案 1 或 2            │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 6.3 数据一致性

**问题描述：**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          数据一致性问题                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  问题：本地内存和 Redis 数据如何保持一致？                                  │
│                                                                             │
│  场景 1：用户登录                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. 本地存储成功                                                 │       │
│  │ 2. Redis 存储失败                                               │       │
│  │ 结果：本地有数据，Redis 没有                                     │       │
│  │                                                                 │       │
│  │ 解决方案：                                                       │       │
│  │ - Redis 失败时记录日志，但不影响登录成功                         │       │
│  │ - 后续操作优先使用本地数据                                       │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  场景 2：用户登出                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. 本地删除成功                                                 │       │
│  │ 2. Redis 删除失败                                               │       │
│  │ 结果：Redis 中仍有残留数据                                       │       │
│  │                                                                 │       │
│  │ 解决方案：                                                       │       │
│  │ - 设置合理的 TTL，让数据自动过期                                 │       │
│  │ - Redis 操作失败不影响主流程                                     │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  场景 3：服务器崩溃                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 问题：本地内存丢失，Redis 中仍有数据                             │       │
│  │                                                                 │       │
│  │ 解决方案：                                                       │       │
│  │ - 服务器重启流程：                                               │       │
│  │   1. 服务器启动                                                  │       │
│  │   2. 连接 Redis                                                  │       │
│  │   3. 用户请求携带 sessionId                                      │       │
│  │   4. 从 Redis 验证 sessionId                                     │       │
│  │   5. 恢复用户会话                                                │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 6.4 性能考量

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          性能考量                                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. Redis 操作延迟                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ - 网络延迟：通常 0.1-1ms（本地）                                 │       │
│  │ - Redis 处理：通常 < 0.1ms                                      │       │
│  │ - 总延迟：1-2ms                                                  │       │
│  │                                                                 │       │
│  │ 影响：如果同步执行，会阻塞 EventLoop                             │       │
│  │ 解决：使用线程池异步执行                                         │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  2. 连接数限制                                                              │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ - Redis 默认最大连接数：10000                                    │       │
│  │ - 每个服务器实例使用 1 个连接                                    │       │
│  │ - 分布式部署时需要注意连接数管理                                 │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  3. 内存使用                                                                │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ - 每个会话约 200-500 字节                                        │       │
│  │ - 10 万用户约需 20-50MB                                          │       │
│  │ - 需要合理设置 TTL 避免内存泄漏                                  │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 7. Redis接入原因与思路

### 7.1 为什么需要 Redis？

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Redis 接入原因                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 分布式会话存储                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │                                                                 │       │
│  │   单机模式：                                                    │       │
│  │   ┌─────────────┐                                              │       │
│  │   │   Server    │                                              │       │
│  │   │  本地内存   │  ← 会话存储在本地内存                        │       │
│  │   └─────────────┘                                              │       │
│  │                                                                 │       │
│  │   问题：                                                        │       │
│  │   - 服务器重启，所有会话丢失                                    │       │
│  │   - 无法支持分布式部署                                          │       │
│  │                                                                 │       │
│  │   分布式模式：                                                  │       │
│  │   ┌─────────────┐     ┌─────────────┐                          │       │
│  │   │  Server A   │     │  Server B   │                          │       │
│  │   └──────┬──────┘     └──────┬──────┘                          │       │
│  │          │                   │                                  │       │
│  │          └─────────┬─────────┘                                  │       │
│  │                    │                                            │       │
│  │                    ▼                                            │       │
│  │            ┌─────────────┐                                      │       │
│  │            │    Redis    │  ← 会话存储在 Redis                  │       │
│  │            └─────────────┘                                      │       │
│  │                                                                 │       │
│  │   优势：                                                        │       │
│  │   - 服务器重启后可从 Redis 恢复会话                             │       │
│  │   - 支持多服务器部署，会话共享                                  │       │
│  │                                                                 │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  2. 在线状态共享                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │                                                                 │       │
│  │   场景：用户 A 在 Server 1，用户 B 在 Server 2                  │       │
│  │   问题：用户 A 如何知道用户 B 是否在线？                        │       │
│  │   解决：在线状态存储在 Redis，所有服务器共享                     │       │
│  │                                                                 │       │
│  │   Redis Key 设计：                                             │       │
│  │   user:online:{userId} → "1" (TTL: 30s)                        │       │
│  │                                                                 │       │
│  │   心跳刷新：每次心跳更新 TTL                                    │       │
│  │   超时自动过期：30s 无心跳则自动下线                            │       │
│  │                                                                 │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  3. 性能优化                                                                │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │                                                                 │       │
│  │   Redis 特点：                                                 │       │
│  │   - 内存存储，读写速度快（10万+ QPS）                           │       │
│  │   - 支持过期时间，自动清理过期数据                              │       │
│  │   - 支持多种数据结构（String、Hash、Set 等）                    │       │
│  │                                                                 │       │
│  │   适用场景：                                                    │       │
│  │   - 会话存储：需要频繁读写                                      │       │
│  │   - 在线状态：需要频繁更新                                      │       │
│  │   - 消息队列：可以使用 List 实现                                │       │
│  │                                                                 │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 Redis Key 设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Redis Key 设计                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 会话信息                                                                │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ Key:   session:{sessionId}                                      │       │
│  │ Value: JSON {userId, sessionId, deviceId, loginTime, ...}       │       │
│  │ TTL:   86400s (24小时)                                          │       │
│  │                                                                 │       │
│  │ 示例:                                                           │       │
│  │ session:18f3a2b1c000-7f3d2a1b9c4e5f6a =                        │       │
│  │   {"userId":"user123","sessionId":"18f3a2b1c000-...",...}      │       │
│  │                                                                 │       │
│  │ 用途：验证会话有效性，恢复用户上下文                            │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  2. 用户会话映射                                                            │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ Key:   user:session:{userId}                                    │       │
│  │ Value: sessionId                                                │       │
│  │ TTL:   86400s (24小时)                                          │       │
│  │                                                                 │       │
│  │ 示例:                                                           │       │
│  │ user:session:user123 = "18f3a2b1c000-7f3d2a1b9c4e5f6a"         │       │
│  │                                                                 │       │
│  │ 用途：根据用户 ID 查找会话 ID                                   │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  3. 在线状态                                                                │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ Key:   user:online:{userId}                                     │       │
│  │ Value: "1"                                                      │       │
│  │ TTL:   30s (心跳超时时间)                                        │       │
│  │                                                                 │       │
│  │ 示例:                                                           │       │
│  │ user:online:user123 = "1"                                       │       │
│  │                                                                 │       │
│  │ 用途：判断用户是否在线，支持跨服务器查询                        │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  Key 命名规范：                                                             │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. 使用冒号分隔，形成命名空间                                    │       │
│  │ 2. 格式：{业务}:{实体}:{ID}                                     │       │
│  │ 3. 便于管理和查询                                                │       │
│  │                                                                 │       │
│  │ 示例：                                                          │       │
│  │ session:xxx          → 会话命名空间                             │       │
│  │ user:session:xxx     → 用户命名空间下的会话                     │       │
│  │ user:online:xxx      → 用户命名空间下的在线状态                 │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.3 接入思路总结

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Redis 接入思路                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Step 1: 封装 Redis 客户端                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ • 基于 hiredis 库封装                                           │       │
│  │ • 提供常用操作的封装（set/get/setex/del 等）                     │       │
│  │ • 统一错误处理                                                   │       │
│  │ • 支持连接超时设置                                               │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  Step 2: 设计 Key 结构                                                      │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ • 会话信息：session:{sessionId}                                 │       │
│  │ • 用户映射：user:session:{userId}                               │       │
│  │ • 在线状态：user:online:{userId}                                │       │
│  │ • 合理设置 TTL                                                   │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  Step 3: 集成到服务层                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ • AuthService：会话存储和验证                                   │       │
│  │ • HeartbeatService：在线状态管理                                │       │
│  │ • 使用条件编译保证兼容性                                         │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  Step 4: 异步操作优化                                                       │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ • 使用线程池执行 Redis 操作                                     │       │
│  │ • 避免阻塞 EventLoop                                            │       │
│  │ • 注意生命周期管理                                               │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  Step 5: 错误处理和降级                                                     │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ • Redis 连接失败时降级为本地存储                                │       │
│  │ • 操作失败不影响主流程                                           │       │
│  │ • 记录日志便于排查                                               │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 8. 扩展指南

### 8.1 Redis 连接池

```cpp
class RedisConnectionPool
{
public:
    RedisConnectionPool(size_t poolSize, const std::string& host, int port);
    std::shared_ptr<RedisClient> getConnection();
    void returnConnection(std::shared_ptr<RedisClient> conn);

private:
    std::queue<std::shared_ptr<RedisClient>> m_pool;
    std::mutex m_mutex;
    std::condition_variable m_cond;
};
```

### 8.2 Redis 集群支持

```cpp
class RedisClusterClient
{
public:
    bool connectCluster(const std::vector<std::pair<std::string, int>>& nodes);
    bool set(const std::string& key, const std::string& value);
};
```

### 8.3 Redis 订阅/发布

```cpp
class RedisPubSub
{
public:
    void subscribe(const std::string& channel, MessageCallback cb);
    void publish(const std::string& channel, const std::string& message);
};
```

### 8.4 Redis Lua 脚本

```lua
local current = redis.call('GET', KEYS[1])
if current == ARGV[1] then
    redis.call('SETEX', KEYS[1], ARGV[2], ARGV[1])
    return 1
end
return 0
```

---

## （本章完）
