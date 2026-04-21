# P0 执行清单（二）：消息可靠性、并发与数据层逐阶段任务清单

## 1. 目标

这一部分的目标是把当前“在线直发 + 内存状态”升级为真正的 IM 主链路：

- 先落盘
- 再投递
- 等待 ACK
- 失败可重试
- 断线可补拉

同时收紧线程边界，让 I/O 线程和业务线程池职责清晰。

---

## 2. 涉及文件总览

## 重点修改的现有文件

- `src/service/messageService.h`
- `src/service/messageService.cpp`
- `src/service/authService.h`
- `src/service/authService.cpp`
- `src/service/heartbeatService.h`
- `src/service/heartbeatService.cpp`
- `src/base/threadpool.h`
- `src/base/threadpool.cpp`
- `src/server_full.cpp`
- `src/codec/protocol.h`
- `conf/server.ini`
- `src/CMakeLists.txt`

## 建议新增的文件

- `src/storage/mysqlClient.h`
- `src/storage/mysqlClient.cpp`
- `src/storage/sessionRepository.h`
- `src/storage/sessionRepository.cpp`
- `src/storage/messageRepository.h`
- `src/storage/messageRepository.cpp`
- `src/storage/deliveryRepository.h`
- `src/storage/deliveryRepository.cpp`
- `src/service/reliableMessageService.h`
- `src/service/reliableMessageService.cpp`
- `sql/001_init_users.sql`
- `sql/002_init_sessions.sql`
- `sql/003_init_messages.sql`

如果你不想把 `ReliableMessageService` 单独拆类，也可以先把逻辑沉到 `MessageService`，但长期更建议拆分。

---

## 3. 阶段一：定义可靠消息数据模型

## 3.1 协议字段任务

在 `protocol.h` / Protobuf 中补齐：

- `request_id`
- `client_msg_id`
- `message_id`
- `client_seq`
- `server_seq`
- `ack_code`

建议新增命令：

```cpp
CmdMessageAckReq
CmdMessageAckResp
CmdPullOfflineReq
CmdPullOfflineResp
CmdMessageNotify
```

## 3.2 业务状态枚举

建议新增：

```cpp
enum class DeliveryStatus
{
    CREATED = 0,
    PERSISTED = 1,
    DELIVERING = 2,
    DELIVERED = 3,
    FAILED = 4,
    READ = 5
};
```

## 3.3 验收标准

- 协议里能表达 ACK、重试、补拉
- 服务端代码里有统一状态枚举，不再依赖字符串硬编码

---

## 4. 阶段二：MySQL 接入基础设施

## 4.1 新增 `src/storage/mysqlClient.*`

建议接口：

```cpp
class MySqlClient
{
public:
    bool connect(const std::string& host, int port,
                 const std::string& user,
                 const std::string& password,
                 const std::string& database);

    bool begin();
    bool commit();
    bool rollback();

    QueryResult query(const std::string& sql);
    bool execute(const std::string& sql);
};
```

建议技术选型：

- 第一版可使用 MySQL C API
- 如果你想兼顾可读性，也可封装轻量 `PreparedStatement` 风格接口

## 4.2 CMake 任务

在 `CMakeLists.txt` / `src/CMakeLists.txt` 中新增：

- MySQL client library 查找与链接
- `src/storage` 编译

## 4.3 配置项

```ini
[mysql]
enabled = true
host = 127.0.0.1
port = 3306
user = im_user
password = your_password
database = im_server
pool_size = 4
connect_timeout_ms = 2000
read_timeout_ms = 2000
write_timeout_ms = 2000
```

---

## 5. 阶段三：表结构落地

## 5.1 `users`

```sql
CREATE TABLE users (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    user_id VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    status TINYINT NOT NULL DEFAULT 1,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
```

## 5.2 `user_sessions`

```sql
CREATE TABLE user_sessions (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    session_id VARCHAR(128) NOT NULL UNIQUE,
    user_id VARCHAR(64) NOT NULL,
    device_id VARCHAR(128) NOT NULL,
    server_node_id VARCHAR(64) NOT NULL,
    last_acked_seq BIGINT NOT NULL DEFAULT 0,
    expires_at DATETIME NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user_device (user_id, device_id),
    INDEX idx_expires_at (expires_at)
);
```

## 5.3 `messages`

```sql
CREATE TABLE messages (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    message_id BIGINT NOT NULL UNIQUE,
    sender_id VARCHAR(64) NOT NULL,
    receiver_id VARCHAR(64) NOT NULL,
    client_msg_id VARCHAR(128) NOT NULL,
    payload_type SMALLINT NOT NULL,
    payload_blob BLOB NOT NULL,
    status TINYINT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_sender_client (sender_id, client_msg_id),
    INDEX idx_receiver_created (receiver_id, created_at)
);
```

## 5.4 `message_deliveries`

```sql
CREATE TABLE message_deliveries (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    message_id BIGINT NOT NULL,
    receiver_id VARCHAR(64) NOT NULL,
    server_seq BIGINT NOT NULL,
    delivery_status TINYINT NOT NULL,
    retry_count INT NOT NULL DEFAULT 0,
    last_retry_at DATETIME NULL,
    acked_at DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_receiver_seq (receiver_id, server_seq),
    INDEX idx_receiver_status (receiver_id, delivery_status),
    INDEX idx_message_id (message_id)
);
```

## 5.5 `offline_messages`（可选）

如果你想单独建离线索引表：

```sql
CREATE TABLE offline_messages (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    receiver_id VARCHAR(64) NOT NULL,
    server_seq BIGINT NOT NULL,
    message_id BIGINT NOT NULL,
    expire_at DATETIME NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_receiver_seq (receiver_id, server_seq)
);
```

---

## 6. 阶段四：Repository 层

## 6.1 `sessionRepository`

建议接口：

```cpp
class SessionRepository
{
public:
    bool createSession(...);
    bool deleteSession(const std::string& sessionId);
    std::optional<SessionRecord> findBySessionId(const std::string& sessionId);
    bool updateLastAckedSeq(const std::string& sessionId, uint64_t seq);
};
```

## 6.2 `messageRepository`

建议接口：

```cpp
class MessageRepository
{
public:
    bool createMessage(const MessageRecord& record);
    std::optional<MessageRecord> findByMessageId(uint64_t messageId);
    std::vector<MessageRecord> listByReceiver(const std::string& receiverId, size_t limit);
};
```

## 6.3 `deliveryRepository`

建议接口：

```cpp
class DeliveryRepository
{
public:
    bool createDelivery(const DeliveryRecord& record);
    bool markDelivering(uint64_t messageId, const std::string& receiverId);
    bool markDelivered(uint64_t messageId, const std::string& receiverId);
    bool markFailed(uint64_t messageId, const std::string& receiverId);
    std::vector<DeliveryRecord> listPendingByReceiver(const std::string& receiverId, uint64_t afterSeq, size_t limit);
    bool incrementRetryCount(uint64_t messageId, const std::string& receiverId);
};
```

## 6.4 验收标准

- 表和 Repository 的职责一一对应
- 上层 `Service` 不直接拼大量 SQL

---

## 7. 阶段五：`MessageService` 升级为可靠消息入口

## 7.1 修改 `src/service/messageService.h`

当前 `sendP2PMessage()` 只适合在线直发。  
建议升级为：

```cpp
struct SendMessageContext
{
    std::string senderId;
    std::string receiverId;
    std::string clientMsgId;
    std::string payload;
    uint64_t requestId;
};

class MessageService
{
public:
    void setThreadPool(ThreadPool* pool);
    void setMessageRepository(std::shared_ptr<MessageRepository> repo);
    void setDeliveryRepository(std::shared_ptr<DeliveryRepository> repo);

    bool submitP2PMessage(const SendMessageContext& ctx, uint64_t& messageId, std::string& errorMsg);
    bool handleAck(const std::string& userId, uint64_t messageId, uint64_t serverSeq, std::string& errorMsg);
    std::vector<MessageRecord> pullOfflineMessages(const std::string& userId, uint64_t lastAckedSeq, size_t limit);
};
```

## 7.2 修改 `src/service/messageService.cpp`

任务清单：

- 生成全局 `message_id`
- 为接收方分配单调递增 `server_seq`
- 持久化 `messages`
- 持久化 `message_deliveries`
- 若用户在线则投递
- 若用户离线则等待补拉

## 7.3 重要约束

- 业务线程池负责落盘和状态更新
- `conn->send()` 必须回切到所属 `EventLoop`

建议发送回切形式：

```cpp
conn->getLoop()->runInLoop([conn, payload]() {
    conn->send(payload.data(), payload.size());
});
```

---

## 8. 阶段六：ACK 处理链路

## 8.1 协议任务

新增 ACK 请求体：

```proto
message MessageAckReq {
  uint64 message_id = 1;
  uint64 server_seq = 2;
  uint32 ack_code = 3;
}
```

## 8.2 服务层任务

在 `MessageService` 或 `ReliableMessageService` 中新增：

```cpp
bool processAck(const std::string& userId,
                uint64_t messageId,
                uint64_t serverSeq,
                uint32_t ackCode);
```

任务清单：

- 校验 ACK 是否属于该用户
- 更新 `message_deliveries.delivery_status = DELIVERED`
- 写入 `acked_at`
- 更新 `user_sessions.last_acked_seq`

## 8.3 超时重试任务

建议新增后台重试扫描逻辑：

- 扫描 `delivery_status = DELIVERING`
- 距上次发送超过 `ack_timeout_ms`
- 且 `retry_count < max_retry_count`

可先放到 `MessageService` 定时任务里，后续再抽调度器。

## 8.4 配置项

```ini
[reliability]
ack_timeout_ms = 3000
max_retry_count = 3
offline_pull_batch_size = 100
enable_read_ack = false
```

---

## 9. 阶段七：去重与补拉

## 9.1 客户端上行去重

依赖 `(sender_id, client_msg_id)` 唯一索引。  
如果重复提交：

- 直接返回已有 `message_id`
- 不重复创建消息记录

## 9.2 断线重连补拉

在登录成功后增加：

- 客户端上报 `last_acked_seq`
- 服务端查询 `server_seq > last_acked_seq` 的未确认消息

建议在 `AuthService` 中补接口：

```cpp
void bindSessionNode(const std::string& sessionId, const std::string& nodeId);
std::optional<uint64_t> getLastAckedSeq(const std::string& sessionId);
```

建议在 `server_full.cpp` 登录成功分支中增加：

- 会话恢复逻辑
- 首次补拉逻辑

## 9.3 验收标准

- 断线重连后可以补齐未确认消息
- 重复上行不会重复入库

---

## 10. 阶段八：线程池与线程边界收敛

## 10.1 修改 `src/base/threadpool.h/.cpp`

建议新增：

```cpp
size_t queueSize() const;
void setMaxQueueSize(size_t size);
void setRejectCallback(std::function<void()> cb);
```

建议调整停止语义：

- `stop(bool drain = true)`

当 `drain=true`：

- 尽量执行完队列中已有任务再退出

## 10.2 业务线程划分规则

强制约束：

- I/O 线程：收包、解包、连接状态变更、最终发送
- 工作线程：鉴权、Redis、MySQL、可靠消息状态更新

任何新逻辑都必须标记自己属于哪一类线程。

---

## 11. 阶段九：入口整合

## 11.1 修改 `server_full.cpp`

任务清单：

- 初始化 MySQL client / repositories
- 初始化 `MessageService` 的 repository 依赖
- 初始化可靠性配置
- 注册 ACK 命令处理
- 注册离线补拉命令处理

## 11.2 配置项汇总

```ini
[mysql]
enabled = true
host = 127.0.0.1
port = 3306
user = im_user
password = your_password
database = im_server
pool_size = 4

[reliability]
ack_timeout_ms = 3000
max_retry_count = 3
offline_pull_batch_size = 100
enable_read_ack = false
message_expire_seconds = 604800
```

---

## 12. 本文档阶段性交付物

完成本文件对应任务后，应至少具备：

- MySQL schema
- Repository 层
- 可靠消息发送链路
- ACK 更新链路
- 重连补拉链路
- 线程池边界与停止语义收敛

如果这些交付物都完成，你的项目主链路就会从“在线聊天 Demo”升级成“具备可靠投递语义的 IM 服务端骨架”。
