# A-阶段五：优化与集成测试详解

## 目录

1. [概述](#1-概述)
2. [整体架构设计](#2-整体架构设计)
3. [TcpServer 详解](#3-tcpserver-详解)
4. [TcpConnection 详解](#4-tcpconnection-详解)
5. [服务器主程序详解](#5-服务器主程序详解)
6. [客户端测试程序详解](#6-客户端测试程序详解)
7. [CMake 构建系统详解](#7-cmake-构建系统详解)
8. [配置文件详解](#8-配置文件详解)
9. [组件协作流程](#9-组件协作流程)
10. [设计模式分析](#10-设计模式分析)
11. [跨平台兼容性设计](#11-跨平台兼容性设计)
12. [测试与运行指南](#12-测试与运行指南)

---

## 1. 概述

### 1.1 阶段五目标

阶段五的核心任务是完成项目的集成测试与优化工作，主要包括：

1. **TcpServer 组件开发**：实现完整的 TCP 服务器，支持新连接的接受和管理
2. **TcpConnection 组件完善**：实现 TCP 连接的抽象，支持跨平台操作
3. **服务器主程序整合**：将所有组件整合到主程序中
4. **客户端测试程序开发**：编写用于测试服务器功能的客户端程序
5. **CMake 构建系统配置**：支持 Linux 环境下的编译和运行

### 1.2 新增文件清单

```
src/
├── net/
│   ├── tcpServer.h          # TCP 服务器头文件
│   └── tcpServer.cpp        # TCP 服务器实现
├── server.cpp               # 服务器主程序
test/
└── simpleClient.cpp         # 客户端测试程序
conf/
└── server.ini               # 服务器配置文件
CMakeLists.txt               # 根 CMake 配置
src/CMakeLists.txt           # 源码 CMake 配置
build.sh                     # Linux 构建脚本
```

### 1.3 架构总览图

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              应用层 (Application Layer)                          │
│  ┌─────────────────────────────────────────────────────────────────────────┐    │
│  │                           server.cpp                                     │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │    │
│  │  │   Config    │  │  Log/Logger │  │  ThreadPool │  │   Signal    │    │    │
│  │  │  配置管理   │  │   日志系统  │  │   线程池    │  │   信号处理  │    │    │
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
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘            │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              网络层 (Network Layer)                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐    │
│  │                           TcpServer                                      │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                      │    │
│  │  │  InetAddress│  │   Channel   │  │TcpConnection│                      │    │
│  │  │  地址封装   │  │  事件通道   │  │  连接抽象   │                      │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                      │    │
│  └─────────────────────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────────────────────┐    │
│  │                           EventLoop                                      │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                      │    │
│  │  │ EpollPoller │  │   Channel   │  │  wakeupFd   │                      │    │
│  │  │  I/O多路复用│  │  事件分发   │  │  事件唤醒   │                      │    │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                      │    │
│  └─────────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              编解码层 (Codec Layer)                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐            │
│  │   Codec     │  │  Protocol   │  │ Serializer  │  │   Buffer    │            │
│  │  协议编解码 │  │  协议定义   │  │  消息序列化 │  │  应用缓冲区 │            │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘            │
└─────────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              基础工具层 (Base Layer)                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐            │
│  │     Log     │  │   Config    │  │    Util     │  │ ThreadPool  │            │
│  │   日志库    │  │   配置库    │  │   工具库    │  │   线程池    │            │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘            │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 整体架构设计

### 2.1 分层架构说明

#### 2.1.1 应用层 (Application Layer)

应用层是整个系统的入口点，负责：

- **配置加载**：从配置文件读取服务器参数
- **组件初始化**：创建和配置各个服务组件
- **信号处理**：响应系统信号实现优雅关闭
- **生命周期管理**：控制服务器的启动和停止

#### 2.1.2 服务层 (Service Layer)

服务层实现具体的业务逻辑：

| 服务 | 职责 |
|------|------|
| AuthService | 用户认证、会话管理 |
| HeartbeatService | 心跳检测、超时踢出 |
| MessageService | 消息路由、点对点/广播 |
| OverloadProtectService | 过载保护、连接限流 |

#### 2.1.3 网络层 (Network Layer)

网络层提供网络通信能力：

| 组件 | 职责 |
|------|------|
| TcpServer | 监听端口、接受新连接 |
| TcpConnection | 表示一个 TCP 连接 |
| EventLoop | 事件循环、I/O 多路复用 |
| Channel | 文件描述符事件管理 |

#### 2.1.4 编解码层 (Codec Layer)

编解码层处理协议相关逻辑：

| 组件 | 职责 |
|------|------|
| Codec | 协议帧的打包和解包 |
| Protocol | 协议常量和数据结构定义 |
| MessageSerializer | 消息的 JSON 序列化/反序列化 |
| Buffer | 应用层缓冲区管理 |

### 2.2 数据流向图

```
┌──────────────┐                    ┌──────────────┐
│   Client     │                    │   Server     │
│simpleClient  │                    │  server.cpp  │
└──────┬───────┘                    └──────┬───────┘
       │                                   │
       │  1. TCP Connect                   │
       │ ─────────────────────────────────>│
       │                                   │
       │  2. Send LoginReq (packed)        │
       │ ─────────────────────────────────>│
       │                                   │
       │                    ┌──────────────┴──────────────┐
       │                    │                             │
       │                    ▼                             ▼
       │           ┌──────────────┐             ┌──────────────┐
       │           │   TcpServer  │             │   EventLoop  │
       │           │              │             │              │
       │           │ newConnection│             │   epoll_wait │
       │           └──────┬───────┘             └──────┬───────┘
       │                  │                            │
       │                  ▼                            │
       │           ┌──────────────┐                    │
       │           │TcpConnection │                    │
       │           │              │                    │
       │           │   created    │                    │
       │           └──────┬───────┘                    │
       │                  │                            │
       │                  ▼                            │
       │           ┌──────────────┐                    │
       │           │    Codec     │                    │
       │           │              │                    │
       │           │   unpack     │                    │
       │           └──────┬───────┘                    │
       │                  │                            │
       │                  ▼                            │
       │           ┌──────────────┐                    │
       │           │ AuthService  │                    │
       │           │              │                    │
       │           │    login     │                    │
       │           └──────┬───────┘                    │
       │                  │                            │
       │  3. Send LoginResp                        │
       │ <─────────────────────────────────│
       │                                   │
       │  4. TCP Close                     │
       │ ─────────────────────────────────>│
       │                                   │
```

---

## 3. TcpServer 详解

### 3.1 类设计概览

```cpp
// TcpServer.h

class InetAddress
{
public:
    InetAddress(uint16_t port);                    // 监听任意 IP
    InetAddress(const std::string& ip, uint16_t port);  // 监听指定 IP
    
    const struct sockaddr_in& getSockAddrInet() const;
    
private:
    struct sockaddr_in m_addr;
};

class TcpServer : NonCopyable
{
public:
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*, Timestamp)>;
    
    TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name);
    ~TcpServer();
    
    void setThreadNum(int numThreads);
    void start();
    
    void setConnectionCallback(ConnectionCallback cb);
    void setMessageCallback(MessageCallback cb);
    
private:
    void newConnection();
    void removeConnection(const std::shared_ptr<TcpConnection>& conn);
    
    // 成员变量
    EventLoop* m_loop;
    const std::string m_name;
    int m_listenfd;
    std::unique_ptr<Channel> m_acceptChannel;
    std::atomic<int> m_started;
    int m_nextConnId;
    
    ConnectionCallback m_connectionCallback;
    MessageCallback m_messageCallback;
    
    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> m_connections;
};
```

### 3.2 核心成员变量解析

#### 3.2.1 InetAddress - 地址封装类

```
┌──────────────────────────────────────────────────────┐
│                    InetAddress                        │
├──────────────────────────────────────────────────────┤
│  m_addr: struct sockaddr_in                          │
│    ├── sin_family: AF_INET                           │
│    ├── sin_port: 网络字节序端口                       │
│    └── sin_addr: IP 地址                             │
├──────────────────────────────────────────────────────┤
│  + InetAddress(port)      → 监听 0.0.0.0:port       │
│  + InetAddress(ip, port)  → 监听 ip:port            │
│  + getSockAddrInet()      → 返回地址结构体           │
└──────────────────────────────────────────────────────┘
```

**设计要点：**

1. **封装 sockaddr_in**：提供类型安全的地址操作
2. **自动初始化**：构造函数自动清零并设置地址族
3. **网络字节序转换**：内部使用 `htons()` 和 `inet_pton()`

#### 3.2.2 TcpServer 核心成员

| 成员变量 | 类型 | 作用 |
|----------|------|------|
| `m_loop` | `EventLoop*` | 所属的事件循环 |
| `m_name` | `std::string` | 服务器名称，用于连接命名 |
| `m_listenfd` | `int` | 监听套接字文件描述符 |
| `m_acceptChannel` | `std::unique_ptr<Channel>` | 监听套接字的事件通道 |
| `m_started` | `std::atomic<int>` | 启动状态，防止重复启动 |
| `m_nextConnId` | `int` | 下一个连接的 ID |
| `m_connections` | `unordered_map<string, shared_ptr<TcpConnection>>` | 连接管理表 |

### 3.3 核心方法详解

#### 3.3.1 构造函数

```cpp
TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name)
    : m_loop(loop)
    , m_name(name)
    , m_listenfd(createNonblockingSocket())  // 创建非阻塞 socket
    , m_acceptChannel(new Channel(loop, m_listenfd))
    , m_started(0)
    , m_nextConnId(1)
{
    // 设置 socket 选项
    int optval = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    
    // 绑定地址
    struct sockaddr_in addr = listenAddr.getSockAddrInet();
    bind(m_listenfd, (struct sockaddr*)&addr, sizeof(addr));
    
    // 设置 accept 回调
    m_acceptChannel->setReadCallback([this](Timestamp) { newConnection(); });
}
```

**关键步骤解析：**

```
┌─────────────────────────────────────────────────────────────────┐
│                      TcpServer 构造流程                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. 创建非阻塞 Socket                                            │
│     ┌─────────────────────────────────────────────────────┐     │
│     │ socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK |       │     │
│     │        SOCK_CLOEXEC, IPPROTO_TCP)                    │     │
│     └─────────────────────────────────────────────────────┘     │
│                           │                                      │
│                           ▼                                      │
│  2. 设置 Socket 选项                                             │
│     ┌─────────────────────────────────────────────────────┐     │
│     │ SO_REUSEADDR: 允许地址重用                           │     │
│     │ SO_REUSEPORT: 允许多个 socket 绑定同一端口           │     │
│     └─────────────────────────────────────────────────────┘     │
│                           │                                      │
│                           ▼                                      │
│  3. 绑定地址                                                     │
│     ┌─────────────────────────────────────────────────────┐     │
│     │ bind(m_listenfd, addr, sizeof(addr))                 │     │
│     └─────────────────────────────────────────────────────┘     │
│                           │                                      │
│                           ▼                                      │
│  4. 创建 Channel 并设置回调                                      │
│     ┌─────────────────────────────────────────────────────┐     │
│     │ m_acceptChannel->setReadCallback(newConnection)      │     │
│     └─────────────────────────────────────────────────────┘     │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

#### 3.3.2 start() - 启动服务器

```cpp
void TcpServer::start()
{
    if (m_started.exchange(1) == 0)  // 原子操作，保证只启动一次
    {
        // 开始监听
        if (listen(m_listenfd, SOMAXCONN) < 0)
        {
            LOG_ERROR("TcpServer::start - listen error: %d", errno);
            return;
        }
        
        // 注册读事件（可接受新连接）
        m_acceptChannel->enableReading();
        LOG_INFO("TcpServer::start - listening on fd %d", m_listenfd);
    }
}
```

**启动流程图：**

```
start() 调用
    │
    ├── m_started.exchange(1) ──── 检查是否已启动
    │       │
    │       ├── 返回 0 (未启动) ──→ 继续启动流程
    │       │
    │       └── 返回 1 (已启动) ──→ 直接返回
    │
    ├── listen(m_listenfd, SOMAXCONN)
    │       │
    │       └── 开始监听，等待连接
    │
    └── m_acceptChannel->enableReading()
            │
            └── 将监听 fd 注册到 epoll
                    │
                    └── 当有新连接时，触发 newConnection()
```

#### 3.3.3 newConnection() - 接受新连接

```cpp
void TcpServer::newConnection()
{
    struct sockaddr_in peerAddr;
    socklen_t peerLen = sizeof(peerAddr);
    
    // 接受新连接（非阻塞）
    int connfd = accept4(m_listenfd, (struct sockaddr*)&peerAddr, &peerLen, 
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd < 0)
    {
        LOG_ERROR("TcpServer::newConnection - accept error: %d", errno);
        return;
    }
    
    // 生成连接名称
    char buf[64];
    snprintf(buf, sizeof(buf), ":%s#%d", m_name.c_str(), m_nextConnId++);
    std::string connName = buf;
    
    // 创建 TcpConnection 对象
    auto conn = std::make_shared<TcpConnection>(connfd, connName);
    m_connections[connName] = conn;
    
    // 设置回调
    conn->setConnectionCallback(m_connectionCallback);
    conn->setMessageCallback(m_messageCallback);
    conn->setCloseCallback([this](const std::shared_ptr<TcpConnection>& c) { 
        removeConnection(c); 
    });
    
    // 在事件循环中调用 connectEstablished
    m_loop->queueInLoop([conn]() { conn->connectEstablished(); });
}
```

**连接建立流程：**

```
                    newConnection() 被调用
                            │
                            ▼
┌───────────────────────────────────────────────────────────────┐
│  1. accept4() 接受新连接                                       │
│     ┌─────────────────────────────────────────────────────┐   │
│     │ connfd = accept4(listenfd, peerAddr,                │   │
│     │                   SOCK_NONBLOCK | SOCK_CLOEXEC)      │   │
│     └─────────────────────────────────────────────────────┘   │
│                            │                                   │
│                            ▼                                   │
│  2. 生成连接名称                                               │
│     ┌─────────────────────────────────────────────────────┐   │
│     │ connName = ":TCPServer#1"                           │   │
│     └─────────────────────────────────────────────────────┘   │
│                            │                                   │
│                            ▼                                   │
│  3. 创建 TcpConnection                                        │
│     ┌─────────────────────────────────────────────────────┐   │
│     │ conn = make_shared<TcpConnection>(connfd, connName) │   │
│     └─────────────────────────────────────────────────────┘   │
│                            │                                   │
│                            ▼                                   │
│  4. 存入连接表                                                 │
│     ┌─────────────────────────────────────────────────────┐   │
│     │ m_connections[connName] = conn                      │   │
│     └─────────────────────────────────────────────────────┘   │
│                            │                                   │
│                            ▼                                   │
│  5. 设置回调                                                   │
│     ┌─────────────────────────────────────────────────────┐   │
│     │ conn->setConnectionCallback(...)                    │   │
│     │ conn->setMessageCallback(...)                       │   │
│     │ conn->setCloseCallback(removeConnection)            │   │
│     └─────────────────────────────────────────────────────┘   │
│                            │                                   │
│                            ▼                                   │
│  6. 触发连接建立回调                                           │
│     ┌─────────────────────────────────────────────────────┐   │
│     │ loop->queueInLoop([conn]() {                        │   │
│     │     conn->connectEstablished();                     │   │
│     │ })                                                  │   │
│     └─────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────┘
```

#### 3.3.4 removeConnection() - 移除连接

```cpp
void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn)
{
    LOG_INFO("TcpServer::removeConnection - connection %s closed", conn->name().c_str());
    
    // 从连接表中移除
    m_connections.erase(conn->name());
    
    // 在事件循环中调用 connectDestroyed
    m_loop->queueInLoop([conn]() { conn->connectDestroyed(); });
}
```

### 3.4 TcpServer 与其他组件的交互

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           TcpServer 组件交互图                               │
└─────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────┐
                    │   server.cpp    │
                    │   (应用层)       │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
     setConnectionCallback  start()   setMessageCallback
              │              │              │
              │              │              │
┌─────────────┴──────────────┴──────────────┴─────────────┐
│                         TcpServer                        │
│  ┌────────────────────────────────────────────────────┐  │
│  │ m_acceptChannel (Channel)                          │  │
│  │     │                                              │  │
│  │     │ enableReading()                              │  │
│  │     │                                              │  │
│  │     ▼                                              │  │
│  │  ┌──────────────────────────────────────────────┐  │  │
│  │  │              EventLoop                        │  │  │
│  │  │  ┌────────────────────────────────────────┐  │  │  │
│  │  │  │            EpollPoller                 │  │  │  │
│  │  │  │   epoll_ctl(EPOLL_CTL_ADD, listenfd)  │  │  │  │
│  │  │  └────────────────────────────────────────┘  │  │  │
│  │  └──────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────┘  │
│                                                           │
│  新连接到达时:                                             │
│  ┌────────────────────────────────────────────────────┐  │
│  │ newConnection()                                    │  │
│  │     │                                              │  │
│  │     ├── accept4() → 获取 connfd                    │  │
│  │     │                                              │  │
│  │     ├── 创建 TcpConnection                        │  │
│  │     │                                              │  │
│  │     └── 调用 m_connectionCallback(conn)           │  │
│  └────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────┘
                             │
                             │ m_connectionCallback
                             │
                             ▼
              ┌──────────────────────────────┐
              │      应用层回调处理           │
              │  ┌────────────────────────┐  │
              │  │ overloadService->      │  │
              │  │   canAcceptConnection()│  │
              │  └────────────────────────┘  │
              │  ┌────────────────────────┐  │
              │  │ heartbeatService->     │  │
              │  │   onConnection()       │  │
              │  └────────────────────────┘  │
              └──────────────────────────────┘
```

---

## 4. TcpConnection 详解

### 4.1 类设计概览

```cpp
// TcpConnection.h

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*, Timestamp)>;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    
    TcpConnection(int sockfd, const std::string& name);
    ~TcpConnection();
    
    // 状态查询
    int fd() const;
    std::string name() const;
    bool connected() const;
    
    // 数据发送
    void send(const void* data, size_t len);
    void send(const std::string& data);
    
    // 连接控制
    void forceClose();
    void shutdown();
    
    // 回调设置
    void setConnectionCallback(ConnectionCallback cb);
    void setMessageCallback(MessageCallback cb);
    void setCloseCallback(CloseCallback cb);
    
    // 生命周期管理
    void connectEstablished();
    void connectDestroyed();
    
private:
    int m_sockfd;
    std::string m_name;
    bool m_connected;
    
    ConnectionCallback m_connectionCallback;
    MessageCallback m_messageCallback;
    CloseCallback m_closeCallback;
};
```

### 4.2 设计要点分析

#### 4.2.1 继承 enable_shared_from_this

```cpp
class TcpConnection : public std::enable_shared_from_this<TcpConnection>
```

**作用：** 允许在类内部安全地获取 `shared_ptr<this>`

**使用场景：**

```cpp
void TcpConnection::connectEstablished()
{
    if (m_connectionCallback)
    {
        // 安全地获取 shared_ptr
        m_connectionCallback(shared_from_this());
    }
}
```

**为什么需要：**

```
┌─────────────────────────────────────────────────────────────────┐
│                    shared_from_this 原理                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  错误做法:                                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ void TcpConnection::someMethod() {                       │    │
│  │     shared_ptr<TcpConnection> self(this);  // 危险!      │    │
│  │     // 会创建新的控制块，导致双重释放                      │    │
│  │ }                                                        │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
│  正确做法:                                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │ void TcpConnection::someMethod() {                       │    │
│  │     shared_ptr<TcpConnection> self = shared_from_this(); │    │
│  │     // 共享已有的控制块，安全                              │    │
│  │ }                                                        │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

#### 4.2.2 跨平台设计

```cpp
#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
```

**平台差异处理：**

| 操作 | Linux | Windows |
|------|-------|---------|
| 关闭 socket | `close(fd)` | `closesocket(fd)` |
| 发送数据 | `write(fd, data, len)` | `send(fd, data, len, 0)` |
| 接收数据 | `read(fd, buf, len)` | `recv(fd, buf, len, 0)` |
| 关闭写端 | `shutdown(fd, SHUT_WR)` | `shutdown(fd, SD_SEND)` |

### 4.3 生命周期管理

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        TcpConnection 生命周期                                │
└─────────────────────────────────────────────────────────────────────────────┘

                    TcpServer::newConnection()
                            │
                            │ make_shared<TcpConnection>()
                            ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              创建状态                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  m_sockfd = connfd                                                  │    │
│  │  m_name = "TCPServer#1"                                             │    │
│  │  m_connected = true                                                 │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                            │
                            │ loop->queueInLoop(conn->connectEstablished)
                            ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              建立状态                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  connectEstablished() 被调用                                         │    │
│  │      │                                                              │    │
│  │      └── m_connectionCallback(shared_from_this())                   │    │
│  │              │                                                      │    │
│  │              └── 应用层处理连接建立事件                               │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                            │
                            │ 连接正常工作期间
                            ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              活跃状态                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  send(data)     → 发送数据                                          │    │
│  │  connected()    → 返回 true                                         │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                            │
                            │ 连接关闭 (forceClose 或对端关闭)
                            ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              关闭状态                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  m_connected = false                                                │    │
│  │      │                                                              │    │
│  │      └── m_closeCallback(shared_from_this())                        │    │
│  │              │                                                      │    │
│  │              └── TcpServer::removeConnection()                      │    │
│  │                      │                                              │    │
│  │                      └── connectDestroyed()                         │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                            │
                            │ shared_ptr 引用计数归零
                            ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              析构状态                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  ~TcpConnection()                                                   │    │
│  │      │                                                              │    │
│  │      └── close(m_sockfd) 或 closesocket(m_sockfd)                   │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.4 回调机制详解

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          TcpConnection 回调机制                              │
└─────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────────────┐
                    │        TcpServer            │
                    │                             │
                    │  setConnectionCallback() ───┼───→ m_connectionCallback
                    │  setMessageCallback() ──────┼───→ m_messageCallback
                    │  setCloseCallback() ────────┼───→ m_closeCallback
                    │                             │
                    └─────────────────────────────┘
                                 │
                                 │ 创建 TcpConnection 时传递
                                 ▼
                    ┌─────────────────────────────┐
                    │      TcpConnection          │
                    │                             │
                    │  m_connectionCallback ──────┼───→ 连接建立/断开时调用
                    │  m_messageCallback ─────────┼───→ 收到消息时调用
                    │  m_closeCallback ───────────┼───→ 连接关闭时调用
                    │                             │
                    └─────────────────────────────┘

回调调用时机:

┌──────────────────┐     connectEstablished()     ┌──────────────────┐
│                  │ ─────────────────────────>   │                  │
│  TcpServer       │                              │  应用层回调      │
│  newConnection() │                              │  - 过载检查      │
│                  │                              │  - 心跳注册      │
└──────────────────┘                              └──────────────────┘

┌──────────────────┐     connectDestroyed()      ┌──────────────────┐
│                  │ ─────────────────────────>   │                  │
│  TcpServer       │                              │  应用层回调      │
│  removeConnection│                              │  - 资源清理      │
│                  │                              │  - 心跳注销      │
└──────────────────┘                              └──────────────────┘
```

---

## 5. 服务器主程序详解

### 5.1 整体结构

```cpp
// Server.cpp

#include <iostream>
#include <memory>
#include <csignal>
#include "base/log.h"
#include "base/config.h"
#include "base/threadpool.h"
#include "net/tcpServer.h"
#include "net/eventLoop.h"
#include "codec/codec.h"
#include "service/authService.h"
#include "service/heartbeatService.h"
#include "service/messageService.h"
#include "service/overloadProtectService.h"

using namespace std;

// 全局指针，用于信号处理
EventLoop* g_loop = nullptr;
TcpServer* g_server = nullptr;

// 信号处理函数
void SignalHandler(int sig);

int main(int argc, char* argv[])
{
    // 1. 初始化日志
    // 2. 加载配置
    // 3. 创建组件
    // 4. 设置回调
    // 5. 启动服务
    // 6. 事件循环
    // 7. 清理资源
}
```

### 5.2 启动流程详解

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Server 启动流程                                    │
└─────────────────────────────────────────────────────────────────────────────┘

main() 开始
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 1: 日志初始化                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  Log::init("server", "logs/server.log", spdlog::level::info);       │    │
│  │                                                                      │    │
│  │  输出:                                                               │    │
│  │  ========================================                            │    │
│  │  HighConcurrencyTCPGateway starting...                              │    │
│  │  ========================================                            │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 2: 配置加载                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  Config& config = Config::instance();                               │    │
│  │  config.load("conf/server.ini");                                     │    │
│  │                                                                      │    │
│  │  读取配置:                                                           │    │
│  │  - port = 8888                                                       │    │
│  │  - worker_threads = 4                                                │    │
│  │  - heartbeat_timeout = 30                                            │    │
│  │  - max_connections = 10000                                           │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 3: 组件创建                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  // 线程池                                                           │    │
│  │  ThreadPool threadPool(workerThreads);                               │    │
│  │  threadPool.start();                                                 │    │
│  │                                                                      │    │
│  │  // 服务组件                                                         │    │
│  │  auto authService = make_shared<AuthService>();                      │    │
│  │  auto overloadService = make_shared<OverloadProtectService>(...);    │    │
│  │  auto heartbeatService = make_shared<HeartbeatService>(...);         │    │
│  │  auto messageService = make_shared<MessageService>();                │    │
│  │                                                                      │    │
│  │  // 网络组件                                                         │    │
│  │  EventLoop loop;                                                     │    │
│  │  InetAddress listenAddr(port);                                       │    │
│  │  TcpServer server(&loop, listenAddr, "TCPServer");                   │    │
│  │                                                                      │    │
│  │  // 编解码器                                                         │    │
│  │  Codec codec;                                                        │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 4: 回调设置                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  // 编解码器消息回调                                                  │    │
│  │  codec.setMessageCallback([&](conn, command, seqId, message) {       │    │
│  │      LOG_DEBUG("Received message: command=0x%04X", command);         │    │
│  │  });                                                                 │    │
│  │                                                                      │    │
│  │  // 连接回调                                                         │    │
│  │  server.setConnectionCallback([&](conn) {                            │    │
│  │      if (conn->connected()) {                                        │    │
│  │          // 过载检查                                                  │    │
│  │          if (!overloadService->canAcceptConnection()) {              │    │
│  │              conn->forceClose();                                     │    │
│  │              return;                                                 │    │
│  │          }                                                           │    │
│  │          overloadService->onConnectionAccepted();                    │    │
│  │          heartbeatService->onConnection(conn, true);                 │    │
│  │      } else {                                                        │    │
│  │          overloadService->onConnectionClosed();                      │    │
│  │          heartbeatService->onConnection(conn, false);                │    │
│  │          authService->logout(conn);                                  │    │
│  │      }                                                               │    │
│  │  });                                                                 │    │
│  │                                                                      │    │
│  │  // 消息回调                                                         │    │
│  │  server.setMessageCallback([&](conn, buffer, receiveTime) {          │    │
│  │      codec.onMessage(conn, buffer, receiveTime);                     │    │
│  │  });                                                                 │    │
│  │                                                                      │    │
│  │  // 心跳踢人回调                                                     │    │
│  │  heartbeatService->setKickCallback([&](conn, reason) {               │    │
│  │      conn->forceClose();                                             │    │
│  │  });                                                                 │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 5: 信号处理与服务启动                                                  │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  // 注册信号处理                                                     │    │
│  │  signal(SIGINT, SignalHandler);   // Ctrl+C                         │    │
│  │  signal(SIGTERM, SignalHandler);  // kill 命令                       │    │
│  │                                                                      │    │
│  │  // 启动服务                                                         │    │
│  │  server.start();                  // 开始监听                        │    │
│  │  heartbeatService->start();       // 启动心跳检测                    │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 6: 事件循环                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  loop.loop();                                                        │    │
│  │                                                                      │    │
│  │  阻塞在这里，处理所有 I/O 事件:                                       │    │
│  │  - 新连接到达                                                        │    │
│  │  - 数据可读                                                          │    │
│  │  - 定时器超时                                                        │    │
│  │  - 异步任务执行                                                      │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    │ 收到 SIGINT/SIGTERM 信号
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 7: 优雅关闭                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  loop.quit();                     // 退出事件循环                    │    │
│  │  heartbeatService->stop();        // 停止心跳服务                    │    │
│  │  threadPool.stop();               // 停止线程池                      │    │
│  │                                                                      │    │
│  │  输出:                                                               │    │
│  │  Server shutting down...                                             │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
main() 返回 0
```

### 5.3 组件协作关系

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          组件协作关系图                                      │
└─────────────────────────────────────────────────────────────────────────────┘

                              ┌─────────────┐
                              │   server    │
                              │   .cpp      │
                              └──────┬──────┘
                                     │
         ┌───────────────┬───────────┼───────────┬───────────────┐
         │               │           │           │               │
         ▼               ▼           ▼           ▼               ▼
   ┌───────────┐   ┌───────────┐ ┌───────────┐ ┌───────────┐ ┌───────────┐
   │   Log     │   │  Config   │ │ThreadPool │ │ TcpServer │ │  EventLoop │
   │           │   │           │ │           │ │           │ │           │
   │ 日志记录  │   │ 配置管理  │ │ 任务执行  │ │ 连接管理  │ │ 事件分发  │
   └───────────┘   └───────────┘ └───────────┘ └─────┬─────┘ └─────┬─────┘
                                                       │             │
                                                       │             │
                                         ┌─────────────┴─────────────┤
                                         │                           │
                                         ▼                           ▼
                                  ┌───────────┐               ┌───────────┐
                                  │TcpConnection│              │  Channel  │
                                  │           │               │           │
                                  │ 连接抽象  │               │ 事件通道  │
                                  └───────────┘               └───────────┘
                                         │
         ┌───────────────┬───────────────┼───────────────┬───────────────┐
         │               │               │               │               │
         ▼               ▼               ▼               ▼               ▼
   ┌───────────┐   ┌───────────┐   ┌───────────┐   ┌───────────┐   ┌───────────┐
   │   Auth    │   │ Heartbeat │   │  Message  │   │  Overload │   │   Codec   │
   │  Service  │   │  Service  │   │  Service  │   │  Service  │   │           │
   │           │   │           │   │           │   │           │   │ 编解码   │
   │ 认证授权  │   │ 心跳检测  │   │ 消息路由  │   │ 过载保护  │   │           │
   └───────────┘   └───────────┘   └───────────┘   └───────────┘   └───────────┘
```

### 5.4 连接处理流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          连接处理完整流程                                    │
└─────────────────────────────────────────────────────────────────────────────┘

客户端发起连接
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  1. TcpServer::newConnection()                                               │
│     ├── accept4() 接受连接                                                   │
│     ├── 创建 TcpConnection                                                  │
│     └── 调用 connectEstablished()                                           │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  2. ConnectionCallback 被触发                                                │
│     │                                                                        │
│     ├── conn->connected() == true                                           │
│     │   │                                                                    │
│     │   ├── overloadService->canAcceptConnection()                          │
│     │   │   ├── true  → 继续                                                 │
│     │   │   └── false → conn->forceClose(), 拒绝连接                        │
│     │   │                                                                    │
│     │   ├── overloadService->onConnectionAccepted()                         │
│     │   │   └── 连接计数 +1                                                  │
│     │   │                                                                    │
│     │   └── heartbeatService->onConnection(conn, true)                      │
│     │       └── 注册心跳检测                                                 │
│     │                                                                        │
│     └── conn->connected() == false (连接断开)                               │
│         ├── overloadService->onConnectionClosed()                           │
│         │   └── 连接计数 -1                                                  │
│         ├── heartbeatService->onConnection(conn, false)                     │
│         │   └── 移除心跳检测                                                 │
│         └── authService->logout(conn)                                       │
│             └── 清理会话信息                                                 │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  3. 数据到达时                                                               │
│     │                                                                        │
│     └── MessageCallback 被触发                                               │
│         └── codec.onMessage(conn, buffer, receiveTime)                      │
│             ├── 解析协议头                                                   │
│             ├── 校验 CRC16                                                  │
│             ├── 解析消息体                                                   │
│             └── 触发 messageCallback                                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 6. 客户端测试程序详解

### 6.1 整体结构

```cpp
// SimpleClient.cpp

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#endif

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

// 跨平台头文件
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include "base/log.h"
#include "codec/codec.h"
#include "codec/protocol.h"
#include "codec/buffer.h"
#include "codec/messageSerializer.h"

// Windows 特有函数
#ifdef _WIN32
void InitWinsock();
void CleanupWinsock();
#endif

int main(int argc, char* argv[])
{
    // 1. 初始化 (Windows 需要 WSAStartup)
    // 2. 创建 Socket
    // 3. 连接服务器
    // 4. 构造并发送登录请求
    // 5. 接收响应
    // 6. 清理资源
}
```

### 6.2 执行流程详解

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        SimpleClient 执行流程                                 │
└─────────────────────────────────────────────────────────────────────────────┘

main() 开始
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 1: 平台初始化 (仅 Windows)                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  #ifdef _WIN32                                                       │    │
│  │      InitWinsock();                                                  │    │
│  │      // WSAStartup(MAKEWORD(2, 2), &wsaData);                        │    │
│  │  #endif                                                              │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 2: 日志初始化                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  Log::init("client", "logs/client.log", spdlog::level::debug);      │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 3: 创建 Socket                                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  sockfd = socket(AF_INET, SOCK_STREAM, 0);                           │    │
│  │                                                                      │    │
│  │  参数说明:                                                           │    │
│  │  - AF_INET: IPv4 地址族                                              │    │
│  │  - SOCK_STREAM: TCP 流式套接字                                       │    │
│  │  - 0: 自动选择协议 (IPPROTO_TCP)                                      │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 4: 连接服务器                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  struct sockaddr_in serverAddr;                                      │    │
│  │  serverAddr.sin_family = AF_INET;                                    │    │
│  │  serverAddr.sin_port = htons(8888);                                  │    │
│  │  inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);              │    │
│  │                                                                      │    │
│  │  connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)); │    │
│  │                                                                      │    │
│  │  输出:                                                               │    │
│  │  Connected to server                                                 │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 5: 构造登录请求                                                        │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  protocol::LoginReq loginReq;                                        │    │
│  │  loginReq.userId = "user1";                                          │    │
│  │  loginReq.token = "valid_token_user1";                               │    │
│  │  loginReq.deviceId = "device1";                                      │    │
│  │                                                                      │    │
│  │  // 序列化为 JSON                                                    │    │
│  │  string loginMsg = serializer::Serialize(loginReq);                  │    │
│  │                                                                      │    │
│  │  // 打包协议帧                                                       │    │
│  │  Buffer buffer;                                                      │    │
│  │  Codec::pack(&buffer, protocol::CmdLoginReq, 1, loginMsg);           │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 6: 发送数据                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  #ifdef _WIN32                                                       │    │
│  │      send(sockfd, buffer.peek(), buffer.readableBytes(), 0);         │    │
│  │  #else                                                               │    │
│  │      write(sockfd, buffer.peek(), buffer.readableBytes());           │    │
│  │  #endif                                                              │    │
│  │                                                                      │    │
│  │  输出:                                                               │    │
│  │  Login request sent                                                  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 7: 接收响应                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  char readBuf[4096];                                                 │    │
│  │  int n = recv/read(sockfd, readBuf, sizeof(readBuf), ...);           │    │
│  │                                                                      │    │
│  │  if (n > 0) {                                                        │    │
│  │      LOG_INFO("Received %d bytes", n);                               │    │
│  │  }                                                                   │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  阶段 8: 等待并清理                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  this_thread::sleep_for(chrono::seconds(5));                         │    │
│  │                                                                      │    │
│  │  // 关闭 Socket                                                      │    │
│  │  #ifdef _WIN32                                                       │    │
│  │      closesocket(sockfd);                                            │    │
│  │      CleanupWinsock();                                               │    │
│  │  #else                                                               │    │
│  │      close(sockfd);                                                  │    │
│  │  #endif                                                              │    │
│  │                                                                      │    │
│  │  输出:                                                               │    │
│  │  Disconnected                                                        │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
main() 返回 0
```

### 6.3 协议数据构造详解

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        登录请求数据构造过程                                   │
└─────────────────────────────────────────────────────────────────────────────┘

1. 创建登录请求结构体
   ┌─────────────────────────────────────────────────────────────────────┐
   │  protocol::LoginReq loginReq;                                        │
   │  loginReq.userId = "user1";                                          │
   │  loginReq.token = "valid_token_user1";                               │
   │  loginReq.deviceId = "device1";                                      │
   └─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
2. 序列化为 JSON
   ┌─────────────────────────────────────────────────────────────────────┐
   │  string loginMsg = serializer::Serialize(loginReq);                  │
   │                                                                      │
   │  结果:                                                               │
   │  {                                                                   │
   │      "userId": "user1",                                              │
   │      "token": "valid_token_user1",                                   │
   │      "deviceId": "device1"                                           │
   │  }                                                                   │
   └─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
3. 打包协议帧
   ┌─────────────────────────────────────────────────────────────────────┐
   │  Buffer buffer;                                                      │
   │  Codec::pack(&buffer, protocol::CmdLoginReq, 1, loginMsg);           │
   │                                                                      │
   │  协议帧结构:                                                         │
   │  ┌──────────────────────────────────────────────────────────────┐   │
   │  │ TotalLen (4B) │ Cmd (2B) │ SeqId (4B) │ CRC16 (2B) │         │   │
   │  │ BodyLen (4B)  │          │            │            │ Body    │   │
   │  └──────────────────────────────────────────────────────────────┘   │
   │                                                                      │
   │  字段值:                                                             │
   │  - TotalLen: 16 + bodyLen                                            │
   │  - Cmd: 0x0001 (CmdLoginReq)                                         │
   │  - SeqId: 1                                                          │
   │  - CRC16: 计算得出                                                    │
   │  - BodyLen: JSON 字符串长度                                          │
   │  - Body: JSON 字符串                                                 │
   └─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
4. 发送到网络
   ┌─────────────────────────────────────────────────────────────────────┐
   │  write(sockfd, buffer.peek(), buffer.readableBytes());              │
   └─────────────────────────────────────────────────────────────────────┘
```

### 6.4 跨平台兼容性设计

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        跨平台兼容性设计                                      │
└─────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────────────┐
                    │      #ifdef _WIN32          │
                    └──────────────┬──────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
              ▼                    │                    ▼
     ┌─────────────────┐          │           ┌─────────────────┐
     │    Windows      │          │           │     Linux       │
     └────────┬────────┘          │           └────────┬────────┘
              │                    │                    │
              ▼                    │                    ▼
     ┌─────────────────┐          │           ┌─────────────────┐
     │ WSAStartup()    │          │           │ (无需初始化)    │
     │ Winsock2.h      │          │           │ sys/socket.h    │
     │ ws2tcpip.h      │          │           │ arpa/inet.h     │
     │ ws2_32.lib      │          │           │ unistd.h        │
     └────────┬────────┘          │           └────────┬────────┘
              │                    │                    │
              ▼                    │                    ▼
     ┌─────────────────┐          │           ┌─────────────────┐
     │ socket()        │          │           │ socket()        │
     │ connect()       │          │           │ connect()       │
     │ send()          │          │           │ write()         │
     │ recv()          │          │           │ read()          │
     │ closesocket()   │          │           │ close()         │
     │ WSACleanup()    │          │           │ (无需清理)      │
     └─────────────────┘          │           └─────────────────┘
                                  │
                                  ▼
                    ┌─────────────────────────────┐
                    │      统一的应用逻辑          │
                    │  - 协议编解码               │
                    │  - 消息序列化               │
                    │  - 日志记录                 │
                    └─────────────────────────────┘
```

---

## 7. CMake 构建系统详解

### 7.1 根目录 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(TCPServer VERSION 1.0.0 LANGUAGES CXX)

# C++ 标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 第三方库
include(FetchContent)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

add_subdirectory(src)
```

### 7.2 src/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

# 源文件分组
set(BASE_SOURCES
    base/log.cpp
    base/config.cpp
    base/util.cpp
    base/threadpool.cpp
)

set(NET_SOURCES
    net/eventLoop.cpp
    net/channel.cpp
    net/epollPoller.cpp
    net/currentThread.cpp
    net/timestamp.cpp
    net/tcpServer.cpp
)

set(CODEC_SOURCES
    codec/buffer.cpp
    codec/codec.cpp
    codec/protocol.cpp
    codec/messageSerializer.cpp
)

set(SERVICE_SOURCES
    service/authService.cpp
    service/heartbeatService.cpp
    service/messageService.cpp
    service/overloadProtectService.cpp
)

# 合并所有源文件
set(ALL_SOURCES
    ${BASE_SOURCES}
    ${NET_SOURCES}
    ${CODEC_SOURCES}
    ${SERVICE_SOURCES}
)

# 创建静态库
add_library(server_lib STATIC ${ALL_SOURCES})
target_include_directories(server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server_lib PUBLIC 
    spdlog::spdlog 
    nlohmann_json::nlohmann_json 
    pthread
)

# 服务器可执行文件
add_executable(server server.cpp)
target_link_libraries(server PRIVATE server_lib)

# 客户端可执行文件
add_executable(simpleClient
    ${BASE_SOURCES}
    ${CODEC_SOURCES}
    ../test/simpleClient.cpp
)
target_include_directories(simpleClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(simpleClient PRIVATE 
    spdlog::spdlog 
    nlohmann_json::nlohmann_json 
    pthread
)
```

### 7.3 构建系统架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          CMake 构建系统架构                                  │
└─────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────────────┐
                    │     CMakeLists.txt          │
                    │     (根目录)                 │
                    │                             │
                    │  - 项目配置                  │
                    │  - 编译选项                  │
                    │  - 第三方库下载              │
                    └──────────────┬──────────────┘
                                   │
                                   │ add_subdirectory(src)
                                   │
                                   ▼
                    ┌─────────────────────────────┐
                    │     src/CMakeLists.txt      │
                    │                             │
                    │  - 源文件分组                │
                    │  - 库目标定义                │
                    │  - 可执行文件定义            │
                    └──────────────┬──────────────┘
                                   │
         ┌─────────────────────────┼─────────────────────────┐
         │                         │                         │
         ▼                         ▼                         ▼
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   server_lib    │     │     server      │     │  simpleClient   │
│   (静态库)      │     │   (可执行文件)   │     │   (可执行文件)   │
│                 │     │                 │     │                 │
│  BASE_SOURCES   │     │  server.cpp     │     │  simpleClient   │
│  NET_SOURCES    │     │       +         │     │     .cpp        │
│  CODEC_SOURCES  │     │  server_lib     │     │       +         │
│  SERVICE_SOURCES│     │                 │     │  BASE_SOURCES   │
│                 │     │                 │     │  CODEC_SOURCES  │
└────────┬────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                       │
         │                       │                       │
         └───────────────────────┼───────────────────────┘
                                 │
                                 ▼
                    ┌─────────────────────────────┐
                    │       第三方依赖库           │
                    │  ┌───────────────────────┐  │
                    │  │ spdlog (日志库)       │  │
                    │  │ nlohmann_json (JSON)  │  │
                    │  │ pthread (线程库)      │  │
                    │  └───────────────────────┘  │
                    └─────────────────────────────┘
```

### 7.4 FetchContent 依赖管理

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        FetchContent 工作流程                                │
└─────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────────────┐
                    │   cmake .. (配置阶段)       │
                    └──────────────┬──────────────┘
                                   │
                                   ▼
                    ┌─────────────────────────────┐
                    │   FetchContent_Declare()    │
                    │   声明需要下载的库           │
                    └──────────────┬──────────────┘
                                   │
                                   ▼
                    ┌─────────────────────────────┐
                    │   FetchContent_MakeAvailable│
                    │   下载并配置库               │
                    │                             │
                    │   下载位置:                  │
                    │   build/_deps/spdlog-src    │
                    │   build/_deps/json-src      │
                    └──────────────┬──────────────┘
                                   │
                                   ▼
                    ┌─────────────────────────────┐
                    │   库已可用                   │
                    │   target_link_libraries(    │
                    │       spdlog::spdlog        │
                    │       nlohmann_json::...    │
                    │   )                         │
                    └─────────────────────────────┘
```

---

## 8. 配置文件详解

### 8.1 server.ini 结构

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
```

### 8.2 配置项说明

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `server.port` | 8888 | 服务器监听端口 |
| `server.worker_threads` | 4 | 工作线程数量 |
| `log.level` | info | 日志级别 |
| `log.path` | ../logs/server.log | 日志文件路径 |
| `log.max_file_size` | 104857600 | 单个日志文件最大大小 (100MB) |
| `log.max_files` | 3 | 保留的日志文件数量 |
| `heartbeat.timeout` | 30 | 心跳超时时间 (秒) |
| `heartbeat.interval` | 10 | 心跳检测间隔 (秒) |
| `overload.max_connections` | 10000 | 最大连接数 |
| `overload.max_qps` | 100000 | 最大 QPS |

---

## 9. 组件协作流程

### 9.1 完整请求处理流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          完整请求处理流程                                    │
└─────────────────────────────────────────────────────────────────────────────┘

客户端发送数据
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  1. 内核层 - 数据到达网卡                                                    │
│     ├── 网卡接收数据包                                                       │
│     ├── TCP 协议栈处理                                                       │
│     └── 数据放入接收缓冲区                                                   │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  2. 系统调用层 - epoll 检测到可读事件                                        │
│     ├── epoll_wait 返回                                                     │
│     ├── Channel::handleEvent() 被调用                                       │
│     └── 触发 ReadCallback                                                   │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  3. 网络层 - TcpServer 处理                                                 │
│     ├── MessageCallback 被触发                                              │
│     └── codec.onMessage(conn, buffer, receiveTime)                          │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  4. 编解码层 - Codec 处理                                                   │
│     ├── 解析协议头 (TotalLen, Cmd, SeqId, CRC16, BodyLen)                   │
│     ├── 校验 CRC16                                                          │
│     ├── 提取消息体 (JSON)                                                    │
│     └── 触发 messageCallback                                                │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  5. 服务层 - 业务处理                                                        │
│     ├── 根据 Cmd 分发到对应服务                                              │
│     │   ├── CmdLoginReq → AuthService::login()                              │
│     │   ├── CmdHeartbeatReq → HeartbeatService::heartbeat()                 │
│     │   └── CmdMsgReq → MessageService::handleMessage()                     │
│     │                                                                        │
│     └── 生成响应消息                                                         │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  6. 响应发送                                                                 │
│     ├── 序列化响应消息 (JSON)                                                │
│     ├── 打包协议帧 (Codec::pack)                                             │
│     └── conn->send(buffer)                                                  │
└─────────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  7. 内核层 - 数据发送                                                        │
│     ├── 数据写入发送缓冲区                                                   │
│     ├── TCP 协议栈处理                                                       │
│     └── 网卡发送数据包                                                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 10. 设计模式分析

### 10.1 使用的设计模式

| 模式 | 应用位置 | 说明 |
|------|----------|------|
| **Reactor 模式** | EventLoop + Channel + EpollPoller | 事件驱动的并发模型 |
| **回调模式** | TcpServer/TcpConnection | 解耦事件源和事件处理 |
| **RAII** | TcpConnection (shared_ptr) | 自动管理连接生命周期 |
| **模板方法** | Channel::handleEvent() | 统一的事件处理框架 |
| **观察者模式** | Channel 注册到 EventLoop | 事件订阅与通知 |
| **单例模式** | Config::instance() | 全局配置管理 |

### 10.2 Reactor 模式详解

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Reactor 模式结构                                   │
└─────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────────────┐
                    │         Reactor             │
                    │       (EventLoop)           │
                    │                             │
                    │  - loop()                   │
                    │  - epoll_wait()             │
                    │  - 分发事件到 Handler        │
                    └──────────────┬──────────────┘
                                   │
                    ┌──────────────┼──────────────┐
                    │              │              │
                    ▼              ▼              ▼
             ┌───────────┐  ┌───────────┐  ┌───────────┐
             │  Handler  │  │  Handler  │  │  Handler  │
             │ (Channel) │  │ (Channel) │  │ (Channel) │
             │           │  │           │  │           │
             │ - listen  │  │ - conn1   │  │ - conn2   │
             │   fd      │  │   fd      │  │   fd      │
             └───────────┘  └───────────┘  └───────────┘
                    │              │              │
                    ▼              ▼              ▼
             ┌───────────┐  ┌───────────┐  ┌───────────┐
             │  Callback │  │  Callback │  │  Callback │
             │           │  │           │  │           │
             │ - onRead  │  │ - onRead  │  │ - onRead  │
             │ - onWrite │  │ - onWrite │  │ - onWrite │
             │ - onClose │  │ - onClose │  │ - onClose │
             └───────────┘  └───────────┘  └───────────┘
```

---

## 11. 跨平台兼容性设计

### 11.1 平台差异抽象

```cpp
// 平台检测
#ifdef _WIN32
    // Windows 平台
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    // Linux/Unix 平台
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
#endif
```

### 11.2 平台适配表

| 功能 | Linux | Windows |
|------|-------|---------|
| 初始化 | 无需 | WSAStartup() |
| 创建 Socket | socket() | socket() |
| 关闭 Socket | close(fd) | closesocket(fd) |
| 发送数据 | write() | send() |
| 接收数据 | read() | recv() |
| 非阻塞设置 | SOCK_NONBLOCK | ioctlsocket() |
| 清理 | 无需 | WSACleanup() |

---

## 12. 测试与运行指南

### 12.1 构建步骤

```bash
# 1. 进入项目目录
cd /mnt/d/桌面/Linux+C++服务器开发

# 2. 运行构建脚本
chmod +x build.sh
./build.sh

# 或手动构建
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 12.2 运行测试

```bash
# 终端 1: 启动服务器
./build/bin/server

# 终端 2: 运行客户端
./build/bin/simpleClient
```

### 12.3 预期输出

**服务器端:**
```
========================================
HighConcurrencyTCPGateway starting...
========================================
Config loaded: port=8888, workers=4
TcpServer::start - listening on fd X
Server started, listening on port 8888
TcpServer::newConnection - new connection :TCPServer#1 from 127.0.0.1:XXXXX
Connection :TCPServer#1 is UP
```

**客户端端:**
```
Connected to server
Login request sent
Received XX bytes
Disconnected
```

### 12.4 常见问题排查

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| 连接被拒绝 | 服务器未启动 | 先启动服务器 |
| 地址已被使用 | 端口被占用 | 修改配置端口或 kill 占用进程 |
| 权限不足 | 端口需要 root | 使用 >1024 的端口 |
| 找不到配置文件 | 工作目录错误 | 在项目根目录运行 |

---

## 总结

阶段五完成了项目的集成测试与优化工作，主要成果包括：

1. **TcpServer 组件**：实现了完整的 TCP 服务器功能，支持新连接的接受和管理
2. **TcpConnection 组件**：实现了 TCP 连接的抽象，支持跨平台操作
3. **服务器主程序**：将所有组件整合，实现了完整的业务流程
4. **客户端测试程序**：提供了简单的测试工具，验证服务器功能
5. **CMake 构建系统**：支持 Linux 环境下的编译和运行

通过本阶段的学习，你应该掌握了：

- 如何设计一个完整的 TCP 服务器
- 如何使用回调机制解耦组件
- 如何实现跨平台的网络编程
- 如何使用 CMake 管理项目构建
- 如何进行集成测试和问题排查