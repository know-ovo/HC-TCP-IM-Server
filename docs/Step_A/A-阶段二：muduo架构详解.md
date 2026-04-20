# 阶段二：muduo 网络库架构详解

## 目录

- [1. 概述](#1-概述)
- [2. 整体架构设计](#2-整体架构设计)
- [3. 基础组件详解](#3-基础组件详解)
- [4. Channel 类详解](#4-channel-类详解)
- [5. EpollPoller 类详解](#5-epollpoller-类详解)
- [6. EventLoop 类详解](#6-eventloop-类详解)
- [7. TcpConnection 类详解](#7-tcpconnection-类详解)
- [8. TcpServer 类详解](#8-tcpserver-类详解)
- [9. 数据流完整流程](#9-数据流完整流程)
- [10. 设计模式与最佳实践](#10-设计模式与最佳实践)

---

## 1. 概述

muduo 是陈硕开发的高性能 C++ 网络库，采用 **Reactor 模式** 和 **one loop per thread** 的设计理念。本模块实现了 muduo 的核心组件，包括事件循环、I/O 多路复用、事件分发器、TCP连接管理和服务器框架。

### 1.1 模块职责

| 组件                    | 文件                | 职责                           |
| ----------------------- | ------------------- | ------------------------------ |
| **NonCopyable**   | nonCopyable.h       | 禁止对象拷贝的基类             |
| **Timestamp**     | timestamp.h/cpp     | 微秒级时间戳，用于事件时间记录 |
| **CurrentThread** | currentThread.h/cpp | 线程局部存储，缓存线程 ID      |
| **InetAddress**   | inetAddress.h       | 网络地址封装，支持 IPv4        |
| **Channel**       | channel.h/cpp       | 文件描述符的事件管理器         |
| **EpollPoller**   | epollPoller.h/cpp   | epoll 封装，I/O 多路复用       |
| **EventLoop**     | eventLoop.h/cpp     | 事件循环核心，Reactor 模式实现 |
| **TcpConnection** | tcpConnection.h/cpp | TCP 连接管理，数据收发         |
| **TcpServer**     | tcpServer.h/cpp     | TCP 服务器，接受新连接         |

### 1.2 设计目标

1. **高性能**：基于 epoll 的 I/O 多路复用，支持高并发连接
2. **线程安全**：每个 EventLoop 只属于一个线程，避免锁竞争
3. **可扩展**：支持跨线程调用，通过 eventfd 实现线程间通信
4. **事件驱动**：基于回调机制，非阻塞 I/O

### 1.3 文件结构

```
src/net/
├── nonCopyable.h       # 禁止拷贝基类
├── timestamp.h/cpp     # 时间戳类
├── currentThread.h/cpp # 线程 ID 缓存
├── inetAddress.h       # 网络地址封装
├── channel.h/cpp       # 事件通道
├── epollPoller.h/cpp   # epoll 封装
├── eventLoop.h/cpp     # 事件循环
├── tcpConnection.h/cpp # TCP 连接管理
└── tcpServer.h/cpp     # TCP 服务器
```

---

## 2. 整体架构设计

### 2.1 Reactor 模式架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          Reactor 模式架构                                   │
└─────────────────────────────────────────────────────────────────────────────┘

                            ┌─────────────────────┐
                            │      TcpServer      │
                            │   (服务器入口)      │
                            │                     │
                            │  - listenfd         │
                            │  - acceptChannel    │
                            └──────────┬──────────┘
                                       │ accept()
                                       ▼
                            ┌─────────────────────┐
                            │      EventLoop      │
                            │    (Reactor 核心)   │
                            │                     │
                            │  ┌───────────────┐  │
                            │  │    loop()     │  │
                            │  │  事件主循环   │  │
                            │  └───────────────┘  │
                            │         │           │
                            │         ▼           │
                            │  ┌───────────────┐  │
                            │  │ EpollPoller   │  │
                            │  │  I/O 多路复用 │  │
                            │  └───────────────┘  │
                            │         │           │
                            │         ▼           │
                            │  ┌───────────────┐  │
                            │  │ Channel List  │  │
                            │  │  事件分发器   │  │
                            │  └───────────────┘  │
                            └─────────────────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                    ▼                 ▼                 ▼
            ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
            │TcpConnection│   │TcpConnection│   │  wakeupCh   │
            │  (client1)  │   │  (client2)  │   │  (eventfd)  │
            │             │   │             │   │             │
            │ Channel     │   │ Channel     │   │ read_cb_    │
            │ inputBuffer │   │ inputBuffer │   │             │
            │outputBuffer │   │outputBuffer │   │             │
            └─────────────┘   └─────────────┘   └─────────────┘
```

### 2.2 核心组件关系

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          核心组件关系图                                     │
└─────────────────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────────┐
│                              TcpServer                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ - loop_: EventLoop*           (事件循环)                            │   │
│  │ - listenfd_: int              (监听套接字)                          │   │
│  │ - acceptChannel_: Channel*    (接受连接的 Channel)                  │   │
│  │ - connections_: map<string, TcpConnectionPtr>  (连接表)             │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                      │                                     │
│                    newConnection()   │   removeConnection()                │
│                         ┌────────────┴────────────┐                        │
│                         ▼                         ▼                        │
│              ┌──────────────────┐     ┌──────────────────┐                 │
│              │  TcpConnection   │     │  TcpConnection   │                 │
│              │                  │     │                  │                 │
│              │ - loop_          │     │ - loop_          │                 │
│              │ - sockfd_        │     │ - sockfd_        │                 │
│              │ - channel_       │     │ - channel_       │                 │
│              │ - inputBuffer_   │     │ - inputBuffer_   │                 │
│              │ - outputBuffer_  │     │ - outputBuffer_  │                 │
│              └────────┬─────────┘     └────────┬─────────┘                 │
│                       │                        │                           │
└───────────────────────┼────────────────────────┼───────────────────────────┘
                        │                        │
                        ▼                        ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              EventLoop                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ - poller_: EpollPoller*       (I/O 多路复用)                        │    │
│  │ - wakeupFd_: int              (线程间通信)                          │    │
│  │ - wakeupChannel_: Channel*    (唤醒通道)                            │    │
│  │ - pendingFunctors_: vector<Functor>  (待执行任务)                   │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                      │                                      │
│                     poll() / updateChannel() / removeChannel()              │
│                                      │                                      │
└──────────────────────────────────────┼──────────────────────────────────────┘
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                             EpollPoller                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │ - epollfd_: int               (epoll 实例)                          │    │
│  │ - events_: vector<epoll_event> (事件数组)                           │    │
│  │ - channels_: map<int, Channel*> (fd 到 Channel 的映射)              │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                      │                                      │
│                     epoll_ctl(ADD/MOD/DEL) / epoll_wait()                   │
│                                      │                                      │
└──────────────────────────────────────┼──────────────────────────────────────┘
                                       │
                                       ▼
┌────────────────────────────────────────────────────────────────────────────┐
│                               Channel                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ - fd_: int                    (文件描述符)                          │   │
│  │ - events_: int                (关注的事件)                          │   │
│  │ - revents_: int               (返回的事件)                          │   │
│  │ - readCallback_               (读回调)                              │   │
│  │ - writeCallback_              (写回调)                              │   │
│  │ - closeCallback_              (关闭回调)                            │   │
│  │ - errorCallback_              (错误回调)                            │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘
```

### 2.3 数据流图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              数据流图                                       │
└─────────────────────────────────────────────────────────────────────────────┘

客户端连接请求：
┌─────────┐    SYN     ┌─────────┐   accept()   ┌──────────────┐
│ Client  │ ─────────> │TcpServer│ ──────────>  │TcpConnection │
└─────────┘            └─────────┘              └──────────────┘
                                                        │
                                          enableReading()
                                                        ▼
                                                 ┌─────────────┐
                                                 │   Channel   │
                                                 │  (可读事件) │
                                                 └─────────────┘
                                                        │
                                                   epoll_wait()
                                                        ▼
                                                 ┌─────────────┐
                                                 │  EventLoop  │
                                                 │  handleRead │
                                                 └─────────────┘

数据接收流程：
┌─────────┐   data    ┌──────────────┐   readFd()   ┌─────────────┐
│ Client  │ ────────> │TcpConnection │ ───────────> │ inputBuffer │
└─────────┘           └──────────────┘              └─────────────┘
                            │                              │
                      handleRead()                  messageCallback
                            │                              │
                            ▼                              ▼
                      ┌─────────────┐              ┌─────────────┐
                      │   Channel   │              │   Codec     │
                      │  (EPOLLIN)  │              │  (解码器)   │
                      └─────────────┘              └─────────────┘

数据发送流程：
┌─────────────┐   send()   ┌──────────────┐   write()    ┌─────────┐
│   Codec     │ ────────>  │TcpConnection │ ───────────> │ Client  │
│  (编码器)   │            └──────────────┘              └─────────┘
└─────────────┘                  │
                          sendInLoop()
                                 │
                                 ▼
                          ┌─────────────┐
                          │outputBuffer │
                          │ (写缓冲区)  │
                          └─────────────┘
                                 │
                        enableWriting()
                                 │
                                 ▼
                          ┌─────────────┐
                          │   Channel   │
                          │ (EPOLLOUT)  │
                          └─────────────┘
```

---

## 3. 基础组件详解

### 3.1 NonCopyable 类

**源码位置**：[nonCopyable.h](file:///d:/桌面/Linux+C++服务器开发/src/net/nonCopyable.h)

```cpp
#pragma once

class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

private:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};
```

**设计思路：**

| 特性                    | 说明                       |
| ----------------------- | -------------------------- |
| `protected` 构造/析构 | 不能直接实例化，只能被继承 |
| `= delete` 拷贝操作   | 禁止拷贝，防止对象复制     |

**为什么需要禁止拷贝？**

```
muduo 中的类大多是语义对象：

EventLoop：
  - 代表一个事件循环
  - 拷贝没有意义，可能导致资源重复管理

Channel：
  - 代表一个文件描述符的事件
  - 拷贝会导致同一个 fd 被多个 Channel 管理

TcpConnection：
  - 代表一个 TCP 连接
  - 拷贝会导致同一个 socket 被多次关闭
```

### 3.2 Timestamp 类

**源码位置**：[timestamp.h](file:///d:/桌面/Linux+C++服务器开发/src/net/timestamp.h) | [timestamp.cpp](file:///d:/桌面/Linux+C++服务器开发/src/net/timestamp.cpp)

#### 3.2.1 类定义

```cpp
class Timestamp
{
public:
    Timestamp() : m_microSecondsSinceEpoch(0) {}
    explicit Timestamp(int64_t microSeconds)
        : m_microSecondsSinceEpoch(microSeconds) {}

    static Timestamp now();
    static Timestamp invalid() { return Timestamp(); }

    std::string toString() const;
    std::string toFormattedString(bool showMicroseconds = true) const;

    bool valid() const { return m_microSecondsSinceEpoch > 0; }

    int64_t microSecondsSinceEpoch() const { return m_microSecondsSinceEpoch; }
    int64_t secondsSinceEpoch() const
    {
        return static_cast<int64_t>(m_microSecondsSinceEpoch / kMicroSecondsPerSecond);
    }

    static const int64_t kMicroSecondsPerSecond = 1000 * 1000;

private:
    int64_t m_microSecondsSinceEpoch;
};
```

#### 3.2.2 核心实现

```cpp
Timestamp Timestamp::now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t seconds = tv.tv_sec;
    return Timestamp(seconds * kMicroSecondsPerSecond + tv.tv_usec);
}

std::string Timestamp::toFormattedString(bool showMicroseconds) const
{
    char buf[64] = {0};
    time_t seconds = static_cast<time_t>(m_microSecondsSinceEpoch / kMicroSecondsPerSecond);
    struct tm tm_time;
    localtime_r(&seconds, &tm_time);  // 线程安全版本

    if (showMicroseconds)
    {
        int microseconds = static_cast<int>(m_microSecondsSinceEpoch % kMicroSecondsPerSecond);
        snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
                 microseconds);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    }
    return buf;
}
```

**设计要点：**

| 特性              | 说明                                        |
| ----------------- | ------------------------------------------- |
| 微秒精度          | 使用 `int64_t` 存储微秒，精度足够且范围大 |
| `explicit` 构造 | 防止隐式转换                                |
| `localtime_r`   | 线程安全版本，多线程环境下安全              |
| 比较操作符        | 支持 `<`、`==`，可用于排序和比较        |

### 3.3 CurrentThread 类

**源码位置**：[currentThread.h](file:///d:/桌面/Linux+C++服务器开发/src/net/currentThread.h) | [currentThread.cpp](file:///d:/桌面/Linux+C++服务器开发/src/net/currentThread.cpp)

#### 3.3.1 类定义

```cpp
namespace CurrentThread
{

extern __thread int t_cached_tid;

void cacheTid();

inline int tid()
{
    if (__builtin_expect(t_cached_tid == 0, 0))
    {
        cacheTid();
    }
    return t_cached_tid;
}

} // namespace CurrentThread
```

#### 3.3.2 核心实现

```cpp
namespace CurrentThread
{

__thread int t_cached_tid = 0;

void cacheTid()
{
    if (t_cached_tid == 0)
    {
        t_cached_tid = static_cast<int>(::syscall(SYS_gettid));
    }
}

} // namespace CurrentThread
```

**设计要点：**

| 特性                 | 说明                                                         |
| -------------------- | ------------------------------------------------------------ |
| `__thread`         | 线程局部存储（TLS），每个线程独立一份变量                    |
| 缓存 tid             | `syscall(SYS_gettid)` 是系统调用，开销大，缓存后只调用一次 |
| `__builtin_expect` | 分支预测优化，告诉编译器 `t_cached_tid != 0` 是大概率事件  |

**为什么用 `__thread`？**

```
普通全局变量：
┌─────────────────────────────────────────────────────────────┐
│  int tid;  // 所有线程共享同一个变量                          │
│                                                             │
│  Thread1: tid = 100;                                        │
│  Thread2: tid = 200;  // 覆盖了 Thread1 的值！               │
└─────────────────────────────────────────────────────────────┘

线程局部存储：
┌─────────────────────────────────────────────────────────────┐
│  __thread int tid;  // 每个线程有独立的副本                   │
│                                                             │
│  Thread1: tid = 100;  // Thread1 的 tid                     │
│  Thread2: tid = 200;  // Thread2 的 tid（独立存储）          │
└─────────────────────────────────────────────────────────────┘
```

### 3.4 InetAddress 类

**源码位置**：[inetAddress.h](file:///d:/桌面/Linux+C++服务器开发/src/net/inetAddress.h)

#### 3.4.1 类定义

```cpp
class InetAddress
{
public:
    InetAddress(uint16_t port)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_addr.s_addr = INADDR_ANY;
        m_addr.sin_port = htons(port);
    }

    InetAddress(const std::string& ip, uint16_t port)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &m_addr.sin_addr);
    }

    InetAddress(const struct sockaddr_in& addr)
        : m_addr(addr)
    {
    }

    const struct sockaddr_in& getSockAddrInet() const { return m_addr; }
    void setSockAddrInet(const struct sockaddr_in& addr) { m_addr = addr; }

    std::string toIp() const;
    std::string toIpPort() const;
    uint16_t port() const { return ntohs(m_addr.sin_port); }

private:
    struct sockaddr_in m_addr;
};
```

**设计要点：**

| 构造函数                                  | 用途                         |
| ----------------------------------------- | ---------------------------- |
| `InetAddress(uint16_t port)`            | 服务器监听地址，监听所有网卡 |
| `InetAddress(string ip, uint16_t port)` | 指定 IP 和端口               |
| `InetAddress(sockaddr_in addr)`         | 从系统结构体构造             |

---

## 4. Channel 类详解

**源码位置**：[channel.h](file:///d:/桌面/Linux+C++服务器开发/src/net/channel.h) | [channel.cpp](file:///d:/桌面/Linux+C++服务器开发/src/net/channel.cpp)

### 4.1 类定义

```cpp
class Channel : NonCopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    void handleEvent(Timestamp receiveTime);

    void setReadCallback(ReadEventCallback cb) { m_readCallback = std::move(cb); }
    void setWriteCallback(EventCallback cb) { m_writeCallback = std::move(cb); }
    void setCloseCallback(EventCallback cb) { m_closeCallback = std::move(cb); }
    void setErrorCallback(EventCallback cb) { m_errorCallback = std::move(cb); }

    int fd() const { return m_fd; }
    int events() const { return m_events; }
    void setRevents(int revt) { m_revents = revt; }
    bool isNoneEvent() const { return m_events == kNoneEvent; }

    void enableReading() { m_events |= kReadEvent; update(); }
    void disableReading() { m_events &= ~kReadEvent; update(); }
    void enableWriting() { m_events |= kWriteEvent; update(); }
    void disableWriting() { m_events &= ~kWriteEvent; update(); }
    void disableAll() { m_events = kNoneEvent; update(); }

    bool isWriting() const { return m_events & kWriteEvent; }
    bool isReading() const { return m_events & kReadEvent; }

    int index() { return m_index; }
    void setIndex(int idx) { m_index = idx; }

    EventLoop* ownerLoop() { return m_loop; }
    void remove();

private:
    void update();

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* m_loop;
    const int m_fd;
    int m_events;
    int m_revents;
    int m_index;

    ReadEventCallback m_readCallback;
    EventCallback m_writeCallback;
    EventCallback m_closeCallback;
    EventCallback m_errorCallback;

    bool m_eventHandling;
    bool m_addedToLoop;
};
```

### 4.2 成员变量说明

| 变量                | 类型                  | 作用                                       |
| ------------------- | --------------------- | ------------------------------------------ |
| `m_loop`          | `EventLoop*`        | 所属的事件循环                             |
| `m_fd`            | `const int`         | 管理的文件描述符                           |
| `m_events`        | `int`               | 关注的事件（可读、可写等）                 |
| `m_revents`       | `int`               | epoll 返回的实际发生事件                   |
| `m_index`         | `int`               | 在 Poller 中的状态（kNew/kAdded/kDeleted） |
| `m_readCallback`  | `ReadEventCallback` | 可读事件回调                               |
| `m_writeCallback` | `EventCallback`     | 可写事件回调                               |
| `m_closeCallback` | `EventCallback`     | 连接关闭回调                               |
| `m_errorCallback` | `EventCallback`     | 错误事件回调                               |

### 4.3 事件类型定义

```cpp
const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;   // 普通数据 + 紧急数据
const int Channel::kWriteEvent = POLLOUT;
```

**事件类型说明：**

| 事件         | 值           | 说明             |
| ------------ | ------------ | ---------------- |
| `POLLIN`   | 普通数据可读 | 有数据可读       |
| `POLLPRI`  | 紧急数据可读 | TCP 带外数据     |
| `POLLOUT`  | 可写         | 发送缓冲区有空间 |
| `POLLHUP`  | 挂起         | 对方关闭连接     |
| `POLLERR`  | 错误         | 发生错误         |
| `POLLNVAL` | 无效         | fd 未打开        |

### 4.4 handleEvent 实现

```cpp
void Channel::handleEvent(Timestamp receiveTime)
{
    m_eventHandling = true;
  
    // 1. 无效 fd
    if (m_revents & POLLNVAL)
    {
        LOG_WARN("Channel::handleEvent() POLLNVAL");
    }
  
    // 2. 挂起事件（对方关闭连接）
    if ((m_revents & POLLHUP) && !(m_revents & POLLIN))
    {
        if (m_closeCallback)
            m_closeCallback();
    }
  
    // 3. 错误事件
    if (m_revents & (POLLERR | POLLNVAL))
    {
        if (m_errorCallback)
            m_errorCallback();
    }
  
    // 4. 可读事件
    if (m_revents & (POLLIN | POLLPRI | POLLRDHUP))
    {
        if (m_readCallback)
            m_readCallback(receiveTime);
    }
  
    // 5. 可写事件
    if (m_revents & POLLOUT)
    {
        if (m_writeCallback)
            m_writeCallback();
    }
  
    m_eventHandling = false;
}
```

**事件处理顺序：**

```
┌─────────────────────────────────────────────────────────────┐
│                    handleEvent 处理顺序                     │
└─────────────────────────────────────────────────────────────┘

1. POLLNVAL  → 警告日志（fd 无效）
2. POLLHUP   → closeCallback（对方关闭）
3. POLLERR   → errorCallback（发生错误）
4. POLLIN    → readCallback（有数据可读）
5. POLLOUT   → writeCallback（可写）
```

### 4.5 Channel 状态管理

```
┌─────────────────────────────────────────────────────────────┐
│                    Channel 状态转换                         │
└─────────────────────────────────────────────────────────────┘

                    ┌──────────────┐
                    │    kNew      │
                    │   (index=-1) │
                    └──────┬───────┘
                           │
                    enableReading()
                    enableWriting()
                           │
                           ▼
                    ┌──────────────┐
                    │   kAdded     │◄────────────────┐
                    │   (index=1)  │                 │
                    └──────┬───────┘                 │
                           │                         │
              ┌────────────┼────────────┐            │
              │            │            │            │
       disableAll()  modify events  keep watching    │
              │            │            │            │
              ▼            │            │            │
       ┌──────────────┐    │            │            │
       │   kDeleted   │    │            │            │
       │   (index=2)  │    │            │            │
       └──────┬───────┘    │            │            │
              │            │            │            │
       enableReading() ────┼────────────┘            │
              │            │                         │
              └────────────┼─────────────────────────┘
                           │
                    remove()
                           │
                           ▼
                    ┌──────────────┐
                    │    kNew      │
                    │   (index=-1) │
                    └──────────────┘
```

---

## 5. EpollPoller 类详解

**源码位置**：[epollPoller.h](file:///d:/桌面/Linux+C++服务器开发/src/net/epollPoller.h) | [epollPoller.cpp](file:///d:/桌面/Linux+C++服务器开发/src/net/epollPoller.cpp)

### 5.1 类定义

```cpp
class EpollPoller : NonCopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    explicit EpollPoller(EventLoop* loop);
    ~EpollPoller();

    Timestamp poll(int timeoutMs, ChannelList* activeChannels);

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    void assertInLoopThread() const;

private:
    static const int kInitEventListSize = 16;
    static const int kNew = -1;
    static const int kAdded = 1;
    static const int kDeleted = 2;

    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
    void update(int operation, Channel* channel);

    using EventList = std::vector<struct epoll_event>;
    using ChannelMap = std::map<int, Channel*>;

    EventLoop* m_ownerLoop;
    int m_epollfd;
    EventList m_events;
    ChannelMap m_channels;
};
```

### 5.2 核心实现

#### 5.2.1 构造与析构

```cpp
EpollPoller::EpollPoller(EventLoop* loop)
    : m_ownerLoop(loop),
      m_epollfd(::epoll_create1(EPOLL_CLOEXEC)),  // 创建 epoll 实例
      m_events(kInitEventListSize)                 // 初始事件数组大小
{
    if (m_epollfd < 0)
    {
        LOG_FATAL("EpollPoller::EpollPoller epoll_create1 failed");
    }
}

EpollPoller::~EpollPoller()
{
    ::close(m_epollfd);  // 关闭 epoll 实例
}
```

#### 5.2.2 poll 函数

```cpp
Timestamp EpollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    // 1. 调用 epoll_wait 等待事件
    int numEvents = ::epoll_wait(m_epollfd, m_events.data(),
                                   static_cast<int>(m_events.size()), timeoutMs);
    int savedErrno = errno;
    Timestamp now(Timestamp::now());
  
    if (numEvents > 0)
    {
        LOG_DEBUG("{} events happened", numEvents);
        fillActiveChannels(numEvents, activeChannels);
      
        // 2. 动态扩容：如果事件数组满了，扩大一倍
        if (static_cast<size_t>(numEvents) == m_events.size())
        {
            m_events.resize(m_events.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("nothing happened");
    }
    else
    {
        // 3. 错误处理（忽略 EINTR，即被信号中断）
        if (savedErrno != EINTR)
        {
            errno = savedErrno;
            LOG_ERROR("EpollPoller::poll() error: {}", strerror(errno));
        }
    }
  
    return now;
}
```

#### 5.2.3 fillActiveChannels 函数

```cpp
void EpollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    assert(static_cast<size_t>(numEvents) <= m_events.size());
  
    for (int i = 0; i < numEvents; ++i)
    {
        // 1. 从 epoll_event.data.ptr 获取 Channel 指针
        Channel* channel = static_cast<Channel*>(m_events[i].data.ptr);
      
        // 2. 设置返回的事件
        channel->setRevents(m_events[i].events);
      
        // 3. 添加到活跃列表
        activeChannels->push_back(channel);
    }
}
```

#### 5.2.4 updateChannel 函数

```cpp
void EpollPoller::updateChannel(Channel* channel)
{
    assertInLoopThread();
    const int index = channel->index();
  
    LOG_DEBUG("fd = {} events = {} index = {}", 
              channel->fd(), channel->events(), index);
  
    // 1. 新 Channel 或已删除的 Channel：添加到 epoll
    if (index == kNew || index == kDeleted)
    {
        int fd = channel->fd();
        if (index == kNew)
        {
            // 新 Channel：添加到映射表
            assert(m_channels.find(fd) == m_channels.end());
            m_channels[fd] = channel;
        }
        else
        {
            // 已删除的 Channel：确认在映射表中
            assert(m_channels.find(fd) != m_channels.end());
            assert(m_channels[fd] == channel);
        }
        channel->setIndex(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
    // 2. 已添加的 Channel：修改或删除
    else
    {
        int fd = channel->fd();
        assert(m_channels.find(fd) != m_channels.end());
        assert(m_channels[fd] == channel);
        assert(index == kAdded);
      
        if (channel->isNoneEvent())
        {
            // 不再关注任何事件：从 epoll 删除
            update(EPOLL_CTL_DEL, channel);
            channel->setIndex(kDeleted);
        }
        else
        {
            // 修改关注的事件
            update(EPOLL_CTL_MOD, channel);
        }
    }
}
```

#### 5.2.5 update 函数（epoll_ctl 封装）

```cpp
void EpollPoller::update(int operation, Channel* channel)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->events();      // 关注的事件
    event.data.ptr = channel;              // 绑定 Channel 指针
  
    int fd = channel->fd();
  
    LOG_DEBUG("epoll_ctl op = {}, fd = {}",
              (operation == EPOLL_CTL_ADD ? "ADD" : 
               (operation == EPOLL_CTL_MOD ? "MOD" : "DEL")),
              fd);
  
    if (::epoll_ctl(m_epollfd, operation, fd, &event) < 0)
    {
        LOG_ERROR("epoll_ctl op={} fd={} failed: {}", operation, fd, strerror(errno));
    }
}
```

### 5.3 epoll 事件数组动态扩容

```
┌─────────────────────────────────────────────────────────────┐
│                    动态扩容机制                             │
└─────────────────────────────────────────────────────────────┘

初始状态：
  m_events.size() = 16

当 numEvents == m_events.size() 时扩容：
  
  ┌─────────────────────────────────────────────┐
  │  events[0]  events[1] ... events[15]        │
  │     ↓          ↓            ↓               │
  │   Channel1  Channel2 ... Channel16          │
  └─────────────────────────────────────────────┘
                    │
                    │ 扩容
                    ▼
  ┌─────────────────────────────────────────────┐
  │  events[0] ... events[15] events[16]...[31] │
  │     ↓           ↓            ↓              │
  │   Channel1 ... Channel16    (空闲)          │
  └─────────────────────────────────────────────┘

优点：
1. 避免频繁内存分配
2. 按需扩容，不浪费内存
3. 扩容策略简单高效
```

---

## 6. EventLoop 类详解

**源码位置**：[eventLoop.h](file:///d:/桌面/Linux+C++服务器开发/src/net/eventLoop.h) | [eventLoop.cpp](file:///d:/桌面/Linux+C++服务器开发/src/net/eventLoop.cpp)

### 6.1 类定义

```cpp
class EventLoop : NonCopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop();
    void quit();

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    void assertInLoopThread() const;
    bool isInLoopThread() const;

    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    void wakeup();

    Timestamp pollReturnTime() const { return m_pollReturnTime; }

    static EventLoop* getEventLoopOfCurrentThread();

private:
    void abortNotInLoopThread() const;
    void handleRead();
    void doPendingFunctors();

    using ChannelList = std::vector<Channel*>;

    std::atomic<bool> m_looping;
    std::atomic<bool> m_quit;
    std::atomic<bool> m_callingPendingFunctors;

    const pid_t m_threadId;
    Timestamp m_pollReturnTime;

    std::unique_ptr<EpollPoller> m_poller;
    ChannelList m_activeChannels;
    Channel* m_currentActiveChannel;

    int m_wakeupFd;
    std::unique_ptr<Channel> m_wakeupChannel;

    std::mutex m_mutex;
    std::vector<Functor> m_pendingFunctors;

    static const int kPollTimeoutMs = 10000;
};
```

### 6.2 核心成员变量

| 变量                  | 类型                        | 作用                    |
| --------------------- | --------------------------- | ----------------------- |
| `m_looping`         | `atomic<bool>`            | 是否正在循环            |
| `m_quit`            | `atomic<bool>`            | 是否退出                |
| `m_threadId`        | `const pid_t`             | 所属线程 ID             |
| `m_poller`          | `unique_ptr<EpollPoller>` | I/O 多路复用器          |
| `m_wakeupFd`        | `int`                     | eventfd，用于线程间通信 |
| `m_wakeupChannel`   | `unique_ptr<Channel>`     | wakeupFd 的 Channel     |
| `m_pendingFunctors` | `vector<Functor>`         | 待执行的任务队列        |

### 6.3 核心实现

#### 6.3.1 构造函数

```cpp
__thread EventLoop* t_loop_in_this_thread = nullptr;

EventLoop::EventLoop()
    : m_looping(false),
      m_quit(false),
      m_callingPendingFunctors(false),
      m_threadId(CurrentThread::tid()),
      m_poller(new EpollPoller(this)),
      m_currentActiveChannel(nullptr)
{
    LOG_DEBUG("EventLoop created {} in thread {}", 
              static_cast<const void*>(this), m_threadId);
  
    // 1. 检查当前线程是否已有 EventLoop
    if (t_loop_in_this_thread)
    {
        LOG_FATAL("Another EventLoop {} exists in this thread {}",
                  static_cast<const void*>(t_loop_in_this_thread), m_threadId);
    }
    else
    {
        t_loop_in_this_thread = this;
    }
  
    // 2. 创建 eventfd 用于线程间通信
    m_wakeupFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeupFd < 0)
    {
        LOG_FATAL("EventLoop::EventLoop eventfd failed");
    }
  
    // 3. 创建 wakeupChannel 并注册读事件
    m_wakeupChannel.reset(new Channel(this, m_wakeupFd));
    m_wakeupChannel->setReadCallback(std::bind(&EventLoop::handleRead, this));
    m_wakeupChannel->enableReading();
}
```

#### 6.3.2 loop 函数

```cpp
void EventLoop::loop()
{
    assert(!m_looping);
    assertInLoopThread();
    m_looping = true;
    m_quit = false;
  
    LOG_INFO("EventLoop {} start looping", static_cast<const void*>(this));
  
    while (!m_quit)
    {
        // 1. 清空活跃 Channel 列表
        m_activeChannels.clear();
      
        // 2. 调用 poll 等待事件
        m_pollReturnTime = m_poller->poll(kPollTimeoutMs, &m_activeChannels);
      
        // 3. 处理活跃 Channel 的事件
        for (Channel* channel : m_activeChannels)
        {
            m_currentActiveChannel = channel;
            m_currentActiveChannel->handleEvent(m_pollReturnTime);
        }
        m_currentActiveChannel = nullptr;
      
        // 4. 执行待处理的任务
        doPendingFunctors();
    }
  
    LOG_INFO("EventLoop {} stop looping", static_cast<const void*>(this));
    m_looping = false;
}
```

#### 6.3.3 runInLoop 和 queueInLoop

```cpp
void EventLoop::runInLoop(Functor cb)
{
    // 如果在 IO 线程，直接执行
    if (isInLoopThread())
    {
        cb();
    }
    // 否则放入队列
    else
    {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    // 1. 加锁添加任务
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingFunctors.push_back(std::move(cb));
    }
  
    // 2. 唤醒 EventLoop
    // - 不在 IO 线程：需要唤醒
    // - 正在执行任务：需要唤醒（因为可能在执行任务期间有新任务加入）
    if (!isInLoopThread() || m_callingPendingFunctors)
    {
        wakeup();
    }
}
```

#### 6.3.4 wakeup 和 handleRead

```cpp
void EventLoop::wakeup()
{
    // 写入 8 字节数据，唤醒 eventfd
    uint64_t one = 1;
    ssize_t n = ::write(m_wakeupFd, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup writes {} bytes instead of 8", n);
    }
}

void EventLoop::handleRead()
{
    // 读取 eventfd 数据，清除可读状态
    uint64_t one = 1;
    ssize_t n = ::read(m_wakeupFd, &one, sizeof(one));
    if (n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead reads {} bytes instead of 8", n);
    }
}
```

#### 6.3.5 doPendingFunctors

```cpp
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    m_callingPendingFunctors = true;
  
    // 1. 交换而非复制，减少临界区时间
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        functors.swap(m_pendingFunctors);
    }
  
    // 2. 执行所有任务
    for (const Functor& functor : functors)
    {
        functor();
    }
  
    m_callingPendingFunctors = false;
}
```

### 6.4 线程间通信机制

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          线程间通信机制                                     │
└─────────────────────────────────────────────────────────────────────────────┘

                    ┌─────────────────────────────────────┐
                    │           工作线程                  │
                    │                                     │
                    │  void someTask() {                  │
                    │      loop->runInLoop(callback);     │
                    │  }                                  │
                    └──────────────┬──────────────────────┘
                                   │
                                   │ runInLoop()
                                   ▼
                    ┌─────────────────────────────────────┐
                    │           任务队列                  │
                    │                                     │
                    │  pendingFunctors_.push_back(cb);    │
                    └──────────────┬──────────────────────┘
                                   │
                                   │ wakeup()
                                   ▼
                    ┌─────────────────────────────────────┐
                    │           eventfd                   │
                    │                                     │
                    │  write(wakeupFd_, 1);               │
                    └──────────────┬──────────────────────┘
                                   │
                                   │ epoll_wait() 返回
                                   ▼
                    ┌─────────────────────────────────────┐
                    │           IO 线程                   │
                    │                                     │
                    │  EventLoop::loop() {                │
                    │      poll();                        │
                    │      handleEvent();                 │
                    │      doPendingFunctors();  // 执行  │
                    │  }                                  │
                    └─────────────────────────────────────┘

关键点：
1. eventfd 是线程间通信的高效机制
2. 写入任意数据即可唤醒 epoll_wait
3. 读取数据清除可读状态
```

---

## 7. TcpConnection 类详解

**源码位置**：[tcpConnection.h](file:///d:/桌面/Linux+C++服务器开发/src/net/tcpConnection.h) | [tcpConnection.cpp](file:///d:/桌面/Linux+C++服务器开发/src/net/tcpConnection.cpp)

### 7.1 类定义

```cpp
class TcpConnection : public std::enable_shared_from_this<TcpConnection>, 
                      private NonCopyable
{
public:
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*, Timestamp)>;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;

    TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                  const InetAddress& localAddr, const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return m_loop; }
    const std::string& name() const { return m_name; }
    const InetAddress& localAddress() const { return m_localAddr; }
    const InetAddress& peerAddress() const { return m_peerAddr; }
    bool connected() const { return m_state == State::kConnected; }
    int fd() const { return m_sockfd; }

    void send(const void* data, size_t len);
    void send(const std::string& data);
    void send(Buffer* buffer);

    void shutdown();
    void forceClose();
    void forceCloseWithDelay(double seconds);

    void setConnectionCallback(ConnectionCallback cb);
    void setMessageCallback(MessageCallback cb);
    void setCloseCallback(CloseCallback cb);
    void setWriteCompleteCallback(WriteCompleteCallback cb);

    void connectEstablished();
    void connectDestroyed();

private:
    enum class State {
        kConnecting,
        kConnected,
        kDisconnecting,
        kDisconnected
    };

    void setState(State s) { m_state = s; }
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void* data, size_t len);
    void shutdownInLoop();
    void forceCloseInLoop();

    EventLoop* m_loop;
    const std::string m_name;
    State m_state;
    int m_sockfd;
    std::unique_ptr<Channel> m_channel;
    InetAddress m_localAddr;
    InetAddress m_peerAddr;
  
    ConnectionCallback m_connectionCallback;
    MessageCallback m_messageCallback;
    CloseCallback m_closeCallback;
    WriteCompleteCallback m_writeCompleteCallback;
  
    Buffer m_inputBuffer;
    Buffer m_outputBuffer;
};
```

### 7.2 连接状态管理

```
┌─────────────────────────────────────────────────────────────┐
│                      连接状态转换                           │
└─────────────────────────────────────────────────────────────┘

                    ┌──────────────┐
                    │ kConnecting  │  构造函数
                    │   (初始)     │
                    └──────┬───────┘
                           │
                  connectEstablished()
                           │
                           ▼
                    ┌──────────────┐
                    │  kConnected  │◄────────────────┐
                    │   (已连接)   │                 │
                    └──────┬───────┘                 │
                           │                         │
              ┌────────────┼────────────┐            │
              │            │            │            │
         shutdown()   forceClose()   正常通信        │
              │            │            │            │
              ▼            │            │            │
       ┌──────────────┐    │            │            │
       │kDisconnecting│    │            │            │
       │   (断开中)   │────┼────────────┘            │
       └──────┬───────┘    │                         │
              │            │                         │
              │   forceCloseWithDelay()              │
              │            │                         │
              └────────────┼─────────────────────────┘
                           │
                    handleClose()
                           │
                           ▼
                    ┌──────────────┐
                    │ kDisconnected│
                    │   (已断开)   │
                    └──────────────┘
```

### 7.3 核心实现

#### 7.3.1 构造函数

```cpp
TcpConnection::TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                               const InetAddress& localAddr, const InetAddress& peerAddr)
    : m_loop(loop)
    , m_name(name)
    , m_state(State::kConnecting)
    , m_sockfd(sockfd)
    , m_channel(new Channel(loop, sockfd))
    , m_localAddr(localAddr)
    , m_peerAddr(peerAddr)
{
    // 设置 Channel 的回调函数
    m_channel->setReadCallback([this](Timestamp t) { handleRead(t); });
    m_channel->setWriteCallback([this]() { handleWrite(); });
    m_channel->setCloseCallback([this]() { handleClose(); });
    m_channel->setErrorCallback([this]() { handleError(); });
}
```

#### 7.3.2 handleRead（数据接收）

```cpp
void TcpConnection::handleRead(Timestamp receiveTime)
{
    m_loop->assertInLoopThread();
    int savedErrno = 0;
  
    // 1. 从 socket 读取数据到输入缓冲区
    ssize_t n = m_inputBuffer.readFd(m_sockfd, &savedErrno);
  
    if (n > 0)
    {
        // 2. 调用消息回调
        if (m_messageCallback)
        {
            m_messageCallback(shared_from_this(), &m_inputBuffer, receiveTime);
        }
    }
    else if (n == 0)
    {
        // 3. 对方关闭连接
        handleClose();
    }
    else
    {
        // 4. 发生错误
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead error");
        handleError();
    }
}
```

#### 7.3.3 handleWrite（数据发送）

```cpp
void TcpConnection::handleWrite()
{
    m_loop->assertInLoopThread();
  
    if (m_channel->isWriting())
    {
        // 1. 尝试发送输出缓冲区中的数据
        ssize_t n = ::write(m_sockfd, m_outputBuffer.peek(), 
                            m_outputBuffer.readableBytes());
      
        if (n > 0)
        {
            // 2. 移动缓冲区读指针
            m_outputBuffer.retrieve(n);
          
            // 3. 如果数据发送完毕
            if (m_outputBuffer.readableBytes() == 0)
            {
                // 禁用可写事件
                m_channel->disableWriting();
              
                // 调用写完成回调
                if (m_writeCompleteCallback)
                {
                    m_writeCompleteCallback(shared_from_this());
                }
              
                // 如果正在关闭，执行 shutdown
                if (m_state == State::kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite error");
        }
    }
}
```

#### 7.3.4 send 函数

```cpp
void TcpConnection::send(const void* data, size_t len)
{
    if (m_state == State::kConnected)
    {
        if (m_loop->isInLoopThread())
        {
            // 在 IO 线程，直接发送
            sendInLoop(data, len);
        }
        else
        {
            // 跨线程调用，放入队列
            void (TcpConnection::*fp)(const void*, size_t) = &TcpConnection::sendInLoop;
            m_loop->queueInLoop(std::bind(fp, this, data, len));
        }
    }
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
    m_loop->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
  
    // 1. 如果已断开，放弃发送
    if (m_state == State::kDisconnected)
    {
        LOG_WARN("disconnected, give up writing");
        return;
    }
  
    // 2. 如果输出缓冲区为空且未关注可写事件，尝试直接发送
    if (!m_channel->isWriting() && m_outputBuffer.readableBytes() == 0)
    {
        nwrote = ::write(m_sockfd, data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && m_writeCompleteCallback)
            {
                m_writeCompleteCallback(shared_from_this());
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop error");
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;
                }
            }
        }
    }
  
    // 3. 如果还有数据未发送且没有错误，放入输出缓冲区
    if (!faultError && remaining > 0)
    {
        m_outputBuffer.append(static_cast<const char*>(data) + nwrote, remaining);
      
        // 关注可写事件
        if (!m_channel->isWriting())
        {
            m_channel->enableWriting();
        }
    }
}
```

### 7.4 数据发送流程

```
┌─────────────────────────────────────────────────────────────┐
│                      数据发送流程                            │
└─────────────────────────────────────────────────────────────┘

send(data, len)
       │
       ▼
┌─────────────────┐
│ 是否在 IO 线程？ │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
    ▼         ▼
  是         否
    │         │
    │         └──> queueInLoop(sendInLoop)
    │                    │
    ▼                    ▼
sendInLoop()      等待 IO 线程执行
    │
    ▼
┌─────────────────────────┐
│ 输出缓冲区为空且未关注写？│
└────────┬────────────────┘
         │
    ┌────┴────┐
    │         │
    ▼         ▼
  是         否
    │         │
    │         └──> 追加到输出缓冲区
    │              enableWriting()
    ▼
直接 write()
    │
    ▼
┌─────────────────┐
│ 发送完毕？       │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
    ▼         ▼
  是         否
    │         │
    │         └──> 剩余数据放入输出缓冲区
    │              enableWriting()
    ▼
writeCompleteCallback()
```

---

## 8. TcpServer 类详解

**源码位置**：[tcpServer.h](file:///d:/桌面/Linux+C++服务器开发/src/net/tcpServer.h) | [tcpServer.cpp](file:///d:/桌面/Linux+C++服务器开发/src/net/tcpServer.cpp)

### 8.1 类定义

```cpp
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

### 8.2 核心实现

#### 8.2.1 构造函数

```cpp
namespace
{
int createNonblockingSocket()
{
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_ERROR("TcpServer::createNonblockingSocket - socket error: {}", errno);
    }
    return sockfd;
}
}

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name)
    : m_loop(loop)
    , m_name(name)
    , m_listenfd(createNonblockingSocket())
    , m_acceptChannel(new Channel(loop, m_listenfd))
    , m_started(0)
    , m_nextConnId(1)
{
    // 1. 设置地址重用
    int optval = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    // 2. 绑定地址
    struct sockaddr_in addr = listenAddr.getSockAddrInet();
    if (bind(m_listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        LOG_ERROR("TcpServer::TcpServer - bind error: {}", errno);
    }

    // 3. 设置 accept 回调
    m_acceptChannel->setReadCallback([this](Timestamp) { newConnection(); });
}
```

#### 8.2.2 start 函数

```cpp
void TcpServer::start()
{
    // 使用原子操作确保只启动一次
    if (m_started.exchange(1) == 0)
    {
        // 1. 开始监听
        if (listen(m_listenfd, SOMAXCONN) < 0)
        {
            LOG_ERROR("TcpServer::start - listen error: {}", errno);
            return;
        }
      
        // 2. 关注 acceptChannel 的可读事件
        m_acceptChannel->enableReading();
      
        LOG_INFO("TcpServer::start - listening on fd {}", m_listenfd);
    }
}
```

#### 8.2.3 newConnection 函数

```cpp
void TcpServer::newConnection()
{
    struct sockaddr_in peerAddr;
    socklen_t peerLen = sizeof(peerAddr);
  
    // 1. 接受新连接（非阻塞）
    int connfd = accept4(m_listenfd, (struct sockaddr*)&peerAddr, &peerLen, 
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd < 0)
    {
        LOG_ERROR("TcpServer::newConnection - accept error: {}", errno);
        return;
    }

    // 2. 生成连接名称
    char buf[64];
    snprintf(buf, sizeof(buf), ":%s#%d", m_name.c_str(), m_nextConnId++);
    std::string connName = buf;

    // 3. 获取本地地址
    struct sockaddr_in localAddr;
    socklen_t localLen = sizeof(localAddr);
    getsockname(connfd, (struct sockaddr*)&localAddr, &localLen);

    InetAddress localAddrObj(localAddr);
    InetAddress peerAddrObj(peerAddr);

    LOG_INFO("TcpServer::newConnection - new connection {} from {}:{}",
             connName, peerAddrObj.toIp(), peerAddrObj.port());

    // 4. 创建 TcpConnection 对象
    auto conn = std::make_shared<TcpConnection>(m_loop, connName, connfd, 
                                                  localAddrObj, peerAddrObj);
    m_connections[connName] = conn;

    // 5. 设置回调函数
    conn->setConnectionCallback(m_connectionCallback);
    conn->setMessageCallback(m_messageCallback);
    conn->setCloseCallback([this](const std::shared_ptr<TcpConnection>& c) { 
        removeConnection(c); 
    });

    // 6. 在 IO 线程中建立连接
    m_loop->queueInLoop([conn]() { conn->connectEstablished(); });
}
```

#### 8.2.4 removeConnection 函数

```cpp
void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn)
{
    LOG_INFO("TcpServer::removeConnection - connection {} closed", conn->name());
  
    // 1. 从连接表中移除
    m_connections.erase(conn->name());
  
    // 2. 在 IO 线程中销毁连接
    m_loop->queueInLoop([conn]() { conn->connectDestroyed(); });
}
```

### 8.3 连接生命周期

```
┌─────────────────────────────────────────────────────────────┐
│                      连接生命周期                           │
└─────────────────────────────────────────────────────────────┘

1. 新连接到来
   ┌─────────────────────────────────────────────────────────┐
   │  TcpServer::newConnection()                             │
   │    │                                                    │
   │    ├─> accept4() 获取 connfd                            │
   │    ├─> 创建 TcpConnection 对象                          │
   │    ├─> 设置回调函数                                     │
   │    └─> queueInLoop(connectEstablished)                  │
   └─────────────────────────────────────────────────────────┘
                              │
                              ▼
2. 连接建立
   ┌─────────────────────────────────────────────────────────┐
   │  TcpConnection::connectEstablished()                    │
   │    │                                                    │
   │    ├─> setState(kConnected)                             │
   │    ├─> channel_->enableReading()                        │
   │    └─> connectionCallback_(shared_from_this())          │
   └─────────────────────────────────────────────────────────┘
                              │
                              ▼
3. 数据传输
   ┌─────────────────────────────────────────────────────────┐
   │  TcpConnection::handleRead() / handleWrite()            │
   │    │                                                    │
   │    ├─> 读取数据到 inputBuffer_                          │
   │    ├─> messageCallback_() 处理消息                      │
   │    └─> 发送响应到 outputBuffer_                         │
   └─────────────────────────────────────────────────────────┘
                              │
                              ▼
4. 连接关闭
   ┌─────────────────────────────────────────────────────────┐
   │  TcpConnection::handleClose()                           │
   │    │                                                    │
   │    ├─> setState(kDisconnected)                          │
   │    ├─> channel_->disableAll()                           │
   │    ├─> connectionCallback_()                            │
   │    └─> closeCallback_() -> removeConnection()           │
   └─────────────────────────────────────────────────────────┘
                              │
                              ▼
5. 连接销毁
   ┌─────────────────────────────────────────────────────────┐
   │  TcpConnection::connectDestroyed()                      │
   │    │                                                    │
   │    ├─> setState(kDisconnected)                          │
   │    ├─> channel_->disableAll()                           │
   │    └─> channel_->remove()                               │
   └─────────────────────────────────────────────────────────┘
```

---

## 9. 数据流完整流程

### 9.1 服务器启动流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          服务器启动流程                                     │
└─────────────────────────────────────────────────────────────────────────────┘

main()
   │
   ├─> EventLoop loop;                    // 创建事件循环
   │
   ├─> InetAddress listenAddr(8888);      // 创建监听地址
   │
   ├─> TcpServer server(&loop, listenAddr, "Server");  // 创建服务器
   │
   ├─> server.setConnectionCallback(...); // 设置连接回调
   ├─> server.setMessageCallback(...);    // 设置消息回调
   │
   ├─> server.start();                    // 启动服务器
   │      │
   │      ├─> socket() -> listenfd        // 创建监听套接字
   │      ├─> bind()                      // 绑定地址
   │      ├─> listen()                    // 开始监听
   │      └─> acceptChannel_->enableReading()  // 关注可读事件
   │
   └─> loop.loop();                       // 进入事件循环
          │
          └─> while (!quit) {
                 poll();                   // 等待事件
                 handleEvent();            // 处理事件
                 doPendingFunctors();      // 执行任务
              }
```

### 9.2 新连接处理流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          新连接处理流程                                     │
└─────────────────────────────────────────────────────────────────────────────┘

客户端 SYN
     │
     ▼
┌─────────────┐
│   listenfd  │ 可读
└──────┬──────┘
       │
       ▼
epoll_wait() 返回
       │
       ▼
┌──────────────────────────────────────────────────────────────┐
│ acceptChannel_->handleEvent()                                │
│   │                                                          │
│   └─> readCallback_() -> TcpServer::newConnection()          │
│         │                                                    │
│         ├─> accept4() -> connfd                              │
│         ├─> 创建 TcpConnection                               │
│         ├─> 设置回调                                         │
│         └─> queueInLoop(connectEstablished)                  │
└──────────────────────────────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────────┐
│ doPendingFunctors()                                          │
│   │                                                          │
│   └─> TcpConnection::connectEstablished()                    │
│         │                                                    │
│         ├─> setState(kConnected)                             │
│         ├─> channel_->enableReading()                        │
│         └─> connectionCallback_(shared_from_this())          │
└──────────────────────────────────────────────────────────────┘
```

### 9.3 数据接收流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          数据接收流程                                       │
└─────────────────────────────────────────────────────────────────────────────┘

客户端发送数据
     │
     ▼
┌─────────────┐
│   connfd    │ 可读
└──────┬──────┘
       │
       ▼
epoll_wait() 返回
       │
       ▼
┌──────────────────────────────────────────────────────────────┐
│ TcpConnection::handleRead(receiveTime)                       │
│   │                                                          │
│   ├─> inputBuffer_.readFd(connfd) -> 读取数据                │
│   │                                                          │
│   └─> messageCallback_(conn, &inputBuffer, receiveTime)      │
│         │                                                    │
│         └─> Codec::onMessage() -> 解码 -> 业务处理           │
└──────────────────────────────────────────────────────────────┘
```

### 9.4 数据发送流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          数据发送流程                                       │
└─────────────────────────────────────────────────────────────────────────────┘

业务处理完成，需要发送响应
     │
     ▼
┌──────────────────────────────────────────────────────────────┐
│ TcpConnection::send(buffer)                                  │
│   │                                                          │
│   └─> sendInLoop()                                           │
│         │                                                    │
│         ├─> 输出缓冲区为空？                                 │
│         │     ├─ 是 -> 直接 write()                          │
│         │     │        └─> 发送完毕 -> writeCompleteCallback │
│         │     └─ 否 -> 追加到 outputBuffer_                  │
│         │              └─> enableWriting()                   │
│         │                                                    │
│         └─> 等待可写事件                                     │
└──────────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────┐
│   connfd    │ 可写
└──────┬──────┘
       │
       ▼
┌──────────────────────────────────────────────────────────────┐
│ TcpConnection::handleWrite()                                 │
│   │                                                          │
│   ├─> write(connfd, outputBuffer_.peek(), len)               │
│   ├─> outputBuffer_.retrieve(n)                              │
│   │                                                          │
│   └─> 发送完毕？                                             │
│         ├─ 是 -> disableWriting()                            │
│         │        └─> writeCompleteCallback()                 │
│         └─ 否 -> 继续等待可写事件                            │
└──────────────────────────────────────────────────────────────┘
```

---

## 10. 设计模式与最佳实践

### 10.1 使用的设计模式

#### 10.1.1 Reactor 模式

```
┌─────────────────────────────────────────────────────────────┐
│                      Reactor 模式                           │
└─────────────────────────────────────────────────────────────┘

核心思想：
- 事件驱动
- 非阻塞 I/O
- 单线程事件循环

组件：
- EventLoop：Reactor 核心，事件循环
- EpollPoller：多路复用器
- Channel：事件处理器

优点：
1. 响应快，不必为单个同步时间阻塞
2. 编程简单，可以最大程度避免多线程问题
3. 可扩展性好，通过增加 EventLoop 实例数量来利用多核
```

#### 10.1.2 回调模式

```
┌─────────────────────────────────────────────────────────────┐
│                      回调模式                               │
└─────────────────────────────────────────────────────────────┘

Channel 的回调机制：
┌─────────────────────────────────────────────────────────────┐
│  Channel                                                    │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ setReadCallback(ReadEventCallback cb)                   ││
│  │ setWriteCallback(EventCallback cb)                      ││
│  │ setCloseCallback(EventCallback cb)                      ││
│  │ setErrorCallback(EventCallback cb)                      ││
│  └─────────────────────────────────────────────────────────┘│
│                          │                                  │
│                          ▼                                  │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ handleEvent(Timestamp receiveTime)                      ││
│  │   ├─> m_readCallback(receiveTime)                       ││
│  │   ├─> m_writeCallback()                                 ││
│  │   ├─> m_closeCallback()                                 ││
│  │   └─> m_errorCallback()                                 ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘

TcpConnection 的回调机制：
┌─────────────────────────────────────────────────────────────┐
│  TcpConnection                                              │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ setConnectionCallback(ConnectionCallback cb)            ││
│  │ setMessageCallback(MessageCallback cb)                  ││
│  │ setWriteCompleteCallback(WriteCompleteCallback cb)      ││
│  │ setCloseCallback(CloseCallback cb)                      ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘

优点：
1. 解耦：事件源和事件处理分离
2. 灵活：运行时动态设置回调
3. 可扩展：易于添加新的事件处理逻辑
```

#### 10.1.3 RAII 模式

```
┌─────────────────────────────────────────────────────────────┐
│                      RAII 模式                              │
└─────────────────────────────────────────────────────────────┘

资源管理：
┌─────────────────────────────────────────────────────────────┐
│  类                  │  管理的资源                          │
│─────────────────────────────────────────────────────────────│
│  EpollPoller         │  epollfd (epoll 实例)                │
│  EventLoop           │  wakeupFd (eventfd)                  │
│  TcpConnection       │  sockfd (socket)                     │
│  Channel             │  fd 的生命周期由外部管理             │
│  unique_ptr<Channel> │  Channel 对象                        │
│  unique_ptr<Poller>  │  Poller 对象                         │
└─────────────────────────────────────────────────────────────┘

示例：
class TcpConnection {
    std::unique_ptr<Channel> m_channel;  // 自动管理 Channel 生命周期
    ~TcpConnection() {
        // 析构时自动关闭 socket
        if (m_state == State::kConnected) {
            close(m_sockfd);
        }
    }
};
```

#### 10.1.4 智能指针模式

```
┌─────────────────────────────────────────────────────────────┐
│                    智能指针模式                             │
└─────────────────────────────────────────────────────────────┘

TcpConnection 使用 shared_ptr 管理：
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│  class TcpConnection :                                      │
│      public std::enable_shared_from_this<TcpConnection>     │
│  {                                                          │
│  };                                                         │
│                                                             │
│  使用场景：                                                 │
│  1. TcpServer::m_connections_ 保存连接                      │
│     map<string, shared_ptr<TcpConnection>>                  │
│                                                             │
│  2. 回调函数中传递连接对象                                  │
│     m_messageCallback(shared_from_this(), buffer, time)     │
│                                                             │
│  3. 异步操作中保持对象存活                                  │
│     queueInLoop([conn]() { conn->connectEstablished(); })   │
│                                                             │
└─────────────────────────────────────────────────────────────┘

生命周期管理：
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│  1. 创建连接：                                              │
│     auto conn = make_shared<TcpConnection>(...);            │
│     m_connections[name] = conn;  // 引用计数 = 1            │
│                                                             │
│  2. 回调中引用：                                            │
│     [conn]() { ... }  // lambda 捕获，引用计数 +1           │
│                                                             │
│  3. 连接关闭：                                              │
│     m_connections.erase(name);  // 引用计数 -1              │
│     // 当引用计数 = 0 时，自动析构                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 10.2 最佳实践

#### 10.2.1 线程安全

```
┌─────────────────────────────────────────────────────────────┐
│                      线程安全                                │
└─────────────────────────────────────────────────────────────┘

1. One Loop Per Thread
   - 每个 EventLoop 只属于一个线程
   - 避免锁竞争，提高性能

2. 跨线程调用
   - 使用 runInLoop() / queueInLoop()
   - 通过 eventfd 唤醒目标线程

3. 任务队列
   - 使用 mutex 保护
   - swap 技巧减少临界区时间

示例：
void EventLoop::doPendingFunctors()
{
    vector<Functor> functors;
    {
        lock_guard<mutex> lock(m_mutex);
        functors.swap(m_pendingFunctors);  // swap 而非 copy
    }
    for (const auto& functor : functors) {
        functor();
    }
}
```

#### 10.2.2 错误处理

```
┌─────────────────────────────────────────────────────────────┐
│                      错误处理                                │
└─────────────────────────────────────────────────────────────┘

1. 系统调用错误
   - 保存 errno
   - 忽略 EINTR（被信号中断）
   - 处理 EAGAIN/EWOULDBLOCK（非阻塞正常情况）

2. 连接错误
   - EPIPE：对方关闭连接
   - ECONNRESET：连接重置
   - 处理方式：关闭连接

3. 日志记录
   - 使用 LOG_ERROR 记录错误
   - 使用 LOG_FATAL 记录致命错误
```

#### 10.2.3 性能优化

```
┌─────────────────────────────────────────────────────────────┐
│                      性能优化                                │
└─────────────────────────────────────────────────────────────┘

1. 非阻塞 I/O
   - socket 使用 SOCK_NONBLOCK
   - accept4 使用 SOCK_NONBLOCK | SOCK_CLOEXEC

2. 批量处理
   - Buffer 使用 readv() 批量读取
   - epoll 事件数组动态扩容

3. 缓存友好
   - 线程局部存储缓存 tid
   - 避免频繁系统调用

4. 内存管理
   - 使用 unique_ptr 管理资源
   - 避免内存泄漏
```

---

## 总结

本文档详细介绍了 muduo 网络库的核心架构和实现，包括：

1. **基础组件**：NonCopyable、Timestamp、CurrentThread、InetAddress
2. **核心组件**：Channel、EpollPoller、EventLoop
3. **连接管理**：TcpConnection、TcpServer
4. **设计模式**：Reactor、回调、RAII、智能指针

通过学习这些内容，可以深入理解高性能网络编程的核心技术，为后续开发打下坚实基础。
