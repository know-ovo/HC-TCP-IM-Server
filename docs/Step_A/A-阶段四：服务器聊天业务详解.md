# 阶段四：服务器聊天业务详解

## 目录

- [1. 概述](#1-概述)
- [2. 整体架构设计](#2-整体架构设计)
- [3. 认证服务详解](#3-认证服务详解)
- [4. 心跳服务详解](#4-心跳服务详解)
- [5. 消息服务详解](#5-消息服务详解)
- [6. 过载保护服务详解](#6-过载保护服务详解)
- [7. 服务间协作关系](#7-服务间协作关系)
- [8. 完整业务流程](#8-完整业务流程)
- [9. 设计模式与最佳实践](#9-设计模式与最佳实践)
- [10. 扩展指南](#10-扩展指南)

---

## 1. 概述

`src/service` 模块是服务器的**业务逻辑层**，负责实现即时通讯系统的核心业务功能。该模块采用**服务化架构**，将不同业务领域分离为独立的服务类，实现了高内聚、低耦合的设计目标。

### 1.1 模块职责

| 服务 | 文件 | 职责 |
|------|------|------|
| **AuthService** | AuthService.h/cpp | 用户认证、登录/登出管理、会话管理 |
| **HeartbeatService** | HeartbeatService.h/cpp | 心跳检测、连接存活监控、超时踢人 |
| **MessageService** | MessageService.h/cpp | 消息路由、点对点消息、广播消息 |
| **OverloadProtectService** | OverloadProtectService.h/cpp | 连接数限制、QPS限制、过载保护 |

### 1.2 设计目标

1. **服务化架构**：每个服务独立封装，职责单一，便于维护和测试
2. **线程安全**：所有服务支持多线程并发访问，使用互斥锁保护共享数据
3. **异步回调**：采用回调机制处理异步操作结果，避免阻塞
4. **可配置性**：关键参数可通过配置文件动态调整

### 1.3 文件结构

```
src/service/
├── AuthService.h              # 认证服务头文件
├── AuthService.cpp            # 认证服务实现
├── HeartbeatService.h         # 心跳服务头文件
├── HeartbeatService.cpp       # 心跳服务实现
├── MessageService.h           # 消息服务头文件
├── MessageService.cpp         # 消息服务实现
├── OverloadProtectService.h   # 过载保护服务头文件
├── OverloadProtectService.cpp # 过载保护服务实现
└── server.ini                 # 服务器配置文件
```

---

## 2. 整体架构设计

### 2.1 服务层架构图

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              应用层 (Application Layer)                          │
│                         主服务器程序、消息分发器、业务编排                          │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              服务层 (Service Layer)                              │
│  ┌───────────────────────────────────────────────────────────────────────────┐  │
│  │                           业务服务集群                                      │  │
│  │                                                                           │  │
│  │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐            │  │
│  │  │  AuthService    │  │ HeartbeatService│  │ MessageService  │            │  │
│  │  │                 │  │                 │  │                 │            │  │
│  │  │ • 用户登录/登出  │  │ • 心跳检测      │  │ • 消息路由      │            │  │
│  │  │ • 会话管理      │  │ • 超时踢人      │  │ • 点对点消息    │            │  │
│  │  │ • Token验证     │  │ • 连接监控      │  │ • 广播消息      │            │  │
│  │  │ • 用户上下文    │  │ • 定时检查      │  │ • 消息ID生成    │            │  │
│  │  └─────────────────┘  └─────────────────┘  └─────────────────┘            │  │
│  │           │                    │                    │                     │  │
│  │           └────────────────────┼────────────────────┘                     │  │
│  │                                │                                          │  │
│  │  ┌───────────────────────────────────────────────────────────────────┐   │  │
│  │  │                    OverloadProtectService                         │   │  │
│  │  │                                                                   │   │  │
│  │  │  • 连接数限制 (max_connections)    • QPS限制 (max_qps)            │   │  │
│  │  │  • 实时监控                        • 过载拒绝                     │   │  │
│  │  └───────────────────────────────────────────────────────────────────┘   │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              网络层 (Network Layer)                              │
│              EventLoop、TcpConnection、Channel、EpollPoller                      │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              编解码层 (Codec Layer)                              │
│                    Protocol、Codec、MessageSerializer                            │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 服务间依赖关系

```
                    ┌─────────────────────┐
                    │   主服务器程序       │
                    │   (Server Main)     │
                    └──────────┬──────────┘
                               │
           ┌───────────────────┼───────────────────┐
           │                   │                   │
           ▼                   ▼                   ▼
    ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
    │  AuthService │   │ HeartbeatSvc │   │ MessageService│
    │  认证服务     │   │  心跳服务     │   │  消息服务     │
    └──────┬───────┘   └──────────────┘   └──────┬───────┘
           │                                      │
           │         依赖关系                      │
           └──────────────────────────────────────┘
                      MessageService 依赖 AuthService
                      (获取用户上下文、验证用户状态)

    ┌──────────────────────────────────────────────────────┐
    │               OverloadProtectService                 │
    │               过载保护服务 (全局独立)                  │
    │                                                      │
    │  被所有服务调用，进行连接数和QPS检查                    │
    └──────────────────────────────────────────────────────┘
```

### 2.3 配置文件结构

```ini
[server]
port = 8888              # 服务器监听端口
worker_threads = 4       # 工作线程数

[log]
level = info             # 日志级别
path = ../logs/server.log
max_file_size = 104857600  # 100MB
max_files = 3

[heartbeat]
timeout = 30             # 心跳超时时间(秒)
interval = 10            # 心跳检查间隔(秒)

[overload]
max_connections = 10000  # 最大连接数
max_qps = 100000         # 最大QPS
```

---

## 3. 认证服务详解

### 3.1 类设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              AuthService                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private:                                                                    │
│   - m_mutex: mutable std::mutex                    // 线程安全锁            │
│   - m_connToUser: unordered_map<string, UserContext>  // 连接→用户映射      │
│   - m_userToContext: unordered_map<string, UserContext> // 用户→上下文映射   │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public:                                                                     │
│   + login(conn, userId, token, deviceId, callback)  // 用户登录             │
│   + logout(conn)                                    // 用户登出             │
│   + isAuthenticated(conn) -> bool                   // 检查认证状态          │
│   + getUserContext(conn) -> UserContext             // 获取用户上下文        │
│   + getUserById(userId) -> UserContext              // 按用户ID查询          │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private:                                                                    │
│   - validateToken(userId, token) -> bool            // Token验证            │
│   - generateSessionId() -> string                   // 生成会话ID           │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ 使用
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              UserContext                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public:                                                                     │
│   + m_userId: string           // 用户ID                                    │
│   + m_sessionId: string        // 会话ID                                    │
│   + m_deviceId: string         // 设备ID                                    │
│   + m_loginTime: int64_t       // 登录时间戳                                │
│   + m_authenticated: bool      // 认证状态                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public:                                                                     │
│   + UserContext()              // 默认构造函数                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 核心数据结构

#### 3.2.1 UserContext - 用户上下文

```cpp
class UserContext
{
public:
    std::string m_userId;        // 用户唯一标识
    std::string m_sessionId;     // 会话ID，用于后续请求验证
    std::string m_deviceId;      // 设备标识，支持多设备管理
    int64_t m_loginTime;         // 登录时间戳（毫秒）
    bool m_authenticated;        // 认证状态

    UserContext() : m_loginTime(0), m_authenticated(false) {}
};
```

**设计说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `m_userId` | string | 用户唯一标识，业务系统定义 |
| `m_sessionId` | string | 会话ID，登录成功后生成，格式：`{timestamp}-{random}` |
| `m_deviceId` | string | 设备标识，用于多设备登录控制和消息推送 |
| `m_loginTime` | int64_t | 登录时间戳（毫秒级），用于统计和审计 |
| `m_authenticated` | bool | 认证状态，true表示已通过认证 |

#### 3.2.2 双向映射设计

```cpp
// 连接ID → 用户上下文（快速查找连接对应的用户）
std::unordered_map<std::string, std::shared_ptr<UserContext>> m_connToUser;

// 用户ID → 用户上下文（快速查找用户信息）
std::unordered_map<std::string, std::shared_ptr<UserContext>> m_userToContext;
```

**双向映射的优势：**

```
场景1：收到消息时，需要知道是哪个用户发的
       连接ID ──[m_connToUser]──> UserContext ──> userId

场景2：需要给某用户发消息时，需要找到对应的连接
       userId ──[m_userToContext]──> UserContext ──> 需要额外存储连接信息

当前设计的改进建议：
       在 UserContext 中增加 m_connId 字段，实现完整的双向查找
```

### 3.3 登录流程详解

```cpp
void AuthService::login(const std::shared_ptr<TcpConnection>& conn,
                        const std::string& userId,
                        const std::string& token,
                        const std::string& deviceId,
                        AuthCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Step 1: 验证Token
    if (!validateToken(userId, token))
    {
        callback(false, "", "Invalid token");
        return;
    }

    // Step 2: 生成连接ID（使用socket fd）
    std::string connId = std::to_string(conn->socket_->fd());
    
    // Step 3: 创建用户上下文
    auto context = std::make_shared<UserContext>();
    context->m_userId = userId;
    context->m_deviceId = deviceId;
    context->m_sessionId = generateSessionId();
    context->m_loginTime = util::getTimestampMs();
    context->m_authenticated = true;

    // Step 4: 存储映射关系
    m_connToUser[connId] = context;
    m_userToContext[userId] = context;

    // Step 5: 回调通知结果
    callback(true, context->m_sessionId, "");
}
```

**登录流程图：**

```
客户端                         服务器                          AuthService
   │                            │                                │
   │──── LoginReq ─────────────>│                                │
   │   (userId, token, deviceId)│                                │
   │                            │──── login() ──────────────────>│
   │                            │                                │
   │                            │                        ┌───────┴───────┐
   │                            │                        │ 1. validateToken
   │                            │                        │ 2. generateSessionId
   │                            │                        │ 3. create UserContext
   │                            │                        │ 4. store mappings
   │                            │                        └───────┬───────┘
   │                            │                                │
   │                            │<─── callback(true, sessionId) ─│
   │<─── LoginResp ─────────────│                                │
   │   (resultCode=0, sessionId)│                                │
   │                            │                                │
```

### 3.4 SessionId 生成算法

```cpp
std::string AuthService::generateSessionId()
{
    // 使用随机设备作为种子
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    
    // 生成64位随机数
    uint64_t randNum = dis(gen);
    
    // 获取当前时间戳
    int64_t timestamp = util::getTimestampMs();
    
    // 组合格式：{timestamp(hex)}-{random(hex)}
    std::stringstream ss;
    ss << std::hex << timestamp << "-" << randNum;
    return ss.str();
}
```

**SessionId 特性：**

| 特性 | 说明 |
|------|------|
| **唯一性** | 时间戳 + 64位随机数，碰撞概率极低 |
| **不可预测** | 使用真随机数生成器，防止会话劫持 |
| **可追溯** | 包含时间戳，便于问题排查 |
| **示例** | `18f3a2b1c000-7f3d2a1b9c4e5f6a` |

### 3.5 Token 验证机制

```cpp
bool AuthService::validateToken(const std::string& userId, const std::string& token)
{
    if (userId.empty() || token.empty())
    {
        return false;
    }
    // 当前为演示实现，实际应对接认证中心
    return token == "valid_token_" + userId;
}
```

**生产环境建议：**

```
┌─────────────────────────────────────────────────────────────────────┐
│                      Token 验证架构（生产环境）                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐          │
│   │ Auth Server │────>│   Redis     │────>│   MySQL     │          │
│   │ 认证中心     │     │ Token缓存   │     │ 用户数据库   │          │
│   └─────────────┘     └─────────────┘     └─────────────┘          │
│          │                                                          │
│          │ JWT Token 验证流程                                        │
│          ▼                                                          │
│   1. 解析 JWT Token                                                 │
│   2. 验证签名（RSA/ECDSA）                                           │
│   3. 检查过期时间                                                    │
│   4. 检查黑名单（Redis）                                             │
│   5. 返回用户信息                                                    │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.6 登出流程

```cpp
void AuthService::logout(const std::shared_ptr<TcpConnection>& conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string connId = std::to_string(conn->socket_->fd());
    auto it = m_connToUser.find(connId);
    
    if (it != m_connToUser.end())
    {
        std::string userId = it->second->m_userId;
        LOG_INFO("AuthService::logout - userId: %s", userId.c_str());
        
        // 清除双向映射
        m_userToContext.erase(userId);
        m_connToUser.erase(it);
    }
}
```

---

## 4. 心跳服务详解

### 4.1 类设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            HeartbeatService                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private:                                                                    │
│   - m_loop: EventLoop*                             // 事件循环引用          │
│   - m_timeoutSeconds: int                          // 心跳超时时间          │
│   - m_checkIntervalSeconds: int                    // 检查间隔              │
│   - m_mutex: mutable std::mutex                    // 线程安全锁            │
│   - m_lastHeartbeatTime: map<string, int64_t>      // 最后心跳时间          │
│   - m_connections: map<string, TcpConnection>      // 连接映射              │
│   - m_running: atomic<bool>                        // 运行状态              │
│   - m_kickCallback: KickCallback                   // 踢人回调              │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public:                                                                     │
│   + HeartbeatService(loop, timeout, interval)      // 构造函数              │
│   + ~HeartbeatService()                            // 析构函数              │
│   + start()                                        // 启动服务              │
│   + stop()                                         // 停止服务              │
│   + onHeartbeat(conn)                              // 处理心跳              │
│   + onConnection(conn, connected)                  // 连接状态变化          │
│   + setKickCallback(cb)                            // 设置踢人回调          │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private:                                                                    │
│   - checkHeartbeat()                               // 检查心跳超时          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 心跳机制原理

```
时间轴视图：
─────────────────────────────────────────────────────────────────────────────>

客户端A    │──H1──│──H2──│──H3──│     超时      │
           │      │      │      │               │
服务器记录  T1     T2     T3     T3+30s         T3+30s触发踢人
                                         │
                                         ▼
                                    kickCallback(A)

心跳检测周期：
┌────────────────────────────────────────────────────────────────────────────┐
│  checkInterval = 10s                                                        │
│                                                                            │
│  0s        10s        20s        30s        40s        50s                 │
│  │          │          │          │          │          │                  │
│  ▼          ▼          ▼          ▼          ▼          ▼                  │
│  检查      检查       检查       检查       检查       检查                  │
│                                                                            │
│  每次检查：遍历所有连接，找出 lastHeartbeatTime > timeout 的连接            │
└────────────────────────────────────────────────────────────────────────────┘
```

### 4.3 定时检查实现

```cpp
void HeartbeatService::start()
{
    if (m_running.exchange(true))
    {
        return;  // 已经在运行
    }
    
    LOG_INFO("HeartbeatService started, timeout: %ds, check interval: %ds", 
             m_timeoutSeconds, m_checkIntervalSeconds);
    
    // 使用 lambda 递归实现定时任务
    std::function<void()> checkFunc;
    checkFunc = [this, checkFunc]() {
        if (m_running)
        {
            checkHeartbeat();  // 执行检查
            m_loop->runAfter(m_checkIntervalSeconds * 1000, checkFunc);  // 重新注册
        }
    };
    
    // 首次注册定时任务
    m_loop->runAfter(m_checkIntervalSeconds * 1000, checkFunc);
}
```

**定时任务执行流程：**

```
EventLoop                          HeartbeatService
    │                                    │
    │<── runAfter(10s, checkFunc) ───────│
    │                                    │
    │   ... 10秒后 ...                   │
    │                                    │
    │──── 执行 checkFunc ───────────────>│
    │                                    │
    │                           ┌────────┴────────┐
    │                           │ checkHeartbeat()│
    │                           │ 检查所有连接     │
    │                           │ 踢出超时连接     │
    │                           └────────┬────────┘
    │                                    │
    │<── runAfter(10s, checkFunc) ───────│ 重新注册
    │                                    │
    │   ... 循环执行 ...                 │
```

### 4.4 心跳超时检测

```cpp
void HeartbeatService::checkHeartbeat()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int64_t now = util::getTimestampMs();
    int64_t timeoutMs = m_timeoutSeconds * 1000;
    
    // 收集需要踢出的连接（不能在遍历时修改容器）
    std::vector<std::pair<std::string, std::shared_ptr<TcpConnection>>> toKick;
    
    for (const auto& pair : m_lastHeartbeatTime)
    {
        const std::string& connId = pair.first;
        int64_t lastTime = pair.second;
        
        if (now - lastTime > timeoutMs)
        {
            auto it = m_connections.find(connId);
            if (it != m_connections.end())
            {
                toKick.emplace_back(connId, it->second);
            }
        }
    }
    
    // 执行踢人操作
    for (const auto& pair : toKick)
    {
        const std::string& connId = pair.first;
        const auto& conn = pair.second;
        
        LOG_WARN("HeartbeatService::checkHeartbeat - kicking connId: %s, timeout", 
                 connId.c_str());
        
        if (m_kickCallback)
        {
            m_kickCallback(conn, "heartbeat timeout");
        }
        
        // 清理数据
        m_connections.erase(connId);
        m_lastHeartbeatTime.erase(connId);
    }
}
```

**关键设计点：**

| 设计点 | 说明 |
|--------|------|
| **延迟收集** | 先收集超时连接，遍历结束后再踢出，避免迭代器失效 |
| **回调机制** | 通过 `m_kickCallback` 通知上层处理踢人逻辑 |
| **线程安全** | 使用 `std::lock_guard` 保护共享数据 |

### 4.5 连接状态管理

```cpp
void HeartbeatService::onConnection(const std::shared_ptr<TcpConnection>& conn, 
                                     bool connected)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::string connId = std::to_string(conn->socket_->fd());
    
    if (connected)
    {
        // 新连接：添加到监控列表
        m_connections[connId] = conn;
        m_lastHeartbeatTime[connId] = util::getTimestampMs();
        LOG_INFO("HeartbeatService::onConnection - added, connId: %s", connId.c_str());
    }
    else
    {
        // 连接断开：从监控列表移除
        m_connections.erase(connId);
        m_lastHeartbeatTime.erase(connId);
        LOG_INFO("HeartbeatService::onConnection - removed, connId: %s", connId.c_str());
    }
}
```

### 4.6 心跳时序图

```
客户端                              服务器                           HeartbeatService
   │                                  │                                    │
   │═══════════ 连接建立 ═════════════│                                    │
   │                                  │──── onConnection(conn, true) ─────>│
   │                                  │                                    │
   │──── HeartbeatReq ───────────────>│                                    │
   │                                  │──── onHeartbeat(conn) ────────────>│
   │                                  │                                    │ 更新 lastHeartbeatTime
   │<─── HeartbeatResp ───────────────│                                    │
   │                                  │                                    │
   │     ... 30秒无心跳 ...           │                                    │
   │                                  │                                    │
   │                                  │                        ┌───────────┴───────────┐
   │                                  │                        │ checkHeartbeat()      │
   │                                  │                        │ 发现超时              │
   │                                  │                        │ 调用 kickCallback     │
   │                                  │                        └───────────┬───────────┘
   │                                  │                                    │
   │                                  │<─── kickCallback(conn, "timeout") ─│
   │                                  │                                    │
   │<─── 连接关闭 ────────────────────│                                    │
   │                                  │──── onConnection(conn, false) ────>│
   │                                  │                                    │
```

---

## 5. 消息服务详解

### 5.1 类设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            MessageService                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private:                                                                    │
│   - m_authService: shared_ptr<AuthService>         // 认证服务引用          │
│   - m_mutex: mutable std::mutex                    // 线程安全锁            │
│   - m_userToConn: map<string, TcpConnection>       // 用户→连接映射         │
│   - m_nextMsgId: atomic<uint32_t>                  // 消息ID生成器          │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public:                                                                     │
│   + MessageService()                               // 构造函数              │
│   + setAuthService(authService)                    // 设置认证服务          │
│   + registerConnection(userId, conn)               // 注册用户连接          │
│   + unregisterConnection(userId)                   // 注销用户连接          │
│   + sendP2PMessage(from, to, content, msgId, err)  // 发送点对点消息        │
│   + broadcastMessage(fromUserId, content)          // 广播消息              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.2 用户连接映射

```cpp
// 用户ID → TCP连接的映射
std::unordered_map<std::string, std::shared_ptr<TcpConnection>> m_userToConn;
```

**映射关系图：**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          用户连接映射表                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   m_userToConn:                                                             │
│   ┌─────────────────┬───────────────────────────────────────────┐          │
│   │     userId      │              TcpConnection                │          │
│   ├─────────────────┼───────────────────────────────────────────┤          │
│   │  "user_001"     │  ──────────────────────────────────────>  │          │
│   │  "user_002"     │  ──────────────────────────────────────>  │          │
│   │  "user_003"     │  ──────────────────────────────────────>  │          │
│   │  ...            │  ...                                      │          │
│   └─────────────────┴───────────────────────────────────────────┘          │
│                                                                             │
│   用途：                                                                     │
│   1. 点对点消息：根据 toUserId 找到目标连接                                   │
│   2. 广播消息：遍历所有连接发送消息                                          │
│   3. 用户状态：判断用户是否在线                                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.3 点对点消息实现

```cpp
bool MessageService::sendP2PMessage(const std::string& fromUserId,
                                     const std::string& toUserId,
                                     const std::string& content,
                                     uint32_t& outMsgId,
                                     std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Step 1: 查找目标用户连接
    auto it = m_userToConn.find(toUserId);
    if (it == m_userToConn.end())
    {
        errorMsg = "User not online";
        return false;
    }

    // Step 2: 生成消息ID
    outMsgId = m_nextMsgId++;

    LOG_INFO("MessageService::sendP2PMessage - from: %s, to: %s, msgId: %u",
             fromUserId.c_str(), toUserId.c_str(), outMsgId);

    // Step 3: 发送消息（待补全）
    // const auto& conn = it->second;
    // conn->send(message);

    return true;
}
```

**点对点消息流程：**

```
发送方                              服务器                              接收方
   │                                  │                                    │
   │──── P2PMsgReq ──────────────────>│                                    │
   │   (from, to, content)            │                                    │
   │                                  │                                    │
   │                        ┌─────────┴─────────┐                          │
   │                        │ MessageService    │                          │
   │                        │ 1. 查找目标连接    │                          │
   │                        │ 2. 生成消息ID     │                          │
   │                        │ 3. 构造消息       │                          │
   │                        └─────────┬─────────┘                          │
   │                                  │                                    │
   │<─── P2PMsgResp ──────────────────│                                    │
   │   (msgId, result)                │──── P2PMsgNotify ─────────────────>│
   │                                  │   (from, content, timestamp)       │
   │                                  │                                    │
```

### 5.4 广播消息实现

```cpp
void MessageService::broadcastMessage(const std::string& fromUserId, 
                                       const std::string& content)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    LOG_INFO("MessageService::broadcastMessage - from: %s, receivers: %zu",
             fromUserId.c_str(), m_userToConn.size());

    // 遍历所有在线用户连接
    for (const auto& pair : m_userToConn)
    {
        const auto& conn = pair.second;
        
        // 待补全：构造广播消息并发送
        // BroadcastMsgNotify notify;
        // notify.fromUserId = fromUserId;
        // notify.content = content;
        // notify.timestamp = util::getTimestampMs();
        // conn->send(serialize(notify));
    }
}
```

**广播消息流程：**

```
发送方                              服务器                              所有在线用户
   │                                  │                                    │
   │──── BroadcastMsgReq ────────────>│                                    │
   │   (from, content)                │                                    │
   │                                  │                                    │
   │                        ┌─────────┴─────────┐                          │
   │                        │ MessageService    │                          │
   │                        │ 遍历 m_userToConn │                          │
   │                        │ 逐个发送消息      │                          │
   │                        └─────────┬─────────┘                          │
   │                                  │                                    │
   │<─── BroadcastMsgResp ────────────│                                    │
   │                                  │──── BroadcastMsgNotify ───────────>│ User1
   │                                  │──── BroadcastMsgNotify ───────────>│ User2
   │                                  │──── BroadcastMsgNotify ───────────>│ User3
   │                                  │                                    │ ...
```

### 5.5 消息ID生成

```cpp
std::atomic<uint32_t> m_nextMsgId;  // 原子变量，线程安全

// 使用时
outMsgId = m_nextMsgId++;  // 原子自增，保证唯一性
```

**消息ID特性：**

| 特性 | 说明 |
|------|------|
| **唯一性** | 原子自增，全局唯一 |
| **有序性** | 递增生成，可用于消息排序 |
| **线程安全** | 使用 `std::atomic`，无需加锁 |
| **范围** | uint32_t，约42亿条消息后回绕 |

---

## 6. 过载保护服务详解

### 6.1 类设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         OverloadProtectService                              │
├─────────────────────────────────────────────────────────────────────────────┤
│ Private:                                                                    │
│   - m_maxConnections: atomic<size_t>       // 最大连接数限制                │
│   - m_maxQps: atomic<size_t>               // 最大QPS限制                   │
│   - m_currentConnections: atomic<size_t>   // 当前连接数                    │
│   - m_messageCount: atomic<size_t>         // 当前消息计数                  │
│   - m_lastResetTime: atomic<int64_t>       // 上次重置时间                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ Public:                                                                     │
│   + OverloadProtectService(maxConn, maxQps)  // 构造函数                   │
│   + canAcceptConnection() -> bool            // 是否允许新连接              │
│   + onConnectionAccepted()                   // 连接接受回调                │
│   + onConnectionClosed()                     // 连接关闭回调                │
│   + canProcessMessage() -> bool              // 是否允许处理消息            │
│   + onMessageProcessed()                     // 消息处理回调                │
│   + setMaxConnections(max)                   // 设置最大连接数              │
│   + setMaxQps(max)                           // 设置最大QPS                 │
│   + getCurrentConnections() -> size_t        // 获取当前连接数              │
│   + getCurrentQps() -> size_t                // 获取当前QPS                 │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 连接数限制

```cpp
bool OverloadProtectService::canAcceptConnection()
{
    return m_currentConnections.load() < m_maxConnections.load();
}

void OverloadProtectService::onConnectionAccepted()
{
    m_currentConnections++;  // 原子自增
}

void OverloadProtectService::onConnectionClosed()
{
    m_currentConnections--;  // 原子自减
}
```

**连接数限制流程：**

```
新连接请求                         OverloadProtectService
     │                                    │
     │──── canAcceptConnection() ────────>│
     │                                    │
     │                        ┌───────────┴───────────┐
     │                        │ current < max ?       │
     │                        └───────────┬───────────┘
     │                                    │
     │<─── true/false ────────────────────│
     │                                    │
     │     如果接受连接                    │
     │──── onConnectionAccepted() ───────>│ currentConnections++
     │                                    │
     │     连接关闭时                      │
     │──── onConnectionClosed() ─────────>│ currentConnections--
     │                                    │
```

### 6.3 QPS 限制

```cpp
bool OverloadProtectService::canProcessMessage()
{
    int64_t now = util::getTimestampMs();
    int64_t last = m_lastResetTime.load();
    
    // 每秒重置计数器
    if (now - last >= 1000)
    {
        m_messageCount.store(0);
        m_lastResetTime.store(now);
    }
    
    return m_messageCount.load() < m_maxQps.load();
}

void OverloadProtectService::onMessageProcessed()
{
    m_messageCount++;  // 原子自增
}
```

**QPS 限制原理：**

```
时间轴：
─────────────────────────────────────────────────────────────────────────────>

        0ms           1000ms          2000ms          3000ms
         │              │               │               │
         ▼              ▼               ▼               ▼
    ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐
    │ count=0 │    │ reset   │    │ reset   │    │ reset   │
    │         │    │ count=0 │    │ count=0 │    │ count=0 │
    └─────────┘    └─────────┘    └─────────┘    └─────────┘
         │              │               │               │
    消息到达        消息到达         消息到达        消息到达
    count++        count++          count++         count++
         │              │               │               │
    count < max?    count < max?    count < max?    count < max?
         │              │               │               │
       处理            处理            处理           处理/拒绝
```

### 6.4 无锁设计

```cpp
// 所有计数器都使用原子变量，无需加锁
std::atomic<size_t> m_maxConnections;
std::atomic<size_t> m_maxQps;
std::atomic<size_t> m_currentConnections;
std::atomic<size_t> m_messageCount;
std::atomic<int64_t> m_lastResetTime;
```

**无锁设计的优势：**

| 优势 | 说明 |
|------|------|
| **高性能** | 原子操作比互斥锁快得多 |
| **无死锁** | 不存在锁竞争问题 |
| **可扩展** | 支持高并发访问 |
| **实时性** | 读写操作都是 O(1) |

---

## 7. 服务间协作关系

### 7.1 服务依赖图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              主服务器 (Server)                               │
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │                        服务初始化与编排                               │  │
│   │                                                                     │  │
│   │   // 1. 创建服务实例                                                 │  │
│   │   auto authService = std::make_shared<AuthService>();               │  │
│   │   auto heartbeatService = std::make_shared<HeartbeatService>(loop); │  │
│   │   auto messageService = std::make_shared<MessageService>();         │  │
│   │   auto overloadService = std::make_shared<OverloadProtectService>();│  │
│   │                                                                     │  │
│   │   // 2. 建立服务依赖                                                 │  │
│   │   messageService->setAuthService(authService);                      │  │
│   │                                                                     │  │
│   │   // 3. 设置回调                                                    │  │
│   │   heartbeatService->setKickCallback([this](conn, reason) {          │  │
│   │       // 踢人逻辑：关闭连接、清理状态                                 │  │
│   │   });                                                               │  │
│   │                                                                     │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 消息处理流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           消息处理完整流程                                    │
└─────────────────────────────────────────────────────────────────────────────┘

网络消息到达
     │
     ▼
┌─────────────────┐
│ OverloadProtect │ ── canProcessMessage()? ── No ──> 拒绝处理
│    Service      │
└────────┬────────┘
         │ Yes
         ▼
┌─────────────────┐
│     Codec       │ ── 解析协议帧、校验CRC
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Dispatcher    │ ── 根据命令类型分发
└────────┬────────┘
         │
    ┌────┴────┬─────────┬─────────┬─────────┐
    ▼         ▼         ▼         ▼         ▼
┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐
│Login  │ │Heart  │ │P2P    │ │Broadcast│ │Kick  │
│Req    │ │beat   │ │Msg    │ │Msg     │ │User  │
└───┬───┘ └───┬───┘ └───┬───┘ └───┬───┘ └───┬───┘
    │         │         │         │         │
    ▼         ▼         ▼         ▼         ▼
┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐
│Auth   │ │Heart  │ │Message│ │Message│ │Auth   │
│Service│ │beatSvc│ │Service│ │Service│ │Service│
└───────┘ └───────┘ └───────┘ └───────┘ └───────┘
    │         │         │         │         │
    └────┬────┴─────────┴────┬────┴─────────┘
         │                   │
         ▼                   ▼
   ┌─────────────────────────────────┐
   │        onMessageProcessed()      │
   │        OverloadProtectService    │
   └─────────────────────────────────┘
```

### 7.3 连接生命周期管理

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           连接生命周期                                       │
└─────────────────────────────────────────────────────────────────────────────┘

                    新连接到达
                        │
                        ▼
              ┌─────────────────┐
              │ canAcceptConn?  │── No ──> 拒绝连接
              │ OverloadProtect │
              └────────┬────────┘
                       │ Yes
                       ▼
              ┌─────────────────┐
              │ onConnection    │
              │ HeartbeatService│  添加到心跳监控
              │ (connected=true)│
              └────────┬────────┘
                       │
                       ▼
              ┌─────────────────┐
              │ onConnAccepted  │
              │ OverloadProtect │  连接计数+1
              └────────┬────────┘
                       │
                       ▼
                   连接已建立
                       │
         ┌─────────────┼─────────────┐
         │             │             │
         ▼             ▼             ▼
    ┌─────────┐  ┌─────────┐  ┌─────────┐
    │ Login   │  │Heartbeat│  │ Message │
    │ Request │  │ Request │  │ Request │
    └────┬────┘  └────┬────┘  └────┬────┘
         │             │             │
         ▼             ▼             ▼
    ┌─────────┐  ┌─────────┐  ┌─────────┐
    │Auth     │  │Heartbeat│  │Message  │
    │Service  │  │Service  │  │Service  │
    │.login() │  │.onHeart │  │.sendP2P │
    └─────────┘  └─────────┘  └─────────┘
         │
         ▼
    ┌─────────────────────────────────┐
    │ registerConnection(userId, conn)│
    │ MessageService                  │  注册用户连接
    └─────────────────────────────────┘
                       │
                       │
         ┌─────────────┴─────────────┐
         │                           │
         ▼                           ▼
    正常登出                      连接断开/超时
         │                           │
         ▼                           ▼
    ┌─────────┐                ┌─────────┐
    │ logout  │                │ 踢人回调 │
    │ AuthSvc │                │ 或断开  │
    └────┬────┘                └────┬────┘
         │                           │
         └───────────┬───────────────┘
                     │
                     ▼
              ┌─────────────────┐
              │ onConnection    │
              │ HeartbeatService│  移除心跳监控
              │ (connected=false)│
              └────────┬────────┘
                       │
                       ▼
              ┌─────────────────┐
              │ unregisterConn  │
              │ MessageService  │  移除用户连接
              └────────┬────────┘
                       │
                       ▼
              ┌─────────────────┐
              │ onConnClosed    │
              │ OverloadProtect │  连接计数-1
              └────────┬────────┘
                       │
                       ▼
                   连接已关闭
```

---

## 8. 完整业务流程

### 8.1 用户登录完整流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           用户登录完整流程                                    │
└─────────────────────────────────────────────────────────────────────────────┘

客户端                         服务器                                各服务组件
   │                            │                                      │
   │══════ TCP连接建立 ═════════│                                      │
   │                            │                                      │
   │                            │── onConnection(true) ───────────────>│ HeartbeatService
   │                            │                                      │
   │──── LoginReq ─────────────>│                                      │
   │  (userId, token, deviceId) │                                      │
   │                            │                                      │
   │                            │── canProcessMessage() ──────────────>│ OverloadProtect
   │                            │<── true ────────────────────────────│
   │                            │                                      │
   │                            │── onMessageProcessed() ─────────────>│ OverloadProtect
   │                            │                                      │
   │                            │              ┌───────────────────────┤
   │                            │              │ AuthService.login()  │
   │                            │              │ 1. validateToken()   │
   │                            │              │ 2. generateSessionId│
   │                            │              │ 3. create UserCtx   │
   │                            │              │ 4. store mappings   │
   │                            │              └───────────────────────┤
   │                            │                                      │
   │                            │── registerConnection() ─────────────>│ MessageService
   │                            │                                      │
   │<─── LoginResp ─────────────│                                      │
   │  (resultCode=0, sessionId) │                                      │
   │                            │                                      │
   │     登录成功，可以开始聊天  │                                      │
```

### 8.2 点对点消息完整流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         点对点消息完整流程                                    │
└─────────────────────────────────────────────────────────────────────────────┘

发送方                         服务器                              接收方
   │                            │                                    │
   │──── P2PMsgReq ────────────>│                                    │
   │  (from, to, content)       │                                    │
   │                            │                                    │
   │                            │── canProcessMessage() ────────────>│ OverloadProtect
   │                            │<── true ──────────────────────────│
   │                            │                                    │
   │                            │── isAuthenticated(conn) ──────────>│ AuthService
   │                            │<── true ──────────────────────────│
   │                            │                                    │
   │                            │              ┌─────────────────────┤
   │                            │              │ MessageService      │
   │                            │              │ .sendP2PMessage()   │
   │                            │              │ 1. 查找目标连接     │
   │                            │              │ 2. 生成消息ID       │
   │                            │              │ 3. 序列化消息       │
   │                            │              │ 4. 发送给目标       │
   │                            │              └─────────────────────┤
   │                            │                                    │
   │<─── P2PMsgResp ────────────│                                    │
   │  (msgId, result)           │                                    │
   │                            │                                    │
   │                            │──── P2PMsgNotify ─────────────────>│
   │                            │  (from, content, timestamp)        │
   │                            │                                    │
   │                            │── onMessageProcessed() ───────────>│ OverloadProtect
   │                            │                                    │
```

### 8.3 心跳超时踢人流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         心跳超时踢人流程                                      │
└─────────────────────────────────────────────────────────────────────────────┘

时间线                          服务器                                客户端
   │                             │                                     │
   │   T0: 最后一次心跳           │                                     │
   │                             │                                     │
   │   T0+30s: 超时              │                                     │
   │                             │                                     │
   │                             │         ┌───────────────────────────┤
   │                             │         │ HeartbeatService          │
   │                             │         │ .checkHeartbeat()         │
   │                             │         │ 发现 connId 超时          │
   │                             │         └───────────────────────────┤
   │                             │                                     │
   │                             │── kickCallback(conn, "timeout") ───>│ 回调处理
   │                             │                                     │
   │                             │         ┌───────────────────────────┤
   │                             │         │ 1. logout(conn)           │
   │                             │         │    AuthService            │
   │                             │         │                           │
   │                             │         │ 2. unregisterConnection() │
   │                             │         │    MessageService         │
   │                             │         │                           │
   │                             │         │ 3. conn->shutdown()       │
   │                             │         │    关闭连接               │
   │                             │         └───────────────────────────┤
   │                             │                                     │
   │                             │── onConnection(false) ─────────────>│ HeartbeatService
   │                             │                                     │
   │                             │── onConnectionClosed() ────────────>│ OverloadProtect
   │                             │                                     │
   │                             │<═══════ 连接关闭 ═══════════════════│
   │                             │                                     │
```

---

## 9. 设计模式与最佳实践

### 9.1 使用的设计模式

#### 9.1.1 单例模式（Singleton）

```cpp
class Config
{
public:
    static Config& instance()
    {
        static Config instance;  // C++11 保证线程安全
        return instance;
    }
    
private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};
```

**应用场景：** 配置管理、日志系统等全局唯一资源

#### 9.1.2 回调模式（Callback）

```cpp
class AuthService
{
public:
    using AuthCallback = std::function<void(bool success, 
                                             const std::string& sessionId, 
                                             const std::string& errorMsg)>;

    void login(..., AuthCallback callback)
    {
        // 异步处理
        callback(true, sessionId, "");
    }
};

class HeartbeatService
{
public:
    using KickCallback = std::function<void(const std::shared_ptr<TcpConnection>&, 
                                             const std::string& reason)>;
    
    void setKickCallback(KickCallback cb) { m_kickCallback = std::move(cb); }
};
```

**应用场景：** 异步操作结果通知、事件处理

#### 9.1.3 依赖注入（Dependency Injection）

```cpp
class MessageService
{
public:
    void setAuthService(std::shared_ptr<AuthService> authService)
    {
        m_authService = authService;
    }
    
private:
    std::shared_ptr<AuthService> m_authService;
};
```

**应用场景：** 服务间解耦、便于测试

#### 9.1.4 观察者模式（Observer）

```cpp
// HeartbeatService 作为被观察者
// 通过回调通知观察者（主服务器）连接超时事件
heartbeatService->setKickCallback([this](conn, reason) {
    // 处理踢人事件
    this->handleKickUser(conn, reason);
});
```

### 9.2 线程安全设计

#### 9.2.1 互斥锁保护

```cpp
class AuthService
{
private:
    mutable std::mutex m_mutex;  // mutable 允许在 const 方法中使用
    
public:
    void login(...)
    {
        std::lock_guard<std::mutex> lock(m_mutex);  // RAII 自动解锁
        // ... 操作共享数据
    }
};
```

#### 9.2.2 原子变量

```cpp
class OverloadProtectService
{
private:
    std::atomic<size_t> m_currentConnections;  // 无锁计数
    std::atomic<bool> m_running;               // 状态标志
    
public:
    void onConnectionAccepted()
    {
        m_currentConnections++;  // 原子操作，无需加锁
    }
};
```

### 9.3 RAII 资源管理

```cpp
// 自动加锁解锁
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 临界区代码
}  // 自动解锁

// 智能指针管理连接生命周期
std::shared_ptr<TcpConnection> conn;
```

---

## 10. 扩展指南

### 10.1 添加新的消息类型

**Step 1：定义协议命令**

```cpp
// protocol.h
enum Command : uint16_t
{
    // ... 现有命令
    CmdGroupMsgReq = 0x000B,      // 新增：群组消息请求
    CmdGroupMsgResp = 0x000C,     // 新增：群组消息响应
};
```

**Step 2：定义消息结构体**

```cpp
// protocol.h
struct GroupMsgReq
{
    std::string fromUserId;
    std::string groupId;
    std::string content;
};

struct GroupMsgResp
{
    uint32_t resultCode;
    std::string resultMsg;
    uint32_t msgId;
};
```

**Step 3：添加序列化函数**

```cpp
// messageSerializer.h
std::string Serialize(const protocol::GroupMsgReq& msg);
bool Deserialize(const std::string& data, protocol::GroupMsgReq& msg);
```

**Step 4：扩展 MessageService**

```cpp
// messageService.h
class MessageService
{
public:
    bool sendGroupMessage(const std::string& fromUserId,
                          const std::string& groupId,
                          const std::string& content,
                          uint32_t& outMsgId,
                          std::string& errorMsg);
                          
private:
    // 群组ID → 成员列表映射
    std::unordered_map<std::string, std::vector<std::string>> m_groupMembers;
};
```

### 10.2 添加新的服务

**Step 1：创建服务类**

```cpp
// GroupService.h
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include "TcpConnection.h"

class GroupService
{
public:
    GroupService() = default;
    ~GroupService() = default;

    bool createGroup(const std::string& groupId, 
                     const std::string& creatorId,
                     const std::vector<std::string>& members);
                     
    bool joinGroup(const std::string& groupId, 
                   const std::string& userId);
                   
    bool leaveGroup(const std::string& groupId, 
                    const std::string& userId);
                    
    std::vector<std::string> getGroupMembers(const std::string& groupId);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::vector<std::string>> m_groups;
};
```

**Step 2：集成到主服务器**

```cpp
// 在主服务器中
auto groupService = std::make_shared<GroupService>();
messageService->setGroupService(groupService);
```

### 10.3 持久化扩展

**当前设计：** 所有数据存储在内存中

**扩展建议：**

```cpp
// 添加数据持久化接口
class IDataStorage
{
public:
    virtual ~IDataStorage() = default;
    
    virtual bool saveUserContext(const UserContext& ctx) = 0;
    virtual bool loadUserContext(const std::string& userId, UserContext& ctx) = 0;
    virtual bool saveMessage(const Message& msg) = 0;
    virtual std::vector<Message> loadMessages(const std::string& userId) = 0;
};

// Redis 实现
class RedisStorage : public IDataStorage
{
    // ... Redis 操作实现
};

// MySQL 实现
class MySQLStorage : public IDataStorage
{
    // ... MySQL 操作实现
};

// 在服务中使用
class AuthService
{
public:
    void setDataStorage(std::shared_ptr<IDataStorage> storage)
    {
        m_storage = storage;
    }
    
private:
    std::shared_ptr<IDataStorage> m_storage;
};
```

### 10.4 分布式扩展

**当前架构：** 单机服务

**分布式扩展建议：**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           分布式架构扩展                                      │
└─────────────────────────────────────────────────────────────────────────────┘

                              ┌─────────────┐
                              │   负载均衡   │
                              │  (Nginx)    │
                              └──────┬──────┘
                                     │
              ┌──────────────────────┼──────────────────────┐
              │                      │                      │
              ▼                      ▼                      ▼
       ┌─────────────┐        ┌─────────────┐        ┌─────────────┐
       │  Server 1   │        │  Server 2   │        │  Server 3   │
       │  (服务实例)  │        │  (服务实例)  │        │  (服务实例)  │
       └──────┬──────┘        └──────┬──────┘        └──────┬──────┘
              │                      │                      │
              └──────────────────────┼──────────────────────┘
                                     │
                              ┌──────┴──────┐
                              │             │
                       ┌──────┴──────┐ ┌────┴────┐
                       │   Redis     │ │  MySQL  │
                       │ (会话/路由)  │ │ (持久化) │
                       └─────────────┘ └─────────┘

关键改造点：
1. AuthService：会话存储到 Redis，支持跨服务器查询
2. MessageService：消息路由通过 Redis Pub/Sub 实现
3. HeartbeatService：心跳数据存储到 Redis
4. 新增：服务注册与发现（etcd/Consul）
```

---

## 总结

`src/service` 模块通过服务化架构实现了即时通讯系统的核心业务功能：

| 服务 | 核心职责 | 关键技术 |
|------|----------|----------|
| **AuthService** | 用户认证、会话管理 | 双向映射、SessionId生成 |
| **HeartbeatService** | 连接存活监控 | 定时任务、超时检测 |
| **MessageService** | 消息路由、转发 | 用户连接映射、消息ID生成 |
| **OverloadProtectService** | 过载保护 | 原子计数、QPS限制 |

**设计亮点：**

1. **服务化架构**：职责单一，易于维护和扩展
2. **线程安全**：互斥锁 + 原子变量保证并发安全
3. **回调机制**：异步处理，避免阻塞
4. **无锁设计**：过载保护服务使用原子变量，高性能

**后续扩展方向：**

1. 持久化存储（Redis/MySQL）
2. 群组聊天功能
3. 消息离线存储
4. 分布式部署支持
