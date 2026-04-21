# Linux 面试八股文（C++ 后端开发）

***

## 一、Linux 核心常用命令

### 1.1 核心原理

Linux 命令是与系统内核交互的用户空间工具，通过系统调用（system call）访问内核资源。

***

### 1.2 基础必背题

#### 1.2.1 【必考】常用系统监控命令

**问题：** 请列举并说明 Linux 下常用的系统监控命令？

**标准答案：**

```
1. top / htop
   - 功能：实时查看系统进程状态
   - 重点关注：CPU 使用率、内存使用率、load average
   - Reactor 关联：查看 EventLoop 线程的 CPU 占用

2. ps
   - 功能：查看进程快照
   - 常用选项：ps aux / ps -ef
   - Reactor 关联：查看 TcpServer 进程信息

3. netstat / ss
   - 功能：查看网络连接状态
   - 常用选项：netstat -tulpn / ss -tulpn
   - Reactor 关联：查看 TCP 连接数、LISTEN 状态

4. vmstat
   - 功能：查看虚拟内存统计
   - 重点关注：si/so（swap 换入换出）、bi/bo（磁盘 I/O）

5. iostat
   - 功能：查看磁盘 I/O 统计
   - 重点关注：await（I/O 等待时间）、%util（磁盘使用率）

6. dstat
   - 功能：综合监控工具（整合 vmstat/iostat/netstat）

7. lsof
   - 功能：查看打开的文件描述符
   - 常用选项：lsof -p <pid> / lsof -i :<port>
   - Reactor 关联：查看 fd 泄漏
```

【面试坑点】

- 不要只列命令，要说清楚**每个命令看什么指标**
- 重点指标：load average（1/5/15分钟）、CPU 使用率、内存使用率、TCP 连接数

【结合项目加分话术】

- "我在项目中用 `ss -s` 监控并发连接数，用 `top -H` 查看线程池各线程的 CPU 占用"
- "排查 fd 泄漏时用 `lsof -p <pid> | wc -l` 统计打开的文件描述符数"

***

#### 1.2.2 【必考】进程管理命令

**问题：** Linux 下如何查看、管理进程？

**标准答案：**

```
查看进程：
1. ps aux         # 查看所有进程
2. ps -ef         # 查看所有进程（另一种格式）
3. top / htop     # 实时查看

进程管理：
1. kill <pid>           # 发送 SIGTERM 信号（15）
2. kill -9 <pid>        # 发送 SIGKILL 信号（强制杀死）
3. kill -l              # 查看所有信号
4. killall <name>       # 按名称杀死进程
5. pkill <pattern>      # 按模式杀死进程

后台运行：
1. <command> &          # 后台运行
2. nohup <command> &    # 后台运行，忽略 SIGHUP
3. jobs                 # 查看后台任务
4. fg %1                # 将任务放到前台
5. bg %1                # 将任务放到后台
```

【面试坑点】

- `kill -9` 是强制终止，进程无法捕获，慎用
- `nohup` 会将输出重定向到 nohup.out

【结合项目加分话术】

- "我写的 TcpServer 支持优雅关闭，通过处理 SIGTERM 信号来停止 EventLoop"

***

#### 1.2.3 【高频】网络相关命令

**问题：** 如何排查网络问题？

**标准答案：**

```
1. ping <host>
   - 功能：测试连通性
   - 检查：丢包率、延迟

2. traceroute <host>
   - 功能：追踪路由路径
   - 检查：网络跳数、各跳延迟

3. telnet <host> <port> / nc <host> <port>
   - 功能：测试端口连通性
   - Reactor 关联：测试 TcpServer 监听端口

4. netstat / ss
   - 功能：查看网络连接
   - 常用：ss -tulpn（查看监听）、ss -s（统计）
   - Reactor 关联：查看 ESTABLISHED 状态连接数

5. tcpdump
   - 功能：抓包分析
   - 常用：tcpdump -i eth0 tcp port 8888
   - Reactor 关联：排查 TCP 三次握手、四次挥手问题

6. iftop
   - 功能：实时查看网络流量
   - 重点：各连接的流量统计
```

【面试坑点】

- `telnet` 很多系统默认不安装，推荐用 `nc`（netcat）

【结合项目加分话术】

- "我用 `tcpdump` 抓包排查过 TCP 粘包问题，发现是客户端发送太快导致多个包合在一起"
- "用 `ss -s` 监控服务器的并发连接数，最高达到 5 万+，并据此调优了 backlog 参数"

***

### 1.3 进阶高频深挖题

#### 1.3.1 【高频】load average 怎么看？

**问题：** Linux 的 load average 三个数值分别代表什么？多少算高？

**标准答案：**

```
load average 三个数值：
- 第一个：1 分钟平均负载
- 第二个：5 分钟平均负载
- 第三个：15 分钟平均负载

什么是 load average？
- 定义：系统中处于"可运行状态"和"不可中断状态"的进程数的平均值
- 可运行状态：正在 CPU 上运行 + 等待 CPU 运行（就绪队列）
- 不可中断状态：等待 I/O（如磁盘读写）

多少算高？
- 单核心：load > 1 算高
- 4 核心：load > 4 算高
- 8 核心：load > 8 算高
- 经验值：load > 核数 * 0.7 需要关注

注意：
- load average ≠ CPU 使用率
- I/O 密集型任务会使 load 高但 CPU 使用率不高
```

【面试坑点】

- 不要把 load average 和 CPU 使用率混淆
- 不要只看 1 分钟的数值，要看 5/15 分钟的趋势

【结合项目加分话术】

- "我的 Reactor 服务器在 500 并发时，load average 在 2-3 之间（4 核），CPU 使用率 60-70%，比较健康"
- "有一次数据库慢查询导致 I/O 等待，load 飙到 10+，但 CPU 使用率只有 20%，用 iostat 发现 await 很高"

***

#### 1.3.2 【了解】如何排查系统瓶颈？

**问题：** 系统慢，如何一步步排查瓶颈？

**标准答案：**

```
排查步骤：

1. 先看整体：top / htop
   - load average：是否过高？
   - CPU 使用率：us（用户态）、sy（内核态）、id（空闲）
   - 内存：free、cached、buffered
   - swap：si/so 是否频繁换入换出

2. 看 CPU：mpstat -P ALL 1
   - 各核心负载是否均衡？
   - sy 过高可能是系统调用太多

3. 看内存：free -h / vmstat 1
   - 是否有足够的可用内存？
   - swap 是否被使用？

4. 看磁盘：iostat -x 1
   - await：I/O 等待时间
   - %util：磁盘使用率

5. 看网络：iftop / ss -s
   - 网络带宽是否打满？
   - TCP 连接数是否过多？

6. 看进程：pidstat 1
   - 哪个进程 CPU/内存 占用最高？

Reactor 场景常见瓶颈：
- CPU：EventLoop 处理事件耗时
- 内存：Buffer 分配频繁
- 网络：连接数过多、流量过大
- 锁：多线程竞争
```

【结合项目加分话术】

- "我排查过一次服务器性能问题，先看 top 发现 sy（内核态）很高，再用 pidstat 发现是 epoll\_wait 调用太频繁，最后优化了超时时间"
- "另一次是内存占用持续增长，用 pmap 查看进程内存映射，发现是 Buffer 没有释放导致内存泄漏"

***

## 二、进程与线程管理

### 2.1 核心原理

#### 进程 vs 线程

```
进程：
- 定义：程序执行的实例
- 资源：独立的地址空间、文件描述符、信号等
- 调度：内核调度单位
- 开销：创建/切换开销大

线程：
- 定义：进程内的执行流
- 资源：共享进程的地址空间、文件描述符
- 调度：内核调度单位（Linux 中线程是轻量级进程 LWP）
- 开销：创建/切换开销小

关系：
- 一个进程可以有多个线程
- 线程是进程的一部分
- Linux 中，线程本质是共享资源的进程
```

#### 进程的生命周期

```
创建 → 就绪 → 运行 → 终止
        ↑       ↓
        └── 阻塞 ←┘

1. 创建（Created）
   - fork() / clone() 系统调用
   - 分配 PID、进程描述符

2. 就绪（Ready）
   - 等待 CPU 调度
   - 加入就绪队列

3. 运行（Running）
   - 正在 CPU 上执行
   - 时间片用完 → 回到就绪
   - 等待资源 → 进入阻塞

4. 阻塞（Blocked）
   - 等待 I/O、锁等资源
   - 移出就绪队列
   - 资源可用 → 回到就绪

5. 终止（Terminated）
   - exit() 或信号终止
   - 进入僵尸状态（Zombie）
   - 父进程 wait() 回收 → 完全消失
```

***

### 2.2 基础必背题

#### 2.2.1 【必考】进程和线程的区别？

**问题：** 请详细说明进程和线程的区别？

**标准答案：**

```
1. 资源角度
   - 进程：独立的地址空间、文件描述符表、信号处理
   - 线程：共享进程的地址空间、文件描述符、信号处理
   
2. 开销角度
   - 进程：创建/切换开销大（需要切换地址空间）
   - 线程：创建/切换开销小（只需要切换寄存器、栈）
   
3. 通信角度
   - 进程：需要 IPC（管道、消息队列、共享内存、信号量、Socket）
   - 线程：直接读写共享变量（需要锁保护）
   
4. 稳定性角度
   - 进程：一个进程崩溃不影响其他进程
   - 线程：一个线程崩溃（如段错误）会导致整个进程崩溃
   
5. 调度角度
   - Linux 中：进程和线程都是内核调度单位（LWP）
   - 调度器不区分进程和线程

Reactor 模型选择：
- 多进程模型：每个进程一个 EventLoop，稳定但开销大
- 多线程模型：一个进程多个线程，每个线程一个 EventLoop，开销小但需要考虑线程安全
```

【面试坑点】

- 不要只说"线程轻量"，要说清楚**为什么轻量**（不需要切换地址空间）
- Linux 中线程本质是轻量级进程（LWP），不要说"线程不是内核调度的"

【结合项目加分话术】

- "我的项目用的是多线程 Reactor 模型，主线程接受连接，分发给工作线程，每个线程一个 EventLoop，既保证了并发能力，又避免了多进程的高开销"
- "选择多线程而不是多进程，是因为我们的服务是计算密集型，线程间共享数据多，用线程通信更高效"

***

#### 2.2.2 【必考】fork() 函数？

**问题：** 请说明 fork() 系统调用的作用和特点？

**标准答案：**

```
fork() 作用：
- 创建一个新进程（子进程）
- 子进程是父进程的副本（Copy-On-Write）

fork() 返回值：
- 成功：父进程返回子进程 PID，子进程返回 0
- 失败：父进程返回 -1，不创建子进程

fork() 特点：
1. Copy-On-Write（写时复制）
   - 子进程共享父进程的物理内存
   - 只有当修改时才真正复制
   - 目的：减少开销

2. 继承的资源
   - 地址空间（COW）
   - 文件描述符
   - 信号处理设置
   - 用户 ID、组 ID
   
3. 不继承的资源
   - PID
   - 父进程 ID（PPID）
   - 挂起的信号
   - 文件锁
   
fork() 经典面试题：
int main() {
    fork();
    fork();
    printf("Hello\n");
    return 0;
}
// 输出几个 Hello？ 答案：4 个
```

【面试坑点】

- 不要忘记 Copy-On-Write 机制
- 注意 fork() 是调用一次，返回两次

【结合项目加分话术】

- "我了解 Copy-On-Write 机制，所以在项目中，大对象尽量避免在子进程中修改，可以减少内存复制"

***

#### 2.2.3 【高频】僵尸进程和孤儿进程？

**问题：** 什么是僵尸进程？什么是孤儿进程？如何避免僵尸进程？

**标准答案：**

```
僵尸进程（Zombie）：
- 定义：子进程已终止，但父进程未调用 wait()/waitpid() 回收资源
- 状态：Z（Zombie）
- 危害：占用 PID（PID 有限），少量没问题，大量会导致 PID 耗尽
- 原因：父进程没有回收子进程

孤儿进程（Orphan）：
- 定义：父进程已终止，子进程还在运行
- 状态：init 进程（PID 1）收养
- 危害：无危害，init 会回收

避免僵尸进程：
1. 父进程调用 wait()/waitpid() 回收
   - 阻塞方式：wait()
   - 非阻塞方式：waitpid(-1, NULL, WNOHANG)
   
2. 忽略 SIGCHLD 信号
   signal(SIGCHLD, SIG_IGN);
   - 子进程终止后内核自动回收
   
3. 两次 fork()
   - 第一次 fork() 创建子进程 A
   - 子进程 A 再 fork() 创建孙子进程 B
   - 子进程 A 立即退出，孙子进程 B 成为孤儿
   - init 回收孙子进程 B
   
Reactor 场景：
- 如果用多进程模型，需要处理子进程回收
- 可以用信号处理 SIGCHLD
```

【面试坑点】

- 不要说"僵尸进程占内存"，实际上僵尸进程只占进程描述符（很小）
- 孤儿进程不是问题，init 会收养

【结合项目加分话术】

- "我在多进程版本的 TcpServer 中，用了 SIGCHLD 信号处理来回收子进程，避免了僵尸进程"
- "还有一次排查问题，发现是测试脚本没有回收子进程，导致 PID 耗尽，无法创建新连接"

***

### 2.3 进阶高频深挖题

#### 2.3.1 【高频】Linux 线程是如何实现的？

**问题：** Linux 中的线程是如何实现的？和进程有什么关系？

**标准答案：**

```
Linux 线程实现：
- Linux 中，线程本质是"轻量级进程"（LWP，Light Weight Process）
- 使用 clone() 系统调用创建（fork() 是 clone() 的特例）
- 内核调度器不区分进程和线程，统一调度

clone() 参数：
- CLONE_VM：共享地址空间
- CLONE_FS：共享文件系统信息
- CLONE_FILES：共享文件描述符表
- CLONE_SIGHAND：共享信号处理
- CLONE_THREAD：加入同一个线程组

对比：
- fork()：clone(SIGCHLD)，不共享任何资源
- pthread_create()：clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD, ...)，共享大部分资源

NPTL（Native POSIX Thread Library）：
- Linux 2.6+ 引入
- 标准的 POSIX 线程实现
- 特点：
  - 1:1 模型（用户线程 : 内核线程）
  - 内核调度
  - 高效的同步原语
```

【面试坑点】

- 不要说"Linux 线程是用户态的"，Linux 是 1:1 模型
- 不要混淆"轻量级进程"和"进程"，LWP 是内核调度单位

【结合项目加分话术】

- "我了解 Linux 是 1:1 线程模型，所以每个 pthread 都会对应一个内核线程，调度开销虽然比 N:M 大，但编程更简单，不会出现用户态调度的问题"

***

#### 2.3.2 【了解】进程的内存布局？

**问题：** 请说明 Linux 进程的内存布局？

**标准答案：**

```
高地址
    ┌─────────────────┐
    │   命令行参数    │ argv/env
    ├─────────────────┤
    │     栈          │ Stack ← 向下增长
    │                 │
    ├─────────────────┤
    │                 │
    │      堆         │ Heap ← 向上增长
    │                 │
    ├─────────────────┤
    │   未初始化数据  │ BSS
    ├─────────────────┤
    │   已初始化数据  │ Data
    ├─────────────────┤
    │      代码       │ Text
    └─────────────────┘
低地址

详细说明：
1. Text Segment（代码段）
   - 存放程序的机器指令
   - 只读、共享
   - 权限：r-x

2. Data Segment（数据段）
   - 存放已初始化的全局变量、静态变量
   - 权限：rw-

3. BSS Segment
   - 存放未初始化的全局变量、静态变量
   - 程序加载时清零
   - 不占用磁盘空间（只记录大小）

4. Heap（堆）
   - 动态分配内存（malloc/new）
   - 向上增长
   - 由程序员管理

5. Stack（栈）
   - 存放局部变量、函数参数、返回地址
   - 向下增长
   - 由编译器管理

6. 命令行参数和环境变量
   - 存放 argv、envp
   - 高地址

Reactor 关联：
- Buffer 一般在堆上分配
- 每个线程有独立的栈
- 线程共享堆、数据段、代码段
```

【面试坑点】

- BSS 段不占用磁盘空间，只在内存中分配并清零
- 栈向下增长，堆向上增长

【结合项目加分话术】

- "我在项目中用了内存池，减少了堆上的频繁分配，降低了内存碎片"
- "排查过一次栈溢出问题，是因为递归太深，后来改成了迭代"

***

## 三、内存管理

### 3.1 核心原理

#### 虚拟内存

```
为什么需要虚拟内存？
1. 隔离进程：每个进程有独立的地址空间
2. 安全：进程不能直接访问物理内存
3. 扩展：可以使用比物理内存更大的地址空间
4. 高效：利用 Copy-On-Write、内存共享等机制

虚拟地址 → 物理地址映射：
- 页表（Page Table）：记录虚拟页 → 物理页的映射
- MMU（Memory Management Unit）：硬件单元，负责地址翻译
- TLB（Translation Lookaside Buffer）：页表缓存，加速翻译

页（Page）：
- 虚拟内存和物理内存的管理单位
- 通常 4KB（也有 2MB/1GB 大页）
- 页内偏移不变

缺页异常（Page Fault）：
- 访问的虚拟页没有对应的物理页
- 类型：
  1. 从未分配：分配物理页
  2. 被交换到磁盘：从磁盘读回（swap in）
  3. Copy-On-Write：复制物理页
```

***

### 3.2 基础必背题

#### 3.2.1 【必考】什么是虚拟内存？有什么用？

**问题：** 请说明虚拟内存的概念和作用？

**标准答案：**

```
虚拟内存概念：
- 操作系统提供的抽象层
- 每个进程看到独立的、连续的地址空间
- 虚拟地址通过页表映射到物理地址

虚拟内存作用：
1. 进程隔离
   - 每个进程有独立的地址空间
   - 进程 A 不能访问进程 B 的内存
   - 提高安全性

2. 地址空间扩展
   - 可以使用比物理内存更大的地址空间
   - 不常用的页可以交换到磁盘（swap）

3. 内存共享
   - 多个进程可以映射同一块物理内存
   - 如共享库、共享内存 IPC

4. Copy-On-Write
   - fork() 时不立即复制内存
   - 只在修改时才复制
   - 减少开销

5. 内存保护
   - 页表设置权限（r/w/x）
   - 访问非法地址会触发段错误（SIGSEGV）

Reactor 关联：
- Buffer 分配在虚拟内存
- 多个线程共享虚拟地址空间
- 大页（Huge Page）可以减少 TLB miss，提高性能
```

【面试坑点】

- 不要把虚拟内存和 swap 混淆，swap 是虚拟内存的一部分功能
- 不要说"虚拟内存比物理内存大"，虚拟地址空间可以比物理内存大，但实际使用受限于物理内存 + swap

【结合项目加分话术】

- "我了解虚拟内存的 Copy-On-Write 机制，所以在 fork() 后，子进程尽量避免马上修改大对象，可以减少内存复制"
- "为了提高性能，尝试过使用大页（Huge Page），减少 TLB miss，内存访问延迟降低了约 10%"

***

#### 3.2.2 【必考】malloc() 的底层原理？

**问题：** 请说明 malloc() 函数的底层实现原理？

**标准答案：**

```
malloc() 不是系统调用，是库函数！

malloc() 实现：
1. 小块内存（< 128KB）：ptmalloc / tcmalloc / jemalloc
   - 内存池机制
   - 预分配多个不同大小的内存块（bins）
   - 避免频繁的系统调用
   
2. 大块内存（≥ 128KB）：mmap()
   - 直接映射文件或匿名内存
   - 避免在堆上分配
   
系统调用：
- brk() / sbrk()：调整堆的大小（向上移动）
- mmap()：映射内存区域
- munmap()：解除映射

ptmalloc（glibc 默认）特点：
- 多线程支持（arena）
- 每个线程有独立的 arena（减少锁竞争）
- 各种大小的 bins（fast bins、small bins、large bins）
- 内存合并、内存回收

常见问题：
1. 内存碎片
   - 外部碎片：空闲内存总量足够，但没有连续的大块
   - 内部碎片：分配的块比实际需要的大
   
2. 内存泄漏
   - malloc() 后忘记 free()
   - 可以用 valgrind、AddressSanitizer 检测

Reactor 关联：
- 频繁分配/释放小 Buffer 会导致内存碎片
- 可以用内存池优化
- tcmalloc/jemalloc 比 ptmalloc 在多线程场景下性能更好
```

【面试坑点】

- 不要说"malloc() 是系统调用"，malloc() 是库函数，内部调用 brk()/mmap()
- 不要忘记 mmap() 分配大块内存

【结合项目加分话术】

- "我在项目中发现 ptmalloc 在多线程高并发时锁竞争严重，后来换成了 tcmalloc，QPS 提升了约 20%"
- "为了避免内存碎片，我实现了一个简单的内存池，专门管理网络 Buffer 的分配和回收，内存碎片率从 30% 降到了 5%"

***

#### 3.2.3 【高频】如何检测内存泄漏？

**问题：** 如何检测和定位内存泄漏？

**标准答案：**

```
工具：
1. valgrind
   - 命令：valgrind --leak-check=full ./program
   - 优点：准确，不需要重新编译
   - 缺点：运行慢（约 10-50 倍），内存开销大
   - 输出：
     - definitely lost：确定泄漏
     - indirectly lost：间接泄漏
     - possibly lost：可能泄漏
     - still reachable：还能访问（不算泄漏）
   
2. AddressSanitizer (ASAN)
   - 编译：-fsanitize=address -g
   - 优点：比 valgrind 快（约 2-5 倍），能检测更多问题
   - 缺点：需要重新编译，内存开销大
   - 能检测：内存泄漏、堆溢出、栈溢出、释放后使用、重复释放
   
3. tcmalloc / jemalloc
   - 自带统计功能
   - 可以查看内存分配情况
   - tcmalloc：HEAPPROFILE=/tmp/profile ./program
   
4. 自定义包装
   - 包装 malloc()/free()，记录调用栈
   - 定期统计未释放的内存

常见内存泄漏场景：
1. 忘记 free()/delete
2. 异常安全：抛异常前没有释放
3. 容器里放指针，清空容器前没有 delete
4. 循环引用（智能指针）
5. fd 泄漏也算资源泄漏

Reactor 关联：
- Buffer 分配后忘记归还到内存池
- 连接断开后没有清理上下文
```

【面试坑点】

- valgrind 的"still reachable"不算泄漏
- ASAN 需要重新编译，生产环境一般不用

【结合项目加分话术】

- "我用 AddressSanitizer 排查过一次内存泄漏，发现是连接断开后没有 delete TcpConnection 对象"
- "在压力测试时，用 valgrind 发现了一个小泄漏，是心跳定时器没有取消，回调中分配的内存没有释放"

***

### 3.3 进阶高频深挖题

#### 3.3.1 【高频】缺页异常的处理流程？

**问题：** 请说明缺页异常（Page Fault）的处理流程？

**标准答案：**

```
缺页异常触发时机：
- 访问的虚拟页没有对应的物理页
- 访问权限不匹配（如写只读页）

缺页异常类型：
1. 次要缺页（Minor Page Fault）
   - 页在内存中，但页表中没有映射
   - 如：共享库的页、Copy-On-Write 的页
   
2. 主要缺页（Major Page Fault）
   - 页不在内存中，需要从磁盘读取
   - 如：第一次访问、被 swap 出去的页
   
处理流程：
1. CPU 访问虚拟地址
2. MMU 查页表，发现没有映射或权限不匹配
3. 触发缺页异常（CPU 陷入内核）
4. 内核缺页异常处理函数执行：
   a. 检查虚拟地址是否合法（在 VMA 范围内）
   b. 检查访问权限是否合法
   c. 如果是 Copy-On-Write：分配新物理页，复制内容
   d. 如果是 Swap：从磁盘读回物理页
   e. 分配物理页，更新页表
   f. 刷新 TLB
5. 异常返回，重新执行触发异常的指令

Copy-On-Write 场景：
1. fork() 时，子进程共享父进程的物理页
2. 页表标记为只读
3. 子进程/父进程修改时，触发缺页异常
4. 分配新物理页，复制内容
5. 更新页表为可写
6. 继续执行

Reactor 关联：
- 频繁的主要缺页会影响性能（需要读磁盘）
- 可以用 mlock() 锁住内存，避免被 swap
- 大页可以减少缺页次数
```

【面试坑点】

- 不要混淆"缺页异常"和"段错误"，缺页异常是正常的，段错误是访问非法地址
- 次要缺页比主要缺页快很多

【结合项目加分话术】

- "我在项目中用 mlock() 锁住了关键的内存区域，避免被 swap 出去，减少了主要缺页，延迟更稳定"
- "有一次性能问题，用 vmstat 发现 in（swap in）很高，说明内存不足，后来增加了物理内存，问题解决"

***

#### 3.3.2 【了解】什么是内存碎片？如何避免？

**问题：** 什么是内存碎片？如何避免和优化？

**标准答案：**

```
内存碎片类型：
1. 外部碎片
   - 定义：空闲内存总量足够，但没有连续的大块
   - 原因：频繁分配和释放不同大小的内存
   - 示例：总空闲 100KB，但都是 10KB 的小块，无法分配 50KB
   
2. 内部碎片
   - 定义：分配的块比实际需要的大
   - 原因：内存对齐、内存池的固定大小
   - 示例：需要 20 字节，分配了 32 字节（对齐）

避免/优化方法：
1. 使用内存池
   - 预分配固定大小的块
   - 避免频繁的 malloc/free
   - Reactor 场景：专门用于 Buffer 分配
   
2. 使用更好的分配器
   - tcmalloc / jemalloc 比 ptmalloc 碎片少
   - 多线程场景更明显
   
3. 延迟分配
   - 用的时候再分配
   - 避免预分配大内存
   
4. 内存紧凑（Compaction）
   - 移动内存块，合并空闲块
   - 如：垃圾回收（GC）
   - 手动实现：Copying GC
   
5. 大页（Huge Page）
   - 减少页表项，减少外部碎片
   - 适合大内存分配

Reactor 关联：
- 频繁分配/释放小 Buffer（4KB-64KB）容易产生碎片
- 内存池是常用的优化手段
- tcmalloc/jemalloc 可以显著减少碎片
```

【面试坑点】

- 不要混淆内部碎片和外部碎片
- 内存碎片不是泄漏，但会导致内存利用率低

【结合项目加分话术】

- "我实现了一个简单的内存池，管理 4KB、8KB、16KB、32KB、64KB 的 Buffer，内存碎片率从 30% 降到了 5%"
- "后来又尝试了 tcmalloc，发现碎片率比自己写的内存池还低，而且性能更好，就换成了 tcmalloc"

***

## 四、文件系统

### 4.1 核心原理

#### VFS（Virtual File System）

```
VFS 作用：
- 抽象层，统一不同文件系统的接口
- 用户程序通过统一的系统调用访问不同文件系统
- 支持：ext4、xfs、btrfs、nfs、proc 等

VFS 核心对象：
1. superblock（超级块）
   - 代表一个文件系统
   - 存储文件系统的元数据
   
2. inode（索引节点）
   - 代表一个文件
   - 存储文件的元数据：大小、权限、时间戳、数据块指针
   - 不存储文件名！
   
3. dentry（目录项）
   - 代表路径中的一个分量
   - 存储：文件名、inode 指针、父目录指针
   - 有缓存（dcache）
   
4. file（文件对象）
   - 代表一个打开的文件
   - 存储：文件偏移、文件操作函数指针
   - 每个 open() 创建一个 file 对象

文件描述符（fd）：
- 进程级的整数，指向 file 对象
- 每个进程有独立的 fd 表
- 0: stdin, 1: stdout, 2: stderr
```

***

### 4.2 基础必背题

#### 4.2.1 【必考】inode 是什么？里面存什么？

**问题：** 请说明 inode 的概念和内容？

**标准答案：**

```
inode（索引节点）：
- 存储文件的元数据
- 每个文件有唯一的 inode
- inode 号在文件系统内唯一

inode 存储内容：
1. 文件类型（普通文件、目录、符号链接...）
2. 文件权限（rwxrwxrwx）
3. 文件大小（字节数）
4. 时间戳（atime、mtime、ctime）
   - atime：最后访问时间
   - mtime：最后修改时间（内容）
   - ctime：最后改变时间（元数据）
5. 链接数（硬链接数）
6. 所有者（UID）
7. 所属组（GID）
8. 数据块指针（指向文件内容的物理块）

inode 不存储：
- 文件名！（文件名存在目录文件里）

查看 inode：
- ls -i：查看文件的 inode 号
- stat <file>：查看 inode 详细信息
- df -i：查看 inode 使用情况

Reactor 关联：
- 日志文件：mtime 可以用来判断是否需要轮转
- 不建议频繁读取 atime（可以用 noatime 挂载选项优化）
```

【面试坑点】

- 不要说"inode 存储文件名"，文件名存在目录文件里
- 不要混淆 atime/mtime/ctime

【结合项目加分话术】

- "我在项目中用了 noatime 挂载选项，避免每次读文件都更新 atime，减少了磁盘 I/O，性能提升了约 5%"
- "日志轮转时，通过 stat() 获取 mtime，判断文件大小是否超过阈值"

***

#### 4.2.2 【必考】硬链接和软链接的区别？

**问题：** 请详细说明硬链接和软链接的区别？

**标准答案：**

```
硬链接（Hard Link）：
- 定义：多个目录项指向同一个 inode
- 特点：
  1. 共享 inode（inode 号相同）
  2. 删除一个不影响其他
  3. 只有当链接数为 0 时才删除文件
  4. 不能跨文件系统
  5. 不能链接目录

软链接（Symbolic Link / Symlink）：
- 定义：独立的文件，存储目标文件的路径
- 特点：
  1. 有独立的 inode
  2. 删除原文件，软链接失效（悬空）
  3. 可以跨文件系统
  4. 可以链接目录

对比：

| 特性 | 硬链接 | 软链接 |
|------|--------|--------|
| inode | 相同 | 不同 |
| 跨文件系统 | ❌ | ✅ |
| 链接目录 | ❌ | ✅ |
| 删除原文件 | 不受影响 | 失效 |
| 额外开销 | 小 | 大（需要读软链接文件） |

命令：
- ln <原文件> <硬链接>
- ln -s <原文件> <软链接>

Reactor 关联：
- 日志文件：可以用硬链接实现原子替换
- 配置文件：可以用软链接方便切换
```

【面试坑点】

- 硬链接不能跨文件系统，因为 inode 号只在一个文件系统内唯一
- 硬链接不能链接目录（防止循环引用）

【结合项目加分话术】

- "我用硬链接实现了日志文件的原子替换：先写新文件，然后 ln 新文件 日志文件，最后删除新文件，避免了日志丢失"

***

#### 4.2.3 【高频】零拷贝（Zero Copy）？

**问题：** 什么是零拷贝？有什么用？

**标准答案：**

```
零拷贝（Zero Copy）：
- 定义：避免数据在用户空间和内核空间之间的拷贝
- 目的：减少 CPU 开销、减少上下文切换

传统方式（4 次拷贝）：
read(file, buf, len);
write(socket, buf, len);

流程：
1. 磁盘 → 内核缓冲区（DMA）
2. 内核缓冲区 → 用户缓冲区（CPU 拷贝）
3. 用户缓冲区 → 内核 Socket 缓冲区（CPU 拷贝）
4. 内核 Socket 缓冲区 → 网卡（DMA）

问题：
- 2 次不必要的 CPU 拷贝
- 4 次上下文切换

零拷贝方式 1：mmap() + write()
1. mmap()：文件映射到用户空间
2. write()：直接从映射区写入

流程：
1. 磁盘 → 内核缓冲区（DMA）
2. 内核缓冲区（共享给用户）
3. 内核缓冲区 → 内核 Socket 缓冲区（CPU 拷贝）
4. 内核 Socket 缓冲区 → 网卡（DMA）

改进：减少 1 次 CPU 拷贝

零拷贝方式 2：sendfile()（Linux 2.1+）
sendfile(socket, file, offset, len);

流程：
1. 磁盘 → 内核缓冲区（DMA）
2. 内核缓冲区 → 内核 Socket 缓冲区（CPU 拷贝）
3. 内核 Socket 缓冲区 → 网卡（DMA）

改进：减少 2 次上下文切换

零拷贝方式 3：sendfile() + DMA Scatter-Gather（Linux 2.4+）
流程：
1. 磁盘 → 内核缓冲区（DMA）
2. 内核缓冲区描述符 → 网卡（DMA Scatter-Gather）
3. 网卡直接从多个内核缓冲区读取

改进：0 次 CPU 拷贝！

零拷贝方式 4：splice()（Linux 2.6+）
- 在两个文件描述符之间移动数据
- 不需要用户空间参与

Reactor 关联：
- 文件下载服务：用 sendfile() 实现零拷贝
- 静态资源服务器：性能提升明显
- 注意：sendfile() 只适用于文件 → Socket，不能 Socket → Socket
```

【面试坑点】

- 零拷贝不是"0 次拷贝"，是"0 次 CPU 拷贝"（DMA 拷贝还是有的）
- sendfile() 不能处理 Socket → Socket（需要 splice()）

【结合项目加分话术】

- "我在静态资源服务器中用了 sendfile() 零拷贝，文件下载的 QPS 提升了约 30%"
- "注意到 sendfile() 有大小限制，所以大文件我还是用 mmap() 的方式"

***

## 五、信号机制

### 5.1 核心原理

```
信号（Signal）：
- 软件中断
- 异步通知机制
- 通知进程发生了某个事件

信号来源：
1. 硬件：如除零、访问非法内存
2. 内核：如定时器到期、子进程终止
3. 进程：如 kill() 系统调用

信号生命周期：
1. 产生（Generation）
2. 未决（Pending）：信号已产生，但未被处理
3. 递送（Delivery）：信号被处理
4. 阻塞（Block）：信号被屏蔽，处于未决状态

信号处理方式：
1. 默认（Default）：终止、忽略、核心转储等
2. 忽略（Ignore）：SIG_IGN
3. 捕获（Catch）：自定义信号处理函数

注意：
- SIGKILL 和 SIGSTOP 不能被捕获、忽略、阻塞
- 信号处理函数要异步信号安全（Async-signal-safe）
```

***

### 5.2 基础必背题

#### 5.2.1 【必考】常见信号？

**问题：** 请列举并说明常见的信号？

**标准答案：**

```
常见信号：

1. SIGHUP (1)
   - 含义：终端挂起
   - 默认：终止进程
   - 用途：守护进程重新加载配置

2. SIGINT (2)
   - 含义：Ctrl+C
   - 默认：终止进程
   - Reactor：优雅关闭信号

3. SIGQUIT (3)
   - 含义：Ctrl+\
   - 默认：终止进程 + 核心转储

4. SIGKILL (9)
   - 含义：强制终止
   - 特点：不能被捕获、忽略、阻塞
   - 慎用！进程无法清理

5. SIGSEGV (11)
   - 含义：段错误（访问非法内存）
   - 默认：终止进程 + 核心转储
   - 原因：空指针解引用、野指针、栈溢出

6. SIGPIPE (13)
   - 含义：写已关闭的管道/Socket
   - 默认：终止进程
   - Reactor：必须处理！忽略或捕获

7. SIGALRM (14)
   - 含义：定时器到期（alarm()）
   - 默认：终止进程
   - 用途：超时控制

8. SIGTERM (15)
   - 含义：终止请求（kill 默认信号）
   - 默认：终止进程
   - Reactor：优雅关闭信号

9. SIGCHLD (17)
   - 含义：子进程终止
   - 默认：忽略
   - 用途：回收子进程，避免僵尸

10. SIGUSR1 (10) / SIGUSR2 (12)
    - 含义：用户自定义信号
    - 默认：终止进程
    - 用途：自定义逻辑（如重新加载配置）
```

【面试坑点】

- 不要记错信号编号，SIGKILL 是 9，SIGTERM 是 15
- 不要说"SIGKILL 可以被捕获"

【结合项目加分话术】

- "我在 TcpServer 中处理了 SIGINT 和 SIGTERM 信号，收到后停止 EventLoop，优雅关闭连接"
- "还处理了 SIGPIPE 信号，设置为 SIG\_IGN，避免写已关闭的 Socket 导致进程崩溃"
- "SIGCHLD 信号处理用来回收子进程，避免僵尸进程"

***

#### 5.2.2 【必考】信号处理函数的注意事项？

**问题：** 写信号处理函数需要注意什么？

**标准答案：**

```
信号处理函数必须是异步信号安全的（Async-signal-safe）！

什么是异步信号安全？
- 可以被信号中断，之后能正确恢复
- 不会因为信号重入导致问题

异步信号安全的函数：
- man 7 signal 查看完整列表
- 常见的：
  - read()、write()
  - _exit()（不是 exit()！）
  - signal()、sigaction()
  - getpid()
  - 等

不安全的函数（不能在信号处理函数中调用）：
- malloc()、free()
- printf()、fprintf()（stdio 函数）
- pthread_mutex_lock()（可能死锁）
- 大多数标准库函数

信号处理函数的最佳实践：
1. 只做简单的事情
   - 设置一个标志位（volatile sig_atomic_t）
   - 主循环检查标志位
   
2. 不要调用非异步信号安全的函数
   - 不要用 printf()，用 write()
   - 不要用 malloc()
   
3. 可重入（Reentrant）
   - 避免使用全局变量
   - 如果用，必须是 volatile sig_atomic_t
   
4. 保存 errno
   - 信号处理函数可能修改 errno
   - 进入时保存，退出时恢复

Reactor 示例：
volatile sig_atomic_t g_running = 1;

void SignalHandler(int sig) {
    g_running = 0;  // 只设置标志位
}

int main() {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    while (g_running) {
        eventLoop.Loop();
    }
    
    // 优雅关闭
    return 0;
}
```

【面试坑点】

- 不要在信号处理函数中调用 printf()、malloc() 等非安全函数
- 标志位要用 volatile sig\_atomic\_t

【结合项目加分话术】

- "我在信号处理函数中只设置一个 volatile sig\_atomic\_t 标志位，主循环检查标志位来决定是否退出"
- "有一次犯了错，在信号处理函数中调用了 printf()，导致程序死锁，后来改成了 write(STDERR\_FILENO, ...)"

***

#### 5.2.3 【高频】signal() 和 sigaction() 的区别？

**问题：** 请说明 signal() 和 sigaction() 的区别？

**标准答案：**

```
signal()：
- 简单的信号设置函数
- 缺点：
  1. 不可靠（早期 Unix 的问题）
  2. 信号处理后会重置为默认行为（System V 语义）
  3. 不能阻塞其他信号
  4. 不同系统行为不同（可移植性差）

sigaction()：
- 推荐使用的函数
- 优点：
  1. 可靠
  2. 可以设置信号处理后不重置
  3. 可以阻塞其他信号（sa_mask）
  4. 可以获取信号详细信息（siginfo_t）
  5. 可移植性好

sigaction 结构体：
struct sigaction {
    void (*sa_handler)(int);              // 信号处理函数
    void (*sa_sigaction)(int, siginfo_t*, void*);  // 带信息的处理函数
    sigset_t sa_mask;                     // 处理时阻塞的信号
    int sa_flags;                         // 标志
};

sa_flags 常用选项：
- SA_RESTART：被信号中断的系统调用自动重启
- SA_SIGINFO：使用 sa_sigaction 而不是 sa_handler
- SA_NODEFER：处理时不阻塞当前信号
- SA_RESETHAND：处理后重置为默认行为

Reactor 推荐：
- 用 sigaction() 而不是 signal()
- 设置 SA_RESTART，避免 EINTR 错误
```

【面试坑点】

- 不要说"signal() 是可靠的"，signal() 在某些系统上不可靠
- 推荐用 sigaction()

【结合项目加分话术】

- "我在项目中用 sigaction() 而不是 signal()，并设置了 SA\_RESTART，这样被信号中断的系统调用会自动重启，避免了处理 EINTR 的麻烦"

***

## 六、TCP/IP 协议栈

### 6.1 核心原理

#### OSI 七层模型 vs TCP/IP 四层模型

```
OSI 七层：
7. 应用层
6. 表示层
5. 会话层
4. 传输层
3. 网络层
2. 数据链路层
1. 物理层

TCP/IP 四层：
4. 应用层（HTTP、FTP、DNS、自定义协议）
3. 传输层（TCP、UDP）
2. 网际层（IP、ICMP、ARP）
1. 网络接口层（以太网、PPP）

数据封装：
应用数据 → TCP 头 → IP 头 → 以太网头 → 传输
             ↓
        Segment（报文段）
                 ↓
            Packet（数据包）
                     ↓
                Frame（帧）

数据解封装：
以太网头 → IP 头 → TCP 头 → 应用数据
```

***

### 6.2 基础必背题

#### 6.2.1 【必考】TCP 三次握手？

**问题：** 请详细说明 TCP 三次握手的过程和原因？

**标准答案：**

```
三次握手过程：

客户端                          服务器
  │                              │
  │  SYN=1, seq=x               │
  │ ────────────────────────────>│
  │                              │
  │  SYN=1, ACK=1, seq=y, ack=x+1│
  │ <────────────────────────────│
  │                              │
  │  ACK=1, seq=x+1, ack=y+1   │
  │ ────────────────────────────>│
  │                              │
  │        连接建立              │

详细说明：
1. 第一次握手（SYN）
   - 客户端发送 SYN 报文
   - SYN=1（同步）
   - seq=x（初始序列号）
   - 客户端进入 SYN_SENT 状态
   
2. 第二次握手（SYN + ACK）
   - 服务器收到 SYN
   - 发送 SYN + ACK 报文
   - SYN=1（同步）
   - ACK=1（确认）
   - seq=y（服务器初始序列号）
   - ack=x+1（确认客户端的 seq）
   - 服务器进入 SYN_RCVD 状态
   
3. 第三次握手（ACK）
   - 客户端收到 SYN + ACK
   - 发送 ACK 报文
   - ACK=1（确认）
   - seq=x+1（客户端序列号）
   - ack=y+1（确认服务器的 seq）
   - 客户端进入 ESTABLISHED 状态
   - 服务器收到 ACK，进入 ESTABLISHED 状态
   - 连接建立完成！

为什么是三次？不是两次？
1. 确认双方的发送和接收能力都正常
   - 一次握手：客户端知道自己能发
   - 二次握手：服务器知道自己能收发，客户端能发
   - 三次握手：客户端知道自己能收发，服务器能收发
   
2. 防止已过期的连接请求
   - 场景：客户端发 SYN，网络延迟，超时重发
   - 服务器收到重发的 SYN，建立连接
   - 之前的 SYN 到达服务器
   - 如果是两次握手，服务器会建立第二个连接
   - 三次握手：客户端会忽略服务器对过期 SYN 的确认

ISN（Initial Sequence Number）：
- 不是 0！
- 随机生成（防止序列号预测攻击）
- 每 4ms 增加 64000
- 2GB 后循环

Reactor 关联：
- 服务器 listen() 后，SYN 报文放在 SYN 队列
- 三次握手完成后，连接放在 accept 队列
- accept() 从 accept 队列取连接
```

【面试坑点】

- 不要说"ISN 是 0"，ISN 是随机生成的
- 不要只画图，要说清楚为什么是三次（确认双方能力、防止过期连接）

【结合项目加分话术】

- "我了解 SYN 队列和 accept 队列，调整了 net.core.somaxconn 和 net.ipv4.tcp\_max\_syn\_backlog 来应对高并发连接"
- "有一次遇到 SYN flood 攻击，用了 net.ipv4.tcp\_syncookies 来缓解"

***

#### 6.2.2 【必考】TCP 四次挥手？

**问题：** 请详细说明 TCP 四次挥手的过程和原因？

**标准答案：**

```
四次挥手过程：

客户端                          服务器
  │                              │
  │  FIN=1, seq=u               │
  │ ────────────────────────────>│
  │                              │
  │  ACK=1, seq=v, ack=u+1     │
  │ <────────────────────────────│
  │                              │
  │  (客户端进入 FIN_WAIT_2)    │
  │                              │
  │  FIN=1, ACK=1, seq=w, ack=u+1│
  │ <────────────────────────────│
  │                              │
  │  ACK=1, seq=u+1, ack=w+1   │
  │ ────────────────────────────>│
  │                              │
  │        连接关闭              │

详细说明：
1. 第一次挥手（FIN）
   - 客户端发送 FIN 报文
   - FIN=1（结束）
   - seq=u（序列号）
   - 客户端进入 FIN_WAIT_1 状态
   - 客户端不再发送数据，但可以接收数据
   
2. 第二次挥手（ACK）
   - 服务器收到 FIN
   - 发送 ACK 报文
   - ACK=1（确认）
   - seq=v（序列号）
   - ack=u+1（确认客户端的 FIN）
   - 服务器进入 CLOSE_WAIT 状态
   - 服务器可能还有数据要发送
   
3. 第三次挥手（FIN + ACK）
   - 服务器数据发送完毕
   - 发送 FIN + ACK 报文
   - FIN=1（结束）
   - ACK=1（确认）
   - seq=w（序列号）
   - ack=u+1（再次确认）
   - 服务器进入 LAST_ACK 状态
   
4. 第四次挥手（ACK）
   - 客户端收到 FIN
   - 发送 ACK 报文
   - ACK=1（确认）
   - seq=u+1（序列号）
   - ack=w+1（确认服务器的 FIN）
   - 客户端进入 TIME_WAIT 状态
   - 服务器收到 ACK，进入 CLOSED 状态
   - 客户端等待 2MSL 后，进入 CLOSED 状态

为什么是四次？不是三次？
- 因为服务器可能还有数据要发送
- FIN 和 ACK 不能合并（和握手不同）
- 挥手是"单向关闭"，每个方向单独 FIN

TIME_WAIT 状态：
- 持续时间：2MSL（Maximum Segment Lifetime，最大报文存活时间）
- MSL 通常：30 秒 - 2 分钟
- Linux 默认：60 秒（可以通过 tcp_fin_timeout 调整）

TIME_WAIT 作用：
1. 保证最后一个 ACK 能到达
   - 如果最后一个 ACK 丢了，服务器会重发 FIN
   - TIME_WAIT 期间可以重发 ACK
   
2. 防止"已过期的报文"
   - 旧连接的报文在网络中延迟
   - 避免被新连接误认为是自己的

TIME_WAIT 过多怎么办？
1. 调整参数：
   - net.ipv4.tcp_tw_reuse = 1（重用 TIME_WAIT 连接）
   - net.ipv4.tcp_tw_recycle = 1（快速回收，不推荐，NAT 环境有问题）
   - net.ipv4.tcp_max_tw_buckets = 65536（最大 TIME_WAIT 数量）
   
2. 短连接改长连接

Reactor 关联：
- 主动关闭的一方会进入 TIME_WAIT
- 服务器一般被动关闭，TIME_WAIT 在客户端
- 但如果服务器主动关闭（如超时踢人），会有 TIME_WAIT
```

【面试坑点】

- 不要把 TIME\_WAIT 和 CLOSE\_WAIT 混淆
- CLOSE\_WAIT 是服务器收到 FIN 后的状态
- TIME\_WAIT 是客户端发完最后 ACK 后的状态
- 不要说"tcp\_tw\_recycle 可以随便开"，NAT 环境有问题

【结合项目加分话术】

- "我在项目中设置了 net.ipv4.tcp\_tw\_reuse = 1，可以重用 TIME\_WAIT 连接，避免端口耗尽"
- "服务器设置了合理的超时踢人策略，避免服务器主动关闭过多连接导致 TIME\_WAIT 堆积"
- "用 ss -s 监控 TIME\_WAIT 数量，超过 10 万时会告警"

***

#### 6.2.3 【必考】TCP 和 UDP 的区别？

**问题：** 请详细说明 TCP 和 UDP 的区别？

**标准答案：**

```
对比：

| 特性 | TCP | UDP |
|------|-----|-----|
| 连接 | 面向连接（三次握手） | 无连接 |
| 可靠性 | 可靠（重传、确认、排序） | 不可靠 |
| 有序性 | 保证有序 | 不保证 |
| 流量控制 | 滑动窗口 | 无 |
| 拥塞控制 | 有 | 无 |
| 首部开销 | 20-60 字节 | 8 字节 |
| 传输方式 | 字节流 | 数据报 |
| 速度 | 慢 | 快 |
| 适用场景 | 文件传输、HTTP、邮件 | 视频、语音、游戏、DNS |

TCP 特点：
1. 面向连接
   - 三次握手建立连接
   - 四次挥手关闭连接
   
2. 可靠传输
   - 确认机制（ACK）
   - 超时重传
   - 序列号 + 确认号
   - 排序（按序列号重组）
   
3. 流量控制
   - 滑动窗口（Sliding Window）
   - 接收方告诉发送方自己能接收多少
   
4. 拥塞控制
   - 慢启动（Slow Start）
   - 拥塞避免（Congestion Avoidance）
   - 快速重传（Fast Retransmit）
   - 快速恢复（Fast Recovery）

UDP 特点：
1. 无连接
   - 不需要建立连接
   - 直接发送数据报
   
2. 不可靠
   - 不确认
   - 不重传
   - 不排序
   
3. 速度快
   - 首部小（8 字节）
   - 没有连接开销
   - 没有重传开销

Reactor 场景选择：
- TCP：需要可靠传输（如聊天、登录、文件传输）
- UDP：实时性要求高、可以容忍丢包（如游戏、音视频）
- 可以结合使用：信令用 TCP，数据用 UDP
```

【面试坑点】

- 不要只说"TCP 可靠，UDP 不可靠"，要说清楚**为什么可靠**（确认、重传、排序、流量控制、拥塞控制）
- 不要说"UDP 一定比 TCP 快"，在丢包率高的网络中，TCP 可能反而快（因为 UDP 应用层重传可能效率低）

【结合项目加分话术】

- "我的项目用 TCP，因为需要可靠的消息传输，不能丢消息"
- "考虑过 UDP，但需要自己实现重传、确认、排序，开发成本高，而且 TCP 的性能已经满足需求"
- "如果未来做实时音视频，可能会用 UDP + QUIC"

***

#### 6.2.4 【必考】TCP 粘包/拆包？

**问题：** 什么是 TCP 粘包/拆包？如何解决？

**标准答案：**

```
TCP 粘包/拆包：
- TCP 是字节流协议，没有消息边界
- 发送方的多个包，接收方可能收到一个包（粘包）
- 发送方的一个包，接收方可能收到多个包（拆包）

原因：
1. 发送方原因：
   - Nagle 算法：小数据包会合并发送
   
2. 接收方原因：
   - 接收缓冲区：数据放在缓冲区，一次 read() 可能读多个包

粘包示例：
发送：[A][B][C]
接收：[ABC]  或  [AB][C]  或  [A][BC]

解决方法：
1. 固定长度
   - 每个消息固定大小
   - 不足补零
   - 缺点：浪费空间
   
2. 特殊分隔符
   - 消息之间用特殊字符分隔（如 \n、\r\n、\0）
   - 缺点：分隔符不能出现在消息内容中
   - 例：HTTP 头部用 \r\n\r\n 分隔
   
3. 长度前缀
   - 消息头 + 消息体
   - 消息头包含消息体长度
   - 优点：灵活、高效
   - 推荐！Reactor 项目常用

长度前缀协议示例（我们的项目）：
┌─────────────────────────────────────────────────────────┐
│  magic(2B) │ version(2B) │ cmd(2B) │ seq(4B) │ len(4B) │
├─────────────────────────────────────────────────────────┤
│                    data(len 字节)                        │
└─────────────────────────────────────────────────────────┘

解析流程：
1. 先读 14 字节固定头部
2. 从头部获取 len（消息体长度）
3. 再读 len 字节消息体
4. 完整消息交给业务逻辑

Buffer 设计：
- 可扩展的缓冲区
- 可读区域 + 可写区域
- write() 追加数据到可写区域
- read() 从可读区域消费数据
- 内部可能有扩容、移动数据

Reactor 关联：
- Codec 模块负责处理粘包/拆包
- Buffer 支持 prependable/writable 区域
- 解析时先看可读区域是否够一个完整包
- 不够就继续收，够了就解析
```

【面试坑点】

- 不要说"TCP 有消息边界"，TCP 是字节流，没有消息边界
- 不要混淆"粘包"和"丢包"，粘包是字节流问题，丢包是可靠性问题

【结合项目加分话术】

- "我的项目用了长度前缀 + magic 来解决粘包问题，先读固定头部，再读消息体"
- "Codec 模块和 Buffer 配合，Buffer 有可读区域和可写区域，每次 onMessage 先检查可读区域是否够一个完整包"
- "还加了 magic 来做简单的校验，避免解析非法数据"

***

### 6.3 进阶高频深挖题

#### 6.3.1 【高频】TCP 流量控制（滑动窗口）？

**问题：** 请说明 TCP 的流量控制（滑动窗口）机制？

**标准答案：**

```
流量控制（Flow Control）：
- 目的：防止发送方发太快，淹没接收方
- 机制：滑动窗口（Sliding Window）

滑动窗口：
- 接收方在 ACK 中告诉发送方自己的接收窗口大小（rwnd）
- 发送方根据 rwnd 调整发送速度
- 窗口大小动态变化

窗口字段：
- TCP 首部有 16 位的窗口字段（Window Size）
- 最多表示 65535 字节
- 窗口缩放选项（Window Scale）：可以扩大到 1GB

发送窗口：
- 由接收窗口（rwnd）和拥塞窗口（cwnd）共同决定
- 发送窗口 = min(rwnd, cwnd)

滑动过程：

接收方缓冲区：
┌─────────────────────────────────────────┐
│  已接收  │  已接收  │  可用      │
│  未读取  │  未读取  │  空间      │
└─────────────────────────────────────────┘
     ↑                    ↑
  lastByteRead      lastByteRcvd
     
接收窗口大小 = 缓冲区大小 - (lastByteRcvd - lastByteRead)

发送方发送：
- 可以发送：[lastByteAcked, lastByteAcked + 发送窗口)
- 发送后等待 ACK
- ACK 到达后，窗口向右滑动

零窗口（Zero Window）：
- 接收方缓冲区满了，rwnd = 0
- 发送方停止发送
- 接收方有空间后，发送窗口更新
- 持续计时器（Persist Timer）：防止窗口更新丢失

Reactor 关联：
- 可以通过调整 SO_SNDBUF 和 SO_RCVBUF 来优化
- 大文件传输时，滑动窗口很重要
```

【面试坑点】

- 不要把"流量控制"和"拥塞控制"混淆
- 流量控制：防止发送方淹没接收方（端到端）
- 拥塞控制：防止发送方淹没网络（网络全局）

【结合项目加分话术】

- "我在项目中设置了 SO\_RCVBUF 和 SO\_SNDBUF 为 256KB，优化了大消息传输"
- "监控过 TCP 的零窗口情况，发现是接收方处理慢导致的，优化了接收端的处理逻辑"

***

#### 6.3.2 【高频】TCP 拥塞控制？

**问题：** 请说明 TCP 的拥塞控制机制？

**标准答案：**

```
拥塞控制（Congestion Control）：
- 目的：防止发送方发太快，淹没网络
- 假设：丢包是因为网络拥塞
- 目标：最大化利用带宽，同时不造成拥塞

四个核心算法：
1. 慢启动（Slow Start）
2. 拥塞避免（Congestion Avoidance）
3. 快速重传（Fast Retransmit）
4. 快速恢复（Fast Recovery）

1. 慢启动（Slow Start）：
- 初始拥塞窗口 cwnd = 1 MSS（Maximum Segment Size）
- 每收到一个 ACK，cwnd += 1（指数增长）
- cwnd 增长：1 → 2 → 4 → 8 → 16 → ...
- 到达慢启动阈值（ssthresh）后，进入拥塞避免
- 或者：丢包（超时），进入慢启动

2. 拥塞避免（Congestion Avoidance）：
- cwnd > ssthresh
- 每经过一个 RTT（往返时间），cwnd += 1（线性增长）
- 目标：缓慢增长，避免拥塞

3. 快速重传（Fast Retransmit）：
- 收到 3 个重复 ACK（Dup ACK）
- 不等超时，立即重传丢失的包
- 比超时重传快很多

4. 快速恢复（Fast Recovery）：
- 快速重传后，不进入慢启动
- ssthresh = cwnd / 2
- cwnd = ssthresh + 3（因为收到 3 个 Dup ACK）
- 然后进入拥塞避免
- 目标：避免网络利用率骤降

拥塞控制总结：

情况 1：超时重传
- ssthresh = cwnd / 2
- cwnd = 1 MSS
- 进入慢启动

情况 2：快速重传（3 个 Dup ACK）
- ssthresh = cwnd / 2
- cwnd = ssthresh + 3
- 进入快速恢复，然后拥塞避免

TCP 拥塞控制算法演进：
- Tahoe：慢启动、拥塞避免、快速重传
- Reno：增加快速恢复
- NewReno：改进快速恢复
- Cubic：当前 Linux 默认（更适合高带宽长延迟网络）
- BBR：Google 开源，基于带宽和延迟（较新）

Reactor 关联：
- 可以通过 net.ipv4.tcp_congestion_control 选择算法
- Cubic 一般是默认，适合大多数场景
- BBR 在高带宽长延迟网络（如数据中心）可能更好
```

【面试坑点】

- 不要把"慢启动"理解为"启动慢"，慢启动是指数增长，很快！
- 不要混淆"超时重传"和"快速重传"

【结合项目加分话术】

- "我了解 TCP 的拥塞控制，慢启动、拥塞避免、快速重传、快速恢复"
- "服务器上用的是 Cubic 算法，在跨地域传输时性能不错"
- "监控过重传率，超过 1% 时会告警，排查网络问题"

***

## 七、网络编程

***

### 7.1 核心原理

#### Socket 编程基础

```
Socket 概念：
- 套接字，网络通信的端点
- 一个 Socket 由 IP 地址 + 端口号 唯一标识
- Linux 中一切皆文件，Socket 也是文件描述符（fd）

Socket 类型：
1. SOCK_STREAM（流套接字）
   - TCP
   - 面向连接、可靠、字节流
   
2. SOCK_DGRAM（数据报套接字）
   - UDP
   - 无连接、不可靠、数据报
   
3. SOCK_RAW（原始套接字）
   - 直接访问 IP 层
   - 用于实现自定义协议

网络字节序：
- 大端序（Big Endian）：高位在低地址
- 网络协议统一使用大端序
- 主机字节序可能是大端或小端（x86 是小端）
- 需要转换：htonl()、htons()、ntohl()、ntohs()
```

#### 服务器端 Socket 编程流程

```
TCP 服务器端：
1. socket()       → 创建 Socket
2. bind()         → 绑定 IP 和端口
3. listen()       → 开始监听
4. accept()       → 接受连接（阻塞，返回新 Socket）
5. recv()/send()  → 收发数据
6. close()        → 关闭连接

TCP 客户端：
1. socket()       → 创建 Socket
2. connect()      → 连接服务器
3. send()/recv()  → 收发数据
4. close()        → 关闭连接
```

***

### 7.2 基础必背题

#### 7.2.1 【必考】Socket 编程的基本流程？

**问题：** 请说明 TCP Socket 服务器端和客户端的编程流程？

**标准答案：**

```
TCP 服务器端流程：
1. socket()
   - 作用：创建 Socket
   - 参数：AF_INET（IPv4）、SOCK_STREAM（TCP）、0
   - 返回：文件描述符 fd

2. bind()
   - 作用：绑定 IP 地址和端口
   - 参数：fd、sockaddr_in（包含 IP:Port）、地址长度
   - 注意：IP 可以是 INADDR_ANY（监听所有网卡）

3. listen()
   - 作用：开始监听，转为被动 Socket
   - 参数：fd、backlog（全连接队列大小）
   - backlog：待 accept 的连接队列长度

4. accept()
   - 作用：接受连接（阻塞）
   - 返回：新的 fd（用于和客户端通信）
   - 原 fd 继续监听

5. recv()/send()
   - 作用：收发数据
   - recv()：从 Socket 读取数据
   - send()：向 Socket 写入数据

6. close()
   - 作用：关闭 Socket
   - 关闭通信 fd，监听 fd 继续使用

TCP 客户端流程：
1. socket()
   - 同服务器

2. connect()
   - 作用：连接服务器
   - 参数：fd、服务器 sockaddr_in、地址长度
   - 阻塞直到连接建立或失败

3. send()/recv()
   - 同服务器

4. close()
   - 同服务器
```

【面试坑点】

- 不要混淆"监听 fd"和"通信 fd"，accept() 返回的是新 fd
- bind() 可以绑定 INADDR\_ANY（0.0.0.0）表示监听所有网卡
- listen() 的 backlog 参数是全连接队列大小

【结合项目加分话术】

- "我的 TcpServer 实现了这个流程，主线程负责 accept()，然后把连接分发给工作线程"
- "注意到 accept() 是阻塞的，所以用 epoll 监听 listen fd 的可读事件"

***

#### 7.2.2 【必考】listen() 的 backlog 参数？

**问题：** 请说明 listen() 函数的 backlog 参数的含义？

**标准答案：**

```
backlog 参数：
- 定义：全连接队列（accept 队列）的最大长度
- 全连接队列：三次握手完成，等待 accept() 的连接

两个队列：
1. SYN 队列（半连接队列）
   - 收到 SYN，发送 SYN+ACK，等待 ACK
   - 大小：net.ipv4.tcp_max_syn_backlog
   
2. ACCEPT 队列（全连接队列）
   - 三次握手完成，等待 accept()
   - 大小：listen() 的 backlog 参数
   - 受限于：net.core.somaxconn（系统级上限）

backlog 实际值：
- min(backlog, net.core.somaxconn)
- net.core.somaxconn 默认：128
- 高并发场景需要调大

队列满了怎么办？
- SYN 队列满：丢弃 SYN，或发送 SYN+ACK（取决于 tcp_syncookies）
- ACCEPT 队列满：忽略第三次握手的 ACK，服务器重发 SYN+ACK

Reactor 关联：
- 高并发场景需要调大 backlog
- 同时调大 net.core.somaxconn
- 用 ss -lnt 查看 Listen 状态的 Recv-Q（全连接队列当前长度）
```

【面试坑点】

- 不要混淆"半连接队列"和"全连接队列"
- backlog 不是"最大连接数"，是"等待 accept 的连接数"
- backlog 受限于 net.core.somaxconn

【结合项目加分话术】

- "我在项目中把 backlog 设为 1024，同时调大了 net.core.somaxconn 到 65535"
- "用 ss -lnt 监控全连接队列的长度，避免队列满导致连接被拒绝"

***

#### 7.2.3 【高频】TCP 的 SO\_REUSEADDR 选项？

**问题：** 请说明 SO\_REUSEADDR 选项的作用？

**标准答案：**

```
SO_REUSEADDR 作用：
1. 允许绑定 TIME_WAIT 状态的端口
   - 服务器重启时，之前的连接可能还在 TIME_WAIT
   - 不设置 SO_REUSEADDR，bind() 会失败（Address already in use）
   - 设置后，可以立即绑定

2. 允许绑定通配地址和具体地址
   - 可以先绑定 0.0.0.0:8080
   - 再绑定 192.168.1.1:8080
   - 不设置会失败

3. 允许同一端口的多个 Socket（UDP 多播）
   - UDP 场景：多个进程绑定同一端口接收多播

什么时候需要设置？
- TCP 服务器：必须设置！
- 服务器重启时避免 bind() 失败

设置方法：
int opt = 1;
setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
// 注意：在 bind() 之前设置！

SO_REUSEPORT（Linux 3.9+）：
- 允许多个进程绑定同一端口
- 每个进程有独立的 Socket
- 内核负载均衡分发连接
- 适合多进程服务器

Reactor 关联：
- TcpServer 必须设置 SO_REUSEADDR
- 多进程模型可以用 SO_REUSEPORT
```

【面试坑点】

- 不要忘记在 bind() 之前设置
- 不要混淆 SO\_REUSEADDR 和 SO\_REUSEPORT
- SO\_REUSEPORT 需要 Linux 3.9+

【结合项目加分话术】

- "我的 TcpServer 在 bind() 之前设置了 SO\_REUSEADDR，避免重启时 bind() 失败"
- "考虑过多进程模型用 SO\_REUSEPORT，让内核做负载均衡，但最后用了多线程 Reactor"

***

#### 7.2.4 【高频】TCP 的阻塞和非阻塞模式？

**问题：** 请说明 TCP Socket 的阻塞模式和非阻塞模式的区别？

**标准答案：**

```
阻塞模式（默认）：
- 系统调用会阻塞直到完成
- 如：accept() 阻塞直到有连接
- 如：recv() 阻塞直到有数据
- 如：send() 阻塞直到发送缓冲区有空间

非阻塞模式：
- 系统调用立即返回，不管是否完成
- 成功：返回实际读写的字节数
- 失败：返回 -1，errno = EAGAIN 或 EWOULDBLOCK
- 需要配合 IO 多路复用（select/poll/epoll）使用

设置非阻塞：
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

各函数在非阻塞模式下的行为：
1. accept()
   - 无连接：返回 -1，errno = EAGAIN
   - 有连接：返回新 fd

2. connect()
   - 立即返回 -1，errno = EINPROGRESS
   - 需要用 select/poll/epoll 监听可写事件
   - 可写时，用 getsockopt(SO_ERROR) 检查是否成功

3. recv()
   - 无数据：返回 -1，errno = EAGAIN
   - 有数据：返回读到的字节数
   - 对端关闭：返回 0

4. send()
   - 发送缓冲区满：返回 -1，errno = EAGAIN
   - 有空间：返回写入的字节数

Reactor 关联：
- 必须用非阻塞 Socket！
- 配合 epoll 使用
- 避免阻塞整个 EventLoop
```

【面试坑点】

- 不要在非阻塞模式下用阻塞的思维
- connect() 在非阻塞模式下返回 EINPROGRESS，不是错误
- 一定要检查 errno

【结合项目加分话术】

- "我的 TcpServer 所有 Socket 都是非阻塞的，配合 epoll 使用"
- "处理非阻塞 connect() 时，监听可写事件，然后用 getsockopt(SO\_ERROR) 检查连接是否成功"

***

### 7.3 进阶高频深挖题

#### 7.3.1 【高频】TCP 的 Nagle 算法？

**问题：** 请说明 Nagle 算法的作用和优缺点？

**标准答案：**

```
Nagle 算法：
- 目的：减少网络中的小数据包数量
- 发明者：John Nagle（1984）
- 场景：telnet、rlogin 等交互式应用

算法规则：
1. 如果有未确认的数据（即有数据在传输中）
   - 缓冲新数据，直到收到 ACK 或数据积累到 MSS
   
2. 如果没有未确认的数据
   - 立即发送

伪代码：
if (有未确认的数据) {
    缓冲新数据;
} else {
    立即发送;
}

优点：
- 减少小数据包数量
- 降低网络拥塞
- 提高带宽利用率

缺点：
- 增加延迟
- 不适合实时性要求高的场景

禁用 Nagle：
- TCP_NODELAY 选项
- setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

什么时候禁用？
- 实时性要求高：游戏、音视频
- 已经自己处理粘包：发送大消息
- Reactor 服务器：一般禁用

Reactor 关联：
- 推荐禁用 Nagle（设置 TCP_NODELAY）
- 因为我们自己处理粘包，不需要 Nagle 合并
- 降低延迟
```

【面试坑点】

- 不要说"Nagle 算法总是好的"，要看场景
- TCP\_NODELAY 是禁用 Nagle，不是启用

【结合项目加分话术】

- "我的 TcpServer 禁用了 Nagle 算法（设置 TCP\_NODELAY），降低延迟"
- "因为我们用长度前缀处理粘包，不需要 Nagle 来合并小包"

***

#### 7.3.2 【高频】TCP 的心跳保活（Keep-Alive）？

**问题：** 请说明 TCP Keep-Alive 的作用和原理？

**标准答案：**

```
TCP Keep-Alive：
- 作用：检测连接是否存活
- 场景：长连接、防火墙超时
- 注意：TCP Keep-Alive 不是 TCP 标准的一部分，是可选的

原理：
1. 发送 Keep-Alive 探测包
   - 空的 ACK 包
   - 序列号 = 对方最后一次 ACK 的序列号 - 1
   
2. 对方响应
   - 连接存活：响应 ACK
   - 连接断开：响应 RST
   - 无响应：继续发送探测包

Linux 参数：
1. net.ipv4.tcp_keepalive_time = 7200
   - 空闲多久开始发送探测包（秒），默认 2 小时
   
2. net.ipv4.tcp_keepalive_intvl = 75
   - 探测包间隔（秒），默认 75 秒
   
3. net.ipv4.tcp_keepalive_probes = 9
   - 探测次数，默认 9 次
   - 9 次都没响应，认为连接断开

设置 Socket 选项：
- SO_KEEPALIVE：启用 Keep-Alive
- TCP_KEEPIDLE：对应 tcp_keepalive_time
- TCP_KEEPINTVL：对应 tcp_keepalive_intvl
- TCP_KEEPCNT：对应 tcp_keepalive_probes

代码示例：
int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

int idle = 60;  // 60 秒空闲开始探测
setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));

int intvl = 10;  // 10 秒间隔
setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));

int cnt = 3;  // 3 次
setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));

应用层心跳 vs TCP Keep-Alive：
TCP Keep-Alive：
- 优点：系统层面，不需要业务代码
- 缺点：只能检测连接是否存活，不能检测业务是否存活
- 缺点：参数默认值太大（2 小时）

应用层心跳：
- 优点：灵活，可以检测业务状态
- 优点：可以自定义超时时间
- 缺点：需要业务代码

Reactor 关联：
- 推荐用应用层心跳，而不是 TCP Keep-Alive
- 应用层心跳可以检测业务是否正常
- 可以自定义心跳包、超时时间
```

【面试坑点】

- TCP Keep-Alive 默认是关闭的
- TCP Keep-Alive 不能替代应用层心跳
- 默认参数 2 小时太长，不适合服务器

【结合项目加分话术】

- "我的 TcpServer 用应用层心跳，而不是 TCP Keep-Alive"
- "心跳包 30 秒一次，超时 90 秒，超过 3 次没收到心跳就断开连接"
- "心跳包还可以用来做负载信息上报"

***

#### 7.3.3 【了解】TCP 的紧急数据（OOB）？

**问题：** 请说明 TCP 紧急数据（Out-of-Band）的作用？

**标准答案：**

```
TCP 紧急数据（OOB）：
- 作用：发送高优先级数据
- 带外数据，不经过普通数据流
- 实际上是在普通数据流中标记一个位置

TCP 头部 URG 标志：
- URG=1：表示有紧急数据
- 紧急指针：指向紧急数据的下一个字节
- 紧急数据只有 1 字节！
- 就是紧急指针之前的那个字节

发送 OOB：
send(fd, data, len, MSG_OOB);

接收 OOB：
recv(fd, data, len, MSG_OOB);
// 或者用 SIGURG 信号

缺点：
- 只有 1 字节紧急数据
- 实现复杂
- 很少用

替代方案：
- 应用层协议：专门的消息类型
- 优先级队列：业务层处理

Reactor 关联：
- 一般不用 OOB
- 用应用层协议处理优先级
```

【面试坑点】

- 紧急数据只有 1 字节，不是想象的"带外通道"
- 实际很少用

【结合项目加分话术】

- "我了解 OOB，但实际项目中不用，用应用层协议处理优先级"

***

## （本章完）

## 八、IO多路复用与IO模型

***

### 8.1 核心原理

#### 5 种 IO 模型

```
Unix 下 5 种 IO 模型：
1. 阻塞 IO（Blocking IO）
2. 非阻塞 IO（Non-blocking IO）
3. IO 多路复用（IO Multiplexing）
4. 信号驱动 IO（Signal Driven IO）
5. 异步 IO（Asynchronous IO）

两个阶段：
1. 等待数据准备好（Wait for data to be ready）
2. 将数据从内核拷贝到用户空间（Copy data from kernel to user）
```

#### Reactor 模式

```
Reactor 模式：
- 事件驱动模式
- 基于 IO 多路复用
- 核心组件：
  1. Reactor（反应堆）：事件分发器
  2. Acceptor：接受连接
  3. EventHandler：事件处理器
  4. Handle：事件源（fd）

Reactor 模式变种：
1. 单 Reactor 单线程
   - 一个 EventLoop 处理所有事件
   - 简单，但不适合 CPU 密集型

2. 单 Reactor 多线程
   - 一个 EventLoop 处理 IO 事件
   - 线程池处理业务逻辑

3. 多 Reactor 多线程（推荐！）
   - mainReactor：处理 accept 事件
   - subReactors：处理读写事件
   - 每个 subReactor 一个线程
   - muduo、Netty、libuv 都是这种模式
```

***

### 8.2 基础必背题

#### 8.2.1 【必考】5 种 IO 模型？

**问题：** 请详细说明 Unix 下的 5 种 IO 模型？

**标准答案：**

```
5 种 IO 模型：

1. 阻塞 IO（Blocking IO）
   - 流程：
     1. 调用 recv()，阻塞等待数据
     2. 数据到达，内核拷贝到用户空间
     3. recv() 返回
   - 特点：两个阶段都阻塞
   - 缺点：一个线程处理一个连接，并发能力差

2. 非阻塞 IO（Non-blocking IO）
   - 流程：
     1. 调用 recv()，立即返回（有数据就读，没数据返回 EAGAIN）
     2. 轮询调用 recv()，直到数据到达
     3. 内核拷贝到用户空间
   - 特点：第一阶段不阻塞，轮询；第二阶段阻塞
   - 缺点：CPU 忙等待，浪费资源

3. IO 多路复用（IO Multiplexing）
   - 流程：
     1. 调用 select/poll/epoll，阻塞等待多个 fd 就绪
     2. 某个 fd 就绪，返回
     3. 调用 recv()，内核拷贝到用户空间
   - 特点：第一阶段阻塞在 select/poll/epoll，第二阶段阻塞在 recv()
   - 优点：一个线程可以处理多个连接，并发能力强
   - Reactor 模式的基础！

4. 信号驱动 IO（Signal Driven IO）
   - 流程：
     1. 注册信号处理函数（SIGIO）
     2. 立即返回
     3. 数据到达，内核发送 SIGIO 信号
     4. 信号处理函数中调用 recv()
     5. 内核拷贝到用户空间
   - 特点：第一阶段不阻塞，第二阶段阻塞
   - 缺点：信号处理函数复杂，很少用

5. 异步 IO（Asynchronous IO，AIO）
   - 流程：
     1. 调用 aio_read()，立即返回
     2. 内核等待数据 + 拷贝到用户空间
     3. 内核发信号或回调通知
   - 特点：两个阶段都不阻塞！
   - 真正的异步 IO
   - Proactor 模式的基础
   - 缺点：Linux 下实现不完善，很少用

对比总结：
| 模型 | 等待数据 | 拷贝数据 | 阻塞次数 |
|------|----------|----------|----------|
| 阻塞 IO | 阻塞 | 阻塞 | 2 |
| 非阻塞 IO | 非阻塞（轮询） | 阻塞 | 1 |
| IO 多路复用 | 阻塞 | 阻塞 | 2 |
| 信号驱动 IO | 非阻塞 | 阻塞 | 1 |
| 异步 IO | 非阻塞 | 非阻塞 | 0 |

Reactor 关联：
- 用 IO 多路复用（第 3 种）
- Proactor 用异步 IO（第 5 种），但 Linux 下很少用
```

【面试坑点】

- IO 多路复用不是"非阻塞 IO"，它是阻塞在 select/poll/epoll 上
- 异步 IO 才是真正的非阻塞，两个阶段都不阻塞
- Linux 下 AIO 不完善，实际项目还是用 IO 多路复用

【结合项目加分话术】

- "我的项目用 IO 多路复用（epoll），一个线程可以处理数千个连接"
- "了解过异步 IO，但 Linux 下实现不完善，还是用 Reactor 模式"

***

#### 8.2.2 【必考】select、poll、epoll 的区别？

**问题：** 请详细说明 select、poll、epoll 的区别？

**标准答案：**

```
select：
1. 原理：
   - 三个 fd_set（读、写、异常）
   - 用户态 → 内核态 拷贝 fd_set
   - 内核遍历 fd_set，检查就绪
   - 内核修改 fd_set，返回用户态
   
2. 缺点：
   - 最大 fd 限制：1024（FD_SETSIZE）
   - 用户态 ↔ 内核态 拷贝开销大
   - 内核遍历所有 fd，O(n) 复杂度
   - 返回后需要遍历所有 fd，找到就绪的
   
3. 优点：
   - 跨平台（Windows、Linux、Unix）
   - 简单

poll：
1. 原理：
   - pollfd 数组（代替 fd_set）
   - 用户态 → 内核态 拷贝 pollfd 数组
   - 内核遍历 pollfd 数组，检查就绪
   - 内核修改 revents 字段，返回用户态
   
2. 缺点：
   - 用户态 ↔ 内核态 拷贝开销大
   - 内核遍历所有 fd，O(n) 复杂度
   - 返回后需要遍历所有 fd，找到就绪的
   
3. 优点：
   - 没有最大 fd 限制（基于数组）
   - 比 select 灵活

epoll（Linux 2.6+，推荐！）：
1. 三个系统调用：
   a. epoll_create()
      - 创建 epoll 实例（红黑树 + 就绪链表）
      - 返回 epfd
    
   b. epoll_ctl()
      - 注册/修改/删除 fd
      - 操作红黑树
      - fd 上注册回调函数
    
   c. epoll_wait()
      - 等待就绪事件
      - 只返回就绪的 fd
      - 从就绪链表取，O(1) 复杂度
   
2. 两种工作模式：
   a. LT（Level Triggered，水平触发，默认）
      - 只要 fd 可读/可写，就会一直通知
      - 类似 select/poll
      - 编程简单，不容易出错
    
   b. ET（Edge Triggered，边缘触发）
      - 只在状态变化时通知一次
      - 必须一次性把数据读完（while 循环读）
      - 减少 epoll_wait 调用次数，效率更高
      - 编程复杂，容易出错
   
3. 优点：
   - 没有最大 fd 限制
   - 用户态 ↔ 内核态 拷贝开销小（只拷贝一次）
   - 内核只遍历就绪的 fd，O(1) 复杂度
   - 返回的就是就绪的 fd，不需要遍历所有
   - 高并发场景性能远好于 select/poll
   
4. 缺点：
   - 只支持 Linux
   - ET 模式编程复杂

对比总结：
| 特性 | select | poll | epoll |
|------|--------|------|-------|
| 最大 fd 限制 | 1024 | 无 | 无 |
| 用户态 ↔ 内核态拷贝 | 每次调用 | 每次调用 | 只一次（注册时） |
| 查找就绪 fd 复杂度 | O(n) | O(n) | O(1) |
| 跨平台 | ✅ | ✅ | ❌（仅 Linux） |
| LT/ET 模式 | ❌ | ❌ | ✅ |

Reactor 关联：
- 必须用 epoll！
- muduo 用 LT 模式，简单可靠
- 追求极致性能可以用 ET 模式
```

【面试坑点】

- epoll 不是"非阻塞"，epoll\_wait() 是阻塞的
- LT 模式是默认，ET 模式需要设置 EPOLLET
- ET 模式必须一次性把数据读完，否则不会再通知

【结合项目加分话术】

- "我的 TcpServer 用 epoll LT 模式，简单可靠，不容易出错"
- "了解过 ET 模式，性能更好，但编程复杂，需要 while 循环读"
- "对比过 select/poll/epoll，epoll 在 1 万连接时性能优势明显"

***

#### 8.2.3 【必考】Reactor 模式？

**问题：** 请说明 Reactor 模式的原理和常见变种？

**标准答案：**

```
Reactor 模式：
- 事件驱动模式
- 基于 IO 多路复用
- 核心思想：分而治之，事件分发

核心组件：
1. Handle（事件源）
   - 文件描述符 fd
   - 事件源：可读、可写、异常
   
2. EventHandler（事件处理器）
   - 处理事件的回调函数
   - 如：handleRead()、handleWrite()
   
3. Reactor（反应堆/事件分发器）
   - 核心：IO 多路复用（epoll）
   - 注册/删除事件
   - 事件循环（Event Loop）
   - 分发事件给对应的 EventHandler
   
4. Acceptor（接受器）
   - 处理新连接
   - accept() 新连接
   - 创建 TcpConnection
   - 注册到 Reactor

事件循环（Event Loop）：
while (running) {
    activeEvents = epoll_wait(...);  // 等待事件
    for (event : activeEvents) {
        eventHandler.handleEvent(event);  // 分发事件
    }
}

Reactor 模式变种：

1. 单 Reactor 单线程
   ┌─────────────────────────────────┐
   │     EventLoop（单线程）          │
   │  ┌─────┐  ┌─────┐  ┌─────┐   │
   │  │Accept│  │Conn1│  │Conn2│   │
   │  └─────┘  └─────┘  └─────┘   │
   └─────────────────────────────────┘
   - 一个 EventLoop 处理所有事件
   - 优点：简单，没有线程安全问题
   - 缺点：CPU 密集型业务会阻塞 IO
   - 适用：Redis（单线程模型）

2. 单 Reactor 多线程
   ┌─────────────────────────────────┐
   │     EventLoop（IO 线程）         │
   │  ┌─────┐  ┌─────┐  ┌─────┐   │
   │  │Accept│  │Conn1│  │Conn2│   │
   │  └─────┘  └─────┘  └─────┘   │
   └────────┬────────────────────────┘
            │ 分发任务
            ↓
   ┌─────────────────────────────────┐
   │         线程池                  │
   │  ┌─────┐  ┌─────┐  ┌─────┐   │
   │  │Thread│  │Thread│  │Thread│   │
   │  └─────┘  └─────┘  └─────┘   │
   └─────────────────────────────────┘
   - 一个 EventLoop 处理 IO 事件
   - 线程池处理业务逻辑
   - 优点：IO 和业务分离
   - 缺点：线程安全问题复杂

3. 多 Reactor 多线程（推荐！muduo、Netty）
   ┌─────────────────────────────────┐
   │   mainReactor（主线程）          │
   │         ┌─────┐                  │
   │         │Accept│                  │
   │         └─────┘                  │
   └────────────┬─────────────────────┘
                │ 分发连接
                ↓
   ┌─────────────────────────────────────────┐
   │      subReactor 1      │ subReactor 2  │
   │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐   │
   │  │Conn1│ │Conn2│ │Conn3│ │Conn4│   │
   │  └─────┘ └─────┘ └─────┘ └─────┘   │
   └─────────────────────────────────────────┘
   - mainReactor：处理 accept 事件
   - subReactors：处理读写事件（每个 subReactor 一个线程）
   - 负载均衡：轮询分发连接给 subReactor
   - 优点：
     - 充分利用多核
     - 线程安全问题简单（每个 subReactor 一个线程）
     - 性能好
   - muduo、Netty、libuv 都是这种模式

Reactor 关联：
- 必须用多 Reactor 多线程！
- muduo 的 One Loop Per Thread 模型
```

【面试坑点】

- Reactor 不是"单线程"，多 Reactor 是多线程的
- 不要混淆 Reactor 和 Proactor
- Proactor 是异步 IO，Linux 下很少用

【结合项目加分话术】

- "我的 TcpServer 用多 Reactor 多线程模式，mainReactor accept 连接，轮询分发给 subReactor"
- "每个 subReactor 一个线程，处理读写事件，线程安全问题简单"
- "参考了 muduo 的 One Loop Per Thread 模型"

***

#### 8.2.4 【高频】epoll 的 LT 和 ET 模式？

**问题：** 请说明 epoll 的 LT（水平触发）和 ET（边缘触发）模式的区别？

**标准答案：**

```
LT（Level Triggered，水平触发，默认）：
- 定义：只要 fd 可读/可写，就会一直通知
- 类似 select/poll
- 行为：
  - 读缓冲区有数据 → 一直通知可读
  - 写缓冲区有空间 → 一直通知可写
- 优点：
  - 编程简单
  - 不容易出错
- 缺点：
  - 可能多次通知同一个事件
  - 效率比 ET 低

ET（Edge Triggered，边缘触发）：
- 定义：只在状态变化时通知一次
- 行为：
  - 读缓冲区从无到有 → 通知一次
  - 写缓冲区从满到有空间 → 通知一次
- 要求：
  - 必须一次性把数据读完！（while 循环读）
  - 必须用非阻塞 Socket！
- 优点：
  - 减少 epoll_wait 调用次数
  - 效率更高
- 缺点：
  - 编程复杂
  - 容易出错（漏读数据）

设置 ET 模式：
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET;  // 加上 EPOLLET
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

ET 模式读数据（必须 while 循环）：
while (true) {
    int n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
        // 处理数据
    } else if (n == 0) {
        // 连接关闭
        close(fd);
        break;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 数据读完了
            break;
        } else {
            // 错误
            close(fd);
            break;
        }
    }
}

对比总结：
| 特性 | LT | ET |
|------|----|----|
| 通知次数 | 多次（只要就绪） | 一次（状态变化） |
| 编程难度 | 简单 | 复杂 |
| 出错概率 | 低 | 高 |
| 性能 | 一般 | 高 |
| 是否需要非阻塞 | 否 | 是 |
| 是否需要 while 读 | 否 | 是 |

Reactor 关联：
- 推荐用 LT 模式！（muduo 用 LT）
- 追求极致性能可以用 ET 模式
- ET 模式必须注意：非阻塞 + while 读
```

【面试坑点】

- ET 模式只通知一次，必须一次性读完
- ET 模式必须用非阻塞 Socket
- 不要忘记检查 errno == EAGAIN

【结合项目加分话术】

- "我的项目用 LT 模式，简单可靠，不容易出错"
- "了解过 ET 模式，性能更好，但编程复杂，需要 while 循环读"
- "ET 模式需要注意：非阻塞 + 检查 EAGAIN"

***

### 8.3 进阶高频深挖题

#### 8.3.1 【高频】epoll 的底层实现？

**问题：** 请说明 epoll 的底层实现原理？

**标准答案：**

```
epoll 底层实现（Linux 内核）：

1. epoll_create()
   - 创建一个 eventpoll 结构体
   - 包含：
     a. 红黑树（rbr）：存储所有注册的 fd
     b. 就绪链表（rdlist）：存储就绪的 fd
     c. 等待队列（wq）：等待 epoll_wait 的进程
   - 返回 epfd（eventpoll 的文件描述符）

2. epoll_ctl()
   - 操作红黑树：
     - EPOLL_CTL_ADD：添加 fd 到红黑树
     - EPOLL_CTL_MOD：修改 fd 的事件
     - EPOLL_CTL_DEL：从红黑树删除 fd
   - 在 fd 上注册回调函数（ep_poll_callback）
   - 当 fd 就绪时，回调函数会把 fd 加入就绪链表

3. epoll_wait()
   - 检查就绪链表是否为空
   - 如果为空，当前进程加入等待队列，阻塞
   - 如果不为空，返回就绪的 fd
   - 从就绪链表拷贝到用户空间
   - 注意：不是删除，是"消费"（LT 模式下还会重新加入）

红黑树的作用：
- 快速查找 fd：O(log n)
- 快速插入/删除 fd：O(log n)
- 因为 fd 可能频繁添加/删除，红黑树合适

就绪链表的作用：
- 存储就绪的 fd
- epoll_wait() 直接从链表取，O(1)
- 不需要遍历所有 fd

回调机制：
- 每个 fd 设备驱动都有 poll 接口
- epoll_ctl() 时，调用 fd 的 poll 接口，注册回调
- 当 fd 就绪时，设备驱动调用回调函数
- 回调函数把 fd 加入就绪链表
- 唤醒等待在 epoll_wait() 的进程

LT vs ET 底层区别：
- LT：fd 就绪后，一直留在就绪链表（直到事件处理）
- ET：fd 就绪后，加入就绪链表，epoll_wait 返回后移除（需要重新触发才会再次加入）

为什么 epoll 比 select/poll 快？
1. select/poll：
   - 每次调用都要拷贝所有 fd
   - 每次都要遍历所有 fd
   - O(n) 复杂度
   
2. epoll：
   - 只在 epoll_ctl() 时拷贝一次 fd
   - epoll_wait() 只返回就绪的 fd
   - 回调机制，不需要遍历所有 fd
   - O(1) 复杂度

Reactor 关联：
- 理解 epoll 底层实现，有助于理解为什么 epoll 快
- 红黑树 + 就绪链表 + 回调 = 高性能
```

【面试坑点】

- 不要说"epoll 用链表"，epoll 用红黑树存储所有 fd，用链表存储就绪 fd
- 不要忘记回调机制
- 红黑树的查找/插入/删除都是 O(log n)

【结合项目加分话术】

- "我了解 epoll 的底层实现：红黑树存储所有 fd，就绪链表存储就绪 fd，回调机制"
- "理解了为什么 epoll 比 select/poll 快：只拷贝一次，只返回就绪的 fd"

***

#### 8.3.2 【高频】Reactor 和 Proactor 的区别？

**问题：** 请说明 Reactor 模式和 Proactor 模式的区别？

**标准答案：**

```
Reactor 模式：
- 基于 IO 多路复用
- 同步 IO（数据拷贝阶段阻塞）
- 流程：
  1. 等待事件就绪（epoll_wait）
  2. 事件就绪，分发
  3. 主动调用 recv()/send()（阻塞拷贝）
- "来了通知我，我自己处理"
- Linux 下主流（muduo、Netty、libuv）

Proactor 模式：
- 基于异步 IO（AIO）
- 异步 IO（两个阶段都不阻塞）
- 流程：
  1. 发起异步 IO 操作（aio_read）
  2. 立即返回
  3. 内核等待数据 + 拷贝到用户空间
  4. 内核发通知（信号或回调）
  5. 处理数据
- "交给你了，做完通知我"
- Windows 下主流（IOCP）
- Linux 下 AIO 不完善，很少用

对比总结：
| 特性 | Reactor | Proactor |
|------|---------|----------|
| IO 模型 | IO 多路复用 | 异步 IO |
| 数据拷贝 | 应用线程阻塞 | 内核完成 |
| 编程复杂度 | 简单 | 复杂 |
| Linux 支持 | ✅ 完善 | ❌ 不完善 |
| Windows 支持 | ⚠️ 一般 | ✅ 完善（IOCP） |
| 代表实现 | muduo、Netty | Boost.Asio（模拟） |

Reactor 关联：
- Linux 下用 Reactor！
- Proactor 在 Windows 下用 IOCP
- Boost.Asio 在 Linux 下用 Reactor 模拟 Proactor
```

【面试坑点】

- Reactor 不是"异步"，是同步 IO（数据拷贝阶段阻塞）
- Proactor 才是真正的异步
- Linux 下 AIO 不完善，实际还是用 Reactor

【结合项目加分话术】

- "我的项目用 Reactor 模式，Linux 下主流"
- "了解过 Proactor 模式，但 Linux 下 AIO 不完善，还是用 Reactor"

***

#### 8.3.3 【了解】什么是惊群问题？epoll 怎么解决？

**问题：** 请说明惊群问题（Thundering Herd）以及 epoll 怎么解决？

**标准答案：**

```
惊群问题（Thundering Herd）：
- 定义：多个进程/线程等待同一个事件，事件发生时，所有进程/线程都被唤醒，但只有一个能处理，其他的又休眠
- 危害：浪费 CPU 资源，大量上下文切换

accept() 惊群：
- 多个进程/线程阻塞在 accept()
- 新连接到达，所有进程/线程都被唤醒
- 只有一个 accept() 成功，其他失败（EAGAIN）
- Linux 2.6+ 内核已解决！（只唤醒一个）

epoll_ctl 惊群：
- 多个进程/线程 epoll_ctl 同一个 fd 到自己的 epoll 实例
- fd 就绪，所有 epoll_wait() 都返回
- 只有一个能处理，其他失败
- 解决：一个 fd 只注册到一个 epoll 实例（多 Reactor 模式）

epoll_wait 惊群（fork 后）：
- 父进程创建 epollfd
- fork()，子进程继承 epollfd
- 父子进程都调用 epoll_wait()
- 事件发生，父子进程都被唤醒
- 解决：fork() 后，子进程关闭继承的 epollfd，重新创建

SO_REUSEPORT 惊群：
- 多个进程 bind 同一端口（SO_REUSEPORT）
- 新连接到达，内核选择一个进程唤醒
- Linux 3.9+ 内核实现了负载均衡，不是惊群！

总结：
- accept() 惊群：Linux 2.6+ 已解决
- epoll 惊群：避免多个 epoll 实例监听同一 fd
- SO_REUSEPORT：内核负载均衡，不是惊群

Reactor 关联：
- 多 Reactor 模式：一个 fd 只注册到一个 subReactor，不会惊群
- 不需要担心惊群问题
```

【面试坑点】

- accept() 惊群在 Linux 2.6+ 已经解决了
- epoll 惊群是因为多个 epoll 实例监听同一 fd，避免这样做
- SO\_REUSEPORT 不是惊群，是内核负载均衡

【结合项目加分话术】

- "我了解惊群问题，Linux 2.6+ 已经解决了 accept() 惊群"
- "多 Reactor 模式下，一个 fd 只注册到一个 subReactor，不会有惊群问题"

***

## （本章完）

## 九、进程间通信(IPC)

***

### 9.1 核心原理

#### Linux IPC 方式

```
Linux 下常用的 IPC 方式：
1. 管道（Pipe）
   - 无名管道（匿名管道）
   - 命名管道（FIFO）
   
2. System V IPC
   - 消息队列（Message Queue）
   - 共享内存（Shared Memory）
   - 信号量（Semaphore）
   
3. POSIX IPC
   - POSIX 消息队列
   - POSIX 共享内存
   - POSIX 信号量
   
4. Socket
   - Unix Domain Socket（本地 Socket）
   - 网络 Socket

Reactor 场景常用：
- 共享内存 + 信号量：高性能
- Unix Domain Socket：方便，可跨机器（用网络 Socket）
```

***

### 9.2 基础必背题

#### 9.2.1 【必考】Linux 下有哪些 IPC 方式？

**问题：** 请列举 Linux 下常用的进程间通信方式？

**标准答案：**

```
Linux 下常用的 IPC 方式：

1. 管道（Pipe）
   - 无名管道：
     · 父子进程之间通信
     · 半双工（单向）
     · 只能在有亲缘关系的进程间使用
   - 命名管道（FIFO）：
     · 可以在无亲缘关系的进程间使用
     · 有文件系统中的路径名
     · 半双工

2. System V IPC
   - 消息队列：
     · 存放在内核中
     · 消息有类型
     · 按类型读取
   - 共享内存：
     · 最快的 IPC 方式
     · 多个进程映射同一块物理内存
     · 需要同步（信号量）
   - 信号量：
     · 计数器
     · 用于同步/互斥
     · P 操作（减 1）、V 操作（加 1）

3. POSIX IPC
   - POSIX 消息队列：引用计数
   - POSIX 共享内存：mmap
   - POSIX 信号量：更简单

4. Socket
   - Unix Domain Socket：
     · 本地进程通信
     · 性能好
     · 可以用 sendmsg/recvmsg 传递 fd
   - 网络 Socket：
     · 跨机器通信
     · TCP/UDP

对比总结：
| IPC 方式 | 特点 | 速度 | 适用场景 |
|----------|------|------|----------|
| 无名管道 | 半双工、亲缘进程 | 中 | 父子进程简单通信 |
| 命名管道 | 半双工、任意进程 | 中 | 本地任意进程 |
| 消息队列 | 内核、消息类型 | 中 | 需要消息类型 |
| 共享内存 | 最快、需同步 | 最快 | 大数据量、高性能 |
| 信号量 | 同步/互斥 | 快 | 配合共享内存 |
| Unix Domain Socket | 本地、可靠 | 中 | 通用、可传递 fd |
| 网络 Socket | 跨机器 | 慢 | 跨机器 |

Reactor 关联：
- 多进程模型：共享内存 + 信号量
- 日志服务：Unix Domain Socket
```

【面试坑点】

- 不要忘记 Unix Domain Socket，它也是 IPC 方式
- 共享内存最快，但需要同步
- 信号量是同步，不是通信（不能传数据）

【结合项目加分话术】

- "我了解各种 IPC 方式，项目中用 Unix Domain Socket 传递日志"
- "考虑过多进程模型用共享内存 + 信号量，但最后用了多线程 Reactor"

***

#### 9.2.2 【必考】管道（Pipe）？

**问题：** 请说明无名管道和命名管道的区别？

**标准答案：**

```
无名管道（Anonymous Pipe）：
- 创建：pipe() 系统调用
- 特点：
  1. 半双工（单向）：一端读，一端写
  2. 只能在有亲缘关系的进程间使用（父子、兄弟）
  3. 没有文件系统路径
  4. 内核缓冲区
- 用法：
  · 父进程创建管道
  · fork() 子进程
  · 父进程写，子进程读（或相反）
  · 关闭不用的一端

代码示例：
int pipefd[2];
pipe(pipefd);  // pipefd[0] 读，pipefd[1] 写

if (fork() == 0) {
    // 子进程
    close(pipefd[1]);  // 关闭写端
    read(pipefd[0], buf, sizeof(buf));  // 读
    close(pipefd[0]);
} else {
    // 父进程
    close(pipefd[0]);  // 关闭读端
    write(pipefd[1], "Hello", 5);  // 写
    close(pipefd[1]);
    wait(NULL);
}

命名管道（FIFO，First In First Out）：
- 创建：mkfifo() 系统调用 或 mkfifo 命令
- 特点：
  1. 半双工（单向）
  2. 可以在无亲缘关系的进程间使用
  3. 有文件系统路径（如 /tmp/myfifo）
  4. 内核缓冲区
- 用法：
  · 进程 A：创建 FIFO，打开写，写入
  · 进程 B：打开 FIFO 读，读取
  · 都需要先打开，否则阻塞

代码示例：
// 进程 A（写）
mkfifo("/tmp/myfifo", 0666);
int fd = open("/tmp/myfifo", O_WRONLY);
write(fd, "Hello", 5);
close(fd);

// 进程 B（读）
int fd = open("/tmp/myfifo", O_RDONLY);
read(fd, buf, sizeof(buf));
close(fd);

无名管道 vs 命名管道：
| 特性 | 无名管道 | 命名管道 |
|------|----------|----------|
| 亲缘关系 | 需要 | 不需要 |
| 文件系统路径 | 无 | 有 |
| 创建方式 | pipe() | mkfifo() |
| 半双工 | ✅ | ✅ |
| 内核缓冲区 | ✅ | ✅ |

Reactor 关联：
- 简单的父子进程通信可以用无名管道
- 本地任意进程通信可以用命名管道
- 但更推荐用 Unix Domain Socket（功能更强）
```

【面试坑点】

- 管道是半双工，不是全双工
- 无名管道只能在亲缘进程间使用
- 命名管道需要先打开两端，否则阻塞

【结合项目加分话术】

- "我用过无名管道在父子进程间传递简单的控制信息"
- "命名管道需要两端都打开，有时会用 Unix Domain Socket 替代"

***

#### 9.2.3 【必考】共享内存（Shared Memory）？

**问题：** 请说明共享内存的原理和优缺点？

**标准答案：**

```
共享内存（Shared Memory）：
- 原理：多个进程映射同一块物理内存到自己的虚拟地址空间
- 特点：
  1. 最快的 IPC 方式（不需要拷贝）
  2. 需要同步（信号量、互斥锁）
  3. 生命周期随内核（直到显式删除或系统重启）
  4. 没有血缘关系限制

System V 共享内存：
- 创建：shmget()
- 映射：shmat()
- 解除映射：shmdt()
- 删除：shmctl()

代码示例：
// 1. 创建共享内存
int shmid = shmget(key, size, IPC_CREAT | 0666);

// 2. 映射到虚拟地址空间
void *ptr = shmat(shmid, NULL, 0);

// 3. 读写（直接操作 ptr）
strcpy((char*)ptr, "Hello");

// 4. 解除映射
shmdt(ptr);

// 5. 删除共享内存
shmctl(shmid, IPC_RMID, NULL);

POSIX 共享内存：
- 创建：shm_open()
- 映射：mmap()
- 删除：shm_unlink()

代码示例：
// 1. 创建共享内存对象
int fd = shm_open("/myshm", O_CREAT | O_RDWR, 0666);

// 2. 设置大小
ftruncate(fd, size);

// 3. 映射
void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// 4. 读写
strcpy((char*)ptr, "Hello");

// 5. 解除映射
munmap(ptr, size);

// 6. 删除
shm_unlink("/myshm");

优缺点：
优点：
1. 速度最快（零拷贝）
2. 适合大数据量
3. 没有血缘限制

缺点：
1. 需要同步（信号量、互斥锁）
2. 没有访问控制（需要自己实现）
3. 生命周期随内核（忘记删除会泄漏）

Reactor 关联：
- 多进程模型：共享内存 + 信号量
- 性能要求极高的场景
- 注意：多线程不需要共享内存（直接共享地址空间）
```

【面试坑点】

- 共享内存需要同步，否则会有竞态条件
- System V 共享内存生命周期随内核，不是进程
- 多线程不需要共享内存（已经共享地址空间）

【结合项目加分话术】

- "我了解共享内存是最快的 IPC 方式，但需要同步"
- "考虑过多进程模型用共享内存 + 信号量，但最后用了多线程 Reactor"

***

#### 9.2.4 【高频】信号量（Semaphore）？

**问题：** 请说明信号量的原理和作用？

**标准答案：**

```
信号量（Semaphore）：
- 定义：计数器，用于同步/互斥
- 发明者：Dijkstra（1965）
- P 操作（wait）：减 1，如果 < 0 则阻塞
- V 操作（signal）：加 1，如果 <= 0 则唤醒一个

信号量类型：
1. 二值信号量（Binary Semaphore）
   - 值只能是 0 或 1
   - 用于互斥（类似互斥锁）
   
2. 计数信号量（Counting Semaphore）
   - 值可以是任意非负整数
   - 用于资源计数（如连接池）

System V 信号量：
- 创建：semget()
- 操作：semop()（P/V）
- 删除：semctl()

代码示例（二值信号量）：
// 1. 创建信号量
int semid = semget(key, 1, IPC_CREAT | 0666);

// 2. 初始化（设置为 1）
union semun arg;
arg.val = 1;
semctl(semid, 0, SETVAL, arg);

// 3. P 操作（加锁）
struct sembuf p = {0, -1, 0};
semop(semid, &p, 1);

// 临界区...

// 4. V 操作（解锁）
struct sembuf v = {0, 1, 0};
semop(semid, &v, 1);

POSIX 信号量：
- 有名信号量：sem_open()
- 无名信号量：sem_init()
- P 操作：sem_wait()
- V 操作：sem_post()

代码示例：
// 有名信号量
sem_t *sem = sem_open("/mysem", O_CREAT, 0666, 1);

// P 操作
sem_wait(sem);

// 临界区...

// V 操作
sem_post(sem);

// 关闭
sem_close(sem);
sem_unlink("/mysem");

信号量 vs 互斥锁：
| 特性 | 信号量 | 互斥锁 |
|------|--------|--------|
| 所有权 | 无 | 有（谁加锁谁解锁） |
| 值 | 可以 > 1 | 只能 0 或 1 |
| 用途 | 同步/互斥 | 互斥 |
| 线程/进程 | 都可以 | 线程（进程间用进程共享互斥锁） |

Reactor 关联：
- 配合共享内存使用
- 多进程模型的同步
- 多线程一般用互斥锁（pthread_mutex）
```

【面试坑点】

- 信号量没有所有权（任何进程/线程都可以 V 操作）
- 二值信号量可以当互斥锁用，但互斥锁更安全（有所有权）
- 信号量是同步，不是通信（不能传数据）

【结合项目加分话术】

- "我了解信号量，P 操作减 1，V 操作加 1"
- "配合共享内存使用时，用信号量做同步"

***

#### 9.2.5 【高频】Unix Domain Socket？

**问题：** 请说明 Unix Domain Socket 的特点和用途？

**标准答案：**

```
Unix Domain Socket（本地 Socket）：
- 定义：用于本地进程间通信的 Socket
- 特点：
  1. 不需要经过网络协议栈
  2. 性能好（比网络 Socket 快）
  3. 可靠（类似 TCP）
  4. 可以传递文件描述符（sendmsg/recvmsg）
  5. 有两种类型：
     · SOCK_STREAM：流式（类似 TCP）
     · SOCK_DGRAM：数据报（类似 UDP）

地址结构：
struct sockaddr_un {
    sa_family_t sun_family;  // AF_UNIX
    char sun_path[108];       // 路径名
};

服务器端流程：
1. socket(AF_UNIX, SOCK_STREAM, 0)
2. bind(sockaddr_un)
3. listen()
4. accept()
5. recv()/send()

客户端流程：
1. socket(AF_UNIX, SOCK_STREAM, 0)
2. connect(sockaddr_un)
3. send()/recv()

代码示例（服务器端）：
int listenfd = socket(AF_UNIX, SOCK_STREAM, 0);

struct sockaddr_un servaddr;
bzero(&servaddr, sizeof(servaddr));
servaddr.sun_family = AF_UNIX;
strcpy(servaddr.sun_path, "/tmp/mysocket");

unlink("/tmp/mysocket");  // 先删除旧的
bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

listen(listenfd, 10);

int connfd = accept(listenfd, NULL, NULL);
char buf[1024];
recv(connfd, buf, sizeof(buf), 0);
send(connfd, buf, strlen(buf), 0);
close(connfd);
close(listenfd);
unlink("/tmp/mysocket");

传递文件描述符：
- 使用 sendmsg()/recvmsg()
- 可以在进程间传递 open 的 fd
- 常用于：进程池、负载均衡
- 原理：传递文件描述符的引用（不是复制）

Reactor 关联：
- 日志服务：用 Unix Domain Socket 传递日志
- 监控服务：本地进程通信
- 多进程模型：传递 fd 给子进程
```

【面试坑点】

- Unix Domain Socket 不是网络 Socket，不走网络协议栈
- bind() 前要先 unlink() 旧的 socket 文件
- 可以传递文件描述符，这是它的一个强大功能

【结合项目加分话术】

- "我的项目中用 Unix Domain Socket 传递日志，性能好"
- "了解过用 Unix Domain Socket 传递文件描述符，用于进程池"

***

### 9.3 进阶高频深挖题

#### 9.3.1 【高频】共享内存的同步问题？

**问题：** 共享内存需要同步，有哪些同步方式？

**标准答案：**

```
共享内存的同步方式：

1. 信号量（Semaphore）
   - System V 信号量
   - POSIX 信号量
   - 最常用
   - P 操作进入临界区，V 操作退出

2. 进程共享互斥锁（Pthread Mutex）
   - pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)
   - 互斥锁放在共享内存中
   - 多个进程可以用同一个互斥锁

代码示例：
// 共享内存中放互斥锁
struct SharedData {
    pthread_mutex_t mutex;
    int data;
};

// 初始化互斥锁属性
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

// 初始化互斥锁
pthread_mutex_init(&sharedData->mutex, &attr);

// 使用
pthread_mutex_lock(&sharedData->mutex);
// 临界区
pthread_mutex_unlock(&sharedData->mutex);

3. 进程共享读写锁（Pthread RWLock）
   - pthread_rwlockattr_setpshared(PTHREAD_PROCESS_SHARED)
   - 读多写少场景

4. 文件锁（fcntl）
   - 对共享内存关联的文件加锁
   - 性能较差

5. 无锁编程（Lock-Free）
   - 原子操作（CAS）
   - 难度大，容易出错

对比：
| 同步方式 | 难度 | 性能 | 适用场景 |
|----------|------|------|----------|
| 信号量 | 中 | 高 | 通用 |
| 进程共享互斥锁 | 中 | 高 | 互斥 |
| 进程共享读写锁 | 中 | 高 | 读多写少 |
| 文件锁 | 低 | 低 | 简单场景 |
| 无锁编程 | 高 | 最高 | 极致性能 |

Reactor 关联：
- 多进程模型：信号量 或 进程共享互斥锁
- 推荐：进程共享互斥锁（和线程互斥锁用法类似）
```

【面试坑点】

- 共享内存必须同步，否则会有竞态条件
- 进程共享互斥锁需要放在共享内存中
- 无锁编程难度大，不推荐

【结合项目加分话术】

- "共享内存需要同步，我会用信号量或进程共享互斥锁"
- "进程共享互斥锁和线程互斥锁用法类似，只是设置 PTHREAD\_PROCESS\_SHARED 属性"

***

#### 9.3.2 【了解】System V IPC vs POSIX IPC？

**问题：** 请说明 System V IPC 和 POSIX IPC 的区别？

**标准答案：**

```
System V IPC：
- 历史悠久（Unix System V）
- 包括：
  · System V 消息队列
  · System V 共享内存
  · System V 信号量
- 特点：
  · 用 key 标识（ftok() 生成）
  · 生命周期随内核
  · 命令：ipcs、ipcrm
  · 接口复杂

POSIX IPC：
- 新的标准（POSIX.1b）
- 包括：
  · POSIX 消息队列
  · POSIX 共享内存（shm_open + mmap）
  · POSIX 信号量
- 特点：
  · 用名字标识（如 "/myshm"）
  · 引用计数（删除后等所有进程关闭才真正删除）
  · 接口简单
  · 性能更好

对比总结：
| 特性 | System V IPC | POSIX IPC |
|------|--------------|-----------|
| 标识方式 | key | 名字 |
| 生命周期 | 内核 | 引用计数 |
| 接口复杂度 | 复杂 | 简单 |
| 性能 | 一般 | 更好 |
| 可移植性 | 好 | 较好 |
| 推荐使用 | ❌ | ✅ |

Reactor 关联：
- 推荐用 POSIX IPC（接口简单、性能好）
- POSIX 共享内存：shm_open + mmap
- POSIX 信号量：sem_open
```

【面试坑点】

- System V IPC 生命周期随内核，忘记删除会泄漏
- POSIX IPC 用名字标识，不是 key
- POSIX IPC 引用计数，删除后等所有进程关闭才真正删除

【结合项目加分话术】

- "我推荐用 POSIX IPC，接口简单，性能更好"
- "POSIX 共享内存用 shm\_open + mmap，比 System V 简单"

***

##

## 十、锁与线程同步

### 10.1 核心原理

#### 并发问题

```
为什么需要锁？
- 多个线程同时访问共享资源
- 竞态条件（Race Condition）：执行顺序不同，结果不同
- 数据竞争（Data Race）：同时读写共享数据

共享资源：
- 全局变量
- 静态变量
- 堆上的共享对象
- 文件描述符

同步原语：
1. 互斥锁（Mutex）
2. 读写锁（RWLock）
3. 条件变量（Condition Variable）
4. 自旋锁（Spinlock）
5. 信号量（Semaphore）
6. 原子操作（Atomic）
```

***

### 10.2 基础必背题

#### 10.2.1 【必考】互斥锁（Mutex）？

**问题：** 请说明互斥锁的原理和用法？

**标准答案：**

```
互斥锁（Mutex，Mutual Exclusion）：
- 作用：保护临界区，同一时间只有一个线程能进入
- 特点：
  1. 有所有权：谁加锁谁解锁
  2. 可重入（可选）：同一线程可以多次加锁
  3. 阻塞：加锁失败时线程休眠

pthread_mutex 用法：
1. 初始化：
   pthread_mutex_t mutex;
   pthread_mutex_init(&mutex, NULL);
   
2. 加锁：
   pthread_mutex_lock(&mutex);
   // 阻塞，直到拿到锁
   
3. 尝试加锁：
   pthread_mutex_trylock(&mutex);
   // 非阻塞，成功返回 0，失败返回 EBUSY
   
4. 解锁：
   pthread_mutex_unlock(&mutex);
   
5. 销毁：
   pthread_mutex_destroy(&mutex);

代码示例：
pthread_mutex_t mutex;
int count = 0;

void *ThreadFunc(void *arg) {
    for (int i = 0; i < 10000; i++) {
        pthread_mutex_lock(&mutex);
        count++;  // 临界区
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main() {
    pthread_mutex_init(&mutex, NULL);
  
    pthread_t t1, t2;
    pthread_create(&t1, NULL, ThreadFunc, NULL);
    pthread_create(&t2, NULL, ThreadFunc, NULL);
  
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
  
    printf("count = %d\n", count);  // 20000
    pthread_mutex_destroy(&mutex);
    return 0;
}

互斥锁属性：
1. PTHREAD_MUTEX_NORMAL：普通锁
   - 同一线程重复加锁 → 死锁
   
2. PTHREAD_MUTEX_RECURSIVE：递归锁
   - 同一线程可以多次加锁
   - 解锁次数要和加锁次数相同
   
3. PTHREAD_MUTEX_ERRORCHECK：检错锁
   - 同一线程重复加锁 → 返回错误
   
4. PTHREAD_MUTEX_DEFAULT：默认（一般是普通锁）

Reactor 关联：
- 多线程 Reactor：线程间共享数据需要互斥锁
- 尽量减小临界区
- 避免死锁
```

【面试坑点】

- 互斥锁有所有权，谁加锁谁解锁
- 普通锁同一线程重复加锁会死锁
- 忘记解锁会死锁

【结合项目加分话术】

- "我的项目中用互斥锁保护共享的连接表"
- "尽量减小临界区，只保护必要的操作"

***

#### 10.2.2 【必考】死锁？

**问题：** 请说明死锁的产生条件和避免方法？

**标准答案：**

```
死锁（Deadlock）：
- 定义：多个线程互相等待对方的资源，永久阻塞

死锁产生的 4 个必要条件（Coffman 条件）：
1. 互斥条件
   - 资源同一时间只能被一个线程使用
   
2. 请求与保持条件
   - 持有资源，同时请求新资源
   
3. 不剥夺条件
   - 资源不能被强行剥夺，只能主动释放
   
4. 循环等待条件
   - 线程形成等待环

死锁示例：
// 线程 A
pthread_mutex_lock(&mutex1);
pthread_mutex_lock(&mutex2);  // 等待 mutex2

// 线程 B
pthread_mutex_lock(&mutex2);
pthread_mutex_lock(&mutex1);  // 等待 mutex1
// 死锁！

避免死锁的方法：
1. 破坏互斥条件
   - 很难（资源本来就是互斥的）
   
2. 破坏请求与保持条件
   - 一次性请求所有资源
   - 或者：请求新资源前先释放已有资源
   
3. 破坏不剥夺条件
   - 可以用 pthread_mutex_trylock()
   - 失败就释放已有资源
   
4. 破坏循环等待条件
   - 按固定顺序加锁（如：按 mutex 地址排序）
   - 避免：A 锁 1 等 2，B 锁 2 等 1

死锁检测：
1. gdb
2. pstack
3. valgrind --tool=helgrind
4. Google ThreadSanitizer

Reactor 关联：
- 按固定顺序加锁（如：先锁 Connection，再锁 Buffer）
- 避免在锁中调用回调（可能导致重入）
- 尽量用 RAII（C++ std::lock_guard）
```

【面试坑点】

- 死锁 4 个必要条件，缺一不可
- 破坏任意一个条件就能避免死锁
- 按固定顺序加锁是最常用的方法

【结合项目加分话术】

- "我避免死锁的方法是按固定顺序加锁"
- "用 RAII（std::lock\_guard）自动解锁，避免忘记解锁"

***

#### 10.2.3 【必考】读写锁（RWLock）？

**问题：** 请说明读写锁的特点和适用场景？

**标准答案：**

```
读写锁（Reader-Writer Lock）：
- 作用：读多写少场景，提高并发
- 特点：
  1. 读-读：共享（多个线程可以同时读）
  2. 读-写：互斥（读和写不能同时）
  3. 写-写：互斥（写和写不能同时）

pthread_rwlock 用法：
1. 初始化：
   pthread_rwlock_t rwlock;
   pthread_rwlock_init(&rwlock, NULL);
   
2. 读锁：
   pthread_rwlock_rdlock(&rwlock);
   
3. 写锁：
   pthread_rwlock_wrlock(&rwlock);
   
4. 解锁：
   pthread_rwlock_unlock(&rwlock);
   
5. 销毁：
   pthread_rwlock_destroy(&rwlock);

代码示例：
pthread_rwlock_t rwlock;
int data = 0;

void *Reader(void *arg) {
    for (int i = 0; i < 10000; i++) {
        pthread_rwlock_rdlock(&rwlock);
        // 读 data
        int x = data;
        pthread_rwlock_unlock(&rwlock);
    }
    return NULL;
}

void *Writer(void *arg) {
    for (int i = 0; i < 1000; i++) {
        pthread_rwlock_wrlock(&rwlock);
        // 写 data
        data++;
        pthread_rwlock_unlock(&rwlock);
    }
    return NULL;
}

读写锁的两种策略：
1. 读者优先（Read-Preferring）
   - 只要有读者在读，新读者可以直接加读锁
   - 可能导致写者饥饿（Writer Starvation）
   
2. 写者优先（Write-Preferring）
   - 只要有写者等待，新读者必须等待
   - 可能导致读者饥饿
   
3. 公平策略（Fair）
   - 按请求顺序处理
   - 避免饥饿

Reactor 关联：
- 读多写少的场景：配置文件、路由表
- 注意：写操作不能太频繁，否则不如互斥锁
```

【面试坑点】

- 读-读可以并发，读-写、写-写互斥
- 读者优先可能导致写者饥饿
- 写操作频繁时，读写锁不如互斥锁

【结合项目加分话术】

- "我的项目中用读写锁保护配置文件，读多写少"
- "注意写操作不能太频繁，否则读写锁性能反而下降"

***

#### 10.2.4 【必考】条件变量（Condition Variable）？

**问题：** 请说明条件变量的作用和用法？

**标准答案：**

```
条件变量（Condition Variable）：
- 作用：线程间同步，等待某个条件成立
- 特点：
  1. 必须和互斥锁一起使用
  2. 可以让线程休眠等待条件
  3. 条件成立时唤醒等待的线程

为什么需要条件变量？
- 互斥锁只能保护临界区
- 条件变量可以等待条件（如：队列不为空、队列不满）

pthread_cond 用法：
1. 初始化：
   pthread_cond_t cond;
   pthread_cond_init(&cond, NULL);
   
2. 等待：
   pthread_mutex_lock(&mutex);
   while (!condition) {  // 注意：while 循环！
       pthread_cond_wait(&cond, &mutex);
   }
   // 条件成立，处理
   pthread_mutex_unlock(&mutex);
   
3. 唤醒一个：
   pthread_cond_signal(&cond);
   
4. 唤醒所有：
   pthread_cond_broadcast(&cond);
   
5. 销毁：
   pthread_cond_destroy(&cond);

为什么用 while 循环，不用 if？
- 虚假唤醒（Spurious Wakeup）：线程被唤醒，但条件不成立
- 所以需要 while 循环重新检查条件

代码示例（生产者-消费者模型）：
pthread_mutex_t mutex;
pthread_cond_t notEmpty;
pthread_cond_t notFull;
std::queue<int> q;
const int MAX_SIZE = 10;

void *Producer(void *arg) {
    for (int i = 0; i < 100; i++) {
        pthread_mutex_lock(&mutex);
      
        // 队列满了，等待
        while (q.size() >= MAX_SIZE) {
            pthread_cond_wait(&notFull, &mutex);
        }
      
        // 生产
        q.push(i);
      
        // 唤醒消费者
        pthread_cond_signal(&notEmpty);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

void *Consumer(void *arg) {
    for (int i = 0; i < 100; i++) {
        pthread_mutex_lock(&mutex);
      
        // 队列空了，等待
        while (q.empty()) {
            pthread_cond_wait(&notEmpty, &mutex);
        }
      
        // 消费
        int data = q.front();
        q.pop();
      
        // 唤醒生产者
        pthread_cond_signal(&notFull);
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

pthread_cond_wait 做了什么？
1. 释放互斥锁
2. 线程休眠，等待被唤醒
3. 被唤醒后，重新获取互斥锁
4. 返回

Reactor 关联：
- 生产者-消费者模型：线程池
- 工作队列：主线程生产，工作线程消费
```

【面试坑点】

- 条件变量必须和互斥锁一起使用
- 等待条件必须用 while 循环，不能用 if（虚假唤醒）
- pthread\_cond\_wait 会先释放锁，再休眠，被唤醒后重新获取锁

【结合项目加分话术】

- "我的项目中用条件变量实现线程池的工作队列"
- "注意用 while 循环检查条件，避免虚假唤醒"

***

#### 10.2.5 【高频】自旋锁（Spinlock）？

**问题：** 请说明自旋锁的特点和适用场景？

**标准答案：**

```
自旋锁（Spinlock）：
- 定义：加锁失败时，线程忙等待（自旋），不进入休眠
- 特点：
  1. 不进入休眠，减少上下文切换开销
  2. 长时间自旋浪费 CPU
  3. 适合：锁持有时间短、多核 CPU

和互斥锁对比：
| 特性 | 自旋锁 | 互斥锁 |
|------|--------|--------|
| 加锁失败 | 自旋（忙等待） | 休眠（阻塞） |
| 上下文切换 | 无 | 有 |
| CPU 占用 | 高（自旋时） | 低（休眠时） |
| 适用场景 | 锁持有时间短 | 锁持有时间长 |
| 单核 CPU | ❌（浪费 CPU） | ✅ |

pthread_spinlock 用法：
1. 初始化：
   pthread_spinlock_t spinlock;
   pthread_spin_init(&spinlock, PTHREAD_PROCESS_PRIVATE);
   
2. 加锁：
   pthread_spin_lock(&spinlock);
   
3. 尝试加锁：
   pthread_spin_trylock(&spinlock);
   
4. 解锁：
   pthread_spin_unlock(&spinlock);
   
5. 销毁：
   pthread_spin_destroy(&spinlock);

适用场景：
✅ 适合：
- 锁持有时间短（几行代码）
- 多核 CPU
- 临界区很小

❌ 不适合：
- 锁持有时间长
- 单核 CPU
- 临界区大

Reactor 关联：
- 很少用（临界区一般不小）
- 极端性能优化场景可能用
```

【面试坑点】

- 自旋锁在单核 CPU 上没用（只有一个 CPU，自旋也拿不到锁，浪费 CPU）
- 锁持有时间长不要用自旋锁
- 自旋锁不进入休眠，减少上下文切换

【结合项目加分话术】

- "我了解自旋锁，但项目中一般用互斥锁"
- "自旋锁适合锁持有时间短、多核 CPU 的场景"

***

### 10.3 进阶高频深挖题

#### 10.3.1 【高频】原子操作（Atomic）？

**问题：** 请说明原子操作的原理和适用场景？

**标准答案：**

```
原子操作（Atomic Operation）：
- 定义：不可中断的操作，要么全部完成，要么不执行
- 实现：
  1. 硬件支持：CPU 指令（如 x86 的 LOCK 前缀）
  2. 无锁（Lock-Free）：不需要锁

常见原子操作：
1. 原子加：fetch_add
2. 原子减：fetch_sub
3. 原子交换：exchange
4. 原子比较交换：CAS（Compare-And-Swap）

CAS（Compare-And-Swap）：
- 伪代码：
  bool CAS(T* ptr, T expected, T desired) {
      if (*ptr == expected) {
          *ptr = desired;
          return true;
      }
      return false;
  }
- 作用：原子地比较并交换
- 是无锁编程的基础

C++11 std::atomic 用法：
#include <atomic>

std::atomic<int> count(0);

// 原子加
count.fetch_add(1);
// 等价于：++count（也是原子的）

// CAS
int expected = 5;
int desired = 10;
if (count.compare_exchange_weak(expected, desired)) {
    // 成功
}

原子操作 vs 锁：
| 特性 | 原子操作 | 锁 |
|------|----------|---|
| 是否阻塞 | 否 | 是 |
| 上下文切换 | 无 | 可能有 |
| 适用场景 | 简单操作 | 复杂临界区 |
| 实现难度 | 高 | 低 |

Reactor 关联：
- 简单的计数器可以用原子操作
- 复杂的临界区还是用锁
- 避免无锁编程（难度大，容易出错）
```

【面试坑点】

- 原子操作是硬件支持的，不是软件
- CAS 可能失败，需要循环重试
- 无锁编程难度大，不推荐

【结合项目加分话术】

- "我的项目中用 std::atomic 做计数器，简单高效"
- "复杂的临界区还是用锁，无锁编程难度大"

***

#### 10.3.2 【高频】CAS 的 ABA 问题？

**问题：** 请说明 CAS 的 ABA 问题以及解决方法？

**标准答案：**

```
ABA 问题：
- 场景：
  1. 线程 1：读取 ptr 的值为 A
  2. 线程 2：把 ptr 改为 B
  3. 线程 2：把 ptr 改回 A
  4. 线程 1：CAS 成功（因为值还是 A）
- 问题：虽然值是 A，但中间已经被修改过了
- 危害：如果是指针，可能导致内存问题

示例：
// 栈
Node *top = A;

// 线程 1：准备 pop
Node *old_top = top;  // A
// 此时线程 1 被切走

// 线程 2：pop A，push B，pop B，push A
// 此时 top 还是 A

// 线程 1：继续执行
if (CAS(&top, old_top, old_top->next)) {
    // CAS 成功！
    // 但 old_top->next 可能已经不对了！
}

解决方法：
1. 带版本号的 CAS（Double-CAS）
   - 不仅比较值，还比较版本号
   - 每次修改版本号 + 1
   
   伪代码：
   struct TaggedPtr {
       T* ptr;
       int tag;
   };
   
   bool CAS(TaggedPtr* ptr, TaggedPtr expected, TaggedPtr desired) {
       if (ptr->ptr == expected.ptr && ptr->tag == expected.tag) {
           *ptr = desired;
           return true;
       }
       return false;
   }

2. 引用计数
   - 不回收节点，延迟回收（Epoch-Based Reclamation）
   
3.  hazard pointer
   - 记录正在访问的指针，延迟回收

Reactor 关联：
- 避免无锁编程，用锁更简单
- 如果必须用 CAS，注意 ABA 问题
```

【面试坑点】

- ABA 问题是 CAS 的经典问题
- 带版本号的 CAS 可以解决
- 避免无锁编程，用锁更简单

【结合项目加分话术】

- "我了解 CAS 的 ABA 问题，可以用带版本号的 CAS 解决"
- "项目中一般用锁，避免无锁编程的复杂性"

***

#### 10.3.3 【了解】无锁编程（Lock-Free）？

**问题：** 请说明无锁编程的特点和适用场景？

**标准答案：**

```
无锁编程（Lock-Free Programming）：
- 定义：不使用锁，用原子操作实现并发
- 目标：
  1. 避免死锁
  2. 减少上下文切换
  3. 提高并发性能

无锁数据结构：
1. 无锁队列（Lock-Free Queue）
   - Michael-Scott Queue
   
2. 无锁栈（Lock-Free Stack）
   - Treiber Stack
   
3. 无锁哈希表

无锁编程的三个级别（按保证）：
1. Obstruction-Free（无障碍）
   - 只要没有竞争，线程就能完成
   - 最弱的保证
   
2. Lock-Free（无锁）
   - 至少有一个线程能完成
   - 不会死锁，但可能饥饿
   
3. Wait-Free（无等待）
   - 每个线程都能在有限步骤内完成
   - 最强的保证，也是最难实现的

无锁编程的优缺点：
优点：
1. 避免死锁
2. 减少上下文切换
3. 高并发下性能好

缺点：
1. 难度极大（比用锁难 10 倍）
2. 容易出错（ABA、内存回收）
3. 可维护性差
4. 不一定比锁快（取决于场景）

Reactor 关联：
- 不推荐！（除非极致性能优化）
- 用锁更简单、更可靠
- 除非性能测试证明锁是瓶颈，否则不要用无锁
```

【面试坑点】

- 无锁编程不是"一定比锁快"
- 无锁编程难度极大，容易出错
- 不推荐用无锁编程

【结合项目加分话术】

- "我了解无锁编程，但项目中一般用锁，更简单可靠"
- "除非性能测试证明锁是瓶颈，否则不用无锁"

***

##

## 十一、系统调用

***

### 11.1 核心原理

#### 系统调用 vs 库函数

```
系统调用（System Call）：
- 定义：用户空间 → 内核空间的接口
- 作用：访问内核资源（进程、内存、文件、网络）
- 特点：
  1. 由操作系统内核提供
  2. 需要从用户态切换到内核态（上下文切换）
  3. 速度相对较慢（但很快）

库函数（Library Function）：
- 定义：用户空间的函数库（如 glibc）
- 特点：
  1. 运行在用户空间
  2. 可能调用系统调用，也可能不调用
  3. 速度快（不需要上下文切换）

关系：
- 很多库函数内部调用系统调用
- 如：printf() → write()
- 如：malloc() → brk()/mmap()
- 如：fopen() → open()

举例：
| 库函数 | 内部调用的系统调用 |
|--------|-------------------|
| printf() | write() |
| malloc() | brk()/mmap() |
| fopen() | open() |
| fread() | read() |
| fwrite() | write() |
| fclose() | close() |
| sleep() | nanosleep() |
```

### 用户态 vs 内核态

```
CPU 特权级别（x86）：
- Ring 0：内核态（最高特权）
  · 可以执行所有指令
  · 可以访问所有内存
  · 可以访问所有硬件
  
- Ring 3：用户态（最低特权）
  · 只能执行非特权指令
  · 只能访问用户空间内存
  · 不能直接访问硬件

为什么需要区分？
- 安全：用户程序不能直接操作硬件
- 稳定：用户程序崩溃不影响内核

状态切换：
- 用户态 → 内核态：系统调用、中断、异常
- 内核态 → 用户态：iret 指令（x86）

系统调用过程：
1. 用户程序调用库函数（如 glibc 的 write()）
2. 库函数准备参数，触发软中断（int 0x80 或 syscall）
3. CPU 切换到内核态
4. 内核查找系统调用表，执行对应的系统调用处理函数
5. 内核处理完成，返回结果
6. CPU 切换回用户态
7. 库函数返回用户程序

上下文切换开销：
- 保存/恢复寄存器
- 切换页表（TLB flush）
- 虽然开销不大，但频繁切换会影响性能
```

***

### 11.2 基础必背题

#### 11.2.1 【必考】系统调用和库函数的区别？

**问题：** 请说明系统调用和库函数的区别？

**标准答案：**

```
系统调用 vs 库函数：

| 特性 | 系统调用 | 库函数 |
|------|----------|--------|
| 运行空间 | 内核空间 | 用户空间 |
| 提供者 | 操作系统内核 | 函数库（如 glibc） |
| 状态切换 | 需要（用户态 → 内核态） | 不需要 |
| 速度 | 相对较慢 | 快 |
| 可移植性 | 差（不同系统不同） | 好（C 标准库） |
| 示例 | open()、read()、write() | fopen()、fread()、fwrite() |

详细说明：
1. 系统调用
   - 是内核提供的接口
   - 必须从用户态切换到内核态
   - 可以访问内核资源
   - 不同操作系统的系统调用不同（可移植性差）
   - 如：Linux 的 open()、Windows 的 CreateFile()

2. 库函数
   - 是用户空间的函数
   - 运行在用户态，不需要切换
   - 可能内部调用系统调用，也可能不调用
   - 可移植性好（如 C 标准库）
   - 如：printf()、malloc()

举例：
- fopen() 是库函数，内部调用 open() 系统调用
- printf() 是库函数，内部调用 write() 系统调用
- malloc() 是库函数，内部调用 brk()/mmap() 系统调用
- strlen() 是库函数，不调用系统调用

Reactor 关联：
- 我们写的代码一般用库函数
- 理解系统调用有助于理解底层原理
- 性能优化时，减少系统调用次数
```

【面试坑点】

- 系统调用不是库函数，是内核提供的接口
- 库函数可能内部调用系统调用，也可能不调用
- 系统调用需要从用户态切换到内核态

【结合项目加分话术】

- "我理解系统调用和库函数的区别，很多库函数内部调用系统调用"
- "性能优化时，减少系统调用次数（如 Buffer 批处理）"

***

#### 11.2.2 【必考】常用的系统调用？

**问题：** 请列举 Linux 下常用的系统调用？

**标准答案：**

```
Linux 下常用系统调用：

1. 进程管理
   - fork()：创建子进程
   - exec() 系列：执行新程序
   - exit()：终止进程
   - wait()/waitpid()：等待子进程
   - getpid()：获取当前进程 PID
   - getppid()：获取父进程 PID

2. 内存管理
   - brk()/sbrk()：调整堆大小
   - mmap()：映射内存
   - munmap()：解除映射
   - mprotect()：设置内存保护

3. 文件操作
   - open()：打开文件
   - close()：关闭文件
   - read()：读文件
   - write()：写文件
   - lseek()：移动文件指针
   - stat()：获取文件状态
   - fstat()：通过 fd 获取文件状态
   - unlink()：删除文件

4. 目录操作
   - mkdir()：创建目录
   - rmdir()：删除目录
   - opendir()：打开目录
   - readdir()：读目录
   - closedir()：关闭目录

5. 网络操作
   - socket()：创建 Socket
   - bind()：绑定
   - listen()：监听
   - accept()：接受连接
   - connect()：连接
   - send()/recv()：收发数据
   - sendto()/recvfrom()：UDP 收发
   - close()：关闭 Socket

6. IO 多路复用
   - select()
   - poll()
   - epoll_create()
   - epoll_ctl()
   - epoll_wait()

7. 信号
   - kill()：发送信号
   - sigaction()：设置信号处理
   - pause()：等待信号

8. 时间
   - time()：获取当前时间
   - gettimeofday()：获取当前时间（微秒）
   - clock_gettime()：获取时间（高精度）
   - sleep()：休眠（秒）
   - nanosleep()：休眠（纳秒）

9. 其他
   - ioctl()：设备控制
   - fcntl()：文件控制
   - pipe()：创建管道
   - dup()/dup2()：复制 fd
   - select()：IO 多路复用

Reactor 关联：
- 常用：socket()、bind()、listen()、accept()、connect()、send()、recv()
- IO 多路复用：epoll_create()、epoll_ctl()、epoll_wait()
```

【面试坑点】

- 系统调用不是函数，是内核接口
- 注意区分系统调用和库函数
- 如：open() 是系统调用，fopen() 是库函数

【结合项目加分话术】

- "我了解常用的系统调用，如 socket、bind、listen、accept、epoll 等"
- "理解系统调用有助于理解 Reactor 的底层实现"

***

#### 11.2.3 【高频】如何查看系统调用？

**问题：** 如何查看一个程序调用了哪些系统调用？

**标准答案：**

```
查看系统调用的工具：

1. strace
   - 作用：追踪系统调用
   - 常用命令：
     · strace ./program          # 追踪所有系统调用
     · strace -c ./program       # 统计系统调用次数和时间
     · strace -e trace=open ./program  # 只追踪 open()
     · strace -p <pid>           # 追踪正在运行的进程
   
   - 输出示例：
     open("file.txt", O_RDONLY) = 3
     read(3, "Hello", 5) = 5
     close(3) = 0

2. ltrace
   - 作用：追踪库函数调用
   - 常用命令：
     · ltrace ./program
   
3. gdb
   - 作用：调试程序，也可以查看系统调用
   - 常用命令：
     · catch syscall  # 捕获所有系统调用
     · catch syscall open  # 捕获 open()

4. /proc/<pid>/syscall
   - 作用：查看进程当前正在执行的系统调用
   - 命令：cat /proc/<pid>/syscall

5. perf
   - 作用：性能分析，也可以查看系统调用
   - 常用命令：
     · perf trace ./program

Reactor 关联：
- 用 strace 查看 Reactor 服务器的系统调用
- 看 epoll_wait() 的调用频率
- 看 send()/recv() 的次数
```

【面试坑点】

- strace 追踪系统调用，ltrace 追踪库函数
- strace 会影响性能，生产环境慎用
- strace -c 可以统计系统调用次数和时间

【结合项目加分话术】

- "我用 strace 查看过程序的系统调用，优化了 epoll\_wait() 的超时时间"
- "strace -c 可以统计系统调用次数，发现频繁的小 read()/write()，用 Buffer 优化"

***

### 11.3 进阶高频深挖题

#### 11.3.1 【高频】系统调用的过程？

**问题：** 请说明 Linux 下系统调用的执行过程？

**标准答案：**

```
Linux 系统调用执行过程（x86_64）：

1. 用户程序准备参数
   - 系统调用号放在 rax 寄存器
   - 参数依次放在 rdi、rsi、rdx、r10、r8、r9 寄存器
   
2. 触发 syscall 指令
   - x86_64 用 syscall 指令（不是 int 0x80）
   - CPU 从用户态（Ring 3）切换到内核态（Ring 0）
   
3. 内核保存上下文
   - 保存用户态寄存器到内核栈
   
4. 查找系统调用表
   - 根据 rax 中的系统调用号，查找 sys_call_table
   - 得到对应的系统调用处理函数地址
   
5. 执行系统调用处理函数
   - 如：sys_read()、sys_write()
   
6. 内核恢复上下文
   - 从内核栈恢复用户态寄存器
   - 返回值放在 rax 寄存器
   
7. 返回到用户态
   - sysret 指令（x86_64）
   - CPU 从内核态切换回用户态
   
8. 用户程序继续执行
   - 检查返回值

系统调用号：
- 定义：每个系统调用有一个唯一的编号
- 查看：/usr/include/asm/unistd_64.h
- 如：
  · #define __NR_read 0
  · #define __NR_write 1
  · #define __NR_open 2
  · #define __NR_close 3

举例（write 系统调用）：
// 用户程序
write(1, "Hello", 5);

// glibc 内部实现（汇编）
mov $1, %rax        # 系统调用号 1 是 write
mov $1, %rdi        # 参数 1：fd = 1（stdout）
mov $message, %rsi  # 参数 2：缓冲区
mov $5, %rdx        # 参数 3：长度 5
syscall             # 触发系统调用
# 返回值在 %rax

Reactor 关联：
- 理解系统调用过程有助于理解底层原理
- epoll_wait() 也是系统调用
```

【面试坑点】

- x86\_64 用 syscall 指令，不是 int 0x80
- 系统调用号放在 rax 寄存器
- 参数放在 rdi、rsi、rdx、r10、r8、r9 寄存器

【结合项目加分话术】

- "我理解系统调用的执行过程，用户态 → 内核态 → 执行 → 返回"
- "x86\_64 用 syscall 指令，系统调用号放在 rax 寄存器"

***

#### 11.3.2 【了解】系统调用的优化？

**问题：** 系统调用有开销，如何优化？

**标准答案：**

```
系统调用优化：

1. 减少系统调用次数
   - 批处理：一次 read()/write() 更多数据
   - Buffer：用户空间缓冲，减少 write() 次数
   - 示例：
     · 差：每次写 1 字节，10000 次 write()
     · 好：Buffer 攒 4KB，一次 write()

2. 用更快的系统调用
   - 如：pread()/pwrite() 替代 lseek() + read()/write()
   - 如：splice() 零拷贝

3. 用 mmap() 替代 read()/write()
   - mmap() 映射文件到内存
   - 直接读写内存，不需要 read()/write() 系统调用
   - 适合随机访问

4. 异步 IO
   - io_setup()、io_submit()、io_getevents()
   - 提交多个 IO 请求，一次获取结果
   - 减少系统调用次数

5. 避免不必要的系统调用
   - 如：strlen() 不调用系统调用
   - 如：用用户空间的缓存

6. 调整参数
   - 如：调整 epoll_wait() 的超时时间
   - 避免频繁调用 epoll_wait()

Reactor 关联：
- Buffer 批处理，减少 send()/recv() 次数
- 调整 epoll_wait() 超时时间
- 零拷贝（sendfile()）
```

【面试坑点】

- 系统调用开销不大，但频繁调用会影响性能
- 减少系统调用次数是主要优化手段
- Buffer 批处理是常用方法

【结合项目加分话术】

- "我的项目中用 Buffer 批处理，减少 send()/recv() 次数"
- "调整 epoll\_wait() 的超时时间，避免频繁调用"
- "文件下载用 sendfile() 零拷贝，减少系统调用和拷贝"

***

## （本章完）

## 十二、Linux性能调优

***

### 12.1 核心原理

#### 性能分析方法论

```
性能调优步骤：
1. 定位瓶颈
   - 用工具找出瓶颈在哪里
   - CPU？内存？网络？磁盘？
   
2. 分析原因
   - 为什么是瓶颈？
   - 是应用问题？还是系统问题？
   
3. 优化方案
   - 应用层优化？
   - 系统层优化？
   
4. 验证效果
   - 优化后有没有效果？
   - 有没有副作用？

性能指标：
1. 延迟（Latency）：响应时间
2. 吞吐量（Throughput）：QPS、TPS
3. 利用率（Utilization）：CPU、内存、磁盘、网络
4. 饱和度（Saturation）：队列长度、等待时间
```

***

### 12.2 基础必背题

#### 12.2.1 【必考】如何排查系统瓶颈？

**问题：** 系统慢，如何一步步排查瓶颈？

**标准答案：**

```
性能排查步骤：

1. 整体查看（top / htop）
   - load average：1/5/15 分钟
   - CPU 使用率：us（用户态）、sy（内核态）、id（空闲）
   - 内存使用率：free、cached、buffered
   - swap：si/so（换入换出）
   
2. CPU 分析
   - mpstat -P ALL 1：查看各核心负载
   - pidstat 1：查看各进程 CPU 占用
   - perf top：查看 CPU 热点函数
   
3. 内存分析
   - free -h：查看内存使用
   - vmstat 1：查看虚拟内存统计
   - pmap <pid>：查看进程内存映射
   - valgrind / AddressSanitizer：检测内存泄漏
   
4. 磁盘分析
   - iostat -x 1：查看磁盘 I/O
   - iotop：查看进程 I/O 占用
   - df -h：查看磁盘空间
   - du -sh：查看目录大小
   
5. 网络分析
   - ss -s：查看网络连接统计
   - ss -tulpn：查看监听端口
   - iftop：查看网络流量
   - sar -n DEV 1：查看网络接口统计
   - tcpdump：抓包分析
   
6. 应用层分析
   - 日志：查看应用日志
   - 性能分析工具：gprof、perf
   - 链路追踪：Jaeger、Zipkin

Reactor 场景常见瓶颈：
1. CPU
   - EventLoop 处理事件耗时
   - 业务逻辑耗时
   - 锁竞争
   
2. 内存
   - Buffer 分配频繁
   - 内存碎片
   - 内存泄漏
   
3. 网络
   - 连接数过多
   - 流量过大
   - TCP 重传率高
   
4. 锁
   - 锁竞争激烈
   - 临界区过大

常见问题定位：
- CPU us 高：应用层逻辑耗时
- CPU sy 高：系统调用频繁
- load 高但 CPU 使用率低：I/O 等待（磁盘/网络）
- swap si/so 高：内存不足
```

【面试坑点】

- 不要只看一个指标，要综合分析
- load average ≠ CPU 使用率
- I/O 密集型任务 load 高但 CPU 使用率低

【结合项目加分话术】

- "我排查性能问题的步骤是：先看 top，再看 CPU/内存/磁盘/网络"
- "有一次 sy（内核态）很高，用 perf 发现是 epoll\_wait 调用太频繁，优化了超时时间"
- "另一次内存持续增长，用 valgrind 发现是 Buffer 没有释放"

***

#### 12.2.2 【必考】CPU 性能调优？

**问题：** 请说明 CPU 性能调优的方法？

**标准答案：**

```
CPU 性能调优：

1. 定位 CPU 瓶颈
   - top / htop：查看 CPU 使用率
   - mpstat -P ALL 1：查看各核心负载是否均衡
   - pidstat 1：查看哪个进程 CPU 占用高
   - perf top：查看热点函数
   - perf record -g ./program; perf report：查看调用栈

2. 应用层优化
   - 算法优化：O(n²) → O(nlogn)
   - 减少不必要的计算
   - 缓存：热点数据缓存
   - 异步化：耗时操作异步执行
   - 批处理：减少系统调用
   - 锁优化：
     · 减小临界区
     · 读写锁替代互斥锁
     · 无锁编程（原子操作）
     · 避免锁竞争（每个线程独立数据）

3. 系统层优化
   - CPU 亲和性（CPU Affinity）：
     · 绑定线程到特定 CPU 核心
     · 减少缓存失效（Cache Miss）
     · 命令：taskset、pthread_setaffinity_np()
   
   - 关闭不必要的服务：
     · 减少后台进程 CPU 占用
   
   - 调整进程优先级：
     · nice、renice
     · 不推荐（影响其他进程）

4. 架构优化
   - 多线程：充分利用多核
   - 多进程：进一步利用多核
   - 分布式：负载均衡到多台机器

Reactor 场景 CPU 优化：
- 多 Reactor 多线程：充分利用多核
- 每个 subReactor 一个线程，绑定到一个 CPU 核心
- 减小临界区，避免锁竞争
- 业务逻辑放到线程池，不阻塞 EventLoop
```

【面试坑点】

- 不要盲目优化，先定位瓶颈
- 锁优化是重点：减小临界区、避免锁竞争
- CPU 亲和性可以减少缓存失效

【结合项目加分话术】

- "我优化过 CPU 性能，先用 perf 找到热点函数，然后优化算法"
- "多 Reactor 模式，每个线程绑定到一个 CPU 核心，减少缓存失效"
- "减小临界区，把业务逻辑放到线程池，不阻塞 EventLoop"

***

#### 12.2.3 【必考】内存性能调优？

**问题：** 请说明内存性能调优的方法？

**标准答案：**

```
内存性能调优：

1. 定位内存瓶颈
   - free -h：查看内存使用
   - vmstat 1：查看 swap（si/so）
   - pmap <pid>：查看进程内存映射
   - valgrind --leak-check=full ./program：检测内存泄漏
   - AddressSanitizer：编译时加 -fsanitize=address

2. 应用层优化
   - 避免内存泄漏：
     · malloc/free 配对
     · C++ 用 RAII（std::unique_ptr、std::shared_ptr）
     · 容器里放指针要记得 delete
   
   - 减少内存分配：
     · 内存池：预分配内存，避免频繁 malloc/free
     · 对象池：预分配对象
     · Buffer 复用
   
   - 减少内存碎片：
     · 内存池
     · tcmalloc/jemalloc 替代 ptmalloc
     · 大页（Huge Page）
   
   - 优化数据结构：
     · 选择合适的容器（vector vs list）
     · 避免不必要的拷贝

3. 系统层优化
   - 调整内存分配器：
     · tcmalloc（Google）：多线程性能好，碎片少
     · jemalloc（FreeBSD）：多线程性能好，碎片少
     · 替代 glibc 的 ptmalloc
   
   - 大页（Huge Page）：
     · 减少 TLB miss
     · 提高内存访问速度
     · 适合大内存分配
     · 配置：/sys/kernel/mm/hugepages/
   
   - 关闭 swap：
     · swap 会导致性能下降
     · 生产环境建议关闭 swap
     · 命令：swapoff -a
   
   - 调整内存参数：
     · /proc/sys/vm/

4. 架构优化
   - 分布式缓存：Redis、Memcached
   - 冷热数据分离：热点数据放内存，冷数据放磁盘

Reactor 场景内存优化：
- Buffer 内存池：减少频繁 malloc/free
- tcmalloc/jemalloc 替代 ptmalloc
- 避免大内存拷贝（用移动语义、引用）
```

【面试坑点】

- 内存泄漏不是"内存占用高"，是"只增不减"
- swap 会严重影响性能，生产环境建议关闭
- tcmalloc/jemalloc 在多线程场景比 ptmalloc 好很多

【结合项目加分话术】

- "我用 valgrind 检测过内存泄漏，发现是连接断开后没有 delete TcpConnection"
- "项目中用了 tcmalloc，QPS 提升了约 20%"
- "实现了 Buffer 内存池，减少了频繁 malloc/free，内存碎片率降低"

***

#### 12.2.4 【高频】网络性能调优？

**问题：** 请说明网络性能调优的方法？

**标准答案：**

```
网络性能调优：

1. 定位网络瓶颈
   - ss -s：查看连接统计
   - ss -tulpn：查看监听端口
   - ss -ti：查看 TCP 连接信息
   - iftop：查看网络流量
   - sar -n DEV 1：查看网络接口统计
   - netstat -s：查看网络统计
   - tcpdump：抓包分析
   - tcptrace：分析 tcpdump 结果

2. 应用层优化
   - 协议优化：
     · 合理的协议设计（长度前缀）
     · 减少数据包大小
     · 压缩：gzip、snappy
   
   - 连接优化：
     · 长连接替代短连接
     · 连接池
     · 心跳保活（应用层心跳）
   
   - 数据传输优化：
     · Buffer 批处理，减少 send()/recv() 次数
     · 零拷贝：sendfile()、mmap()
     · 避免小数据包（禁用 Nagle，设置 TCP_NODELAY）
   
   - 并发优化：
     · 多 Reactor 多线程
     · 充分利用多核

3. 系统层优化
   - Socket 选项：
     · SO_REUSEADDR：允许重用端口
     · SO_RCVBUF/SO_SNDBUF：调整缓冲区大小
     · TCP_NODELAY：禁用 Nagle 算法
     · TCP_QUICKACK：快速 ACK
   
   - 内核参数调优：
     · 见下一章（高并发内核参数优化）

4. 架构优化
   - 负载均衡：Nginx、LVS、HAProxy
   - CDN：静态资源 CDN
   - 多机房：就近接入
   - 协议升级：HTTP/2、HTTP/3、QUIC

Reactor 场景网络优化：
- 长连接 + 应用层心跳
- Buffer 批处理
- 禁用 Nagle（TCP_NODELAY）
- 零拷贝（sendfile()）
- 调整内核参数（见下一章）
```

【面试坑点】

- 短连接性能差，尽量用长连接
- Nagle 算法会增加延迟，Reactor 场景一般禁用（TCP\_NODELAY）
- 零拷贝可以减少 CPU 开销

【结合项目加分话术】

- "我的项目用长连接 + 应用层心跳，避免频繁建立连接"
- "禁用了 Nagle 算法（TCP\_NODELAY），降低延迟"
- "文件下载用 sendfile() 零拷贝，QPS 提升了约 30%"

***

#### 12.2.5 【了解】磁盘性能调优？

**问题：** 请说明磁盘性能调优的方法？

**标准答案：**

```
磁盘性能调优：

1. 定位磁盘瓶颈
   - iostat -x 1：查看磁盘 I/O
     · %util：磁盘使用率
     · await：I/O 等待时间
     · r/s、w/s：读写次数
     · rMB/s、wMB/s：读写带宽
   - iotop：查看进程 I/O 占用
   - df -h：查看磁盘空间
   - du -sh：查看目录大小
   - smartctl：查看磁盘健康状态

2. 应用层优化
   - 减少磁盘 I/O：
     · 缓存：热点数据放内存（Redis、Memcached）
     · 批量写：Buffer 攒一批再写
     · 异步写：后台线程写磁盘
   
   - 顺序写替代随机写：
     · 日志：顺序写（WAL、Append-Only）
     · 避免随机读写
   
   - 文件系统优化：
     · 选择合适的文件系统（ext4、xfs）
     · noatime：不更新 atime（减少磁盘 I/O）
     · 日志模式：data=writeback（ext4）

3. 系统层优化
   - I/O 调度器：
     · cfq：完全公平队列（默认，适合通用）
     · deadline：截止时间（适合数据库）
     · noop：无操作（适合 SSD）
     · 查看：cat /sys/block/<dev>/queue/scheduler
     · 修改：echo deadline > /sys/block/<dev>/queue/scheduler
   
   - 调整 I/O 缓冲区：
     · /proc/sys/vm/dirty_background_ratio
     · /proc/sys/vm/dirty_ratio
   
   - RAID：
     · RAID 0：条带化（性能好，无冗余）
     · RAID 1：镜像（冗余，性能一般）
     · RAID 5/6：校验（性能 + 冗余）
     · RAID 10：镜像 + 条带（性能 + 冗余，推荐）

4. 硬件优化
   - SSD 替代 HDD：SSD 随机读写快很多
   - NVMe SSD：比 SATA SSD 更快
   - 增加磁盘：用 RAID 0/10

Reactor 场景磁盘优化：
- 日志：顺序写，异步刷盘
- 配置：noatime 挂载
- SSD 替代 HDD
```

【面试坑点】

- SSD 随机读写比 HDD 快很多，数据库场景建议用 SSD
- noatime 可以减少磁盘 I/O（不更新访问时间）
- 顺序写比随机写快很多

【结合项目加分话术】

- "日志用顺序写 + 异步刷盘，性能好"
- "文件系统用 noatime 挂载，减少磁盘 I/O"
- "数据库用 SSD，随机读写性能提升明显"

***

### 12.3 进阶高频深挖题

#### 12.3.1 【高频】perf 工具的使用？

**问题：** 请说明 perf 工具的使用方法？

**标准答案：**

```
perf 工具：
- Linux 性能分析工具
- 基于性能计数器（Performance Counter）
- 可以分析 CPU 缓存、分支预测、TLB 等

常用命令：

1. perf top
   - 实时查看 CPU 热点函数
   - 类似 top，但看的是函数
   - 命令：perf top
   
2. perf record
   - 记录性能数据
   - 常用选项：
     · -g：记录调用栈
     · -p <pid>：指定进程
     · sleep 10：记录 10 秒
   - 命令：
     · perf record -g ./program
     · perf record -g -p <pid> sleep 10
   
3. perf report
   - 分析 perf record 生成的数据
   - 命令：perf report
   
4. perf stat
   - 统计性能计数器
   - 命令：perf stat ./program
   - 输出：
     · 周期数
     · 指令数
     · 缓存 miss 数
     · 分支 miss 数
     · ...
   
5. perf list
   - 列出所有可用的性能计数器
   - 命令：perf list

常见场景：
- 找 CPU 热点函数：perf top / perf record + perf report
- 分析调用栈：perf record -g + perf report
- 分析缓存 miss：perf stat -e cache-misses ./program
- 分析分支预测：perf stat -e branch-misses ./program

Reactor 关联：
- 用 perf 分析 EventLoop 的 CPU 占用
- 找热点函数优化
- 分析锁竞争
```

【面试坑点】

- perf record 需要权限（一般需要 root）
- perf record -g 可以记录调用栈
- perf 是 Linux 性能分析的神器

【结合项目加分话术】

- "我用 perf top 找过热点函数，优化了算法"
- "perf record -g 记录调用栈，perf report 分析，找到了性能瓶颈"

***

#### 12.3.2 【了解】CPU 亲和性？

**问题：** 请说明 CPU 亲和性（CPU Affinity）的作用？

**标准答案：**

```
CPU 亲和性（CPU Affinity）：
- 定义：绑定进程/线程到特定的 CPU 核心
- 作用：
  1. 减少缓存失效（Cache Miss）
  2. 提高缓存命中率
  3. 减少上下文切换开销

为什么能提高性能？
- CPU 有 L1/L2/L3 缓存
- 线程在同一个 CPU 核心运行，缓存的数据还在
- 线程切换到不同 CPU 核心，缓存失效，需要重新从内存加载
- L1/L2 缓存是每个核心私有的，L3 是共享的

命令行设置：
- taskset：设置进程的 CPU 亲和性
  · taskset -c 0 ./program  # 绑定到 CPU 0
  · taskset -c 0,2,4-7 ./program  # 绑定到 CPU 0,2,4,5,6,7
  · taskset -p -c 0 <pid>  # 绑定运行中的进程

代码设置（pthread）：
#include <pthread.h>
#include <sched.h>

// 绑定当前线程到 CPU core
void SetCPUAffinity(int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// 绑定线程到 CPU core
void SetThreadCPUAffinity(pthread_t tid, int core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset);
}

查看 CPU 亲和性：
- 命令：taskset -p <pid>
- 代码：pthread_getaffinity_np()

Reactor 关联：
- 多 Reactor 多线程模式
- 每个 subReactor 一个线程，绑定到一个 CPU 核心
- 减少缓存失效，提高性能
```

【面试坑点】

- CPU 亲和性不是"一定能提高性能"，要实测
- 绑定太死可能导致负载不均衡
- L3 缓存是共享的，绑定到同一个 L3 缓存的核心也可以

【结合项目加分话术】

- "我在多 Reactor 模式中，每个线程绑定到一个 CPU 核心"
- "减少了缓存失效，性能提升了约 5-10%"

***

## （本章完）

## 十三、高并发场景下的内核参数优化

***

### 13.1 核心原理

#### 内核参数

```
内核参数配置文件：
- /etc/sysctl.conf：永久生效（重启后）
- /proc/sys/：临时生效（立即生效，重启后失效）

临时生效：
sysctl -w net.core.somaxconn=65535

永久生效：
1. 编辑 /etc/sysctl.conf
2. 添加配置
3. sysctl -p  # 加载配置

查看参数：
sysctl <参数名>
或
cat /proc/sys/<参数路径>

高并发场景主要优化：
1. 文件描述符
2. TCP 参数
3. 内存参数
4. 网络参数
```

***

### 13.2 基础必背题

#### 13.2.1 【必考】文件描述符限制？

**问题：** 请说明如何调整文件描述符限制的方法？

**标准答案：**

```
文件描述符（fd）限制：
- 每个进程有最大 fd 有限制
- 高并发场景需要调大

两个限制：
1. 系统级限制（所有进程总和）
   - /proc/sys/fs/file-max
   
2. 用户级限制（单个用户的所有进程）
   - /etc/security/limits.conf

查看当前限制：
# 系统级：
cat /proc/sys/fs/file-max
ulimit -n  # 注意：ulimit -n 看的是当前 shell 进程级

# 用户级：
ulimit -a | grep open

临时修改：

1. 系统级限制（/proc/sys/fs/file-max）：
# 临时生效
echo 1000000 > /proc/sys/fs/file-max
或
sysctl -w fs.file-max=1000000

# 永久生效：/etc/sysctl.conf
fs.file-max = 1000000

2. 用户级限制（/etc/security/limits.conf）：
# 永久生效：编辑 /etc/security/limits.conf
* soft nofile 655350
* hard nofile 655350
root soft nofile 655350
root hard nofile 655350

# 说明：
# * 表示所有用户
# soft 软限制（警告）
# hard 硬限制（最大值）
# nofile 最大打开文件数
# 655350 是推荐值

进程级限制（当前进程）：
// C++ 代码中设置
#include <sys/resource.h>

struct rlimit rlim;
rlim.rlim_cur = 655350;  // 软限制
rlim.rlim_max = 655350;  // 硬限制
setrlimit(RLIMIT_NOFILE, &rlim);

Reactor 关联：
- 高并发服务器，每个连接一个 fd
- 10 万连接需要 10 万+ fd
- 必须调大 fd 限制
- 程序启动时用 setrlimit() 设置
```

【面试坑点】

- 区分系统级、用户级、进程级三个限制
- /etc/security/limits.conf 需要重新登录才生效
- 程序启动时最好用 setrlimit() 设置

【结合项目加分话术】

- "我的 TcpServer 启动时用 setrlimit() 设置最大 fd 为 655350
- "/etc/sysctl.conf 设置 fs.file-max=1000000
- "/etc/security/limits.conf 设置 \* soft/hard nofile 655350

***

#### 13.2.2 【必考】TCP 内核参数优化？

**问题：** 请说明高并发场景下 TCP 内核参数优化？

**标准答案：**

TCP 内核参数优化（/etc/sysctl.conf）：

##### 1. 通用网络核心参数

```
net.core.somaxconn = 65535

#全连接队列（accept 队列）的最大长度

#默认 128，高并发调大

#listen() 的 backlog 参数受限于此

net.core.netdev_max_backlog = 65535

#网络设备接收队列的最大长度

#默认 1000，高并发调大

net.core.rmem_default = 8388608

#接收缓冲区默认大小（字节）

#默认 212992，调大到 8MB

net.core.rmem_max = 16777216

#接收缓冲区最大大小（字节）

#默认 212992，调大到 16MB

net.core.wmem_default = 8388608

#发送缓冲区默认大小（字节）

#默认 212992，调大到 8MB

net.core.wmem_max = 16777216

#发送缓冲区最大大小（字节）

#默认 212992，调大到 16MB

net.core.optmem_max = 16777216

#socket 控制缓冲区最大大小
```

##### 2. TCP 核心参数

```
net.ipv4.tcp_max_syn_backlog = 65535

#半连接队列（SYN 队列）的最大长度

#默认 1024，高并发调大

net.ipv4.tcp_syncookies = 1

#启用 SYN Cookies

#防止 SYN Flood 攻击

#默认 1，保持

net.ipv4.tcp_tw_reuse = 1

#允许重用 TIME_WAIT 状态的连接

#用于新连接可以重用 TIME_WAIT 的端口

#默认 0，高并发建议 1

#注意：只用于客户端连接服务器时有用，服务器端也可以开

net.ipv4.tcp_tw_recycle = 0

#快速回收 TIME_WAIT 状态的连接

#默认 0，建议 0！

#NAT 环境下有问题，不要开！

net.ipv4.tcp_fin_timeout = 30

#FIN_WAIT_2 状态的超时时间

#默认 60，调小到 30

net.ipv4.tcp_max_tw_buckets = 65536

#TIME_WAIT 状态连接的最大数量

#默认 180000，调大

net.ipv4.ip_local_port_range = 1024 65535

#本地端口范围

#客户端连接服务器时用的端口

#默认 32768 60999，调大

net.ipv4.tcp_slow_start_after_idle = 0

#连接空闲后不进入慢启动

#默认 1，建议 0

#长连接场景有用
```

##### 3. TCP 缓冲区

```
net.ipv4.tcp_rmem = 4096 8388608 16777216

#TCP 接收缓冲区（最小 默认 最大

#默认 4096 87380 6291456，调大

net.ipv4.tcp_wmem = 4096 8388608 16777216

#TCP 发送缓冲区（最小 默认 最大）

#默认 4096 16384 4194304，调大

net.ipv4.tcp_mem = 786432 1048576 1572864

#TCP 内存使用（页）

#低  压力  高
```

##### 4. TCP 时间戳

```
net.ipv4.tcp_timestamps = 1

#启用 TCP 时间戳

#默认 1，保持

#用于 RTT 计算、PAWS 等

net.ipv4.tcp_window_scaling = 1

#启用 TCP 窗口缩放

#默认 1，保持

#支持大窗口
```

##### 5. 防攻击防护

```
net.ipv4.tcp_sack = 1
net.ipv4.tcp_syn_retries = 2

#SYN 重试次数

#默认 5，调小到 2

net.ipv4.tcp_synack_retries = 2

#SYN+ACK 重试次数

#默认 5，调小到 2
```

##### 6. 其他

```
net.ipv4.conf.all.rp_filter = 0
net.ipv4.conf.default.rp_filter = 0

#反向路径过滤

#0 关闭，1 开启

#高并发建议 0

net.ipv4.icmp_echo_ignore_broadcasts = 1

#忽略广播 ping 广播

net.ipv4.icmp_ignore_bogus_error_responses = 1

#忽略错误的 ICMP 响应
```

```

【面试坑点】
- net.ipv4.tcp_tw_recycle 不要开！NAT 环境有问题
- net.ipv4.tcp_tw_reuse 可以开
- net.core.somaxconn 和 net.ipv4.tcp_max_syn_backlog 都要调大

【结合项目加分话术】
- "我在高并发场景下调整了这些 TCP 内核参数
- "net.core.somaxconn=65535，net.ipv4.tcp_max_syn_backlog=65535
- "net.ipv4.tcp_tw_reuse=1，允许重用 TIME_WAIT 连接
- "注意 net.ipv4.tcp_tw_recycle=0，NAT 环境有问题，不要开
```

***

#### 13.2.3 【高频】内存内核参数优化？

**问题：** 请说明内存内核参数优化？

**标准答案：**

```
内存内核参数优化（/etc/sysctl.conf）：

# 1. 内存使用
vm.swappiness = 0
# swappiness 0-100
# 0：尽量不用 swap
# 100：积极用 swap
# 高并发建议 0，关闭 swap

vm.overcommit_memory = 1
# 内存 overcommit 策略
# 0：启发式（默认）
# 1：允许 overcommit，不检查
# 2：不允许 overcommit
# 高并发建议 1

vm.overcommit_ratio = 50
# 当 vm.overcommit_memory=2 时生效
# 可分配内存 = 物理内存 * ratio + swap
# 默认 50

# 2. 脏页（Dirty Page）
vm.dirty_background_ratio = 5
# 脏页达到 5% 时，后台开始回写
# 默认 10，调小到 5

vm.dirty_ratio = 10
# 脏页达到 10% 时，进程阻塞回写
# 默认 20，调小到 10

vm.dirty_writeback_centisecs = 500
# 回写线程运行间隔（厘秒）
# 默认 500，5 秒

vm.dirty_expire_centisecs = 3000
# 脏页过期时间（厘秒）
# 默认 3000，30 秒

# 3. 大页（Huge Page）
# 如果用大页，配置：
# vm.nr_hugepages = 1024
# 大页数量
# 需要根据实际情况配置

Reactor 关联：
- vm.swappiness=0，尽量不用 swap
- vm.overcommit_memory=1，允许内存 overcommit
```

【面试坑点】

- vm.swappiness=0 不是"完全不用 swap"，是"尽量不用"
- 生产环境建议关闭 swap（swapoff -a）
- vm.overcommit\_memory=1 允许 overcommit

【结合项目加分话术】

- "我设置了 vm.swappiness=0，尽量不用 swap
- "vm.overcommit\_memory=1，允许内存 overcommit
- "生产环境关闭了 swap，swapoff -a

```

---

### 13.2.4 【了解】其他内核参数优化？

**问题：** 请说明其他内核参数优化？

**标准答案：**

```

其他内核参数优化（/etc/sysctl.conf）：

##### 1. 文件系统

```
fs.file-max = 1000000

# 系统级最大文件描述符

# 前面讲过

fs.inotify.max_user_instances = 8192

# inotify 实例最大数量

fs.inotify.max_user_watches = 524288

# inotify 监控最大数量

##### 2. 网络

net.core.netdev_max_backlog = 65535

# 网络设备接收队列

# 前面讲过

net.nf_conntrack_max = 655360

# 连接跟踪表最大大小

# 有防火墙时需要

net.netfilter.nf_conntrack_tcp_timeout_established = 3600

# ESTABLISHED 状态超时时间

# 默认 432000（5 天），调小到 3600（1 小时）
```

##### 3. 其他

```
kernel.pid_max = 655360

# PID 最大值

# 默认 32768，调大

kernel.threads-max = 655360

# 线程最大值

# 默认根据内存

kernel.msgmax = 65536

# 消息队列最大消息大小

kernel.msgmnb = 65536

# 消息队列最大字节数

kernel.sem = 250 32000 100 128

# 信号量参数

Reactor 关联：

- 有防火墙时需要调大 nf_conntrack_max



【面试坑点】

- nf_conntrack 是连接跟踪，有 iptables 时需要
- nf_conntrack_max 太小会导致丢包

【结合项目加分话术】

- "有防火墙时调大了 net.nf_conntrack_max=655360
- "net.netfilter.nf_conntrack_tcp_timeout_established=3600
```

***

### 13.3 进阶高频深挖题

##### 13.3.1 【高频】完整的 sysctl.conf 配置？

**问题：** 请给出一份高并发场景下完整的 sysctl.conf 配置？

**标准答案：**

高并发场景 sysctl.conf 完整配置：

```
# /etc/sysctl.conf

# 高并发 Linux 内核参数优化

# 适用于：C++ 后端服务器、Reactor 高并发服务器

# ==========================================

# 文件系统

# ==========================================

fs.file-max = 1000000
fs.inotify.max_user_instances = 8192
fs.inotify.max_user_watches = 524288

# ==========================================

# 网络核心

# ==========================================

net.core.somaxconn = 65535
net.core.netdev_max_backlog = 65535
net.core.rmem_default = 8388608
net.core.rmem_max = 16777216
net.core.wmem_default = 8388608
net.core.wmem_max = 16777216
net.core.optmem_max = 16777216

# ==========================================

# IPv4 核心

# ==========================================

net.ipv4.ip_forward = 0
net.ipv4.conf.all.rp_filter = 0
net.ipv4.conf.default.rp_filter = 0
net.ipv4.icmp_echo_ignore_broadcasts = 1
net.ipv4.icmp_ignore_bogus_error_responses = 1

# ==========================================

# TCP

# ==========================================

net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.tcp_syncookies = 1
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_tw_recycle = 0
net.ipv4.tcp_fin_timeout = 30
net.ipv4.tcp_max_tw_buckets = 65536
net.ipv4.ip_local_port_range = 1024 65535
net.ipv4.tcp_slow_start_after_idle = 0

# TCP 缓冲区

net.ipv4.tcp_rmem = 4096 8388608 16777216
net.ipv4.tcp_wmem = 4096 8388608 16777216
net.ipv4.tcp_mem = 786432 1048576 1572864

# TCP 时间戳

net.ipv4.tcp_timestamps = 1
net.ipv4.tcp_window_scaling = 1
net.ipv4.tcp_sack = 1

# TCP 重试

net.ipv4.tcp_syn_retries = 2
net.ipv4.tcp_synack_retries = 2

# ==========================================

# 内存

# ==========================================

vm.swappiness = 0
vm.overcommit_memory = 1
vm.overcommit_ratio = 50

# 脏页

vm.dirty_background_ratio = 5
vm.dirty_ratio = 10
vm.dirty_writeback_centisecs = 500
vm.dirty_expire_centisecs = 3000

# ==========================================

# 其他

# ==========================================

kernel.pid_max = 655360
kernel.threads-max = 655360
kernel.msgmax = 65536
kernel.msgmnb = 65536
kernel.sem = 250 32000 100 128

# ==========================================

# 连接跟踪（有 iptables 时启用）

# ==========================================

# net.nf_conntrack_max = 655360

# net.netfilter.nf_conntrack_tcp_timeout_established = 3600
```

```

【结合项目加分话术】

- "我用这份 sysctl.conf 配置
- "配合 /etc/security/limits.conf 配置 fd 限制
- "程序启动时用 setrlimit() 设置进程级 fd 限制

```

***

### 13.3.2 【了解】limits.conf 配置？

**问题：** 请给出 limits.conf 配置？

**标准答案：**

```

/etc/security/limits.conf 配置：

# /etc/security/limits.conf

# 用户级限制

* soft nofile 655350
* hard nofile 655350

root soft nofile 655350
root hard nofile 655350

* soft nproc 655350
* hard nproc 655350

root soft nproc 655350
root hard nproc 655350

# 说明：

# *：所有用户

# root：root 用户

# soft：软限制

# hard：硬限制

# nofile：最大打开文件数

# nproc：最大进程数

# 655350：推荐值

# 使生效：

# 1. 重新登录

# 2. 或者程序启动时用 setrlimit() 设置

# 查看：

# ulimit -a

```

【结合项目加分话术】

- "我配置了 limits.conf，\* soft/hard nofile 655350
- "程序启动时用 setrlimit() 设置，确保生效

```

---

## 13.4 总结

```

高并发服务器优化步骤：

1. 调整文件描述符限制
   - /etc/sysctl.conf：fs.file-max=1000000
   - /etc/security/limits.conf：\* soft/hard nofile 655350
   - 程序启动：setrlimit()
2. 调整 TCP 内核参数
   - /etc/sysctl.conf 配置 TCP 参数
   - 重点：somaxconn、tcp\_max\_syn\_backlog、tcp\_tw\_reuse
   - 不要开 tcp\_tw\_recycle
3. 调整内存参数
   - vm.swappiness=0
   - vm.overcommit\_memory=1
   - 关闭 swap
4. 应用层优化
   - 多 Reactor 多线程
   - Buffer 批处理
   - 内存池
   - 零拷贝

