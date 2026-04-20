# 工业级聊天服务器全流程串讲

## 目录

- [一、项目概述](#一项目概述)
- [二、模块架构](#二模块架构)
- [三、核心组件详解](#三核心组件详解)
- [四、服务器启动流程](#四服务器启动流程)
- [五、客户端连接与登录流程](#五客户端连接与登录流程)
- [六、消息处理流程](#六消息处理流程)
- [七、心跳监控机制](#七心跳监控机制)
- [八、Redis 集成详解](#八redis-集成详解)
- [九、完整数据流](#九完整数据流)
- [十、总结](#十总结)

---

## 一、项目概述

### 1.1 项目定位

本项目是一个**高性能 TCP 网关/消息服务器**，采用 muduo 风格的 Reactor 架构，实现了完整的即时通讯功能，支持分布式部署。

### 1.2 核心特性

| 特性                 | 说明                                   |
| -------------------- | -------------------------------------- |
| **高性能网络** | 基于 epoll 的非阻塞 I/O，Reactor 模式  |
| **自定义协议** | 16 字节固定头部 + 变长负载，CRC16 校验 |
| **登录鉴权**   | Token 验证，Session 管理               |
| **心跳机制**   | 自动检测超时连接，踢出无效客户端       |
| **过载保护**   | 连接数限制，QPS 限制                   |
| **线程池**     | 异步处理耗时任务                       |
| **Redis 集成** | 分布式会话存储、在线状态共享           |

### 1.3 目录结构

```
src/
├── base/                      # 基础工具层
│   ├── blockingQueue.h        # 线程安全阻塞队列
│   ├── config.h/cpp           # 配置文件解析
│   ├── log.h/cpp              # 日志系统（基于 spdlog）
│   ├── threadpool.h/cpp       # 线程池
│   ├── util.h/cpp             # 工具函数
│   └── redisClient.h/cpp      # Redis 客户端封装
│
├── net/                       # 网络层
│   ├── channel.h/cpp          # 事件通道
│   ├── currentThread.h/cpp    # 线程信息
│   ├── epollPoller.h/cpp      # epoll 封装
│   ├── eventLoop.h/cpp        # 事件循环
│   ├── inetAddress.h          # 网络地址封装
│   ├── nonCopyable.h          # 禁止拷贝基类
│   ├── tcpConnection.h/cpp    # TCP 连接管理
│   ├── tcpServer.h/cpp        # TCP 服务器
│   └── timestamp.h/cpp        # 时间戳
│
├── codec/                     # 编解码层
│   ├── buffer.h/cpp           # 应用层缓冲区
│   ├── codec.h/cpp            # 协议编解码器
│   ├── messageSerializer.h/cpp # JSON 序列化
│   └── protocol.h/cpp         # 协议定义
│
├── service/                   # 业务服务层
│   ├── authService.h/cpp      # 认证服务
│   ├── heartbeatService.h/cpp # 心跳服务
│   ├── messageService.h/cpp   # 消息服务
│   └── overloadProtectService.h/cpp # 过载保护
│
└── server.cpp                 # 服务器入口

conf/
└── server.ini                 # 配置文件

docs/
└── Stap_A/                    # 项目文档
    ├── A-阶段一：线程池模块的详细设计.md
    ├── A-阶段二：muduo架构详解.md
    ├── A-阶段三：服务器消息通信工具详解.md
    ├── A-阶段四：服务器聊天业务详解.md
    ├── A-阶段五：优化与集成测试详解.md
    ├── A-阶段六：引入Redis优化.md
    └── A-总结：全流程串讲.md
```

---

## 二、模块架构

### 2.1 分层架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              【应用层】                                     │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                           server.cpp                                │    │
│  │    配置加载 │ 组件初始化 │ 信号处理 │ 生命周期管理                  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              【业务层】                                     │
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐     │
│  │ AuthService │  │ Heartbeat   │  │  Message    │  │ OverloadProtect │     │
│  │  登录鉴权   │  │  Service    │  │  Service    │  │    Service      │     │
│  │  会话管理   │  │  心跳监控   │  │  消息分发   │  │   过载保护       │    │
│  │  [Redis]    │  │  [Redis]    │  │             │  │                 │    │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘    │
└─────────┼────────────────┼────────────────┼──────────────────┼─────────────┘
          │                │                │                  │
          └────────────────┴────────────────┴──────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              【存储层】                                     │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │                           Redis                                     │    │
│  │   session:{sid} │ user:session:{uid} │ user:online:{uid}            │    │
│  │      会话信息    │     用户会话映射    │      在线状态              │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              【消息层】                                     │
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐     │
│  │  Protocol   │  │   Codec     │  │ Serializer  │  │     Buffer      │     │
│  │  协议定义   │  │  编解码器   │  │ JSON序列化  │  │    缓冲区       │     │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────────┘     │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              【网络层】                                     │
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐     │
│  │  TcpServer  │  │TcpConnection│  │ EventLoop   │  │ EpollPoller     │     │
│  │  TCP服务器  │  │ TCP连接管理 │  │  事件循环   │  │  epoll封装      │     │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘     │
│         │                │                │                  │              │
│         └────────────────┴────────────────┴──────────────────┘              │
│                                    │                                        │
│                           ┌────────┴────────┐                               │
│                           │    Channel      │                               │
│                           │   事件通道      │                               │
│                           └─────────────────┘                               │
└─────────────────────────────────────────────────────────────────────────────┘
                                        │
                                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              【工具层】                                     │
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐     │
│  │    Log      │  │   Config    │  │ ThreadPool  │  │ RedisClient     │     │
│  │   日志系统  │  │   配置管理  │  │   线程池    │  │  Redis客户端    │     │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────────┘     │
│                                                                             │
│                           ┌─────────────────┐                               │
│                           │ BlockingQueue   │                               │
│                           │  阻塞队列       │                               │
│                           └─────────────────┘                               │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 核心组件关系图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              TcpServer                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ - loop_: EventLoop*              (主事件循环)                       │    │
│  │ - listenfd_: int                 (监听套接字)                       │    │
│  │ - acceptChannel_: unique_ptr<Channel> (接受连接的通道)              │    │
│  │ - connections_: map<string, shared_ptr<TcpConnection>>              │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                      │                                      │
│                     newConnection() / removeConnection()                    │
│                                      │                                      │
└──────────────────────────────────────┼──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                             TcpConnection                                   │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ - sockfd_: int                   (连接套接字)                       │    │
│  │ - channel_: unique_ptr<Channel>  (事件通道)                         │    │
│  │ - inputBuffer_: Buffer           (输入缓冲区)                       │    │
│  │ - outputBuffer_: Buffer          (输出缓冲区)                       │    │
│  │ - state_: State                  (连接状态)                         │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                      │                                      │
│                     handleRead() / handleWrite() / handleClose()            │
│                                      │                                      │
└──────────────────────────────────────┼──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                               EventLoop                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ - poller_: unique_ptr<EpollPoller> (I/O多路复用)                    │    │
│  │ - activeChannels_: vector<Channel*> (活跃通道列表)                  │    │
│  │ - wakeupFd_: int                 (唤醒管道)                         │    │
│  │ - pendingFunctors_: vector<Functor> (待执行任务)                    │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                      │                                      │
│                     poll() / updateChannel() / runInLoop()                  │
│                                      │                                      │
└──────────────────────────────────────┼──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                             EpollPoller                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ - epollfd_: int                  (epoll 实例)                       │    │
│  │ - events_: vector<epoll_event>   (事件数组)                         │    │
│  │ - channels_: map<int, Channel*>  (fd到Channel的映射)                │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                      │                                      │
│                     poll() / updateChannel() / removeChannel()              │
│                                      │                                      │
└──────────────────────────────────────┼──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                               Channel                                       │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ - fd_: int                       (文件描述符)                       │    │
│  │ - events_: int                   (关注的事件)                       │    │
│  │ - revents_: int                  (发生的事件)                       │    │
│  │ - readCallback_, writeCallback_, closeCallback_, errorCallback_     │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                      │                                      │
│                     handleEvent() / enableReading() / disableAll()          │
│                                      │                                      │
└──────────────────────────────────────┴──────────────────────────────────────┘
```

---

## 三、核心组件详解

### 3.1 网络层组件

#### 3.1.1 EventLoop（事件循环）

```cpp
class EventLoop
{
public:
    EventLoop();
    ~EventLoop();

    void loop();
    void quit();

    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    bool isInLoopThread() const;

private:
    std::unique_ptr<EpollPoller> poller_;
    std::vector<Channel*> activeChannels_;
    std::vector<Functor> pendingFunctors_;
    std::mutex mutex_;
    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;
    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
};
```

**核心职责**：

1. **事件循环**：持续监听 I/O 事件
2. **任务队列**：支持跨线程任务提交
3. **唤醒机制**：通过 eventfd 实现线程间通信

#### 3.1.2 TcpServer（TCP 服务器）

```cpp
class TcpServer
{
public:
    TcpServer(EventLoop* loop, const InetAddress& listenAddr, const string& name);
    ~TcpServer();

    void start();
    void setThreadNum(int numThreads);

    void setConnectionCallback(const ConnectionCallback& cb);
    void setMessageCallback(const MessageCallback& cb);

private:
    void newConnection(int sockfd, const InetAddress& peerAddr);
    void removeConnection(const TcpConnectionPtr& conn);

    EventLoop* loop_;
    const string name_;
    std::unique_ptr<Acceptor> acceptor_;
    std::shared_ptr<EventLoopThreadPool> threadPool_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    std::map<string, TcpConnectionPtr> connections_;
};
```

**核心职责**：

1. **监听连接**：接受新的客户端连接
2. **连接管理**：维护所有活跃连接
3. **线程池**：支持多线程处理

#### 3.1.3 TcpConnection（TCP 连接）

```cpp
class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop* loop, const string& name, int sockfd,
                  const InetAddress& localAddr, const InetAddress& peerAddr);
    ~TcpConnection();

    void send(const void* message, size_t len);
    void send(Buffer* message);
    void shutdown();
    void forceClose();

    void setConnectionCallback(const ConnectionCallback& cb);
    void setMessageCallback(const MessageCallback& cb);
    void setWriteCompleteCallback(const WriteCompleteCallback& cb);
    void setCloseCallback(const CloseCallback& cb);

    Buffer* inputBuffer() { return &inputBuffer_; }
    Buffer* outputBuffer() { return &outputBuffer_; }

private:
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(const void* message, size_t len);

    EventLoop* loop_;
    const string name_;
    int sockfd_;
    std::unique_ptr<Channel> channel_;
    InetAddress localAddr_;
    InetAddress peerAddr_;
    Buffer inputBuffer_;
    Buffer outputBuffer_;
    State state_;
};
```

**核心职责**：

1. **数据收发**：管理输入输出缓冲区
2. **连接状态**：维护连接生命周期
3. **回调处理**：触发用户注册的回调

### 3.2 业务层组件

#### 3.2.1 AuthService（认证服务）

```cpp
class AuthService
{
public:
    using AuthCallback = std::function<void(bool success, const string& sessionId, const string& errorMsg)>;

    AuthService();
    ~AuthService();

    void setRedisClient(std::shared_ptr<RedisClient> redisClient);
    void setSessionExpire(int expireSeconds);

    void login(const shared_ptr<TcpConnection>& conn,
               const string& userId,
               const string& token,
               const string& deviceId,
               AuthCallback callback);

    void logout(const shared_ptr<TcpConnection>& conn);

    bool isAuthenticated(const shared_ptr<TcpConnection>& conn);
    shared_ptr<UserContext> getUserContext(const shared_ptr<TcpConnection>& conn);
    shared_ptr<UserContext> getUserById(const string& userId);
    bool validateSession(const string& sessionId, string& outUserId);

private:
    bool validateToken(const string& userId, const string& token);
    string generateSessionId();
    string serializeSession(const shared_ptr<UserContext>& context);
    bool deserializeSession(const string& data, shared_ptr<UserContext>& context);

    mutable std::mutex m_mutex;
    std::unordered_map<string, shared_ptr<UserContext>> m_connToUser;
    std::unordered_map<string, shared_ptr<UserContext>> m_userToContext;
    std::shared_ptr<RedisClient> m_redisClient;
    int m_sessionExpire;
    bool m_useRedis;
};
```

**核心职责**：

1. **用户认证**：验证 Token 有效性
2. **会话管理**：创建、存储、销毁会话
3. **Redis 集成**：分布式会话存储

#### 3.2.2 HeartbeatService（心跳服务）

```cpp
class HeartbeatService
{
public:
    using KickCallback = std::function<void(const shared_ptr<TcpConnection>& conn, const string& reason)>;

    HeartbeatService(EventLoop* loop, int timeoutSeconds = 30, int checkIntervalSeconds = 10);
    ~HeartbeatService();

    void setRedisClient(std::shared_ptr<RedisClient> redisClient);
    void setThreadPool(ThreadPool* pool);

    void start();
    void stop();

    void onHeartbeat(const shared_ptr<TcpConnection>& conn);
    void onConnection(const shared_ptr<TcpConnection>& conn, bool connected);

    void registerUserId(const string& connId, const string& userId);
    bool isUserOnline(const string& userId);

    void setKickCallback(KickCallback cb);

private:
    void checkHeartbeat();
    void updateOnlineStatus(const string& userId, bool online);
    void refreshOnlineStatus(const string& userId);
    void updateOnlineStatusAsync(const string& userId, bool online);
    void refreshOnlineStatusAsync(const string& userId);

    EventLoop* m_loop;
    int m_timeoutSeconds;
    int m_checkIntervalSeconds;

    mutable std::mutex m_mutex;
    std::unordered_map<string, int64_t> m_lastHeartbeatTime;
    std::unordered_map<string, shared_ptr<TcpConnection>> m_connections;
    std::unordered_map<string, string> m_connToUser;

    std::shared_ptr<RedisClient> m_redisClient;
    ThreadPool* m_threadPool;
    bool m_useRedis;

    std::atomic<bool> m_running;
    KickCallback m_kickCallback;
};
```

**核心职责**：

1. **心跳检测**：记录每个连接的最后心跳时间
2. **超时踢人**：定时检查，踢出超时连接
3. **在线状态**：将用户在线状态存储到 Redis

#### 3.2.3 OverloadProtectService（过载保护）

```cpp
class OverloadProtectService
{
public:
    OverloadProtectService(int maxConnections, int maxQps);

    bool canAcceptConnection();
    void onConnectionAccepted();
    void onConnectionClosed();

    bool canProcessRequest();
    void onRequestProcessed();

private:
    std::atomic<int> m_currentConnections;
    int m_maxConnections;
    std::atomic<int64_t> m_lastSecond;
    std::atomic<int> m_currentQps;
    int m_maxQps;
};
```

**核心职责**：

1. **连接数限制**：限制最大连接数
2. **QPS 限制**：限制每秒请求数

---

## 四、服务器启动流程

### 4.1 启动流程图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              服务器启动流程                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 初始化日志系统                                                          │
│     └── Log::init("server", "logs/server.log", spdlog::level::info)        │
│                                                                             │
│  2. 加载配置文件                                                            │
│     └── Config::instance().load("conf/server.ini")                          │
│                                                                             │
│  3. 创建线程池                                                              │
│     └── ThreadPool threadPool(workerThreads)                                │
│     └── threadPool.start()                                                  │
│                                                                             │
│  4. 创建服务组件                                                            │
│     ├── authService = make_shared<AuthService>()                            │
│     └── overloadService = make_shared<OverloadProtectService>()             │
│                                                                             │
│  5. 初始化 Redis（可选）                                                    │
│     ├── redisClient = make_shared<RedisClient>()                            │
│     ├── redisClient->connect(host, port, timeout)                           │
│     ├── authService->setRedisClient(redisClient)                            │
│     └── heartbeatService->setRedisClient(redisClient)                       │
│                                                                             │
│  6. 创建网络组件                                                            │
│     ├── EventLoop loop                                                      │
│     ├── InetAddress listenAddr(port)                                        │
│     └── TcpServer server(&loop, listenAddr, "TCPServer")                    │
│                                                                             │
│  7. 设置回调函数                                                            │
│     ├── server.setConnectionCallback(...)                                   │
│     ├── server.setMessageCallback(...)                                      │
│     └── heartbeatService->setKickCallback(...)                              │
│                                                                             │
│  8. 注册信号处理                                                            │
│     ├── signal(SIGINT, SignalHandler)                                       │
│     └── signal(SIGTERM, SignalHandler)                                      │
│                                                                             │
│  9. 启动服务                                                                │
│     ├── server.start()                                                      │
│     └── heartbeatService->start()                                           │
│                                                                             │
│  10. 进入事件循环                                                           │
│      └── loop.loop()                                                        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 启动代码详解

```cpp
int main(int argc, char* argv[])
{
    Log::init("server", "logs/server.log", spdlog::level::info);

    LOG_INFO("========================================");
    LOG_INFO("HighConcurrencyTCPGateway starting...");
    LOG_INFO("========================================");

    Config& config = Config::instance();
    if (!config.load("conf/server.ini"))
    {
        LOG_ERROR("Failed to load config file");
        return -1;
    }

    int port = config.getInt("server", "port", 8888);
    int workerThreads = config.getInt("server", "worker_threads", 4);
    int heartbeatTimeout = config.getInt("heartbeat", "timeout", 30);
    int heartbeatInterval = config.getInt("heartbeat", "interval", 10);
    int maxConnections = config.getInt("overload", "max_connections", 10000);
    int maxQps = config.getInt("overload", "max_qps", 100000);

    LOG_INFO("Config loaded: port={}, workers={}", port, workerThreads);

    ThreadPool threadPool(workerThreads);
    threadPool.start();

    auto authService = make_shared<AuthService>();
    auto overloadService = make_shared<OverloadProtectService>(maxConnections, maxQps);

#ifdef USE_REDIS
    bool redisEnabled = config.getBool("redis", "enabled", false);
    shared_ptr<RedisClient> redisClient;

    if (redisEnabled)
    {
        string redisHost = config.getString("redis", "host", "127.0.0.1");
        int redisPort = config.getInt("redis", "port", 6379);
        int redisTimeout = config.getInt("redis", "timeout_ms", 2000);
        int sessionExpire = config.getInt("redis", "session_expire", 86400);

        redisClient = make_shared<RedisClient>();
        if (redisClient->connect(redisHost, redisPort, redisTimeout))
        {
            LOG_INFO("Redis connected: {}:{}", redisHost, redisPort);
            authService->setRedisClient(redisClient);
            authService->setSessionExpire(sessionExpire);
        }
        else
        {
            LOG_ERROR("Failed to connect Redis: {}", redisClient->getLastError());
            redisClient.reset();
        }
    }
#else
    LOG_INFO("Redis support not compiled in");
#endif

    EventLoop loop;
    g_loop = &loop;

    InetAddress listenAddr(static_cast<uint16_t>(port));
    TcpServer server(&loop, listenAddr, "TCPServer");
    g_server = &server;

    server.setThreadNum(workerThreads);

    Codec codec;
    auto heartbeatService = make_shared<HeartbeatService>(&loop, heartbeatTimeout, heartbeatInterval);
    auto messageService = make_shared<MessageService>();
    messageService->setAuthService(authService);

#ifdef USE_REDIS
    if (redisClient)
    {
        heartbeatService->setRedisClient(redisClient);
        heartbeatService->setThreadPool(&threadPool);
    }
#endif

    codec.setMessageCallback([&](const shared_ptr<TcpConnection>& conn,
                                  uint16_t command,
                                  uint32_t seqId,
                                  const string& message) {
        LOG_INFO("Received message: command=0x{:04X}, seqId={}, len={}", command, seqId, message.size());

        if (command == protocol::CmdLoginReq)
        {
            protocol::LoginReq loginReq;
            serializer::Deserialize(message, loginReq);
            LOG_INFO("Login request: userId={}, token={}, deviceId={}",
                     loginReq.userId, loginReq.token, loginReq.deviceId);

            authService->login(conn, loginReq.userId, loginReq.token, loginReq.deviceId,
                [&, conn, seqId](bool success, const string& sessionId, const string& errorMsg) {
                    protocol::LoginResp loginResp;
                    loginResp.resultCode = success ? 0 : 1;
                    loginResp.resultMsg = success ? "success" : errorMsg;
                    loginResp.sessionId = sessionId;

                    string respMsg = serializer::Serialize(loginResp);
                    Buffer buffer;
                    Codec::pack(&buffer, protocol::CmdLoginResp, seqId, respMsg);
                    conn->send(&buffer);

                    LOG_INFO("Login response sent: success={}, sessionId={}", success, sessionId);

#ifdef USE_REDIS
                    if (success && heartbeatService)
                    {
                        heartbeatService->registerUserId(to_string(conn->fd()), loginReq.userId);
                    }
#endif
                });
        }
        else if (command == protocol::CmdHeartbeatReq)
        {
            heartbeatService->onHeartbeat(conn);

            protocol::HeartbeatResp heartbeatResp;
            heartbeatResp.serverTime = util::GetTimestampMs();

            string respMsg = serializer::Serialize(heartbeatResp);
            Buffer buffer;
            Codec::pack(&buffer, protocol::CmdHeartbeatResp, seqId, respMsg);
            conn->send(&buffer);
        }
    });

    server.setConnectionCallback([&](const shared_ptr<TcpConnection>& conn) {
        LOG_INFO("Connection {} is {}", conn->name(),
                 conn->connected() ? "UP" : "DOWN");

        if (conn->connected())
        {
            if (!overloadService->canAcceptConnection())
            {
                LOG_WARN("Connection rejected: overload");
                conn->forceClose();
                return;
            }
            overloadService->onConnectionAccepted();
            heartbeatService->onConnection(conn, true);
        }
        else
        {
            overloadService->onConnectionClosed();
            heartbeatService->onConnection(conn, false);
            authService->logout(conn);
        }
    });

    server.setMessageCallback([&](const shared_ptr<TcpConnection>& conn,
                                  Buffer* buffer,
                                  Timestamp receiveTime) {
        codec.onMessage(conn, buffer, receiveTime);
    });

    heartbeatService->setKickCallback([&](const shared_ptr<TcpConnection>& conn, const string& reason) {
        conn->forceClose();
    });

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    server.start();
    heartbeatService->start();

    LOG_INFO("Server started on port {}", port);
    loop.loop();

    LOG_INFO("Server stopped");
    return 0;
}
```

---

## 五、客户端连接与登录流程

### 5.1 连接建立流程

```
┌──────────────┐                    ┌──────────────┐                    ┌──────────────┐
│   Client     │                    │    Server    │                    │    Redis     │
│              │                    │              │                    │              │
└──────┬───────┘                    └──────┬───────┘                    └──────┬───────┘
       │                                   │                                   │
       │  1. TCP Connect (SYN)             │                                   │
       │ ─────────────────────────────────>│                                   │
       │                                   │                                   │
       │  2. TCP Connect (SYN+ACK)         │                                   │
       │ <─────────────────────────────────│                                   │
       │                                   │                                   │
       │  3. TCP Connect (ACK)             │                                   │
       │ ─────────────────────────────────>│                                   │
       │                                   │                                   │
       │                                   │  4. ConnectionCallback            │
       │                                   │     ├── canAcceptConnection()?    │
       │                                   │     ├── onConnectionAccepted()    │
       │                                   │     └── heartbeatService->        │
       │                                   │         onConnection(conn, true)  │
       │                                   │                                   │
```

### 5.2 登录流程

```
┌──────────────┐                    ┌──────────────┐                    ┌──────────────┐
│   Client     │                    │    Server    │                    │    Redis     │
│              │                    │              │                    │              │
└──────┬───────┘                    └──────┬───────┘                    └──────┬───────┘
       │                                   │                                   │
       │  1. LoginReq                      │                                   │
       │     {userId, token, deviceId}     │                                   │
       │ ─────────────────────────────────>│                                   │
       │                                   │                                   │
       │                                   │  2. authService->login()          │
       │                                   │     ├── validateToken()           │
       │                                   │     ├── generateSessionId()       │
       │                                   │     └── create UserContext        │
       │                                   │                                   │
       │                                   │  3. 本地存储                       │
       │                                   │     ├── m_connToUser[connId]      │
       │                                   │     └── m_userToContext[userId]   │
       │                                   │                                   │
       │                                   │  4. Redis 存储（如果启用）         │
       │                                   │ ─────────────────────────────────>│
       │                                   │     SETEX session:{sid} {data}    │
       │                                   │     SETEX user:session:{uid} {sid}│
       │                                   │                                   │
       │                                   │  5. 注册用户到心跳服务             │
       │                                   │     heartbeatService->            │
       │                                   │       registerUserId(connId, uid) │
       │                                   │                                   │
       │                                   │ ─────────────────────────────────>│
       │                                   │     SETEX user:online:{uid} "1"   │
       │                                   │                                   │
       │  6. LoginResp                     │                                   │
       │     {resultCode=0, sessionId}     │                                   │
       │ <─────────────────────────────────│                                   │
       │                                   │                                   │
```

---

## 六、消息处理流程

### 6.1 消息处理流程图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              消息处理流程                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 数据到达                                                                │
│     └── epoll_wait 返回，触发 EPOLLIN 事件                                  │
│                                                                             │
│  2. Channel::handleEvent()                                                  │
│     └── 调用注册的 readCallback_                                            │
│                                                                             │
│  3. TcpConnection::handleRead()                                             │
│     ├── read() 系统调用读取数据                                             │
│     └── inputBuffer_.append() 追加到输入缓冲区                              │
│                                                                             │
│  4. messageCallback_                                                        │
│     └── 用户设置的回调函数                                                  │
│                                                                             │
│  5. Codec::onMessage()                                                      │
│     ├── 解析协议头（16 字节）                                               │
│     │   ├── magic: 0xABCD                                                   │
│     │   ├── version: 1                                                      │
│     │   ├── command: 命令类型                                               │
│     │   ├── seqId: 序列号                                                   │
│     │   ├── length: 负载长度                                                │
│     │   └── crc16: 校验码                                                   │
│     ├── 解析负载（变长 JSON）                                               │
│     └── 调用用户回调                                                        │
│                                                                             │
│  6. 业务处理                                                                │
│     ├── LoginReq → authService->login()                                     │
│     ├── HeartbeatReq → heartbeatService->onHeartbeat()                      │
│     └── 其他消息 → messageService->handleMessage()                          │
│                                                                             │
│  7. 发送响应                                                                │
│     ├── 序列化响应消息                                                      │
│     ├── Codec::pack() 打包                                                  │
│     └── TcpConnection::send() 发送                                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 协议格式

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              协议帧格式                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  固定头部（16 字节）：                                                       │
│  ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐ │
│  │  0xABCD │  0x01  │  Cmd   │  SeqId │ Length │  CRC16 │reserved│        │ │
│  │ 2 byte │ 1 byte │2 byte │ 4 byte │ 4 byte │ 2 byte │ 1 byte │        │ │
│  │ magic  │version │command │ seqId  │ length │  crc   │        │        │ │
│  └────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘ │
│                                                                             │
│  变长负载：                                                                  │
│  ┌────────────────────────────────────────────────────────────────────────┐ │
│  │                        JSON 格式消息体                                  │ │
│  │                        (Length 字节)                                    │ │
│  └────────────────────────────────────────────────────────────────────────┘ │
│                                                                             │
│  命令类型：                                                                  │
│  ├── CmdLoginReq (0x0001)      : 登录请求                                   │
│  ├── CmdLoginResp (0x0002)     : 登录响应                                   │
│  ├── CmdHeartbeatReq (0x0003)  : 心跳请求                                   │
│  ├── CmdHeartbeatResp (0x0004) : 心跳响应                                   │
│  ├── CmdMessageReq (0x0005)    : 消息请求                                   │
│  └── CmdMessageResp (0x0006)   : 消息响应                                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 七、心跳监控机制

### 7.1 心跳检测流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              心跳检测机制                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  客户端行为：                                                                │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. 每隔 10 秒发送一次心跳请求                                    │       │
│  │ 2. 等待服务器心跳响应                                            │       │
│  │ 3. 如果超时未收到响应，认为连接断开                              │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  服务器行为：                                                                │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. 收到心跳请求，更新该连接的最后心跳时间                        │       │
│  │ 2. 异步刷新 Redis 中的在线状态 TTL                               │       │
│  │ 3. 返回心跳响应，包含服务器时间戳                                │       │
│  │                                                                 │       │
│  │ 4. 后台线程每 10 秒检查一次所有连接                              │       │
│  │    ├── 如果连接超过 30 秒没有心跳                                │       │
│  │    │   └── 调用 kickCallback 踢出连接                            │       │
│  │    │       └── 从 Redis 删除在线状态                             │       │
│  │    └── 继续检查下一个连接                                        │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 心跳检测代码

```cpp
void HeartbeatService::checkHeartbeat()
{
    int64_t now = util::GetTimestampMs();
    std::vector<std::shared_ptr<TcpConnection>> toKick;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& pair : m_lastHeartbeatTime)
        {
            int64_t elapsed = now - pair.second;
            if (elapsed > m_timeoutSeconds * 1000)
            {
                auto connIt = m_connections.find(pair.first);
                if (connIt != m_connections.end())
                {
                    toKick.push_back(connIt->second);
                }
            }
        }
    }

    for (const auto& conn : toKick)
    {
        LOG_WARN("HeartbeatService: kicking connection {} due to timeout", conn->name());
        if (m_kickCallback)
        {
            m_kickCallback(conn, "heartbeat timeout");
        }
    }
}
```

---

## 八、Redis 集成详解

### 8.1 Redis 在系统中的角色

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Redis 在系统中的定位                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 会话存储 (Session Storage)                                              │
│     ┌─────────────────────────────────────────────────────────────────┐    │
│     │ Key: session:{sessionId}                                        │    │
│     │ Value: JSON {userId, sessionId, deviceId, loginTime, ...}       │    │
│     │ TTL: 86400s (24小时)                                            │    │
│     └─────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  2. 用户会话映射 (User Session Mapping)                                     │
│     ┌─────────────────────────────────────────────────────────────────┐    │
│     │ Key: user:session:{userId}                                      │    │
│     │ Value: sessionId                                                │    │
│     │ TTL: 86400s (24小时)                                            │    │
│     └─────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  3. 在线状态 (Online Status)                                                │
│     ┌─────────────────────────────────────────────────────────────────┐    │
│     │ Key: user:online:{userId}                                       │    │
│     │ Value: "1"                                                      │    │
│     │ TTL: 30s (心跳超时时间)                                          │    │
│     └─────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 8.2 Redis 操作流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Redis 操作流程                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  用户登录：                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. SETEX session:{sessionId} {sessionData} 86400                │       │
│  │ 2. SETEX user:session:{userId} {sessionId} 86400                │       │
│  │ 3. SETEX user:online:{userId} "1" 30                            │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  心跳刷新：                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ SETEX user:online:{userId} "1" 30                               │       │
│  │ (异步执行，不阻塞主线程)                                         │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  用户登出：                                                                  │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ 1. DEL session:{sessionId}                                      │       │
│  │ 2. DEL user:session:{userId}                                    │       │
│  │ 3. DEL user:online:{userId}                                     │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
│  查询在线状态：                                                              │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ EXISTS user:online:{userId}                                     │       │
│  │ 返回 1 表示在线，0 表示离线                                      │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 8.3 分布式部署架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          分布式部署架构                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│                         ┌─────────────┐                                     │
│                         │   客户端    │                                     │
│                         └──────┬──────┘                                     │
│                                │                                            │
│                    ┌───────────┴───────────┐                               │
│                    │                       │                                │
│                    ▼                       ▼                                │
│            ┌─────────────┐         ┌─────────────┐                         │
│            │  Server A   │         │  Server B   │                         │
│            │  (用户1登录) │         │  (用户2登录) │                         │
│            │             │         │             │                         │
│            │  本地会话    │         │  本地会话    │                         │
│            │  本地心跳    │         │  本地心跳    │                         │
│            └──────┬──────┘         └──────┬──────┘                         │
│                   │                       │                                 │
│                   └───────────┬───────────┘                                │
│                               │                                             │
│                               ▼                                             │
│                       ┌─────────────┐                                       │
│                       │    Redis    │                                       │
│                       │             │                                       │
│                       │ 共享会话    │                                       │
│                       │ 共享在线态  │                                       │
│                       └─────────────┘                                       │
│                                                                             │
│  跨服务器查询流程：                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐       │
│  │ Server A 需要给用户2发消息                                       │       │
│  │ 1. 查询 Redis: EXISTS user:online:user2                         │       │
│  │ 2. 返回 1，用户2在线                                             │       │
│  │ 3. 查询 Redis: GET user:session:user2                           │       │
│  │ 4. 获取 sessionId                                               │       │
│  │ 5. 通过消息队列或其他方式通知 Server B 转发消息                   │       │
│  └─────────────────────────────────────────────────────────────────┘       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 九、完整数据流

### 9.1 用户登录完整流程

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              用户登录完整数据流                                           │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                         │
│  Client                     Server                    AuthService      Redis              │
│    │                          │                           │              │                │
│    │  1. connect()            │                           │              │                │
│    │ ────────────────────────>│                           │              │                │
│    │                          │                           │              │                │
│    │                          │  2. newConnection()       │              │                │
│    │                          │    创建 TcpConnection     │              │                │
│    │                          │    注册到 heartbeatService│              │                │
│    │                          │                           │              │                │
│    │  3. LoginReq             │                           │              │                │
│    │ ────────────────────────>│                           │              │                │
│    │                          │                           │              │                │
│    │                          │  4. Codec::onMessage()    │              │                │
│    │                          │    解析协议头              │              │                │
│    │                          │    解析 JSON 负载         │              │                │
│    │                          │                           │              │                │
│    │                          │  5. login() ────────────> │              │                │
│    │                          │                           │              │                │
│    │                          │                           │  6. validateToken()            │
│    │                          │                           │              │                │
│    │                          │                           │  7. generateSessionId()        │
│    │                          │                           │              │                │
│    │                          │                           │  8. 本地存储                   │
│    │                          │                           │    m_connToUser               │
│    │                          │                           │    m_userToContext            │
│    │                          │                           │              │                │
│    │                          │                           │  9. Redis 存储 ──────────────>│
│    │                          │                           │    SETEX session:{sid}        │
│    │                          │                           │    SETEX user:session:{uid}   │
│    │                          │                           │              │                │
│    │                          │  10. callback(true, sid)  │              │                │
│    │                          │ <──────────────────────── │              │                │
│    │                          │                           │              │                │
│    │                          │  11. registerUserId()     │              │                │
│    │                          │    heartbeatService       │              │                │
│    │                          │                           │              │                │
│    │                          │  12. SETEX ─────────────────────────────────────────────> │
│    │                          │      user:online:{uid}    │              │                │
│    │                          │                           │              │                │
│    │  13. LoginResp           │                           │              │                │
│    │ <──────────────────────── │                           │              │                │
│    │                          │                           │              │                │
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

### 9.2 心跳处理完整流程

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                              心跳处理完整数据流                                           │
├─────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                         │
│  Client                     Server                    HeartbeatService   Redis           │
│    │                          │                           │               │               │
│    │  1. HeartbeatReq         │                           │               │               │
│    │ ────────────────────────>│                           │               │               │
│    │                          │                           │               │               │
│    │                          │  2. onHeartbeat() ──────> │               │               │
│    │                          │                           │               │               │
│    │                          │                           │  3. 更新本地   │               │
│    │                          │                           │    心跳时间    │               │
│    │                          │                           │               │               │
│    │                          │                           │  4. 异步刷新 ────────────────> │
│    │                          │                           │    (线程池)    │               │
│    │                          │                           │               │ SETEX         │
│    │                          │                           │               │ user:online   │
│    │                          │                           │               │               │
│    │  5. HeartbeatResp        │                           │               │               │
│    │ <──────────────────────── │                           │               │               │
│    │                          │                           │               │               │
│                                                                                         │
│  ─────────────────────────────────────────────────────────────────────────────────────  │
│                                                                                         │
│  超时检测（独立线程，每 10 秒执行一次）：                                                 │
│                                                                                         │
│    │                          │                           │               │               │
│    │                          │                           │  checkHeartbeat()             │
│    │                          │                           │               │               │
│    │                          │                           │  for each conn:               │
│    │                          │                           │    if (超时):                 │
│    │                          │                           │      kickCallback()           │
│    │                          │                           │      异步删除 ──────────────> │
│    │                          │                           │        DEL user:online        │
│    │                          │                           │               │               │
│    │  6. 连接被关闭           │                           │               │               │
│    │ <──────────────────────── │                           │               │               │
│    │                          │                           │               │               │
│                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 十、总结

### 10.1 技术栈总结

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              技术栈总结                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  网络层：                                                                    │
│  ├── epoll I/O 多路复用                                                    │
│  ├── Reactor 事件驱动模式                                                   │
│  ├── 非阻塞 I/O                                                            │
│  └── 应用层缓冲区                                                          │
│                                                                             │
│  并发模型：                                                                  │
│  ├── one loop per thread                                                   │
│  ├── 线程池处理耗时任务                                                     │
│  └── 事件循环 + 任务队列                                                    │
│                                                                             │
│  协议设计：                                                                  │
│  ├── 自定义二进制协议                                                       │
│  ├── 16 字节固定头部 + 变长负载                                             │
│  ├── CRC16 校验                                                            │
│  └── JSON 序列化                                                           │
│                                                                             │
│  业务层：                                                                    │
│  ├── 服务化架构                                                            │
│  ├── 回调机制                                                              │
│  └── 线程安全设计                                                          │
│                                                                             │
│  存储层：                                                                    │
│  ├── Redis 分布式会话存储                                                  │
│  ├── Redis 在线状态共享                                                    │
│  └── TTL 自动过期                                                          │
│                                                                             │
│  工程实践：                                                                  │
│  ├── 条件编译                                                              │
│  ├── 配置文件管理                                                          │
│  ├── 日志系统                                                              │
│  └── 信号处理                                                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 10.2 设计亮点

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              设计亮点                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 高性能网络架构                                                          │
│     • 基于 epoll 的 Reactor 模式                                           │
│     • 零拷贝发送（输出缓冲区）                                              │
│     • 批量事件处理                                                          │
│                                                                             │
│  2. 灵活的扩展性                                                            │
│     • 服务化架构，易于扩展新业务                                            │
│     • 条件编译，支持可选功能                                                │
│     • 配置驱动，无需重新编译                                                │
│                                                                             │
│  3. 可靠性保障                                                              │
│     • 心跳检测，自动清理无效连接                                            │
│     • 过载保护，防止系统崩溃                                                │
│     • 优雅关闭，确保数据完整                                                │
│                                                                             │
│  4. 分布式支持                                                              │
│     • Redis 会话存储，支持服务器重启恢复                                    │
│     • Redis 在线状态，支持跨服务器查询                                      │
│     • TTL 机制，自动清理过期数据                                            │
│                                                                             │
│  5. 工程化实践                                                              │
│     • 完善的日志系统                                                        │
│     • 清晰的代码结构                                                        │
│     • 详细的文档说明                                                        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 10.3 扩展方向

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              扩展方向                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 性能优化                                                                │
│     • Redis 连接池                                                         │
│     • 内存池                                                                │
│     • 无锁队列                                                              │
│                                                                             │
│  2. 功能扩展                                                                │
│     • 消息持久化                                                            │
│     • 离线消息                                                              │
│     • 群组聊天                                                              │
│     • 文件传输                                                              │
│                                                                             │
│  3. 高可用                                                                  │
│     • Redis 集群                                                           │
│     • 服务器集群                                                            │
│     • 负载均衡                                                              │
│     • 故障转移                                                              │
│                                                                             │
│  4. 安全增强                                                                │
│     • TLS/SSL 加密                                                         │
│     • 消息加密                                                              │
│     • 访问控制                                                              │
│                                                                             │
│  5. 监控运维                                                                │
│     • Prometheus 指标                                                      │
│     • 分布式追踪                                                            │
│     • 告警系统                                                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## （本章完）
