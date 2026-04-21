# 这是一个我个人的学习项目

（该项目将会根据我的学习情况不断更新）

# High-Concurrency TCP Message Server

一个基于 C++17 实现的高并发 IM 服务端项目。项目以 Reactor 网络模型为底座，围绕登录认证、心跳保活、点对点消息、广播消息、可靠消息、Redis 状态中心、MySQL 持久化、限流熔断、结构化日志、Trace 和 `/metrics` 等能力，逐步从“可运行 Demo”演进为“可用于求职展示的工业化 IM 服务端骨架”。

这个项目当前最适合的定位不是“完整商业 IM 产品”，而是：

- 一个能跑通核心主链路的高并发 IM 服务端
- 一个可以系统讲清楚网络、协议、可靠性、存储、分布式、可观测的项目
- 一个适合作为后端 / 基础架构 / 高性能网络方向面试项目的工程样例

***

## 1. 项目亮点

- 基于 `Reactor + EventLoop + Channel + TcpConnection` 的网络层设计
- 自定义二进制协议头，支持 `request_id`、`client_seq`、`server_seq`、`error_code`、`checksum`
- 支持 `JSON / Protobuf` 双格式演进，缺少 Protobuf 时自动回退到 JSON
- 完整 IM 主链路：登录 -> 心跳 -> P2P -> ACK -> 离线补拉
- 可靠消息语义：`message_id`、`client_msg_id`、去重、ACK、重试、补拉、状态机
- Redis 状态中心：在线状态、路由信息、节点健康上报
- MySQL 持久化：消息表、投递表、会话表
- 稳定性治理：限流、熔断、连接治理、背压/高水位
- 可观测能力：结构化日志、Trace ID、Prometheus 风格 `/metrics`
- 工程化：单元测试、集成测试、基准测试、配置化运行

***

## 2. 当前能力边界

当前仓库已经完成 `Step_B` 里的主线里程碑：
（在此说明：Step_A是指导如何搭建一个高并发 IM 服务端的地基，Step_B是指导如何将该地基升级为一个可用的服务器项目）

- `M1`：协议与网络基础升级
- `M2`：消息可靠性闭环
- `M3`：持久化与线程边界收敛
- `M4`：分布式与稳定性增强
- `M5`：可观测与工程化闭环

当前更准确的描述是：

- 已经具备单机高并发 IM 服务端骨架
- 已经具备“有分布式意识”的 Redis 状态中心和路由能力
- 已经具备可靠消息、基础稳定性治理与可观测能力
- 跨节点真正的远程转发通道仍属于下一步可扩展方向

***

## 3. 项目结构

```text
conf/          运行配置
docs/          设计文档、升级计划、任务清单
proto/         Protobuf 协议定义
sql/           MySQL 初始化脚本
src/
  base/        配置、日志、线程池、工具类、Redis 客户端
  net/         Reactor 网络层
  codec/       Buffer、协议头、编解码、序列化
  service/     认证、心跳、消息、路由、过载保护
  storage/     MySQL 客户端、MessageStore、Repository
  infra/       RateLimiter、CircuitBreaker、MetricsRegistry、TraceContext
  http/        /metrics 暴露服务
test/          手工交互客户端、旧压测客户端
tests/         单测、集成测试、benchmark
third_party/   第三方依赖或预留目录
```

***

## 4. 核心模块说明

### 4.1 网络层

位于 `src/net/`，核心类包括：

- `EventLoop`
- `Channel`
- `TcpServer`
- `TcpConnection`

这部分负责：

- socket 监听与连接建立
- 读写事件分发
- 输入/输出缓冲管理
- 高水位、低水位、背压控制

### 4.2 协议与编解码

位于 `src/codec/`，核心文件包括：

- `protocol.h`
- `codec.h/.cpp`
- `messageSerializer.*`
- `protoCodec.*`
- `buffer.*`

这部分负责：

- 协议头定义
- 粘包拆包处理
- CRC 校验
- JSON / Protobuf 消息序列化

### 4.3 业务服务层

位于 `src/service/`，核心模块包括：

- `AuthService`：登录、会话、认证态维护
- `HeartbeatService`：心跳、超时检测、在线状态、连接治理
- `MessageService`：P2P、ACK、补拉、重试、广播
- `RoutingService`：Redis 路由绑定与查询
- `OverloadProtectService`：全局过载保护与细粒度限流

### 4.4 存储层

位于 `src/storage/`，核心模块包括：

- `mysqlClient.*`
- `messageStore.*`
- `messageRepository.*`
- `deliveryRepository.*`
- `sessionRepository.*`

这部分负责：

- 可靠消息存储
- 投递状态管理
- 会话持久化
- 离线消息补拉

### 4.5 稳定性与可观测

位于 `src/infra/` 和 `src/http/`，核心模块包括：

- `rateLimiter.*`
- `circuitBreaker.*`
- `traceContext.*`
- `metricsRegistry.*`
- `metricsServer.*`

这部分负责：

- 限流与熔断
- Trace 透传
- 结构化日志上下文
- Prometheus 风格指标导出

***

## 5. 核心业务流程

### 5.1 登录流程

`连接建立 -> LoginReq -> AuthService 校验 token -> 创建 session -> 写 Redis / MySQL -> HeartbeatService 标记在线 -> RoutingService 绑定路由 -> 返回 LoginResp`

### 5.2 可靠消息流程

`P2PMsgReq -> MessageService 生成/查询 message_id -> 写消息表/投递表 -> 在线用户直接投递，离线用户持久化待补拉 -> 客户端 ACK -> 更新 delivery 状态 -> 重连后按 server_seq 补拉`

### 5.3 连接治理流程

`新连接 -> AUTH_PENDING -> 登录成功后 ACTIVE -> 心跳超时/空闲超时/慢消费者时进入清理路径`

### 5.4 分布式状态流程

Redis 中维护的关键 key 包括：

- `session:{sessionId}`
- `user:session:{userId}`
- `user:online:{userId}`
- `route:{userId}`
- `node:alive:{nodeId}`
- `node:connections:{nodeId}`

***

## 6. 运行环境建议

### 6.1 推荐环境

推荐使用：

- Linux 原生环境
- 或 Windows + WSL2 Ubuntu

原因：

- 网络层基于 `epoll`
- Linux/WSL 下构建和运行链路更稳定
- Redis / MySQL / `ctest` / benchmark 验证路径更自然

### 6.2 Windows 说明

仓库里保留了部分 Windows 兼容代码，但当前项目主路径仍以 Linux/WSL 为主。\
如果你只是想学习、运行、测试这个项目，优先建议走 WSL。

***

## 7. 依赖说明

项目通过 CMake 拉取或查找以下依赖：

- `spdlog`
- `nlohmann/json`
- `Protobuf`（可选）
- `hiredis`（可选）
- `MySQL client`（可选）

缺少可选依赖时的行为：

- 没有 `Protobuf`：自动回退到 JSON
- 没有 `hiredis`：Redis 功能关闭
- 没有 `mysqlclient`：MySQL 持久化关闭，消息退回内存存储

***

## 8. 启动方法

### 8.1 启动 Redis 和 MySQL

项目提供了 `docker-compose.yml`，可以先拉起基础依赖：

```bash
docker compose up -d
```

它会启动：

- Redis `6379`
- MySQL `3306`

并自动挂载：

- `sql/001_m2_reliable_message.sql`

### 8.2 Linux / WSL 构建

在项目根目录执行：

```bash
cmake -S . -B build_linux
cmake --build build_linux -j4
```

生成的主要可执行文件位于：

```text
build_linux/bin/server
build_linux/bin/server_full
build_linux/bin/interactiveClient
build_linux/bin/benchmarkClient
build_linux/bin/server_benchmark_core
```

### 8.3 启动完整服务端

```bash
./build_linux/bin/server_full
```

默认配置文件路径：

```text
conf/server.ini
```

默认端口：

- TCP 服务端口：`8888`
- Metrics 端口：`9100`

***

## 9. 使用方法

### 9.1 使用交互客户端连接

```bash
./build_linux/bin/interactiveClient -h 127.0.0.1 -p 8888
```

进入客户端后可使用以下命令：

```text
login <userId> <token> [deviceId]
p2p <toUserId> <message>
broadcast <message>
kick <userId> [reason]
help
quit
```

### 9.2 登录示例

当前 `AuthService` 默认的 token 校验规则是：

```text
valid_token_<userId>
```

例如用户 `alice` 的登录命令可以写成：

```text
login alice valid_token_alice device-1
```

### 9.3 P2P 示例

用户 A 登录后，可给用户 B 发送消息：

```text
p2p bob hello bob
```

### 9.4 广播示例

```text
broadcast hello everyone
```

### 9.5 查看指标

服务启动后可以通过：

```bash
curl http://127.0.0.1:9100/metrics
```

查看 Prometheus 风格指标。

***

## 10. 配置说明

所有主要运行参数都位于：

```text
conf/server.ini
```

### 10.1 关键配置分组

#### `[server]`

- `port`：服务端监听端口
- `worker_threads`：业务线程数

#### `[thread_pool]`

- `max_queue_size`：业务线程池最大队列长度

#### `[heartbeat]`

- `timeout`：心跳超时
- `interval`：检测间隔

#### `[protocol]`

- `version`：协议版本
- `payload_format`：`json` / `protobuf`

#### `[net]`

- `epoll_mode`：`lt` / `et`
- `tcp_nodelay`
- `high_water_mark_bytes`
- `low_water_mark_bytes`
- `max_output_buffer_bytes`
- `max_packet_bytes`

#### `[redis]`

- `enabled`
- `host`
- `port`
- `timeout_ms`
- `session_expire`

#### `[cluster]`

- `enabled`
- `node_id`
- `node_alive_ttl_seconds`
- `route_ttl_seconds`

#### `[mysql]`

- `enabled`
- `host`
- `port`
- `user`
- `password`
- `database`
- `connect_timeout_ms`

#### `[reliable]`

- `retry_interval_ms`
- `ack_timeout_ms`
- `retry_backoff_ms`
- `max_retry_count`
- `retry_batch_size`

#### `[rate_limit]`

- `enable`
- `max_connect_per_ip_per_sec`
- `login_qps`
- `p2p_qps`
- `broadcast_qps`
- `user_send_per_sec`

#### `[circuit_breaker]`

- `enable`
- `thread_pool_failure_threshold`
- `half_open_after_ms`

#### `[connection]`

- `auth_timeout_seconds`
- `idle_timeout_seconds`
- `slow_consumer_kick_seconds`

#### `[metrics]`

- `enable`
- `bind`
- `port`
- `path`

***

## 11. 调试与排查方法

### 11.1 最常见的调试路径

推荐按下面顺序排查：

1. 先看 `conf/server.ini` 是否开启了对应能力
2. 再看启动日志是否提示依赖缺失或回退
3. 再看 Redis / MySQL 是否真的可连通
4. 再用交互客户端手工验证登录、心跳、P2P、广播
5. 最后看 `/metrics` 与测试结果

### 11.2 结构化日志

项目日志采用结构化格式，关键日志会带上：

- `trace_id`
- `request_id`
- `session_id`
- `user_id`
- `node_id`
- `message_id`
- `server_seq`

这让你在排查以下问题时更容易串起链路：

- 某次登录失败
- 某条消息是否持久化成功
- ACK 是否到达
- 补拉是否命中
- 线程池是否拒绝任务

### 11.3 Metrics 观测建议

当前首批指标包括：

- `im_online_connections`
- `im_login_qps`
- `im_p2p_qps`
- `im_message_retry_total`
- `im_message_ack_timeout_total`
- `im_threadpool_queue_size`
- `im_output_buffer_bytes`
- `im_heartbeat_timeout_total`
- `im_message_latency_ms`

建议重点观察：

- 连接数是否异常增长
- 线程池队列是否持续堆积
- 消息重试和 ACK 超时是否持续上升
- 输出缓冲是否长期高水位

### 11.4 配置调试建议

如果你要单独验证某个功能，建议这样开关配置：

- 验证纯内存模式：关闭 `[mysql]`、`[redis]`
- 验证可靠消息持久化：开启 `[mysql]`
- 验证分布式状态：开启 `[redis]` 和 `[cluster]`
- 验证稳定性：开启 `[rate_limit]`、`[circuit_breaker]`
- 验证可观测：开启 `[metrics]`

### 11.5 Debug 构建

Linux / WSL 下可使用：

```bash
cmake -S . -B build_debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build_debug -j4
```

如果要排查崩溃、断言、死锁或线程问题，建议优先用 Debug 构建。

***

## 12. 测试与基准

### 12.1 单元测试 / 集成测试

构建后可使用：

```bash
cd build_linux
ctest --output-on-failure
```

当前已接入的测试目标包括：

- `buffer_test`
- `rateLimiter_test`
- `circuitBreaker_test`
- `overloadProtectService_test`
- `message_flow_test`

### 12.2 核心 Benchmark

```bash
./build_linux/bin/server_benchmark_core
```

当前基准覆盖方向：

- 序列化往返
- `Codec::pack`
- `Buffer` 追加与回收
- 线程池任务分发

### 12.3 旧压测客户端

项目中还保留了旧的压测客户端：

```bash
./build_linux/bin/benchmarkClient
```

它更偏向“端到端压测脚本”，而 `server_benchmark_core` 更偏向“核心模块基准”。

***

## 13. 学习建议

如果你是第一次看这个项目，推荐按下面顺序阅读：

1. `docs/Step_B/B-升级计划总览.md`
2. `src/net/`
3. `src/codec/`
4. `src/server_full.cpp`
5. `src/service/`
6. `src/storage/`
7. `src/infra/`
8. `tests/`

最重要的阅读主线是：

`Reactor 接入层 -> 协议与编解码 -> 登录与心跳 -> 可靠消息 -> MySQL/Redis 分层 -> 限流熔断 -> Trace/Metrics`

***

## 14. 面试项目介绍

这一部分可以直接当你的项目介绍话术使用。

### 14.1 一句话版本

这是一个基于 C++17 和 Reactor 模型实现的高并发 IM 服务端项目，我在原有 TCP 通信框架基础上，补齐了可靠消息、MySQL 持久化、Redis 状态中心、限流熔断、结构化日志、Trace 和 Prometheus 指标，让它从聊天室 Demo 演进成了一个具备工业级主链路意识的 IM 服务端骨架。

### 14.2 1 分钟版本

这个项目底层是我自己实现的 Reactor 网络框架，包含 `EventLoop`、`Channel`、`TcpServer` 和 `TcpConnection`。在这个底座上，我设计了一套 IM 协议和服务层，把登录、心跳、P2P、广播串成完整主链路。后续又进一步补了可靠消息语义，包括 `message_id`、`client_msg_id`、`ACK`、去重、重试和离线补拉，并把消息和投递状态拆到了 MySQL 的消息表/投递表里。分布式上我用 Redis 做会话、在线状态和路由中心；稳定性上补了限流、熔断和连接治理；可观测性上加了结构化日志、Trace Context 和 `/metrics`。所以它不是简单的 TCP Demo，而是一个能讲清楚网络、协议、可靠性、存储和稳定性设计取舍的 IM 服务端项目。

### 14.3 面试亮点关键词

- Reactor / `epoll`
- 非阻塞 I/O
- 自定义协议头
- JSON / Protobuf 演进
- 可靠消息
- ACK / 去重 / 补拉
- MySQL + Redis 职责分层
- 限流 / 熔断 / 背压
- Trace / Metrics / Benchmark

### 14.4 面试官追问时可以展开的点

- 为什么 `server_seq` 按接收方维度维护
- 为什么消息表和投递表要拆开
- 为什么 Redis 只做状态中心，不做最终消息持久化
- 为什么先做可靠性主链路，再做极致性能优化
- 线程池和 I/O 线程如何分工
- 背压、高水位、慢消费者治理如何触发
- 限流和熔断分别保护什么

### 14.5 诚实边界

面试中也可以明确说明：

- 当前项目已经具备“求职级工业项目”的主干能力
- 但真正的跨节点远程转发通道、生产级运维体系和完整 CI/CD 仍然属于后续演进方向

这样的表达会比“我做了一个完全生产可用的 IM 系统”更可信。

***

## 15. 后续可扩展方向

- 真正的跨节点消息转发通道
- 更完整的协议兼容与版本协商
- GTest / CI 集成
- 更细粒度的压测场景
- 更完善的管理后台与监控面板
- WebSocket / 网关层支持

***

## 16. 相关文档

建议优先阅读：

- `docs/Step_B/B-升级计划总览.md`
- `docs/Step_B/C-任务清单总览.md`
- `docs/Step_B/C-P1-分布式稳定性可观测与工程化逐阶段任务清单.md`

这些文档分别回答：

- 为什么这样升级
- 每个阶段做什么
- 每块能力如何落地和验收

