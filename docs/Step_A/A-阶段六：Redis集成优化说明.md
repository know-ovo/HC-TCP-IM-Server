# Redis 集成优化说明文档

## 一、背景与目标

### 1.1 为什么需要 Redis

在单机服务器架构中，用户会话和在线状态存储在内存中。当需要扩展到集群部署时，面临以下问题：

```
┌─────────────────────────────────────────────────────────────┐
│                    单机架构的问题                             │
└─────────────────────────────────────────────────────────────┘

┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Server A  │     │   Server B  │     │   Server C  │
│  内存存储    │     │  内存存储     │     │  内存存储    │
│  User1 在线 │     │  User2 在线   │     │  User3 在线 │
└─────────────┘     └─────────────┘     └─────────────┘
       │                   │                   │
       └───────────────────┴───────────────────┘
                           │
                    状态不共享！
                    
问题：
1. User1 连接 Server A，Server B 不知道 User1 在线
2. 服务器重启后，所有用户需要重新登录
3. 无法实现跨服务器消息推送
```

引入 Redis 后：

```
┌─────────────────────────────────────────────────────────────┐
│                    集群架构（Redis 共享状态）                   │
└─────────────────────────────────────────────────────────────┘

                    ┌─────────────┐
                    │    Redis    │
                    │  共享存储    │
                    │ User1 在线  │
                    │ User2 在线  │
                    │ User3 在线  │
                    └──────┬──────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
        ▼                  ▼                  ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Server A  │     │   Server B  │     │   Server C  │
│  User1 连接 │      │  User2 连接 │     │  User3 连接  │
└─────────────┘     └─────────────┘     └─────────────┘

优势：
1. 所有服务器共享用户状态
2. 服务器重启不影响用户登录状态
3. 支持跨服务器消息推送
```

### 1.2 优化目标

| 目标 | 说明 |
|------|------|
| Session 共享 | 用户登录状态可在集群间共享 |
| 在线状态同步 | 用户在线状态实时更新到 Redis |
| 高性能 | Redis 操作不阻塞主线程 |
| 可扩展 | 支持水平扩展服务器节点 |

---

## 二、架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Redis 集成架构                            │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      客户端请求                              │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                      TcpServer                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                    EventLoop                        │   │
│  │         (epoll I/O 多路复用，非阻塞)                   │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                 │
│              ┌───────────┼───────────┐                     │
│              │           │           │                     │
│              ▼           ▼           ▼                     │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐          │
│  │AuthService  │ │HeartbeatSvc │ │MessageSvc   │          │
│  │ (登录验证)   │ │ (心跳处理)    │ │ (消息路由)   │          │
│  └──────┬──────┘ └──────┬──────┘ └─────────────┘          │
│         │               │                                  │
└─────────┼───────────────┼──────────────────────────────────┘
          │               │
          │    ┌──────────┴──────────┐
          │    │                     │
          ▼    ▼                     ▼
┌─────────────────────┐    ┌─────────────────────┐
│     ThreadPool      │    │       Redis         │
│   (异步任务执行)      │    │    (状态存储)        │
│                     │    │                     │
│  ┌───────────────┐  │    │  session:xxx        │
│  │ Redis 操作     │──┼──▶│  user:online:xxx    │
│  │ (非阻塞)       │  │    │  user:session:xxx   │
│  └───────────────┘  │    │                     │
└─────────────────────┘    └─────────────────────┘
```

### 2.2 数据结构设计

```
┌─────────────────────────────────────────────────────────────┐
│                    Redis 数据结构                            │
└─────────────────────────────────────────────────────────────┘

1. Session 存储
   Key:   session:{sessionId}..
   Value: JSON {userId, sessionId, deviceId, loginTime, authenticated}
   TTL:   24 小时（可配置）
   
   示例：
   session:a1b2c3d4 = {"userId":"user001","sessionId":"a1b2c3d4",...}
   TTL = 86400 秒

2. 用户在线状态
   Key:   user:online:{userId}
   Value: "1"
   TTL:   心跳超时时间（默认 30 秒）
   
   示例：
   user:online:user001 = "1"
   TTL = 30 秒

3. 用户 Session 映射
   Key:   user:session:{userId}
   Value: {sessionId}
   TTL:   24 小时
   
   示例：
   user:session:user001 = "a1b2c3d4"
   TTL = 86400 秒
```

---

## 三、代码实现

### 3.1 Redis 客户端封装

```cpp
class RedisClient
{
public:
    bool connect(const std::string& host, int port, int timeoutMs = 2000);
    void disconnect();
    
    bool set(const std::string& key, const std::string& value);
    bool setex(const std::string& key, const std::string& value, int expireSeconds);
    std::string get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    
private:
    redisContext* m_context;
    std::string m_lastError;
};
```

### 3.2 心跳服务异步化

**关键设计：Redis 操作异步执行，不阻塞 I/O 线程**

```cpp
class HeartbeatService
{
public:
    void setRedisClient(std::shared_ptr<RedisClient> redisClient);
    void setThreadPool(ThreadPool* pool);
    
    void onHeartbeat(const std::shared_ptr<TcpConnection>& conn);
    
private:
    void updateOnlineStatusAsync(const std::string& userId, bool online);
    void refreshOnlineStatusAsync(const std::string& userId);
    
    std::shared_ptr<RedisClient> m_redisClient;
    ThreadPool* m_threadPool;
};
```

**异步实现：**

```cpp
void HeartbeatService::refreshOnlineStatusAsync(const std::string& userId)
{
    if (!m_useRedis || !m_redisClient) return;
    
    if (m_threadPool)
    {
        auto redisClient = m_redisClient;
        int timeout = m_timeoutSeconds;
        
        m_threadPool->submit([redisClient, userId, timeout]() {
            std::string key = "user:online:" + userId;
            redisClient->setex(key, "1", timeout);
        });
    }
}
```

### 3.3 服务端初始化

```cpp
int main()
{
    ThreadPool threadPool(workerThreads);
    threadPool.start();
    
    #ifdef USE_REDIS
    auto redisClient = make_shared<RedisClient>();
    if (redisClient->connect(redisHost, redisPort, redisTimeout))
    {
        authService->setRedisClient(redisClient);
        heartbeatService->setRedisClient(redisClient);
        heartbeatService->setThreadPool(&threadPool);
    }
    #endif
    
    // ...
}
```

---

## 四、性能优化过程

### 4.1 问题发现

初始集成 Redis 后，性能测试发现最大延迟从 61ms 飙升到 2323ms：

```
┌─────────────────────────────────────────────────────────────┐
│                    问题分析                                  │
└─────────────────────────────────────────────────────────────┘

问题 1：Redis 操作阻塞 I/O 线程
┌──────────┐    ┌──────────────┐    ┌───────────────┐
│ 心跳请求 │───▶│ EventLoop    │───▶│ Redis SETEX   │ (阻塞!)
│          │    │ (I/O线程)    │    │ 同步调用      │
└──────────┘    └──────────────┘    └───────────────┘
                      │
                      ▼ 被阻塞
              其他连接无法处理

问题 2：测试客户端阻塞读取
┌──────────┐    ┌──────────────┐    ┌───────────────┐
│ 发送请求 │───▶│ read()       │───▶│ 无限等待响应  │
│          │    │ 阻塞调用     │    │               │
└──────────┘    └──────────────┘    └───────────────┘
```

### 4.2 优化措施

**优化 1：Redis 操作异步化**

```cpp
void HeartbeatService::onHeartbeat(const std::shared_ptr<TcpConnection>& conn)
{
    // 更新内存（快速）
    m_lastHeartbeatTime[connId] = util::GetTimestampMs();
    
    // 异步更新 Redis（不阻塞）
    refreshOnlineStatusAsync(userId);
}
```

**优化 2：测试客户端非阻塞化**

```cpp
// 设置非阻塞
fcntl(sockfd, F_SETFL, O_NONBLOCK);

// 带超时的读取
bool WaitForRead(int sockfd, int timeoutMs)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    
    return select(sockfd + 1, &readfds, nullptr, nullptr, &tv) > 0;
}
```

### 4.3 优化效果

| 指标 | 基线(无Redis) | Redis同步 | Redis异步 | 客户端修复 |
|------|---------------|-----------|-----------|------------|
| QPS | 41,270 | 45,360 | 45,360 | **47,000** |
| 最大延迟 | 61ms | 2323ms | 2323ms | **103ms** |
| P99延迟 | 24ms | 22ms | 22ms | 27ms |

```
┌─────────────────────────────────────────────────────────────┐
│                    优化效果可视化                            │
└─────────────────────────────────────────────────────────────┘

最大延迟变化：

基线:      61ms    ▓▓▓▓▓▓
Redis同步: 2323ms  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
                   ↑ 问题严重
Redis异步: 2323ms  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
                   ↑ 问题仍在（客户端阻塞）
客户端修复: 103ms   ▓▓▓▓▓▓▓▓▓▓
                   ✅ 改善 95.6%

QPS 变化：

基线:      41,270  ████████████████████████████████████████
客户端修复: 47,000  █████████████████████████████████████████████████
                   ✅ 提升 13.9%
```

---

## 五、最终性能对比

| 指标 | 无 Redis (基线) | Redis 优化后 | 变化 |
|------|-----------------|--------------|------|
| **QPS** | 41,270 | 47,000 | ✅ **+13.9%** |
| **平均延迟** | 10.55 ms | 10.01 ms | ✅ **-5.1%** |
| **P50 延迟** | 10.38 ms | 12.01 ms | ⚠️ +15.7% |
| **P90 延迟** | 13.00 ms | 14.26 ms | ⚠️ +9.7% |
| **P95 延迟** | 19.40 ms | 14.95 ms | ✅ **-22.9%** |
| **P99 延迟** | 24.03 ms | 26.62 ms | ⚠️ +10.8% |
| **最大延迟** | 61.43 ms | 102.89 ms | ⚠️ +67.5% |
| **错误率** | 0.00% | 0.00% | ✅ 持平 |
| **超时率** | - | 0.08% | 新增 |

---

## 六、最佳实践

### 6.1 Redis 操作异步化原则

```
┌─────────────────────────────────────────────────────────────┐
│                    异步操作判断指南                          │
└─────────────────────────────────────────────────────────────┘

                    需要返回值？
                        │
              ┌─────────┴─────────┐
              │                   │
             是                   否
              │                   │
              ▼                   ▼
       使用异步API          使用批量写入
       (回调获取结果)       (Fire-and-forget)
              │                   │
              │                   │
              ▼                   ▼
    ┌─────────────┐       ┌─────────────┐
    │ Session查询  │       │ 心跳更新      │
    │ 登录验证      │       │ 在线状态      │
    │ 排行榜查询    │       │ 计数器增加    │
    └─────────────┘       └─────────────┘
```

### 6.2 配置建议

```ini
[redis]
enabled = true
host = 127.0.0.1
port = 6379
timeout_ms = 2000
session_expire = 86400

[heartbeat]
timeout = 30
interval = 10
```

### 6.3 进一步优化方向

| 优化方向 | 说明 | 预期效果 |
|----------|------|----------|
| **Redis 连接池** | 多连接并行处理 | 减少连接竞争 |
| **Pipeline 批量写入** | 收集更新，批量发送 | 减少网络往返 |
| **hiredis 异步 API** | 使用原生异步接口 | 更高效的 I/O |
| **Redis 集群** | 数据分片存储 | 支持更大规模 |

---

## 七、总结

### 7.1 完成的工作

1. ✅ 集成 hiredis 客户端库
2. ✅ 实现 RedisClient 封装类
3. ✅ AuthService 集成 Session 存储
4. ✅ HeartbeatService 集成在线状态管理
5. ✅ Redis 操作异步化（线程池）
6. ✅ 测试客户端非阻塞化
7. ✅ 性能测试与优化

### 7.2 关键收益

| 收益 | 说明 |
|------|------|
| **集群支持** | 支持多服务器部署，状态共享 |
| **QPS 提升** | 从 41,270 提升到 47,000（+13.9%） |
| **延迟可控** | 最大延迟从 2323ms 降到 103ms |
| **可扩展性** | 为后续功能扩展奠定基础 |

### 7.3 注意事项

1. **编译时需要 hiredis 库**
   
   ```bash
   sudo apt-get install libhiredis-dev
   cmake -DUSE_REDIS=ON ..
   ```
   
2. **运行时需要 Redis 服务**
   ```bash
   sudo apt-get install redis-server
   sudo systemctl start redis
   ```

3. **配置文件需要启用 Redis**
   
   ```ini
   [redis]
   enabled = true
   ```

---

## 附录：文件清单

| 文件 | 说明 |
|------|------|
| `src/base/redisClient.h` | Redis 客户端头文件 |
| `src/base/redisClient.cpp` | Redis 客户端实现 |
| `src/service/authService.h/cpp` | 认证服务（Session 存储） |
| `src/service/heartbeatService.h/cpp` | 心跳服务（在线状态） |
| `src/server.cpp` | 服务器主程序（Redis 初始化） |
| `conf/server.ini` | 配置文件（Redis 配置） |
| `test/benchmarkClient.cpp` | 压测客户端（非阻塞优化） |
