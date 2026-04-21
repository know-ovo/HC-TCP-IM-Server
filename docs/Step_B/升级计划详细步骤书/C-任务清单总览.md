# Step_B 逐阶段任务清单总览

## 1. 文档定位

`B-*` 系列文档回答的是“为什么这样升级、原理是什么、权衡是什么”。  
`C-*` 系列文档回答的是“下一步具体做什么、改哪些类、补哪些接口、怎么验收”。

本文件是 `Step_B` 的执行总表，用来控制升级顺序、任务依赖和交付物。

---

## 2. 执行顺序

严格建议按下面顺序推进，不要并行乱改：

1. 协议头扩展与 Protobuf 接入
2. 网络层背压、高水位、ET/LT 开关
3. 消息可靠性主链路：`message_id` / ACK / 去重 / 补拉
4. MySQL 接入与持久化表结构
5. 线程边界收敛：I/O 线程、业务线程池、`runInLoop()`
6. Redis 状态中心升级与节点路由
7. 限流、熔断、降级
8. 可观测性：日志、Trace、Prometheus
9. 工程化：单测、集成测试、Benchmark、静态检查

原因：

- 协议不稳定，后面接口和表结构都会反复改
- 可靠性语义不定，MySQL 表很难定型
- 数据层不稳定，分布式路由与观测也没有真实依托

---

## 3. 里程碑划分

## M1：协议与网络基础升级

交付目标：

- 新协议头
- Protobuf 基础编译链
- LT/ET 配置开关
- 输出缓冲高水位与背压基础能力

对应文档：

- `C-P0-网络层与协议逐阶段任务清单.md`

## M2：消息可靠性闭环

交付目标：

- `message_id`
- `client_msg_id`
- `server_seq`
- ACK
- 去重
- 重连补拉
- 消息状态机

对应文档：

- `C-P0-消息可靠性并发与数据层逐阶段任务清单.md`

## M3：持久化与线程边界收敛

交付目标：

- MySQL schema
- DAO / Repository 初版
- Redis 职责边界收敛
- 业务线程池与 I/O 线程职责固化

对应文档：

- `C-P0-消息可靠性并发与数据层逐阶段任务清单.md`

## M4：分布式与稳定性增强

交付目标：

- Redis 路由表
- 跨节点消息转发
- 限流熔断
- 连接治理

对应文档：

- `C-P1-分布式稳定性可观测与工程化逐阶段任务清单.md`

## M5：可观测与工程化闭环

交付目标：

- 结构化日志
- Trace ID
- `/metrics`
- 单测
- 集成测试
- Benchmark

对应文档：

- `C-P1-分布式稳定性可观测与工程化逐阶段任务清单.md`

---

## 4. 每个阶段必须产出的内容

每个里程碑完成时，都至少补齐以下产物：

- 设计文档更新
- 源码实现
- 配置项说明
- 最小测试用例
- 运行/验证记录

建议统一沉淀到以下位置：

- 设计说明：`docs/Step_B/`
- 配置示例：`conf/server.ini`
- SQL：新增 `sql/`
- proto：新增 `proto/`
- 测试：新增 `tests/` 或扩展 `test/`

---

## 5. Step_B 目录建议新增结构

建议在本阶段逐步补齐以下目录：

```text
proto/
  common.proto
  auth.proto
  message.proto
  heartbeat.proto

sql/
  001_init_users.sql
  002_init_sessions.sql
  003_init_messages.sql

src/storage/
  mysqlClient.h/.cpp
  sessionRepository.h/.cpp
  messageRepository.h/.cpp
  deliveryRepository.h/.cpp

src/infra/
  metrics.h/.cpp
  traceContext.h
  rateLimiter.h/.cpp
  circuitBreaker.h/.cpp

tests/
  codec/
  service/
  integration/
```

---

## 6. 类别级任务清单索引

| 主题 | 主要改动类/文件 |
|------|-----------------|
| 网络层 | `TcpConnection` `TcpServer` `EventLoop` `Channel` `Buffer` |
| 协议层 | `protocol.h` `codec.h/.cpp` `messageSerializer.*` `proto/*` |
| 业务层 | `AuthService` `MessageService` `HeartbeatService` `OverloadProtectService` |
| 数据层 | 新增 `src/storage/*`，扩展 `RedisClient` |
| 入口层 | `server_full.cpp` `server.cpp` `conf/server.ini` `src/CMakeLists.txt` |
| 工程化 | `CMakeLists.txt` `tests/` `test/` |

---

## 7. 每阶段统一验收标准

每一个阶段都建议按下面 5 项验收：

1. 功能是否能跑通
2. 旧功能是否回归通过
3. 配置项是否可控
4. 是否有日志和最小测试支撑
5. 是否能讲清楚设计取舍

如果这 5 项里有 2 项以上做不到，就不建议进入下一阶段。
