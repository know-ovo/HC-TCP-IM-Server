# P1 执行清单：分布式、稳定性、可观测与工程化逐阶段任务清单

## 1. 目标

P1 的目标不是让系统“更花哨”，而是让它具备：

- 多节点状态共享
- 故障恢复意识
- 自我保护能力
- 可观测能力
- 基础工程化能力

---

## 2. 涉及文件总览

## 重点修改的现有文件

- `src/base/redisClient.h`
- `src/base/redisClient.cpp`
- `src/service/authService.h`
- `src/service/authService.cpp`
- `src/service/heartbeatService.h`
- `src/service/heartbeatService.cpp`
- `src/service/overloadProtectService.h`
- `src/service/overloadProtectService.cpp`
- `src/server_full.cpp`
- `conf/server.ini`
- `src/CMakeLists.txt`
- `CMakeLists.txt`

## 建议新增的文件

- `src/service/routingService.h`
- `src/service/routingService.cpp`
- `src/infra/rateLimiter.h`
- `src/infra/rateLimiter.cpp`
- `src/infra/circuitBreaker.h`
- `src/infra/circuitBreaker.cpp`
- `src/infra/metricsRegistry.h`
- `src/infra/metricsRegistry.cpp`
- `src/infra/traceContext.h`
- `src/infra/traceContext.cpp`
- `src/http/metricsServer.h`
- `src/http/metricsServer.cpp`
- `tests/integration/*`

---

## 3. 阶段一：Redis 状态中心升级

## 3.1 任务目标

让 Redis 从“会话和在线状态存储”升级为“节点共享状态中心”。

## 3.2 Redis key 设计

建议统一键设计：

```text
session:{sessionId}
user:session:{userId}
user:online:{userId}
route:{userId}
node:alive:{nodeId}
node:connections:{nodeId}
```

## 3.3 修改 `AuthService`

建议新增接口：

```cpp
void setNodeId(const std::string& nodeId);
bool bindSessionToNode(const std::string& sessionId, const std::string& nodeId);
std::optional<std::string> getSessionNode(const std::string& sessionId);
```

## 3.4 修改 `HeartbeatService`

建议新增：

```cpp
void setNodeId(const std::string& nodeId);
void reportNodeAlive();
```

节点健康上报建议周期：

- 每 `5s` 刷新一次 `node:alive:{nodeId}`

## 3.5 配置项

```ini
[cluster]
enabled = true
node_id = node-1
node_alive_ttl_seconds = 15
route_ttl_seconds = 30
```

---

## 4. 阶段二：路由服务与跨节点消息转发

## 4.1 新增 `RoutingService`

建议接口：

```cpp
class RoutingService
{
public:
    void setRedisClient(std::shared_ptr<RedisClient> client);
    void setNodeId(const std::string& nodeId);

    bool bindUserRoute(const std::string& userId, const std::string& nodeId);
    bool unbindUserRoute(const std::string& userId);
    std::optional<std::string> lookupUserRoute(const std::string& userId);
};
```

## 4.2 服务层整合任务

登录成功时：

- `AuthService` 建 session
- `HeartbeatService` 建 online
- `RoutingService` 建 route

断开连接时：

- 清理 route
- 清理 online

## 4.3 跨节点转发策略

第一版建议不引入 Kafka/NATS，先走轻量方案：

- 查 Redis 路由
- 如果目标在本节点，直接投递
- 如果目标在其他节点：
  - 先记录日志与返回“跨节点转发未启用”
  - 第二步再补节点间 RPC / 转发通道

这样可以分两段落地，避免一次做太大。

## 4.4 验收标准

- 同一用户登录后，Redis 中可查到 `route:{userId}`
- 断开连接后，route 被及时清理
- 代码结构中已形成“本地投递”和“远端投递”的分支

---

## 5. 阶段三：限流系统升级

## 5.1 新增 `RateLimiter`

建议第一版实现令牌桶：

```cpp
class TokenBucketRateLimiter
{
public:
    TokenBucketRateLimiter(double rate, double burst);
    bool allow();
};
```

## 5.2 修改 `OverloadProtectService`

当前类更偏全局计数保护。  
建议保留它，同时新增更细粒度接口：

```cpp
bool canAcceptConnection(const std::string& ip);
bool canProcessCommand(uint16_t command);
bool canSendMessage(const std::string& userId);
```

## 5.3 建议限流维度

- 全局连接数
- 单 IP 建连速率
- 登录接口 QPS
- 单用户发消息速率
- 广播消息速率

## 5.4 配置项

```ini
[rate_limit]
enable = true
max_connect_per_ip_per_sec = 20
login_qps = 2000
p2p_qps = 10000
broadcast_qps = 200
user_send_per_sec = 50
```

---

## 6. 阶段四：熔断与降级

## 6.1 新增 `CircuitBreaker`

建议接口：

```cpp
class CircuitBreaker
{
public:
    bool allowRequest() const;
    void recordSuccess();
    void recordFailure();
};
```

## 6.2 熔断点建议

- MySQL 写消息失败率过高
- Redis 查询超时率过高
- 线程池排队持续过长

## 6.3 降级策略

优先级建议：

1. 保登录、心跳、ACK
2. 降级广播消息
3. 限制离线补拉批量大小
4. 暂停低优先级统计任务

## 6.4 配置项

```ini
[circuit_breaker]
enable = true
mysql_failure_threshold = 10
redis_failure_threshold = 10
half_open_after_ms = 5000
```

---

## 7. 阶段五：连接治理升级

## 7.1 修改 `HeartbeatService`

建议新增接口：

```cpp
void onAuthenticated(const std::shared_ptr<TcpConnection>& conn, const std::string& userId);
void onWriteBlocked(const std::shared_ptr<TcpConnection>& conn);
void onBackPressureRecovered(const std::shared_ptr<TcpConnection>& conn);
```

## 7.2 增加连接健康状态

建议定义：

- `NEW`
- `AUTH_PENDING`
- `ACTIVE`
- `IDLE`
- `BACK_PRESSURED`
- `CLOSING`

## 7.3 治理规则

- 未认证连接超过 `auth_timeout_seconds` 自动踢除
- 空闲连接超过 `idle_timeout_seconds` 记录并清理
- 持续写阻塞超过 `slow_consumer_kick_seconds` 可踢除

## 7.4 配置项

```ini
[connection]
auth_timeout_seconds = 10
idle_timeout_seconds = 300
slow_consumer_kick_seconds = 30
```

---

## 8. 阶段六：结构化日志与 Trace

## 8.1 新增 `TraceContext`

建议定义：

```cpp
struct TraceContext
{
    std::string traceId;
    uint64_t requestId;
    std::string sessionId;
    std::string userId;
    std::string nodeId;
};
```

## 8.2 任务清单

- 在登录请求入站处生成 `trace_id`
- 跨线程投递时透传 `TraceContext`
- 关键日志带上：
  - `trace_id`
  - `request_id`
  - `user_id`
  - `message_id`
  - `server_seq`

## 8.3 需要修改的类

- `server_full.cpp`
- `AuthService`
- `MessageService`
- `HeartbeatService`
- `OverloadProtectService`

## 8.4 日志格式建议

```text
time=... level=INFO trace_id=... user_id=... command=... message_id=... msg="..."
```

---

## 9. 阶段七：Prometheus 指标

## 9.1 新增 `MetricsRegistry`

建议接口：

```cpp
class MetricsRegistry
{
public:
    void incCounter(const std::string& name);
    void observe(const std::string& name, double value);
    void setGauge(const std::string& name, double value);
};
```

## 9.2 建议首批指标

- `im_online_connections`
- `im_login_qps`
- `im_p2p_qps`
- `im_message_retry_total`
- `im_message_ack_timeout_total`
- `im_threadpool_queue_size`
- `im_output_buffer_bytes`
- `im_heartbeat_timeout_total`
- `im_message_latency_ms`

## 9.3 指标暴露

建议新增一个轻量 `/metrics` HTTP 服务：

- 独立端口，例如 `9100`
- 单独线程运行

## 9.4 配置项

```ini
[metrics]
enable = true
bind = 0.0.0.0
port = 9100
path = /metrics
```

---

## 10. 阶段八：测试体系

## 10.1 单元测试

建议新增测试目录：

```text
tests/
  codec/
  service/
  infra/
```

建议首批测试对象：

- `Codec`
- `Buffer`
- `TokenBucketRateLimiter`
- `CircuitBreaker`
- `OverloadProtectService`

## 10.2 集成测试

建议新增场景：

1. 登录 -> 发消息 -> ACK
2. 用户离线 -> 登录后补拉
3. Redis 开启与关闭
4. 慢客户端触发高水位
5. ACK 超时触发重试

## 10.3 Benchmark

建议覆盖：

- JSON vs Protobuf
- LT vs ET
- `Buffer` 追加与回收
- 线程池任务分发吞吐

---

## 11. 阶段九：CMake 与配置收口

## 11.1 CMake 新依赖

建议在 `CMakeLists.txt` 中逐步接入：

- Protobuf
- MySQL client
- 可选 Prometheus C++ client
- GTest

## 11.2 配置总表

```ini
[cluster]
enabled = true
node_id = node-1
node_alive_ttl_seconds = 15
route_ttl_seconds = 30

[rate_limit]
enable = true
max_connect_per_ip_per_sec = 20
login_qps = 2000
p2p_qps = 10000
broadcast_qps = 200
user_send_per_sec = 50

[circuit_breaker]
enable = true
mysql_failure_threshold = 10
redis_failure_threshold = 10
half_open_after_ms = 5000

[connection]
auth_timeout_seconds = 10
idle_timeout_seconds = 300
slow_consumer_kick_seconds = 30

[metrics]
enable = true
bind = 0.0.0.0
port = 9100
path = /metrics
```

---

## 12. 本文档阶段性交付物

完成本文件对应任务后，应至少具备：

- Redis 路由表与状态中心能力
- 更细粒度限流
- 熔断与降级基础能力
- 连接治理能力
- 结构化日志与 Trace
- Prometheus 指标
- 单测 / 集成测试 / Benchmark 雏形

到这一步，项目就不仅是“能跑的 IM 服务器”，而是“具备分布式意识、稳定性意识和工程化意识的求职级工业项目”。
