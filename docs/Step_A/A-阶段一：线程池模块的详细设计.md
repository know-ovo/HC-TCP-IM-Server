# 阶段一：线程池模块详解

## 目录

- [1. 概述](#1-概述)
- [2. 整体架构设计](#2-整体架构设计)
- [3. 线程安全队列详解](#3-线程安全队列详解)
- [4. 线程池详解](#4-线程池详解)
- [5. 数据流完整流程](#5-数据流完整流程)
- [6. 模块间协作关系](#6-模块间协作关系)
- [7. 设计模式与最佳实践](#7-设计模式与最佳实践)
- [8. 与传统线程池的对比](#8-与传统线程池的对比)
- [9. 扩展指南](#9-扩展指南)

---

## 1. 概述

线程池模块是服务器开发中最基础也是最重要的组件之一，它解决了线程频繁创建销毁的开销问题，提供了任务队列和线程管理的统一抽象。本模块包含两个核心组件：**BlockingQueue（线程安全队列）** 和 **ThreadPool（线程池）**。

### 1.1 模块职责

| 组件 | 文件 | 职责 |
|------|------|------|
| **BlockingQueue** | blockingQueue.h | 线程安全的生产者-消费者队列，支持阻塞/超时获取 |
| **ThreadPool** | threadpool.h/cpp | 固定线程数的线程池，支持异步任务提交和结果获取 |

### 1.2 设计目标

1. **线程安全**：多线程环境下安全操作，无需外部加锁
2. **高效任务调度**：支持异步提交任务，通过 future 获取结果
3. **优雅关闭**：支持安全停止，不丢失正在执行的任务
4. **异常隔离**：单个任务异常不影响其他任务执行
5. **可复用性**：BlockingQueue 可独立使用

### 1.3 文件结构

```
src/base/
├── blockingQueue.h    # 线程安全队列（仅头文件，模板类）
├── threadpool.h       # 线程池头文件
└── threadpool.cpp     # 线程池实现
```

---

## 2. 整体架构设计

### 2.1 分层架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              应用层 (Application)                            │
│                     业务逻辑提交任务、获取异步结果                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            线程池层 (ThreadPool)                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                          ThreadPool                                  │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │   │
│  │  │   submit()   │  │   start()    │  │   stop()     │              │   │
│  │  │  提交任务    │  │  启动线程    │  │  停止线程池   │              │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘              │   │
│  │                                                                      │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │                    工作线程集合 workers_                        │  │   │
│  │  │  [Thread1] [Thread2] [Thread3] [Thread4] ...                  │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         队列层 (BlockingQueue)                               │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        BlockingQueue                                 │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │   │
│  │  │     put()    │  │    take()    │  │   take(timeout)│             │   │
│  │  │  生产者入队   │  │  消费者阻塞  │  │  消费者超时    │             │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘              │   │
│  │                                                                      │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │     std::queue<T>     std::mutex     condition_variable       │  │   │
│  │  │        队列存储          互斥锁          条件变量              │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 生产者-消费者模型

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          生产者-消费者模型                                    │
└─────────────────────────────────────────────────────────────────────────────┘

    生产者（主线程/其他线程）                    消费者（工作线程）
           │                                        │
           │  submit(task)                          │
           │         │                              │
           ▼         ▼                              ▼
    ┌──────────────────────┐              ┌──────────────────────┐
    │                      │              │                      │
    │   put(task)          │              │   take(task)         │
    │   加锁 → 入队 → 通知  │              │   加锁 → 等待 → 出队  │
    │                      │              │                      │
    └──────────┬───────────┘              └──────────┬───────────┘
               │                                     │
               │         ┌─────────────────┐         │
               └────────>│  BlockingQueue  │<────────┘
                         │                 │
                         │  [task1][task2] │
                         │  [task3][task4] │
                         │  [    ...     ] │
                         └─────────────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │                           │
                    ▼                           ▼
           condition_variable              std::mutex
           (通知/等待机制)                 (互斥访问)
```

### 2.3 数据流向

```
任务提交流程：
┌──────────┐    ┌──────────────┐    ┌──────────────┐    ┌───────────────┐
│ 业务代码 │ -> │ submit(fn)   │ -> │ packaged_task│ -> │ BlockingQueue │
│          │    │ 包装成task   │    │ 封装异步结果  │    │   入队等待    │
└──────────┘    └──────────────┘    └──────────────┘    └───────────────┘
                                                              │
                                                              ▼
任务执行流程：                                          ┌───────────────┐
┌──────────┐    ┌──────────────┐    ┌──────────────┐   │   工作线程    │
│ future   │ <- │ 设置结果     │ <- │ task()执行   │ <- │   take()取任务│
│ 获取结果 │    │ promise      │    │ 调用函数     │    └───────────────┘
└──────────┘    └──────────────┘    └──────────────┘
```

---

## 3. 线程安全队列详解

### 3.1 类定义

```cpp
template<typename T>
class BlockingQueue
{
public:
    BlockingQueue() = default;
    ~BlockingQueue() = default;

    void put(const T& task);
    void put(T&& task);

    T take();
    bool take(T& task, int timeoutMs);

    size_t size() const;
    bool empty() const;
    void clear();

private:
    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
};
```

### 3.2 成员变量说明

| 变量 | 类型 | 作用 |
|------|------|------|
| `m_queue` | `std::queue<T>` | 底层数据存储，使用 STL 的 queue 容器 |
| `m_mutex` | `mutable std::mutex` | 互斥锁，保护队列的并发访问 |
| `m_notEmpty` | `std::condition_variable` | 条件变量，实现阻塞等待机制 |

**为什么 m_mutex 是 mutable？**

```cpp
size_t size() const      // const 成员函数
{
    std::lock_guard<std::mutex> lock(m_mutex);  // 需要修改 mutex 的状态
    return m_queue.size();
}
```

`mutable` 允许在 const 成员函数中修改 mutex 的状态（加锁/解锁），因为加锁不改变逻辑上的对象状态。

### 3.3 put 函数详解

#### 3.3.1 左值引用版本

```cpp
template<typename T>
void BlockingQueue<T>::put(const T& task)
{
    std::lock_guard<std::mutex> lock(m_mutex);  // 1. RAII 加锁
    m_queue.push(task);                         // 2. 拷贝入队
    m_notEmpty.notify_one();                    // 3. 唤醒一个消费者
}
```

| 步骤 | 代码 | 设计思路 |
|------|------|---------|
| 1 | `std::lock_guard<std::mutex> lock(m_mutex)` | **RAII 锁管理**：构造时自动加锁，析构时自动解锁，防止死锁 |
| 2 | `m_queue.push(task)` | **拷贝入队**：左值引用版本，task 被拷贝到队列中 |
| 3 | `m_notEmpty.notify_one()` | **唤醒一个等待者**：只唤醒一个消费者，避免惊群效应 |

#### 3.3.2 右值引用版本

```cpp
template<typename T>
void BlockingQueue<T>::put(T&& task)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(std::move(task));              // 移动入队，避免拷贝
    m_notEmpty.notify_one();
}
```

**为什么需要两个 put 版本？**

```
场景1：传入左值
────────────────────────────────────
std::string data = "hello";
queue.put(data);           // 调用 put(const T&)，拷贝

场景2：传入右值
────────────────────────────────────
queue.put(std::string("hello"));  // 调用 put(T&&)，移动
queue.put(std::move(data));       // 调用 put(T&&)，移动

性能差异：
- 拷贝：深拷贝整个对象，可能涉及内存分配
- 移动：只转移资源所有权，O(1) 操作
```

#### 3.3.3 notify_one vs notify_all

```
notify_one：只唤醒一个等待线程
┌─────────────────────────────────────────────────────────────┐
│  等待队列：[Thread1] [Thread2] [Thread3] [Thread4]          │
│                                                             │
│  notify_one() ──> 只唤醒 Thread1                            │
│                                                             │
│  结果：Thread1 被唤醒，其他继续等待                          │
└─────────────────────────────────────────────────────────────┘

notify_all：唤醒所有等待线程（惊群效应）
┌─────────────────────────────────────────────────────────────┐
│  等待队列：[Thread1] [Thread2] [Thread3] [Thread4]          │
│                                                             │
│  notify_all() ──> 全部唤醒                                  │
│                                                             │
│  结果：4个线程都被唤醒，但只有1个能拿到任务                   │
│        其他3个白唤醒后又回去等待（浪费 CPU）                  │
└─────────────────────────────────────────────────────────────┘
```

**本设计选择 notify_one 的原因：**
- 每次只入队一个任务
- 只需要唤醒一个消费者
- 避免惊群效应，提高效率

### 3.4 take 函数详解

#### 3.4.1 阻塞版本

```cpp
template<typename T>
T BlockingQueue<T>::take()
{
    std::unique_lock<std::mutex> lock(m_mutex);                          // 1
    m_notEmpty.wait(lock, [this] { return !m_queue.empty(); });          // 2
    T task = std::move(m_queue.front());                                 // 3
    m_queue.pop();                                                       // 4
    return task;                                                         // 5
}
```

| 步骤 | 代码 | 设计思路 |
|------|------|---------|
| 1 | `std::unique_lock<std::mutex> lock(m_mutex)` | **unique_lock 而非 lock_guard**：条件变量需要能够解锁/加锁，unique_lock 支持此操作 |
| 2 | `wait(lock, predicate)` | **带谓词的等待**：防止虚假唤醒，即使被唤醒也会检查条件 |
| 3 | `std::move(m_queue.front())` | **移动语义**：避免拷贝，提高性能 |
| 4 | `m_queue.pop()` | **出队**：移除队首元素 |
| 5 | `return task` | **返回任务**：NRVO 优化 |

**为什么用 unique_lock 而不是 lock_guard？**

```cpp
// lock_guard：不可解锁再加锁
std::lock_guard<std::mutex> lock(mutex);
cv.wait(lock);  // 编译错误！lock_guard 不能用于 condition_variable

// unique_lock：可以解锁再加锁
std::unique_lock<std::mutex> lock(mutex);
cv.wait(lock);  // 正确！wait 内部会解锁、等待、再加锁
```

**什么是虚假唤醒（Spurious Wakeup）？**

```
虚假唤醒：操作系统可能在没有 notify 的情况下唤醒等待的线程

┌─────────────────────────────────────────────────────────────┐
│  正常唤醒：                                                  │
│    生产者 notify_one() ──> 消费者被唤醒 ──> 队列有数据       │
│                                                             │
│  虚假唤醒：                                                  │
│    无 notify ──────────> 消费者被唤醒 ──> 队列为空！         │
│                                                             │
│  如果不用谓词检查，直接取队列会崩溃！                         │
└─────────────────────────────────────────────────────────────┘

解决方案：使用带谓词的 wait
┌─────────────────────────────────────────────────────────────┐
│  cv.wait(lock, []{ return !queue.empty(); });               │
│                                                             │
│  等价于：                                                    │
│  while (!queue.empty()) {                                   │
│      cv.wait(lock);  // 只有条件满足才真正返回               │
│  }                                                          │
└─────────────────────────────────────────────────────────────┘
```

#### 3.4.2 超时版本

```cpp
template<typename T>
bool BlockingQueue<T>::take(T& task, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_notEmpty.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [this] { return !m_queue.empty(); }))
    {
        return false;  // 超时返回 false
    }
    task = std::move(m_queue.front());
    m_queue.pop();
    return true;  // 成功返回 true
}
```

**两种 take 的使用场景对比：**

| take() | take(T&, timeout) |
|--------|-------------------|
| 阻塞直到有数据 | 最多等待 timeoutMs 毫秒 |
| 返回值就是数据 | 通过引用参数返回数据 |
| 适合必须等待的场景 | 适合需要定期检查的场景 |
| 可能永远阻塞 | 一定会返回 |

**超时版本的应用场景：**

```cpp
// 场景：线程池工作线程需要定期检查是否需要退出
void workerThread()
{
    while (m_runningFlag)
    {
        std::function<void()> task;
        if (m_taskQueue.take(task, 1000))  // 最多等 1 秒
        {
            task();  // 有任务就执行
        }
        // 超时后会再次检查 m_runningFlag
    }
}
```

### 3.5 辅助函数

```cpp
template<typename T>
size_t BlockingQueue<T>::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

template<typename T>
bool BlockingQueue<T>::empty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.empty();
}

template<typename T>
void BlockingQueue<T>::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::queue<T> empty;
    std::swap(m_queue, empty);  // O(1) 清空，原队列会被析构
}
```

---

## 4. 线程池详解

### 4.1 类定义

```cpp
class ThreadPool
{
public:
    explicit ThreadPool(size_t threadCount = 4);
    ~ThreadPool();

    void start();
    void stop();

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;

    size_t size() const { return m_workers.size(); }
    bool isRunning() const { return m_runningFlag; }

private:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void workerThread();

    size_t m_threadCount;
    std::vector<std::thread> m_workers;
    BlockingQueue<std::function<void()>> m_taskQueue;
    std::atomic<bool> m_runningFlag;
};
```

### 4.2 成员变量说明

| 变量 | 类型 | 作用 |
|------|------|------|
| `m_threadCount` | `size_t` | 线程数量，构造时指定 |
| `m_workers` | `std::vector<std::thread>` | 工作线程集合 |
| `m_taskQueue` | `BlockingQueue<std::function<void()>>` | 任务队列，存储可调用对象 |
| `m_runningFlag` | `std::atomic<bool>` | 运行标志，原子变量无需加锁 |

### 4.3 构造与析构

```cpp
ThreadPool::ThreadPool(size_t threadCount)
    : m_threadCount(threadCount)
    , m_runningFlag(false)
{
}

ThreadPool::~ThreadPool()
{
    if (isRunning())
    {
        stop();  // 析构时自动停止
    }
}
```

**构造与启动分离的设计：**

```
方案1：构造即启动（不推荐）
────────────────────────────────────
ThreadPool pool(4);  // 构造时直接启动线程
// 问题：无法在启动前做配置

方案2：构造与启动分离（本设计）
────────────────────────────────────
ThreadPool pool(4);
pool.setSomeConfig(...);  // 启动前配置
pool.start();             // 显式启动
// 优点：更灵活，可以在启动前做任何配置
```

### 4.4 start 函数详解

```cpp
void ThreadPool::start()
{
    if (m_runningFlag.exchange(true))  // 1. 原子交换
    {
        return;  // 已经启动过，直接返回
    }

    m_workers.reserve(m_threadCount);  // 2. 预分配空间
    for (size_t i = 0; i < m_threadCount; ++i)
    {
        m_workers.emplace_back(&ThreadPool::workerThread, this);  // 3. 创建线程
    }

    LOG_INFO("ThreadPool started with %zu threads", m_threadCount);
}
```

| 步骤 | 代码 | 设计思路 |
|------|------|---------|
| 1 | `m_runningFlag.exchange(true)` | **原子操作**：防止多次调用 start，exchange 返回旧值 |
| 2 | `m_workers.reserve(m_threadCount)` | **预分配**：避免 vector 多次扩容 |
| 3 | `emplace_back(...)` | **原地构造**：直接在 vector 中构造 thread 对象 |

**exchange 的作用：**

```cpp
// exchange 返回旧值，并设置新值
bool old = m_runningFlag.exchange(true);
// 如果 old == true，说明已经启动过
// 如果 old == false，说明未启动，现在设置为 true

// 等价于：
bool old = m_runningFlag;
m_runningFlag = true;
return old;
// 但 exchange 是原子的，线程安全
```

### 4.5 submit 函数详解（核心！）

```cpp
template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;  // 1

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );  // 2

    std::future<return_type> result = task->get_future();  // 3

    if (m_runningFlag)
    {
        m_taskQueue.put([task]() { (*task)(); });  // 4
    }

    return result;  // 5
}
```

#### 4.5.1 逐行解析

| 步骤 | 代码 | 设计思路 |
|------|------|---------|
| 1 | `using return_type = ...` | **类型推导**：使用 `std::result_of` 推导函数返回类型 |
| 2 | `std::make_shared<std::packaged_task<...>>` | **封装任务**：将函数和参数打包成可异步执行的对象 |
| 3 | `task->get_future()` | **获取 future**：调用者通过 future 获取异步结果 |
| 4 | `m_taskQueue.put([task]() { (*task)(); })` | **入队**：用 lambda 捕获 task，延长生命周期 |
| 5 | `return result` | **返回 future**：调用者可以等待结果 |

#### 4.5.2 packaged_task 详解

```
std::packaged_task 的作用：
┌─────────────────────────────────────────────────────────────┐
│  将一个可调用对象包装成可以异步执行的形式                      │
│  并通过关联的 future 获取执行结果                             │
└─────────────────────────────────────────────────────────────┘

示例：
────────────────────────────────────
int add(int a, int b) { return a + b; }

// 创建 packaged_task
std::packaged_task<int()> task(std::bind(add, 1, 2));

// 获取 future
std::future<int> result = task.get_future();

// 执行任务
task();  // 执行 add(1, 2)

// 获取结果
std::cout << result.get() << std::endl;  // 输出 3
```

#### 4.5.3 为什么用 shared_ptr 包装？

```
问题：packaged_task 是不可拷贝的
────────────────────────────────────
std::packaged_task<int()> task(...);
std::function<void()> f = task;  // 编译错误！不能拷贝

解决：用 shared_ptr 包装
────────────────────────────────────
auto task = std::make_shared<std::packaged_task<int()>>(...);
std::function<void()> f = [task]() { (*task)(); };  // 正确！

shared_ptr 的生命周期管理：
┌─────────────────────────────────────────────────────────────┐
│  submit() 中创建 shared_ptr                                  │
│       │                                                      │
│       ▼                                                      │
│  lambda 捕获 shared_ptr（引用计数 +1）                        │
│       │                                                      │
│       ▼                                                      │
│  task 入队，submit() 返回                                    │
│       │                                                      │
│       ▼                                                      │
│  工作线程取出 task，执行 lambda                              │
│       │                                                      │
│       ▼                                                      │
│  lambda 执行完毕，shared_ptr 引用计数 -1                     │
│       │                                                      │
│       ▼                                                      │
│  如果引用计数为 0，packaged_task 被销毁                      │
└─────────────────────────────────────────────────────────────┘
```

#### 4.5.4 完整使用示例

```cpp
ThreadPool pool(4);
pool.start();

// 示例1：提交普通函数
int add(int a, int b) { return a + b; }
auto future1 = pool.submit(add, 1, 2);
std::cout << future1.get() << std::endl;  // 输出 3

// 示例2：提交 lambda
auto future2 = pool.submit([](int n) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return n * n;
}, 5);
std::cout << future2.get() << std::endl;  // 输出 25

// 示例3：提交成员函数
class Calculator {
public:
    int multiply(int a, int b) { return a * b; }
};
Calculator calc;
auto future3 = pool.submit(&Calculator::multiply, &calc, 3, 4);
std::cout << future3.get() << std::endl;  // 输出 12
```

### 4.6 stop 函数详解（优雅关闭）

```cpp
void ThreadPool::stop()
{
    if (!m_runningFlag.exchange(false))  // 1. 原子设置为 false
    {
        return;  // 已经停止过
    }

    m_taskQueue.clear();  // 2. 清空队列

    for (auto& worker : m_workers)
    {
        m_taskQueue.put([]() {});  // 3. 放入 N 个空任务
    }

    for (auto& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();  // 4. 等待所有线程结束
        }
    }

    m_workers.clear();  // 5. 清空线程集合

    LOG_INFO("ThreadPool stopped");
}
```

#### 4.6.1 优雅关闭流程图

```
┌─────────────────────────────────────────────────────────────────┐
│                         stop() 关闭流程                         │
└─────────────────────────────────────────────────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │ m_runningFlag = false │
                    │   原子设置停止标志     │
                    └───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │  m_taskQueue.clear()  │
                    │   清空待处理任务队列   │
                    └───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │ 放入 N 个空任务到队列  │
                    │ N = 线程数量          │
                    └───────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
        ▼                       ▼                       ▼
┌───────────────┐     ┌───────────────┐     ┌───────────────┐
│  Thread1      │     │  Thread2      │     │  ThreadN      │
│  take()返回   │     │  take()返回   │     │  take()返回   │
│  空任务       │     │  空任务       │     │  空任务       │
│  检查running  │     │  检查running  │     │  检查running  │
│  退出循环     │     │  退出循环     │     │  退出循环     │
└───────────────┘     └───────────────┘     └───────────────┘
        │                       │                       │
        └───────────────────────┼───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │   join() 所有线程     │
                    │   等待线程安全退出     │
                    └───────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │  m_workers.clear()    │
                    │   清空线程集合         │
                    └───────────────────────┘
```

#### 4.6.2 为什么要放 N 个空任务？

```
问题：工作线程可能阻塞在 take() 上
────────────────────────────────────
void workerThread()
{
    while (m_runningFlag)  // 如果这里不检查...
    {
        task = m_taskQueue.take();  // 可能永远阻塞在这里！
        task();
    }
}

如果不放空任务：
────────────────────────────────────
1. stop() 设置 m_runningFlag = false
2. stop() 调用 join()
3. 但工作线程还在 take() 中阻塞
4. join() 永远等待 → 死锁！

放空任务后：
────────────────────────────────────
1. stop() 设置 m_runningFlag = false
2. stop() 放入 N 个空任务
3. 每个工作线程收到一个空任务
4. take() 返回空任务
5. 执行空任务（什么都不做）
6. 回到 while 循环，检查 m_runningFlag == false
7. 退出循环
8. join() 成功返回
```

### 4.7 workerThread 函数详解

```cpp
void ThreadPool::workerThread()
{
    while (m_runningFlag)  // 1. 循环直到停止
    {
        std::function<void()> task;
        if (m_taskQueue.take(task, 1000))  // 2. 超时 1 秒
        {
            if (task)  // 3. 检查空任务
            {
                try
                {
                    task();  // 4. 执行任务
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR("ThreadPool task exception: %s", e.what());
                }
                catch (...)
                {
                    LOG_ERROR("ThreadPool task unknown exception");
                }
            }
        }
    }
}
```

| 步骤 | 代码 | 设计思路 |
|------|------|---------|
| 1 | `while (m_runningFlag)` | **循环条件**：原子变量，无需加锁 |
| 2 | `take(task, 1000)` | **超时等待**：1 秒超时，定期检查停止标志 |
| 3 | `if (task)` | **空任务检查**：stop() 放入的空任务直接跳过 |
| 4 | `task()` | **执行任务**：在 try-catch 中执行，异常隔离 |

**为什么用超时 1 秒的 take？**

```
方案1：永久阻塞
────────────────────────────────────
task = m_taskQueue.take();  // 永久等待
// 问题：如果忘记放空任务，线程永远无法退出

方案2：超时等待（本设计）
────────────────────────────────────
if (m_taskQueue.take(task, 1000))  // 最多等 1 秒
// 优点：即使忘记放空任务，线程也能在 1 秒后检查一次 runningFlag
// 这是一个安全网，双重保险
```

**异常隔离的意义：**

```
没有异常隔离：
────────────────────────────────────
void workerThread()
{
    while (running) {
        task = take();
        task();  // 如果这里抛异常，整个线程崩溃退出
    }
}
// 一个任务崩溃，整个线程池少一个线程

有异常隔离：
────────────────────────────────────
void workerThread()
{
    while (running) {
        task = take();
        try {
            task();  // 异常被捕获
        } catch (...) {
            LOG_ERROR(...);  // 记录日志
        }
        // 线程继续运行，处理下一个任务
    }
}
```

---

## 5. 数据流完整流程

### 5.1 任务提交流程

```
调用者                  ThreadPool               BlockingQueue           工作线程
   │                        │                         │                     │
   │  1. submit(fn, args)   │                         │                     │
   │───────────────────────>│                         │                     │
   │                        │                         │                     │
   │                        │  2. 创建 packaged_task  │                     │
   │                        │  3. 获取 future         │                     │
   │                        │                         │                     │
   │  4. 返回 future        │                         │                     │
   │<───────────────────────│                         │                     │
   │                        │                         │                     │
   │                        │  5. put(task)           │                     │
   │                        │────────────────────────>│                     │
   │                        │                         │                     │
   │                        │                         │  6. notify_one      │
   │                        │                         │────────────────────>│
   │                        │                         │                     │
   │                        │                         │  7. take() 返回     │
   │                        │                         │<────────────────────│
   │                        │                         │                     │
   │                        │                         │  8. 执行 task       │
   │                        │                         │                     │
   │  9. future.get()       │                         │                     │
   │─────────────────────────────────────────────────────────────────────>│
   │                        │                         │                     │
   │  10. 获取结果          │                         │                     │
   │<─────────────────────────────────────────────────────────────────────│
   ▼                        ▼                         ▼                     ▼
```

### 5.2 线程池生命周期

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          线程池生命周期                                      │
└─────────────────────────────────────────────────────────────────────────────┘

    构造                      启动                      运行中
      │                        │                         │
      ▼                        ▼                         ▼
┌───────────┐           ┌───────────┐           ┌───────────────┐
│ThreadPool │           │  start()  │           │ submit() 任务 │
│ pool(4);  │           │           │           │     执行      │
│           │           │ 创建4个   │           │               │
│ 未启动    │           │ 工作线程  │           │ 循环处理      │
└───────────┘           └───────────┘           └───────────────┘
                                                      │
                                                      ▼
                              停止                     │
                                │                     │
                                ▼                     │
                        ┌───────────┐                 │
                        │  stop()   │<────────────────┘
                        │           │
                        │ 清空队列  │
                        │ 放空任务  │
                        │ join等待  │
                        └───────────┘
                              │
                              ▼
                        ┌───────────┐
                        │  析构     │
                        │  ~ThreadPool()
                        └───────────┘
```

---

## 6. 模块间协作关系

### 6.1 依赖关系图

```
┌──────────────────────────────────────────────────────────────────┐
│                          应用层                                   │
│                    提交任务、获取结果                              │
└────────────────────────────┬─────────────────────────────────────┘
                             │
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│                        ThreadPool                                 │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐     │
│  │    submit()    │  │    start()     │  │    stop()      │     │
│  │  packaged_task │  │  std::thread   │  │  join & clear  │     │
│  └────────────────┘  └────────────────┘  └────────────────┘     │
└────────────────────────────┬─────────────────────────────────────┘
                             │
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│                       BlockingQueue                               │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐     │
│  │     put()      │  │     take()     │  │    clear()     │     │
│  │  生产者入队    │  │  消费者出队    │  │    清空队列    │     │
│  └────────────────┘  └────────────────┘  └────────────────┘     │
└────────────────────────────┬─────────────────────────────────────┘
                             │
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│                        STL 组件                                   │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐     │
│  │  std::queue    │  │  std::mutex    │  │condition_variable│    │
│  │    队列存储    │  │    互斥锁      │  │    条件变量    │     │
│  └────────────────┘  └────────────────┘  └────────────────┘     │
└──────────────────────────────────────────────────────────────────┘
```

### 6.2 与其他模块的集成

```cpp
// 在网络库中使用线程池处理业务逻辑
class TcpServer
{
public:
    void setThreadPool(ThreadPool* pool) { m_threadPool = pool; }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time)
    {
        // 将业务逻辑提交到线程池
        m_threadPool->submit([this, conn, buf] {
            processMessage(conn, buf);
        });
    }

private:
    ThreadPool* m_threadPool;
};
```

---

## 7. 设计模式与最佳实践

### 7.1 使用的设计模式

#### 7.1.1 生产者-消费者模式

```
BlockingQueue 是典型的生产者-消费者模式实现：

┌─────────────────────────────────────────────────────────────┐
│  生产者：submit() 调用者                                     │
│     │                                                       │
│     ▼ put()                                                 │
│  ┌─────────────┐                                            │
│  │   Queue     │                                            │
│  └─────────────┘                                            │
│     │                                                       │
│     ▼ take()                                                │
│  消费者：workerThread() 工作线程                             │
└─────────────────────────────────────────────────────────────┘
```

#### 7.1.2 RAII 资源管理

```cpp
// 锁的 RAII 管理
void put(const T& task)
{
    std::lock_guard<std::mutex> lock(m_mutex);  // 构造时加锁
    // ... 操作队列
}  // 析构时自动解锁，即使发生异常也能正确释放锁

// 线程的 RAII 管理
ThreadPool::~ThreadPool()
{
    if (isRunning())
    {
        stop();  // 析构时自动停止线程池
    }
}
```

#### 7.1.3 模板方法模式

```cpp
// submit 是一个模板方法，支持任意可调用对象
template<typename F, typename... Args>
auto submit(F&& f, Args&&... args) -> std::future<...>
{
    // 固定的处理流程：
    // 1. 包装任务
    // 2. 获取 future
    // 3. 入队
    // 4. 返回 future
}
```

### 7.2 最佳实践

#### 7.2.1 线程安全

```cpp
// 1. 使用 atomic 避免锁
std::atomic<bool> m_runningFlag;  // 原子变量，无需加锁

// 2. 使用 lock_guard 自动管理锁
std::lock_guard<std::mutex> lock(m_mutex);  // RAII

// 3. 使用条件变量避免忙等待
m_notEmpty.wait(lock, predicate);  // 阻塞等待，不消耗 CPU
```

#### 7.2.2 异常安全

```cpp
// 1. 任务执行异常隔离
try {
    task();
} catch (const std::exception& e) {
    LOG_ERROR("Task exception: %s", e.what());
} catch (...) {
    LOG_ERROR("Unknown task exception");
}

// 2. 析构函数确保资源释放
~ThreadPool()
{
    if (isRunning())
    {
        stop();  // 确保线程被正确停止
    }
}
```

#### 7.2.3 性能优化

```cpp
// 1. 使用移动语义避免拷贝
void put(T&& task) { m_queue.push(std::move(task)); }
T task = std::move(m_queue.front());

// 2. 预分配容器空间
m_workers.reserve(m_threadCount);

// 3. 使用 emplace_back 原地构造
m_workers.emplace_back(&ThreadPool::workerThread, this);
```

---

## 8. 与传统线程池的对比

### 8.1 传统简单线程池

```cpp
class SimpleThreadPool
{
public:
    SimpleThreadPool(size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            workers_.emplace_back([this] {
                while (true)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this] { return !queue_.empty() || stop_; });
                        if (stop_ && queue_.empty()) return;
                        task = std::move(queue_.front());
                        queue_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~SimpleThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }

    template<typename F>
    void submit(F f)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queue_.push(std::move(f));
        }
        cv_.notify_one();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};
```

### 8.2 详细对比

| 维度 | 本设计 | 传统简单设计 |
|------|--------|-------------|
| **队列封装** | 独立 BlockingQueue 类 | 直接在 ThreadPool 中 |
| **future 支持** | ✅ submit 返回 future | ❌ fire-and-forget |
| **构造启动分离** | ✅ 先构造，再 start | ❌ 构造即启动 |
| **优雅关闭** | ✅ 放 N 个空任务 | ⚠️ notify_all + 检查 stop |
| **超时 take** | ✅ 1 秒超时 | ❌ 永久阻塞 |
| **异常隔离** | ✅ 捕获任务异常 | ❌ 任务崩溃影响线程 |
| **可复用性** | ✅ BlockingQueue 可独立使用 | ❌ 队列不能单独用 |
| **代码复杂度** | 较高 | 较低 |

### 8.3 本设计的优缺点

**优点：**

1. **职责分离**：BlockingQueue 和 ThreadPool 各司其职
2. **可复用**：BlockingQueue 可用于其他场景
3. **异步结果**：通过 future 获取任务返回值
4. **安全关闭**：双重保险（超时 + 空任务）
5. **异常隔离**：单个任务异常不影响其他任务

**缺点：**

1. **代码量较多**：需要两个类
2. **Stop 清空队列**：未处理的任务会被丢弃
3. **内存开销**：shared_ptr 包装 packaged_task

---

## 9. 扩展指南

### 9.1 添加任务优先级

```cpp
// 使用优先队列替代普通队列
template<typename T>
class PriorityBlockingQueue
{
public:
    void put(const T& task, int priority = 0)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.emplace(priority, task);  // 按优先级排序
        m_notEmpty.notify_one();
    }

    T take()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait(lock, [this] { return !m_queue.empty(); });
        T task = m_queue.top().second;
        m_queue.pop();
        return task;
    }

private:
    using PriorityTask = std::pair<int, T>;
    auto cmp = [](const PriorityTask& a, const PriorityTask& b) {
        return a.first < b.first;  // 优先级高的先出
    };
    std::priority_queue<PriorityTask, std::vector<PriorityTask>, decltype(cmp)> m_queue{cmp};
    std::mutex m_mutex;
    std::condition_variable m_notEmpty;
};
```

### 9.2 添加任务超时

```cpp
// 支持任务执行超时
template<typename F, typename... Args>
auto submitWithTimeout(int timeoutMs, F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    if (m_runningFlag)
    {
        m_taskQueue.put([task, timeoutMs]() {
            std::thread([task, timeoutMs]() {
                std::future<return_type> fut = task->get_future();
                if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::timeout)
                {
                    // 超时处理
                }
                else
                {
                    (*task)();
                }
            }).detach();
        });
    }

    return result;
}
```

### 9.3 动态调整线程数

```cpp
class DynamicThreadPool
{
public:
    void resize(size_t newCount)
    {
        if (newCount > m_workers.size())
        {
            // 添加新线程
            for (size_t i = m_workers.size(); i < newCount; ++i)
            {
                m_workers.emplace_back(&DynamicThreadPool::workerThread, this);
            }
        }
        else if (newCount < m_workers.size())
        {
            // 标记需要减少的线程数
            m_shrinkCount = m_workers.size() - newCount;
            // 放入空任务唤醒线程
            for (size_t i = 0; i < m_shrinkCount; ++i)
            {
                m_taskQueue.put([]() {});
            }
        }
    }

private:
    void workerThread()
    {
        while (m_runningFlag)
        {
            std::function<void()> task;
            if (m_taskQueue.take(task, 1000))
            {
                if (m_shrinkCount > 0)
                {
                    m_shrinkCount--;
                    return;  // 退出线程
                }
                if (task) task();
            }
        }
    }

    std::atomic<size_t> m_shrinkCount{0};
};
```

---

**文档版本**: 2.0  
**最后更新**: 2026-04-02
