# P0 执行清单（一）：网络层与协议逐阶段任务清单

## 1. 目标

本清单聚焦两个核心问题：

- 把当前网络层从“能跑”升级到“可治理、可扩展”
- 把当前协议层从“固定头 + JSON”升级到“可演进协议 + Protobuf 主链路”

---

## 2. 涉及文件总览

## 需要重点修改的现有文件

- `src/net/tcpConnection.h`
- `src/net/tcpConnection.cpp`
- `src/net/tcpServer.h`
- `src/net/tcpServer.cpp`
- `src/net/channel.h`
- `src/net/channel.cpp`
- `src/net/epollPoller.h`
- `src/net/epollPoller.cpp`
- `src/codec/buffer.h`
- `src/codec/buffer.cpp`
- `src/codec/protocol.h`
- `src/codec/protocol.cpp`
- `src/codec/codec.h`
- `src/codec/codec.cpp`
- `src/server_full.cpp`
- `conf/server.ini`
- `src/CMakeLists.txt`
- `CMakeLists.txt`

## 需要新增的文件

- `proto/common.proto`
- `proto/auth.proto`
- `proto/message.proto`
- `proto/heartbeat.proto`
- `src/codec/protoCodec.h`
- `src/codec/protoCodec.cpp`
- 可选：`src/net/connectionOptions.h`

---

## 3. 阶段一：协议头扩展

## 3.1 任务目标

在不立即推翻全部业务逻辑的前提下，把当前固定头扩展成后续可靠消息可用的头。

## 3.2 类与文件修改点

### 修改 `src/codec/protocol.h`

将当前头定义升级为显式结构：

```cpp
struct PacketHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t headerLen;
    uint32_t totalLen;
    uint16_t command;
    uint16_t flags;
    uint32_t requestId;
    uint64_t clientSeq;
    uint64_t serverSeq;
    uint32_t errorCode;
    uint32_t checksum;
};
```

### 新增协议常量

```cpp
constexpr uint32_t kMagic = 0xABCD1234;
constexpr uint16_t kProtocolVersionV1 = 1;
constexpr uint16_t kFlagRequest = 0x0001;
constexpr uint16_t kFlagResponse = 0x0002;
constexpr uint16_t kFlagAck = 0x0004;
constexpr uint16_t kFlagRetry = 0x0008;
```

### 新增统一错误码

建议在 `protocol.h` 中增加：

```cpp
enum ErrorCode : uint32_t
{
    ErrOk = 0,
    ErrInvalidPacket = 1001,
    ErrUnsupportedVersion = 1002,
    ErrAuthFailed = 2001,
    ErrUserOffline = 3001,
    ErrAckTimeout = 3002,
    ErrServerBusy = 5001
};
```

## 3.3 任务清单

- 重构当前头部定义，不再只靠注释描述格式
- 将头部长度由硬编码改为 `sizeof(PacketHeader)` 或显式常量
- 补充序列化/反序列化头部的辅助函数
- 将 `Codec::pack()` 改造为可接受完整头字段

## 3.4 验收标准

- 新头字段可正确编码/解码
- 旧业务命令仍可正常收发
- 错误码可被上层业务读取

---

## 4. 阶段二：Codec 接口升级

## 4.1 任务目标

把当前 `Codec` 从“只认 `command + seqId + JSON string`”升级为“能处理完整包头 + 多种 payload 编码”。

## 4.2 修改 `src/codec/codec.h`

建议新增：

```cpp
struct DecodedPacket
{
    protocol::PacketHeader header;
    std::string payload;
};

using PacketCallback = std::function<void(
    const std::shared_ptr<TcpConnection>&,
    const DecodedPacket&)>;
```

将当前：

```cpp
using MessageCallback = std::function<void(..., uint16_t, uint32_t, const std::string&)>;
```

逐步替换为更完整的 `PacketCallback`。

## 4.3 修改 `src/codec/codec.cpp`

任务清单：

- `parseHeader()` 按新头字段解析
- `parsePayload()` 同时校验 `version`、`totalLen`、`checksum`
- 将解析后的头部对象传给上层
- 增加“不支持协议版本”错误分支
- 为 ACK、请求、响应三类包增加 `flags` 分支

## 4.4 新增错误处理接口

在 `Codec` 中新增：

```cpp
void setPacketCallback(PacketCallback cb);
void setProtocolErrorCallback(ErrorCallback cb);
```

## 4.5 验收标准

- 可以区分请求、响应、ACK 包
- 协议版本错误时能返回标准错误码
- 非法长度/非法校验和时能安全拒绝连接或丢弃消息

---

## 5. 阶段三：Protobuf 接入

## 5.1 任务目标

将客户端长连接主链路从 JSON 升级到 Protobuf，但允许过渡期保留 JSON。

## 5.2 新增 proto 文件

### `proto/common.proto`

建议定义：

- `BaseRequest`
- `BaseResponse`
- `ErrorInfo`

### `proto/auth.proto`

建议定义：

- `LoginReq`
- `LoginResp`
- `ValidateSessionReq`
- `ValidateSessionResp`

### `proto/message.proto`

建议定义：

- `P2PMessageReq`
- `P2PMessageResp`
- `MessageNotify`
- `MessageAckReq`
- `PullOfflineMessagesReq`
- `PullOfflineMessagesResp`

### `proto/heartbeat.proto`

建议定义：

- `HeartbeatReq`
- `HeartbeatResp`

## 5.3 CMake 任务

修改 `CMakeLists.txt` / `src/CMakeLists.txt`：

- `find_package(Protobuf REQUIRED)`
- 生成 `.pb.cc/.pb.h`
- 将生成代码加入 `server_lib`

## 5.4 新增适配层

新增：

- `src/codec/protoCodec.h`
- `src/codec/protoCodec.cpp`

职责：

- 根据 `command` 映射 Protobuf 消息类型
- 提供 `serializeProto(command, message)` / `parseProto(command, payload)` 接口

## 5.5 过渡期兼容策略

配置项建议新增：

```ini
[protocol]
payload_format = protobuf    # protobuf/json
compatible_json = true
version = 1
```

实现要求：

- 开发阶段允许 `payload_format=json`
- 主链路测试完成后切到 `protobuf`

## 5.6 验收标准

- 登录、心跳、P2P 三条链路可走 Protobuf
- 能通过配置切换 JSON / Protobuf
- 抓包可验证头字段和 payload 正常

---

## 6. 阶段四：TcpConnection 背压与高水位

## 6.1 任务目标

为 `TcpConnection` 增加“可治理”的发送能力，避免慢客户端拖垮服务端。

## 6.2 修改 `src/net/tcpConnection.h`

建议新增回调类型：

```cpp
using HighWaterMarkCallback =
    std::function<void(const std::shared_ptr<TcpConnection>&, size_t)>;

using ConnectionStateCallback =
    std::function<void(const std::shared_ptr<TcpConnection>&, int)>;
```

建议新增成员：

```cpp
size_t m_highWaterMark;
size_t m_lowWaterMark;
size_t m_maxOutputBufferBytes;
bool m_backPressured;
HighWaterMarkCallback m_highWaterMarkCallback;
```

建议新增接口：

```cpp
void setHighWaterMark(size_t bytes);
void setLowWaterMark(size_t bytes);
void setMaxOutputBufferBytes(size_t bytes);
size_t outputBufferBytes() const;
bool isBackPressured() const;
```

## 6.3 修改 `src/net/tcpConnection.cpp`

任务清单：

- `sendInLoop()` 中统计输出缓冲大小
- 到达高水位时触发 `highWaterMarkCallback`
- 超过最大输出缓冲时执行保护策略
- 在写回调中缓冲回落后清理 `m_backPressured`

保护策略建议：

- 普通消息直接拒绝入队
- 控制消息允许继续发送
- 极端情况下断开慢连接

## 6.4 配置项

```ini
[net]
tcp_nodelay = true
epoll_mode = et
high_water_mark_bytes = 1048576
low_water_mark_bytes = 262144
max_output_buffer_bytes = 4194304
max_packet_bytes = 1048576
```

## 6.5 验收标准

- 慢客户端压测时输出缓冲不会无上限增长
- 日志能记录高水位事件
- 正常连接不受影响

---

## 7. 阶段五：LT/ET 模式开关

## 7.1 任务目标

保留 LT 作为稳定兜底路径，同时重点打磨 ET 模式。

## 7.2 修改点

### `src/net/channel.h`

建议新增事件模式枚举：

```cpp
enum class TriggerMode
{
    LT,
    ET
};
```

### `src/net/epollPoller.cpp`

任务清单：

- 根据配置决定是否附加 `EPOLLET`
- 输出启动日志，明确当前使用 LT/ET

### `src/net/tcpConnection.cpp`

任务清单：

- ET 模式下读到 `EAGAIN` 为止
- ET 模式下写到 `EAGAIN` 或输出缓冲为空为止

## 7.3 配置项

```ini
[net]
epoll_mode = et
read_spin_max = 64
write_spin_max = 64
```

## 7.4 验收标准

- LT / ET 均能通过最小回归测试
- ET 模式下无漏读、漏写、死循环

---

## 8. 阶段六：入口层与配置收口

## 8.1 修改 `server_full.cpp`

任务清单：

- 从 `Config` 读取新的 `[protocol]`、`[net]` 配置
- 初始化 `Codec` 时设置版本、payload 格式
- 为每个新连接设置高水位、最大包长等参数
- 注册高水位回调和协议错误回调

## 8.2 修改 `conf/server.ini`

至少新增：

```ini
[protocol]
version = 1
payload_format = protobuf
compatible_json = true

[net]
epoll_mode = et
tcp_nodelay = true
high_water_mark_bytes = 1048576
low_water_mark_bytes = 262144
max_output_buffer_bytes = 4194304
max_packet_bytes = 1048576
read_spin_max = 64
write_spin_max = 64
```

---

## 9. 本文档阶段性交付物

完成本文件对应任务后，应至少产出：

- 新协议头定义
- Protobuf proto 文件
- 高水位/背压基础能力
- LT/ET 模式切换能力
- 配置项说明
- 三条基础链路回归测试记录

如果这些交付物未齐，不建议进入 ACK / MySQL 阶段。
