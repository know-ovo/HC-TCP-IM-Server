# 阶段三：服务器消息通信工具详解

## 目录

- [1. 概述](#1-概述)
- [2. 整体架构设计](#2-整体架构设计)
- [3. 协议层详解](#3-协议层详解)
- [4. 编解码层详解](#4-编解码层详解)
- [5. 序列化层详解](#5-序列化层详解)
- [6. 数据流完整流程](#6-数据流完整流程)
- [7. 模块间协作关系](#7-模块间协作关系)
- [8. 设计模式与最佳实践](#8-设计模式与最佳实践)
- [9. 扩展指南](#9-扩展指南)

---

## 1. 概述

`src/codec` 模块是服务器消息通信的核心组件，负责实现**应用层协议**的编解码功能。该模块采用分层设计，将协议定义、编解码逻辑、消息序列化三个关注点分离，实现了高内聚、低耦合的架构。

### 1.1 模块职责

| 模块                        | 文件                    | 职责                                         |
| --------------------------- | ----------------------- | -------------------------------------------- |
| **Protocol**          | Protocol.h/cpp          | 定义协议常量、命令类型、消息结构体、校验算法 |
| **Codec**             | Codec.h/cpp             | 实现协议帧的打包与解析，处理网络字节序转换   |
| **MessageSerializer** | MessageSerializer.h/cpp | 实现消息结构体与字节流之间的序列化/反序列化  |

### 1.2 设计目标

1. **协议标准化**：定义清晰的二进制协议格式，支持跨语言、跨平台通信
2. **高效编解码**：采用状态机解析，支持 TCP 粘包/拆包处理
3. **数据完整性**：使用 CRC16 校验确保消息传输的正确性
4. **易于扩展**：新增消息类型只需添加结构体和序列化函数

### 1.3 文件结构

```
src/codec/
├── Protocol.h           # 协议定义头文件
├── Protocol.cpp         # CRC16 校验算法实现
├── Codec.h              # 编解码器头文件
├── Codec.cpp            # 编解码器实现
├── MessageSerializer.h  # 消息序列化器头文件
└── MessageSerializer.cpp# 消息序列化器实现
```

---

## 2. 整体架构设计

### 2.1 分层架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              应用层 (Application)                           │
│                     业务逻辑处理、消息分发、回调处理                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            序列化层 (Serializer)                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                     MessageSerializer                               │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │    │
│  │  │   Serialize  │  │  Deserialize │  │    JSON库    │               │    │
│  │  │ 结构体→JSON  │  │  JSON→结构体 │  │  nlohmann    │               │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                            编解码层 (Codec)                                │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                          Codec                                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │     pack     │  │   onMessage  │  │  状态机解析  │               │   │
│  │  │  打包发送    │  │  解析接收    │  │Header/Payload│               │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │   │
│  │                                                                     │   │
│  │  ┌──────────────────────────────────────────────────────────────┐   │   │
│  │  │                    协议帧格式处理                            │   │   │
│  │  │  [TotalLen][Cmd][SeqId][Checksum][BodyLen][Body...]          │   │   │
│  │  └──────────────────────────────────────────────────────────────┘   │   │
│  └───────────────────────────────────────────────────────────────────-─┘   │
└────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                            协议层 (Protocol)                               │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                          Protocol                                   │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │   Command    │  │   Message    │  │   CalcCRC16  │               │   │
│  │  │  命令枚举    │  │  结构体定义  │  │   校验算法   │               │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            网络层 (Network)                                 │
│                    TcpConnection、Buffer、EventLoop                         │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 数据流向

```
发送流程（应用层 → 网络层）:
┌──────────┐    ┌──────────────┐    ┌──────────┐    ┌──────────┐
│ 业务数据 │ -> │ Serialize()  │ -> │  pack()  │ -> │  Buffer  │ -> 网络发送
│ (结构体) │    │ 结构体→JSON  │    │ 加协议头 │    │ (字节流) │
└──────────┘    └──────────────┘    └──────────┘    └──────────┘

接收流程（网络层 → 应用层）:
┌───────────┐    ┌───────────┐    ┌──────────────┐    ┌──────────┐
│  Buffer   │ -> │ onMessage │ -> │ Deserialize()│ -> │ 业务数据 │ -> 业务处理
│ (字节流)  │    │ 解析协议  │    │ JSON→结构体  │    │ (结构体) │
└───────────┘    └───────────┘    └──────────────┘    └──────────┘
```

---

## 3. 协议层详解

### 3.1 协议常量定义

```cpp
namespace protocol
{

constexpr uint32_t kHeaderSize = 16;           // 协议头固定 16 字节
constexpr uint32_t kMaxMessageSize = 64 * 1024 * 1024;  // 最大消息 64MB

}
```

**设计说明：**

| 常量                | 值   | 说明                                                         |
| ------------------- | ---- | ------------------------------------------------------------ |
| `kHeaderSize`     | 16   | 协议头固定长度，包含总长度、命令、序列号、校验和、消息体长度 |
| `kMaxMessageSize` | 64MB | 限制单条消息最大尺寸，防止恶意大包攻击导致内存耗尽           |

### 3.2 命令类型枚举

```cpp
enum Command : uint16_t
{
    CmdLoginReq = 0x0001,          // 登录请求
    CmdLoginResp = 0x0002,         // 登录响应
    CmdHeartbeatReq = 0x0003,      // 心跳请求
    CmdHeartbeatResp = 0x0004,     // 心跳响应
    CmdP2pMsgReq = 0x0005,         // 点对点消息请求
    CmdP2pMsgResp = 0x0006,        // 点对点消息响应
    CmdBroadcastMsgReq = 0x0007,   // 广播消息请求
    CmdBroadcastMsgNotify = 0x0008,// 广播消息通知
    CmdKickUserReq = 0x0009,       // 踢人请求
    CmdKickUserResp = 0x000A,      // 踢人响应
};
```

**命令分类规则：**

```
命令编号规则：
┌────────────────────────────────────────────────────┐
│  0x0001 - 0x000F : 用户认证相关                    │
│  0x0010 - 0x001F : 心跳保活相关                    │
│  0x0020 - 0x002F : 消息传输相关                    │
│  0x0030 - 0x003F : 系统管理相关                    │
│  0x0040 - 0x00FF : 预留扩展                        │
└────────────────────────────────────────────────────┘

请求/响应配对规则：
- 请求命令：奇数 (0x0001, 0x0003, 0x0005...)
- 响应命令：偶数 (0x0002, 0x0004, 0x0006...)
- 响应命令 = 请求命令 + 1
```

### 3.3 消息结构体定义

#### 3.3.1 登录消息

```cpp
// 登录请求
struct LoginReq
{
    std::string userId;      // 用户ID
    std::string token;       // 认证令牌
    std::string deviceId;    // 设备ID（可选）
};

// 登录响应
struct LoginResp
{
    uint32_t resultCode;     // 结果码：0=成功，其他=失败
    std::string resultMsg;   // 结果描述
    std::string sessionId;   // 会话ID（成功时返回）
};
```

**结果码定义建议：**

| 结果码 | 含义         |
| ------ | ------------ |
| 0      | 成功         |
| 1001   | 用户不存在   |
| 1002   | 密码错误     |
| 1003   | Token 过期   |
| 1004   | 账号被封禁   |
| 1005   | 设备数量超限 |

#### 3.3.2 心跳消息

```cpp
// 心跳请求（空消息）
struct HeartbeatReq
{
};

// 心跳响应
struct HeartbeatResp
{
    int64_t serverTime;      // 服务器时间戳（毫秒）
};
```

**心跳机制说明：**

```
客户端                              服务器
   │                                 │
   │──── HeartbeatReq (每30秒) ────> │
   │                                 │
   │<─── HeartbeatResp (serverTime) ─│
   │                                 │
   │     超过90秒无响应则断开连接    │
```

#### 3.3.3 点对点消息

```cpp
// 点对点消息请求
struct P2PMsgReq
{
    std::string fromUserId;  // 发送者ID
    std::string toUserId;    // 接收者ID
    std::string content;     // 消息内容
};

// 点对点消息响应
struct P2PMsgResp
{
    uint32_t resultCode;     // 结果码
    std::string resultMsg;   // 结果描述
    uint32_t msgId;          // 消息ID（用于消息确认）
};
```

#### 3.3.4 广播消息

```cpp
// 广播消息请求
struct BroadcastMsgReq
{
    std::string fromUserId;  // 发送者ID
    std::string content;     // 消息内容
};

// 广播消息通知（推送给所有在线用户）
struct BroadcastMsgNotify
{
    std::string fromUserId;  // 发送者ID
    std::string content;     // 消息内容
    int64_t timestamp;       // 广播时间戳
};
```

#### 3.3.5 踢人消息

```cpp
// 踢人请求（管理员操作）
struct KickUserReq
{
    std::string targetUserId;  // 目标用户ID
    std::string reason;        // 踢人原因
};

// 踢人响应
struct KickUserResp
{
    uint32_t resultCode;     // 结果码
    std::string resultMsg;   // 结果描述
};
```

### 3.4 CRC16 校验算法

#### 3.4.1 算法原理

CRC（Cyclic Redundancy Check，循环冗余校验）是一种根据网络数据包或计算机文件等数据产生简短固定位数校验码的一种信道编码技术。

```
CRC16-CCITT 标准：
- 多项式：x^16 + x^12 + x^5 + 1 (0x1021)
- 初始值：0xFFFF
- 输入反转：否
- 输出反转：否
- 异或输出：0x0000
```

#### 3.4.2 代码实现

```cpp
uint16_t CalcCRC16(const void* data, size_t len)
{
    const uint8_t* buf = static_cast<const uint8_t*>(data);
    uint16_t crc = 0xFFFF;              // 初始值
    const uint16_t polynomial = 0x1021; // CRC-CCITT 多项式

    for (size_t i = 0; i < len; ++i)
    {
        // 将当前字节与 CRC 高 8 位异或
        crc ^= static_cast<uint16_t>(buf[i]) << 8;
    
        // 处理 8 位
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x8000)  // 检查最高位
            {
                // 最高位为 1：左移并异或多项式
                crc = (crc << 1) ^ polynomial;
            }
            else
            {
                // 最高位为 0：仅左移
                crc <<= 1;
            }
        }
    }

    return crc;
}
```

#### 3.4.3 计算过程示例

```
假设数据：0x31 0x32 0x33 ("123")

步骤1: 初始化 crc = 0xFFFF

步骤2: 处理第一个字节 0x31
  crc = 0xFFFF ^ (0x31 << 8) = 0xFFFF ^ 0x3100 = 0xCEFF
  处理 8 位后：crc = 0x2672

步骤3: 处理第二个字节 0x32
  crc = 0x2672 ^ (0x32 << 8) = 0x2672 ^ 0x3200 = 0x1472
  处理 8 位后：crc = 0x0D2A

步骤4: 处理第三个字节 0x33
  crc = 0x0D2A ^ (0x33 << 8) = 0x0D2A ^ 0x3300 = 0x3E2A
  处理 8 位后：crc = 0x5BEC

最终结果：0x5BEC
```

---

## 4. 编解码层详解

### 4.1 协议帧格式设计

#### 4.1.1 帧结构

```
┌───────────────────────────────────────────────────────────────────────┐
│                           协议帧结构 (16 + N 字节)                    │
├────────────┬─────────┬─────────┬──────────┬─────────┬─────────────────┤
│   字段     │ 长度    │ 偏移    │ 类型     │ 说明    │                 │
├────────────┼─────────┼─────────┼──────────┼─────────┼─────────────────┤
│ TotalLen   │ 4 字节  │ 0       │ uint32_t │ 总长度  │ (头+体)         │
│ Command    │ 2 字节  │ 4       │ uint16_t │命令类型 │                 │
│ SeqId      │ 4 字节  │ 6       │ uint32_t │ 序列号  │ 请求响应配对    │
│ Checksum   │ 2 字节  │ 10      │ uint16_t │ CRC16   │ 校验和          │
│ BodyLen    │ 4 字节  │ 12      │ uint32_t │ 消息体长│                 │
├────────────┴────────-┴─────────┴──────────┴─────────┴─────────────────┤
│                           消息体 (N 字节)                             │
│                        JSON 格式的业务数据                            │
└───────────────────────────────────────────────────────────────────────┘

TotalLen = 16 + BodyLen
```

#### 4.1.2 字段详解

| 字段               | 大小   | 网络序     | 说明                                     |
| ------------------ | ------ | ---------- | ---------------------------------------- |
| **TotalLen** | 4 字节 | Big-Endian | 整个协议帧的长度，用于解析时确定消息边界 |
| **Command**  | 2 字节 | Big-Endian | 命令类型，标识消息的业务类型             |
| **SeqId**    | 4 字节 | Big-Endian | 序列号，用于请求-响应配对，递增生成      |
| **Checksum** | 2 字节 | Big-Endian | CRC16 校验和，校验整个协议帧的数据完整性 |
| **BodyLen**  | 4 字节 | Big-Endian | 消息体长度，方便直接定位消息体           |
| **Body**     | N 字节 | -          | JSON 格式的业务数据                      |

#### 4.1.3 设计考量

**为什么需要 TotalLen 和 BodyLen 两个字段？**

```
1. TotalLen 用于 TCP 粘包处理：
   - TCP 是字节流协议，没有消息边界
   - 通过 TotalLen 可以精确知道一条完整消息的长度
   - 接收方可以准确分割消息

2. BodyLen 用于快速定位消息体：
   - 避免每次都要计算 BodyLen = TotalLen - 16
   - 方便直接跳过协议头访问消息体

3. 冗余设计的权衡：
   - 4 字节的空间开销换取解析便利性
   - 两个字段可以互相校验：TotalLen == 16 + BodyLen
```

### 4.2 Codec 类设计

#### 4.2.1 类定义

```cpp
class Codec : NonCopyable
{
public:
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                                uint16_t command,
                                                uint32_t seqId,
                                                const std::string& message)>;
    using ErrorCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                              int errorCode)>;

    Codec();
    ~Codec() = default;

    void setMessageCallback(MessageCallback cb) { m_messageCallback = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { m_errorCallback = std::move(cb); }

    static void pack(Buffer* buffer,
                     uint16_t command,
                     uint32_t seqId,
                     const std::string& message);

    void onMessage(const std::shared_ptr<TcpConnection>& conn,
                   Buffer* buffer,
                   Timestamp receiveTime);

private:
    enum ParseState
    {
        kExpectHeader,   // 期望接收协议头
        kExpectPayload,  // 期望接收消息体
    };

    ParseState m_state;           // 当前解析状态
    uint32_t m_expectedLength;    // 期望读取的数据长度

    MessageCallback m_messageCallback;  // 消息解析完成回调
    ErrorCallback m_errorCallback;      // 错误处理回调

    bool parseHeader(Buffer* buffer);
    bool parsePayload(const std::shared_ptr<TcpConnection>& conn, Buffer* buffer);
};
```

#### 4.2.2 成员变量说明

| 变量                  | 类型            | 初始值            | 作用                                   |
| --------------------- | --------------- | ----------------- | -------------------------------------- |
| `m_state`           | ParseState      | `kExpectHeader` | 状态机状态，控制解析流程               |
| `m_expectedLength`  | uint32_t        | `16`            | 期望读取的字节数，用于判断数据是否完整 |
| `m_messageCallback` | MessageCallback | nullptr           | 消息解析完成后的回调函数               |
| `m_errorCallback`   | ErrorCallback   | nullptr           | 解析出错时的回调函数                   |

#### 4.2.3 回调函数类型

```cpp
// 消息回调：成功解析一条完整消息后触发
using MessageCallback = std::function<void(
    const std::shared_ptr<TcpConnection>&,  // 连接对象
    uint16_t command,                        // 命令类型
    uint32_t seqId,                          // 序列号
    const std::string& message               // 消息体(JSON)
)>;

// 错误回调：解析出错时触发
using ErrorCallback = std::function<void(
    const std::shared_ptr<TcpConnection>&,  // 连接对象
    int errorCode                            // 错误码
)>;
```

### 4.3 消息打包流程

#### 4.3.1 打包函数

```cpp
static void pack(Buffer* buffer,
                 uint16_t command,
                 uint32_t seqId,
                 const std::string& message);
```

#### 4.3.2 打包流程图

```
┌─────────────────────────────────────────────────────────────────┐
│                         pack() 打包流程                         │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │ 计算 bodyLen, totalLen │
                    │ bodyLen = message.size │
                    │ totalLen = 16 + bodyLen│
                    └───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │  确保 Buffer 可写空间  │
                    │ EnsureWritableBytes(16)│
                    └───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │    写入协议头字段      │
                    │ (主机序 → 网络序)      │
                    └───────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
        ▼                       ▼                       ▼
┌───────────────┐     ┌───────────────┐     ┌───────────────┐
│ TotalLen(4B)  │     │ Command(2B)   │     │ SeqId(4B)     │
│ HostToNetwork │     │ HostToNetwork │     │ HostToNetwork │
│ offset: 0     │     │ offset: 4     │     │ offset: 6     │
└───────────────┘     └───────────────┘     └───────────────┘

        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
        ▼                       ▼                       ▼
┌───────────────┐     ┌───────────────┐     ┌───────────────┐
│ Checksum(2B)  │     │ BodyLen(4B)   │     │ Body(N B)     │
│ 暂填 0        │     │ HostToNetwork │     │ Append        │
│ offset: 10    │     │ offset: 12    │     │               │
└───────────────┘     └───────────────┘     └───────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │   计算 CRC16 校验和    │
                    │ CalcCRC16(整个帧)      │
                    └───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │  回填 Checksum 字段    │
                    │ offset: 10, 2 字节     │
                    └───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │       打包完成         │
                    │  Buffer 可直接发送     │
                    └───────────────────────┘
```

#### 4.3.3 代码详解

```cpp
void Codec::pack(Buffer* buffer,
                 uint16_t command,
                 uint32_t seqId,
                 const std::string& message)
{
    // 1. 计算长度
    uint32_t bodyLen = static_cast<uint32_t>(message.size());
    uint32_t totalLen = kHeaderLen + bodyLen;

    // 2. 确保缓冲区有足够空间
    buffer->EnsureWritableBytes(kHeaderLen);
    char* header = buffer->BeginWrite();

    // 3. 写入 TotalLen (offset: 0)
    uint32_t netTotalLen = util::HostToNetwork32(totalLen);
    memcpy(header + kLenFieldOffset, &netTotalLen, sizeof(netTotalLen));

    // 4. 写入 Command (offset: 4)
    uint16_t netCmd = util::HostToNetwork16(command);
    memcpy(header + kCmdFieldOffset, &netCmd, sizeof(netCmd));

    // 5. 写入 SeqId (offset: 6)
    uint32_t netSeq = util::HostToNetwork32(seqId);
    memcpy(header + kSeqFieldOffset, &netSeq, sizeof(netSeq));

    // 6. 写入 Checksum 占位 (offset: 10)，稍后回填
    uint16_t checksum = 0;
    memcpy(header + kChecksumFieldOffset, &checksum, sizeof(checksum));

    // 7. 写入 BodyLen (offset: 12)
    uint32_t netBodyLen = util::HostToNetwork32(bodyLen);
    memcpy(header + kBodyLenFieldOffset, &netBodyLen, sizeof(netBodyLen));

    // 8. 更新写指针
    buffer->HasWritten(kHeaderLen);
  
    // 9. 追加消息体
    buffer->Append(message.data(), message.size());

    // 10. 计算并回填 CRC16 校验和
    checksum = protocol::CalcCRC16(buffer->Peek(), totalLen);
    uint16_t netChecksum = util::HostToNetwork16(checksum);
    memcpy(const_cast<char*>(buffer->Peek()) + kChecksumFieldOffset, 
           &netChecksum, sizeof(netChecksum));
}
```

### 4.4 消息解析流程

#### 4.4.1 解析入口函数

```cpp
void onMessage(const std::shared_ptr<TcpConnection>& conn,
               Buffer* buffer,
               Timestamp receiveTime);
```

#### 4.4.2 解析流程图

```
┌──────────────────────────────────────────────────────────--─────┐
│                    onMessage() 解析入口                         │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
                    ┌───────────────────-────┐
                    │     while (true)       │
                    └─────────────────────-──┘
                                │
                                ▼
                    ┌──────────────────────-----─┐
                    │  m_state == kExpectHeader? │
                    └───────────────────────-----┘
                      │                   │
                     Yes                  No
                      │                   │
                      ▼                   ▼
            ┌─────────────────┐   ┌───────────────-------──┐
            │ 数据 >= 16 字节? │   │m_state==kExpectPayload?│
            └─────────────────┘   └────────────────-------─┘
              │           │         │           │
             Yes          No       Yes          No
              │           │         │           │
              ▼           │         ▼            │
    ┌─────────────────┐   │ ┌────────────────-─┐ │
    │  parseHeader()  │   │ │数据>=expectedLen?│ │
    └─────────────────┘   │ └────────────────-─┘ │
              │           │   │           │     │
         ┌────┴────┐      │  Yes          No    │
       true      false    │   │           │     │
         │         │      │   ▼           │     │
         │         └──────┼─│ parsePayload()│ │
         │                │ └─────────────────┘ │
         │                │   │           │     │
         │                │┌──┴──┐     ┌──┴──┐  │
         │                ││true │     │false│  │
         │                │└──┬──┘     └──┬──┘  │
         └────────────────┴───┴────────────┴────┘
                                │
                                ▼
                         ┌─────────────┐
                         │   break     │
                         │  等待更多数据│
                         └─────────────┘
```

#### 4.4.3 parseHeader 详解

```cpp
bool Codec::parseHeader(Buffer* buffer)
{
    const char* data = buffer->Peek();

    // 1. 读取 TotalLen (网络序 → 主机序)
    uint32_t netTotalLen;
    memcpy(&netTotalLen, data + kLenFieldOffset, sizeof(netTotalLen));
    uint32_t totalLen = util::NetworkToHost32(netTotalLen);

    // 2. 合法性检查
    if (totalLen < kHeaderLen || totalLen > protocol::kMaxMessageSize)
    {
        LOG_ERROR("Codec::parseHeader invalid totalLen: %u", totalLen);
        buffer->RetrieveAll();      // 清空缓冲区
        m_state = kExpectHeader;    // 重置状态
        m_expectedLength = kHeaderLen;
        return false;
    }

    // 3. 更新状态
    m_expectedLength = totalLen;    // 设置期望读取的完整消息长度
    m_state = kExpectPayload;       // 切换到等待消息体状态
    return true;
}
```

**合法性检查的意义：**

```
1. totalLen < kHeaderLen
   - 协议错误：消息长度不可能小于协议头
   - 可能原因：数据损坏、恶意攻击

2. totalLen > kMaxMessageSize (64MB)
   - 防止内存耗尽攻击
   - 正常业务消息不会超过此限制
```

#### 4.4.4 parsePayload 详解

```cpp
bool Codec::parsePayload(const std::shared_ptr<TcpConnection>& conn, Buffer* buffer)
{
    const char* data = buffer->Peek();

    // 1. 校验 CRC16
    uint16_t netChecksum;
    memcpy(&netChecksum, data + kChecksumFieldOffset, sizeof(netChecksum));
    uint16_t receivedChecksum = util::NetworkToHost16(netChecksum);
    uint16_t calcChecksum = protocol::CalcCRC16(data, m_expectedLength);

    if (receivedChecksum != calcChecksum)
    {
        LOG_ERROR("Codec::parsePayload checksum mismatch");
        buffer->RetrieveAll();
        m_state = kExpectHeader;
        m_expectedLength = kHeaderLen;
        return false;
    }

    // 2. 解析各字段
    uint16_t netCmd;
    memcpy(&netCmd, data + kCmdFieldOffset, sizeof(netCmd));
    uint16_t command = util::NetworkToHost16(netCmd);

    uint32_t netSeq;
    memcpy(&netSeq, data + kSeqFieldOffset, sizeof(netSeq));
    uint32_t seqId = util::NetworkToHost32(netSeq);

    uint32_t netBodyLen;
    memcpy(&netBodyLen, data + kBodyLenFieldOffset, sizeof(netBodyLen));
    uint32_t bodyLen = util::NetworkToHost32(netBodyLen);

    // 3. 提取消息体
    std::string message(data + kHeaderLen, bodyLen);

    // 4. 消费已解析的数据
    buffer->Retrieve(m_expectedLength);

    // 5. 重置状态机
    m_state = kExpectHeader;
    m_expectedLength = kHeaderLen;

    // 6. 回调上层处理
    if (m_messageCallback)
    {
        m_messageCallback(conn, command, seqId, message);
    }

    return true;
}
```

### 4.5 状态机解析机制

#### 4.5.1 状态转换图

```
                    ┌─────────────────────────────────────┐
                    │                                     │
                    ▼                                     │
            ┌───────────────┐                             │
            │ kExpectHeader │                             │
            │  期望协议头   │                             │
            └───────────────┘                             │
                    │                                     │
                    │ 数据 >= 16 字节                     │
                    │ parseHeader() 成功                  │
                    ▼                                     │
            ┌───────────────┐                             │
            │ kExpectPayload│                             │
            │  期望消息体   │                             │
            └───────────────┘                             │
                    │                                     │
                    │ 数据 >= m_expectedLength            │
                    │ parsePayload() 成功                 │
                    └─────────────────────────────────────┘
                    │
                    │ parsePayload() 失败 (校验错误)
                    ▼
            ┌───────────────┐
            │ kExpectHeader │
            │   重置状态    │
            └───────────────┘
```

#### 4.5.2 TCP 粘包/拆包处理

**问题场景：**

```
场景1: 粘包（多条消息合并）
┌────────────────────────────────────────────────────────────┐
│  消息1完整帧  │  消息2完整帧  │  消息3部分帧  │
└────────────────────────────────────────────────────────────┘
                    TCP 接收缓冲区

场景2: 拆包（一条消息分多次到达）
┌────────────────────────────────────────────────────────────┐
│  消息1部分帧  │  ...等待...  │  消息1剩余部分  │
└────────────────────────────────────────────────────────────┘
              第一次到达          第二次到达
```

**解决方案：**

```cpp
void Codec::onMessage(...)
{
    while (true)  // 循环处理，解决粘包
    {
        if (m_state == kExpectHeader)
        {
            if (buffer->ReadableBytes() < kHeaderLen)
            {
                break;  // 数据不足，等待更多数据（解决拆包）
            }
            parseHeader(buffer);
        }

        if (m_state == kExpectPayload)
        {
            if (buffer->ReadableBytes() < m_expectedLength)
            {
                break;  // 数据不足，等待更多数据（解决拆包）
            }
            parsePayload(conn, buffer);
        }
    }
}
```

**处理流程示例：**

```
假设收到数据：[消息1完整帧][消息2完整帧][消息3半帧]

第1次循环:
  - state = kExpectHeader
  - buffer 有足够数据
  - parseHeader() 成功，state = kExpectPayload
  - buffer 有足够数据
  - parsePayload() 成功，回调上层，state = kExpectHeader
  - 消息1处理完成

第2次循环:
  - state = kExpectHeader
  - buffer 有足够数据
  - parseHeader() 成功，state = kExpectPayload
  - buffer 有足够数据
  - parsePayload() 成功，回调上层，state = kExpectHeader
  - 消息2处理完成

第3次循环:
  - state = kExpectHeader
  - buffer 数据不足（只有半帧）
  - break，退出循环
  - 等待下次 onMessage 继续处理
```

---

## 5. 序列化层详解

### 5.1 序列化器设计

#### 5.1.1 设计理念

```
┌─────────────────────────────────────────────────────────────────┐
│                        序列化层职责                             │
├─────────────────────────────────────────────────────────────────┤
│ 1. 将业务结构体转换为可传输的字节流（序列化）                    │
│ 2. 将字节流还原为业务结构体（反序列化）                         │
│ 3. 提供类型安全的序列化接口                                     │
│ 4. 隔离协议层与业务层                                           │
└─────────────────────────────────────────────────────────────────┘

序列化格式选择：
┌────────────┬────────────┬────────────┬────────────┐
│   格式     │ 可读性     │ 性能       │ 跨语言     │
├────────────┼────────────┼────────────┼────────────┤
│ JSON       │ 高         │ 中         │ 高         │
│ Protobuf   │ 低         │ 高         │ 高         │
│ MessagePack│ 中         │ 中高       │ 高         │
│自定义二进制│ 低         │ 最高       │ 低         │
└────────────┴────────────┴────────────┴────────────┘

本项目选择 JSON 的原因：
1. 调试方便，可直接查看消息内容
2. 前端 JavaScript 原生支持
3. 无需额外的 IDL 文件
4. 学习成本低
```

#### 5.1.2 接口设计

```cpp
namespace serializer
{

// 序列化：结构体 → JSON 字符串
std::string Serialize(const protocol::LoginReq& msg);
std::string Serialize(const protocol::LoginResp& msg);
// ... 其他消息类型

// 反序列化：JSON 字符串 → 结构体
bool Deserialize(const std::string& data, protocol::LoginReq& msg);
bool Deserialize(const std::string& data, protocol::LoginResp& msg);
// ... 其他消息类型

}
```

**设计特点：**

1. **函数重载**：同名函数支持不同消息类型
2. **返回值设计**：
   - `Serialize` 返回字符串，失败时返回空串
   - `Deserialize` 返回 bool，表示是否成功
3. **异常安全**：内部捕获所有异常，返回 false

### 5.2 JSON 序列化实现

#### 5.2.1 使用的库

本项目使用 `nlohmann/json` 库，这是一个现代 C++ JSON 库。

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;
```

**库特点：**

```cpp
// 1. 直观的语法
json j;
j["name"] = "Alice";
j["age"] = 25;
j["skills"] = {"C++", "Python", "Go"};

// 2. STL 风格操作
for (auto& item : j["skills"]) {
    std::cout << item << std::endl;
}

// 3. 类型转换
std::string name = j["name"].get<std::string>();
int age = j["age"].get<int>();

// 4. 序列化/反序列化
std::string s = j.dump();      // JSON → 字符串
json j2 = json::parse(s);      // 字符串 → JSON
```

#### 5.2.2 序列化实现模式

```cpp
std::string Serialize(const protocol::LoginReq& msg)
{
    json j;
    // 字段映射：结构体成员 → JSON 键
    j["user_id"] = msg.userId;
    j["token"] = msg.token;
    j["device_id"] = msg.deviceId;
    return j.dump();  // 转换为紧凑 JSON 字符串
}
```

**JSON 键命名规范：**

```
使用 snake_case（下划线分隔）：
- user_id（不是 userId）
- device_id（不是 deviceId）
- result_code（不是 resultCode）

原因：
1. 与多种语言兼容
2. JSON 标准惯例
3. 数据库字段命名一致
```

#### 5.2.3 反序列化实现模式

```cpp
bool Deserialize(const std::string& data, protocol::LoginReq& msg)
{
    try
    {
        json j = json::parse(data);  // 解析 JSON
    
        // 必填字段：直接获取
        msg.userId = j["user_id"].get<std::string>();
        msg.token = j["token"].get<std::string>();
    
        // 可选字段：先检查是否存在
        if (j.contains("device_id"))
        {
            msg.deviceId = j["device_id"].get<std::string>();
        }
    
        return true;
    }
    catch (...)
    {
        return false;  // 任何异常都返回失败
    }
}
```

### 5.3 各消息类型的序列化

#### 5.3.1 登录消息

**JSON 示例：**

```json
// LoginReq
{
    "user_id": "user123",
    "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
    "device_id": "android-abc123"
}

// LoginResp
{
    "result_code": 0,
    "result_msg": "success",
    "session_id": "sess_xyz789"
}
```

#### 5.3.2 心跳消息

**JSON 示例：**

```json
// HeartbeatResp
{
    "server_time": 1704067200000
}
```

#### 5.3.3 点对点消息

**JSON 示例：**

```json
// P2PMsgReq
{
    "from_user_id": "user123",
    "to_user_id": "user456",
    "content": "Hello, how are you?"
}

// P2PMsgResp
{
    "result_code": 0,
    "result_msg": "success",
    "msg_id": 10001
}
```

#### 5.3.4 广播消息

**JSON 示例：**

```json
// BroadcastMsgNotify
{
    "from_user_id": "admin",
    "content": "System maintenance in 10 minutes",
    "timestamp": 1704067200000
}
```

---

## 6. 数据流完整流程

### 6.1 发送流程详解

```
业务层                    序列化层                   编解码层              网络层
   │                          │                         │                    │
   │  1. 构造业务结构体       │                         │                    │
   │  ┌─────────────────┐     │                         │                    │
   │  │ LoginReq req;   │     │                         │                    │
   │  │ req.userId="xx";│     │                         │                    │
   │  └────────┬────────┘     │                         │                    │
   │           ▼              │                         │                    │
   │  2. Serialize(req) ──────┼────────────────────────>│                    │
   │                          │  3. JSON 序列化         │                    │
   │                          │  return j.dump()        │                    │
   │           ┌──────────────────────────┘             │                    │
   │           ▼              │                         │                    │
   │  4. Codec::pack() ───────┼─────────────────────────┼───────────────────>│
   │                          │                         │  5. 添加协议头     │
   │                          │                         │  6. 计算CRC16      │
   │                          │                         │  7. 写入 Buffer    │
   │                          │                         │  8. 发送数据       │
   ▼                          ▼                         ▼                    ▼
```

### 6.2 接收流程详解

```
网络层                    编解码层                   序列化层              业务层
   │                          │                         │                    │
   │  1. 网络数据到达         │                         │                    │
   │  2. 写入 Buffer          │                         │                    │
   │  3. onMessage(conn,buf) ─┼────────────────────────>│                    │
   │                          │  4. 状态机解析          │                    │
   │                          │  5. 校验 CRC16          │                    │
   │                          │  6. 提取字段            │                    │
   │                          │  7. messageCallback ────┼───────────────────>│
   │                          │                         │  8. Deserialize()  │
   │                          │                         │  9. 业务处理       │
   ▼                          ▼                         ▼                    ▼
```

### 6.3 完整交互示例

```
客户端                                              服务器
   │                                                  │
   │  1. 构造登录请求                                 │
   │     LoginReq req;                                │
   │     req.userId = "user123";                      │
   │                                                  │
   │  2. 序列化                                       │
   │     json = {"user_id":"user123", "token":"..."}  │
   │                                                  │
   │  3. 打包                                         │
   │     [TotalLen][Cmd=0x0001][Seq=1][Checksum]...   │
   │                                                  │
   │─────────── TCP 发送 ────────────────────────────>│
   │                                                  │
   │                          4. 接收并解析           │
   │                             - 状态机解析         │
   │                             - CRC16 校验         │
   │                                                  │
   │                          5. 反序列化             │
   │                             LoginReq req         │
   │                                                  │
   │                          6. 业务处理             │
   │                             - 验证 token         │
   │                             - 创建 session       │
   │                                                  │
   │<─────────── TCP 响应 ────────────────────────────│
   │                                                  │
   │  7. 解析响应                                     │
   │  8. 反序列化                                     │
   ▼                                                  ▼
```

---

## 7. 模块间协作关系

### 7.1 依赖关系图

```
                              ┌──────────────┐
                              │  应用层业务  │
                              │ AuthService  │
                              │ MsgService   │
                              └──────┬───────┘
                                     │ 调用
                                     ▼
┌──────────────┐           ┌──────────────┐           ┌────────────---──┐
│   TcpServer  │──────────>│    Codec     │<──────────│MessageSerializer│
│ TcpConnection│  回调     │  编解码器    │  使用     │   序列化器      │
└──────────────┘           └──────┬───────┘           └──────┬─────---──┘
       │                          │                          │
       ▼                          ▼                          ▼
┌──────────────┐           ┌──────────────┐           ┌──────────────┐
│    Buffer    │           │   Protocol   │           │ nlohmann/json│
└──────────────┘           └──────────────┘           └──────────────┘
```

### 7.2 与网络层的集成

```cpp
// TcpConnection 中使用 Codec 的典型方式

class TcpConnection : NonCopyable
{
public:
    void setCodec(Codec* codec)
    {
        m_codec = codec;
    }

private:
    void handleRead(Timestamp receiveTime)
    {
        int savedErrno = 0;
        ssize_t n = m_inputBuffer.readFd(m_socket.fd(), &savedErrno);
    
        if (n > 0)
        {
            // 将 Buffer 交给 Codec 处理
            m_codec->onMessage(shared_from_this(), &m_inputBuffer, receiveTime);
        }
    }

    Codec* m_codec;
    Buffer m_inputBuffer;
};
```

---

## 8. 设计模式与最佳实践

### 8.1 使用的设计模式

#### 8.1.1 策略模式（序列化器）

```
序列化器采用函数重载实现策略模式：

┌──────────────────────────────────────────────────────────────┐
│                     Serialize 函数族                         │
├──────────────────────────────────────────────────────────────┤
│  Serialize(LoginReq&)     → 策略1：登录请求序列化            │
│  Serialize(LoginResp&)    → 策略2：登录响应序列化            │
│  Serialize(P2PMsgReq&)    → 策略3：点对点消息序列化          │
└──────────────────────────────────────────────────────────────┘

编译器根据参数类型自动选择正确的序列化函数。
```

#### 8.1.2 状态模式（解析状态机）

```
Codec 的解析状态机是状态模式的简化实现：

┌──────────────────────────────────────────────────────────────┐
│                       ParseState                             │
├──────────────────────────────────────────────────────────────┤
│  kExpectHeader  → 期望协议头状态                             │
│  kExpectPayload → 期望消息体状态                             │
└──────────────────────────────────────────────────────────────┘

状态转换：
  kExpectHeader ──(数据足够)──> kExpectPayload
        ↑                                  │
        └──────────(解析完成)──────────────┘
```

#### 8.1.3 模板方法模式（回调机制）

```cpp
// Codec 定义了消息处理的骨架，具体处理由回调完成
class Codec
{
public:
    void onMessage(...)
    {
        // 固定的解析流程
        while (true)
        {
            if (parseHeader(...)) { ... }
            if (parsePayload(...))
            {
                // 变化的部分：由回调处理
                if (m_messageCallback)
                {
                    m_messageCallback(...);  // 用户自定义处理
                }
            }
        }
    }
};
```

### 8.2 最佳实践

#### 8.2.1 错误处理

```cpp
// 1. 校验失败时重置状态
if (checksum != expectedChecksum)
{
    buffer->RetrieveAll();      // 清空缓冲区
    m_state = kExpectHeader;    // 重置状态机
    return false;
}

// 2. 异常捕获
bool Deserialize(const std::string& data, LoginReq& msg)
{
    try
    {
        // ... 解析逻辑
        return true;
    }
    catch (...)
    {
        return false;  // 捕获所有异常，防止崩溃
    }
}

// 3. 边界检查
if (totalLen > kMaxMessageSize)
{
    return false;  // 防止恶意大包攻击
}
```

#### 8.2.2 性能优化

```cpp
// 1. 避免不必要的拷贝
void pack(Buffer* buffer, ...)
{
    buffer->EnsureWritableBytes(kHeaderLen);
    char* header = buffer->BeginWrite();
    // 直接操作 buffer 内存
}

// 2. 使用 memcpy 而非类型转换
uint32_t netTotalLen;
memcpy(&netTotalLen, data + kLenFieldOffset, sizeof(netTotalLen));
// 避免对齐问题和未定义行为

// 3. 循环处理粘包
void onMessage(...)
{
    while (true)  // 一次处理所有完整消息
    {
        // ...
    }
}
```

---

## 9. 扩展指南

### 9.1 添加新消息类型

**完整步骤：**

```
步骤1: 定义协议结构体 (Protocol.h)
────────────────────────────────────
struct GetUserInfoReq
{
    std::string userId;
};

struct GetUserInfoResp
{
    uint32_t resultCode;
    std::string resultMsg;
    std::string nickname;
    std::string avatar;
};

步骤2: 添加命令枚举 (Protocol.h)
────────────────────────────────────
enum Command : uint16_t
{
    // ... 现有命令
    CmdGetUserInfoReq = 0x0010,
    CmdGetUserInfoResp = 0x0011,
};

步骤3: 实现序列化函数 (MessageSerializer.h/cpp)
────────────────────────────────────
std::string Serialize(const protocol::GetUserInfoReq& msg)
{
    json j;
    j["user_id"] = msg.userId;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::GetUserInfoReq& msg)
{
    try
    {
        json j = json::parse(data);
        msg.userId = j["user_id"].get<std::string>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

步骤4: 在业务层处理新命令
────────────────────────────────────
void onMessage(..., uint16_t command, ...)
{
    switch (command)
    {
        case protocol::CmdGetUserInfoReq:
        {
            protocol::GetUserInfoReq req;
            if (serializer::Deserialize(message, req))
            {
                // 处理业务逻辑
                handleGetUserInfo(conn, seqId, req);
            }
            break;
        }
        // ...
    }
}
```

### 9.2 扩展协议帧格式

如果需要扩展协议帧格式，可以：

1. **增加头部字段**：在现有字段后添加新字段
2. **增加消息体格式**：支持 Protobuf 等其他序列化格式
3. **增加压缩支持**：在消息体前添加压缩标志

### 9.3 性能优化方向

1. **零拷贝优化**：减少数据拷贝次数
2. **内存池**：使用内存池管理消息缓冲区
3. **批量处理**：支持批量消息打包发送

---

**文档版本**: 1.0
**最后更新**: 2026-04-01
