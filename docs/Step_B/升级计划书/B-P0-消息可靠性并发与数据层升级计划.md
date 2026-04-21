# P0 升级计划（二）：消息可靠性、并发模型与数据层

## 1. 目标

这一部分是整套升级中最核心的部分。  
如果网络层和协议层解决的是“怎么收发”，那么这里解决的是：

- 消息会不会丢
- 重复消息怎么办
- 断线重连怎么办
- 数据存在哪里
- I/O 线程和业务线程如何协作

这部分做完，项目才真正接近“工业级 IM 主链路”。

---

## 2. 当前实现现状

从现有代码看：

- `MessageService` 已能维护 `userId -> TcpConnection` 映射
- 当前消息发送流程是：
  - 根据 `toUserId` 找连接
  - 生成 `msgId`
  - 直接推送给对端
- `AuthService` 已有本地上下文与 Redis Session 能力
- `HeartbeatService` 已有本地心跳和 Redis 在线状态能力
- `ThreadPool` 已可承担异步任务

但当前 IM 主链路还缺少以下关键闭环：

- 没有客户端 ACK
- 没有重试机制
- 没有服务端投递状态
- 没有离线消息持久化
- 没有断线重连后的补拉能力
- MySQL 尚未落地
- 业务线程池与 I/O 线程的分工还未收敛成严格规范

---

## 3. 消息可靠性设计

## 3.1 先定义语义：本项目采用什么可靠性目标

工业级消息可靠性必须先定语义，否则实现会乱。

建议本项目选择：

- **服务端到客户端：至少一次投递（At-Least-Once）**
- **客户端侧依赖幂等与去重，达到业务上的“近似恰好一次”**

为什么不直接追求 Exactly-Once：

- 真正的 Exactly-Once 实现复杂度极高
- 对求职项目来说收益不高
- 面试中只要你能清楚说明“为什么采用至少一次 + 幂等”，反而更显成熟

---

## 3.2 ACK 机制

### 原理

ACK 的本质是：服务端把消息投递出去后，不立即认为“消息已送达”，而是等待客户端显式确认。

建议拆成两类 ACK：

- **接收 ACK**：客户端已收到消息
- **已读 ACK**：客户端已展示给用户

求职项目阶段，优先完成第一类即可。

### 建议流程

1. 发送方发送消息到服务端
2. 服务端生成全局 `message_id`，写入持久化
3. 服务端尝试推送给目标连接
4. 客户端收到后回 `ACK(message_id, client_seq)`
5. 服务端将消息状态从 `DELIVERING` 更新为 `DELIVERED`

### 权衡

如果不做 ACK：

- 服务端不知道消息是否真正送达
- 一旦客户端中途断线，只能“假设成功”

如果做 ACK：

- 协议更复杂
- 服务端需要维护未确认消息

但对于 IM 系统，这个复杂度是必须承担的。

---

## 3.3 重试与超时控制

### 原理

ACK 机制建立后，接下来必须定义：

- 等多久算超时
- 超时后重试几次
- 重试期间是否允许重复投递

### 建议策略

- 首次投递后等待 `3s`
- 若未收到 ACK，则重投
- 最多重试 `3` 次
- 超过重试次数后转为“离线待补拉”

### 权衡

重试间隔太短：

- 会放大瞬时网络抖动

重试间隔太长：

- 用户体感延迟上升

建议第一版使用简单固定重试即可，后续可以升级为指数退避。

---

## 3.4 去重窗口

### 原理

只要采用“至少一次投递”，就必须接受消息可能重复。  
因此接收端或服务端必须具备去重能力。

### 建议设计

客户端每个会话维护最近一段时间的：

- `received_server_seq`
- 或 `recent_message_id_set`

服务端也可维护一个短期幂等窗口，用于：

- 防止客户端重试导致的重复入库
- 防止网络抖动下重复 ACK 引发状态错乱

### 实现建议

- 单聊消息以 `message_id` 作为主幂等键
- 客户端消息上行时增加 `client_msg_id`
- MySQL 中对 `(sender_id, client_msg_id)` 建唯一索引

这样即使客户端重复提交同一条消息，也不会重复创建记录。

---

## 3.5 重连补拉

### 原理

IM 最容易被问到的问题之一就是：

- 用户断网后重新连接，漏掉的消息怎么办？

解决方法不是“靠重试一直发”，而是：

- 客户端重连时带上自己最后确认的 `server_seq`
- 服务端根据 `server_seq` 查询缺失消息并补发

### 建议流程

1. 客户端登录成功后上报 `last_acked_server_seq`
2. 服务端查询该用户 `server_seq > last_acked_server_seq` 的消息
3. 按顺序补推
4. 客户端逐条 ACK
5. 服务端更新游标

### 权衡

补拉按 `message_id` 查找也可以，但按 `server_seq` 更天然符合“有序增量同步”的需求。

因此建议：

- `message_id` 负责全局唯一
- `server_seq` 负责用户维度的有序补拉

---

## 3.6 消息状态机

建议在服务端定义清晰的消息状态机：

- `CREATED`
- `PERSISTED`
- `DELIVERING`
- `DELIVERED`
- `FAILED`
- `READ`（可选二阶段）

状态流转建议：

`CREATED -> PERSISTED -> DELIVERING -> DELIVERED`

异常路径：

- 投递超时：`DELIVERING -> FAILED`
- 补拉成功：`FAILED -> DELIVERING -> DELIVERED`

好处：

- 面试时可以讲“消息生命周期”
- 代码里能避免状态更新混乱
- MySQL 设计和监控指标都能围绕状态机展开

---

## 4. 并发模型升级

## 4.1 当前模型的问题

当前工程里已有 `ThreadPool` 和 `EventLoop::runInLoop()`，这是很好的基础。  
但主线职责还不够明确。

现阶段建议明确一条硬规则：

- **I/O 线程只负责网络事件、编解码、连接状态切换**
- **业务线程池只负责鉴权、落盘、Redis/MySQL 操作、耗时计算**
- **所有真正的 socket 发送都必须切回连接所属 `EventLoop`**

### 为什么要这样做

如果业务线程直接操作连接对象发送数据，会出现：

- 线程安全边界不清晰
- 发送时序混乱
- 很难定位偶发 bug

因此必须把 `runInLoop()` 作为“跨线程回切连接所属循环”的统一机制。

---

## 4.2 推荐的数据流

推荐消息处理流程：

1. I/O 线程收到数据
2. Codec 解包并得到业务消息
3. 将业务任务投递给线程池
4. 线程池中完成：
   - 鉴权
   - 生成消息 ID
   - MySQL 落盘
   - Redis 状态更新
5. 将发送动作通过 `conn->getLoop()->runInLoop(...)` 回切
6. I/O 线程实际执行发送

这样设计的优点：

- I/O 线程足够轻
- 线程边界清晰
- 后续可方便接入熔断、监控、重试

---

## 4.3 当前线程池需要补的点

当前 `ThreadPool` 已可用，但还不够“服务端级”。

建议补充：

- 队列长度监控
- 提交失败语义
- 拒绝策略
- 线程命名
- 任务执行耗时统计

特别注意当前实现中的一个现实问题：

- `stop()` 会先 `clear()` 队列，再投递空任务唤醒线程

这意味着：

- 服务停机时可能直接丢弃尚未处理的排队任务

对于 IM 主链路而言，这个语义需要重新定义。  
建议改成可配置两种模式：

- `drain`：等待队列任务尽可能执行完
- `discard`：立即停止，仅用于快速退出

---

## 5. 数据层升级方案

## 5.1 MySQL 与 Redis 的职责边界

建议明确分层：

### MySQL 负责

- 用户信息
- 会话元数据
- 离线消息
- 消息投递状态
- ACK 状态
- 用户维度 `server_seq`

### Redis 负责

- 在线状态
- 节点路由表
- 分布式 Session
- 热点缓存
- 短期幂等窗口

这样分层的原因是：

- MySQL 擅长可靠存储与查询
- Redis 擅长高频读写、状态共享与 TTL 数据

---

## 5.2 推荐表设计

建议最少落 5 张表：

### `users`

- `id`
- `user_id`
- `password_hash`
- `status`
- `created_at`
- `updated_at`

### `user_sessions`

- `id`
- `session_id`
- `user_id`
- `device_id`
- `server_node_id`
- `last_acked_seq`
- `expires_at`
- `created_at`

### `messages`

- `id`
- `message_id`
- `sender_id`
- `receiver_id`
- `client_msg_id`
- `payload_type`
- `payload_blob`
- `status`
- `created_at`

### `message_deliveries`

- `id`
- `message_id`
- `receiver_id`
- `server_seq`
- `delivery_status`
- `retry_count`
- `last_retry_at`
- `acked_at`

### `offline_messages`

- `id`
- `receiver_id`
- `server_seq`
- `message_id`
- `expire_at`

注：

- `offline_messages` 可以理解为“待补拉索引表”
- 如果后续数据量不大，也可以先并入 `message_deliveries`

---

## 5.3 落盘流程

建议第一版采用“先落盘，再投递”的可靠路径。

### 流程

1. 收到上行消息
2. 生成 `message_id`
3. 写 `messages`
4. 写 `message_deliveries`
5. 若用户在线则投递
6. 若用户离线则保留为离线消息

### 权衡

优点：

- 崩溃恢复简单
- 语义稳定
- 面试容易讲清楚

缺点：

- 写入路径比“先投递后异步落盘”慢

但求职项目阶段，优先稳定与语义清晰，比极致吞吐更重要。

---

## 6. 推荐实施步骤

建议按以下顺序落地：

1. 定义消息状态机与 ACK 协议
2. 引入 `message_id`、`client_msg_id`、`server_seq`
3. 为客户端重连增加“补拉游标”
4. 接入 MySQL，先落地 `users/messages/message_deliveries`
5. 将 `MessageService` 从“在线直发”升级为“落盘 + 投递”
6. 将 Redis 用作在线状态与 Session 辅助层
7. 收紧线程边界：所有网络发送都走 `runInLoop()`

---

## 7. 本部分的面试价值

这部分做完后，你可以很有说服力地回答：

- IM 消息为什么会丢，怎么解决
- 为什么采用 ACK + 至少一次投递，而不是强行 Exactly-Once
- 断线重连怎么补消息
- 为什么消息需要 `message_id`、`client_msg_id`、`server_seq`
- MySQL 和 Redis 在 IM 系统里分别承担什么角色
- 为什么 I/O 线程不能直接做数据库操作

这会显著提高你的项目深度，也会让 `MessageService` 从“消息转发器”升级为真正的“消息投递服务”。
