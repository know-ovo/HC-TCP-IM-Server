# WSL2 下服务端与客户端使用指南

本文档基于 `WSL2 Ubuntu` 环境实测通过，目标场景如下：

- 启动 `1` 个服务端
- 启动 `2` 个交互式客户端
- 客户端 A 先发送广播消息
- 然后客户端 A 和客户端 B 互相发送点对点消息

## 一、先看结论

要测试广播和点对点消息，必须启动：

- 服务端：`server_full`
- 客户端：`interactiveClient`

不要用基础版 `server` 做这组测试，因为它不处理广播和点对点消息。

## 二、实测前提

在 WSL2 Ubuntu 中进入项目根目录：

```bash
cd /mnt/d/桌面/Linux+C++服务器开发
```

如果还没编译，先执行：

```bash
cmake -S . -B build
cmake --build build -j4
```

## 三、配置说明

本地功能联调时，建议先关闭 Redis 依赖。当前仓库已按此方式配置：

```ini
[redis]
enabled = false
```

对应文件：

```text
conf/server.ini
```

这样可以避免本机没有 Redis，或者 Redis 会话分支影响本地广播/私聊联调。

## 四、登录规则

客户端登录 token 不是任意字符串，必须满足下面规则：

```text
token = valid_token_<userId>
```

例如：

- `user401` 对应 token：`valid_token_user401`
- `user402` 对应 token：`valid_token_user402`

如果 token 不符合这个规则，登录会失败。

## 五、服务端启动

在第 1 个终端启动完整版服务端：

```bash
cd /mnt/d/桌面/Linux+C++服务器开发
./build/bin/server_full
```

启动成功后，通常能看到类似输出：

```text
HighConcurrencyTCPGateway (Full) starting...
TcpServer::start - listening on fd ...
Server (Full) started, listening on port 8888
```

## 六、客户端启动

### 终端 2：客户端 A

```bash
cd /mnt/d/桌面/Linux+C++服务器开发
./build/bin/interactiveClient -h 127.0.0.1 -p 8888
```

### 终端 3：客户端 B

```bash
cd /mnt/d/桌面/Linux+C++服务器开发
./build/bin/interactiveClient -h 127.0.0.1 -p 8888
```

## 七、交互客户端支持的命令

```text
login <userId> <token> [deviceId]
p2p <toUserId> <message>
broadcast <message>
kick <userId> [reason]
help
quit
```

## 八、完整手工测试流程

下面这套命令已经实际验证通过。

### 步骤 1：客户端 A 登录

在终端 2 输入：

```text
login user401 valid_token_user401 dev1
```

预期输出类似：

```text
登录请求已发送...
[系统] 登录成功！SessionId: xxxxxxxxx
```

### 步骤 2：客户端 B 登录

在终端 3 输入：

```text
login user402 valid_token_user402 dev2
```

预期输出类似：

```text
登录请求已发送...
[系统] 登录成功！SessionId: xxxxxxxxx
```

### 步骤 3：客户端 A 发送广播

在终端 2 输入：

```text
broadcast hello_all_from_user401
```

预期现象：

- 终端 2 会看到自己收到广播
- 终端 3 也会收到同一条广播

终端 2 / 终端 3 预期能看到类似输出：

```text
[21:12:18] user401: hello_all_from_user401
```

### 步骤 4：客户端 A 给客户端 B 发私聊

在终端 2 输入：

```text
p2p user402 hello_user402_from_user401
```

预期现象：

- 终端 2 会看到发送成功回执
- 终端 3 会收到来自 `user401` 的消息

终端 2 预期输出类似：

```text
点对点消息已发送
[消息] 点对点消息发送成功，MsgId: 2
```

终端 3 预期输出类似：

```text
[21:12:22] user401: hello_user402_from_user401
```

### 步骤 5：客户端 B 给客户端 A 发私聊

在终端 3 输入：

```text
p2p user401 hello_user401_from_user402
```

预期现象：

- 终端 3 会看到发送成功回执
- 终端 2 会收到来自 `user402` 的消息

终端 3 预期输出类似：

```text
点对点消息已发送
[消息] 点对点消息发送成功，MsgId: 3
```

终端 2 预期输出类似：

```text
[21:12:24] user402: hello_user401_from_user402
```

### 步骤 6：退出客户端

两个客户端分别输入：

```text
quit
```

服务端停止时在终端 1 按：

```text
Ctrl+C
```

## 九、可直接复现的自动化命令

如果你想快速一键复现这次联调流程，可以在 3 个终端分别执行以下命令。

### 终端 1

```bash
cd /mnt/d/桌面/Linux+C++服务器开发
./build/bin/server_full
```

### 终端 2

```bash
cd /mnt/d/桌面/Linux+C++服务器开发
{ 
  sleep 1
  echo "login user401 valid_token_user401 dev1"
  sleep 4
  echo "broadcast hello_all_from_user401"
  sleep 4
  echo "p2p user402 hello_user402_from_user401"
  sleep 5
  echo "quit"
} | ./build/bin/interactiveClient -h 127.0.0.1 -p 8888
```

### 终端 3

```bash
cd /mnt/d/桌面/Linux+C++服务器开发
{
  sleep 1
  echo "login user402 valid_token_user402 dev2"
  sleep 10
  echo "p2p user401 hello_user401_from_user402"
  sleep 6
  echo "quit"
} | ./build/bin/interactiveClient -h 127.0.0.1 -p 8888
```

## 十、这次为跑通联调所做的修复

本次已经完成并验证通过的修复包括：

1. 清理了 `interactiveClient.cpp` 和 `channel.h` 文件头部异常字符，修复编译失败。
2. 修正了 `interactiveClient.cpp` 的收包解析逻辑，使其与服务端 `Codec::pack` 协议一致。
3. 修正了 `TcpConnection::send(const void*, size_t)` 的发送实现，改为先复制数据再跨线程/异步发送，避免发送数据生命周期问题。
4. 将 `conf/server.ini` 的 Redis 默认开关调整为 `false`，避免本地功能联调被 Redis 分支影响。

## 十一、版本说明

| 项目 | 用途 |
| ---- | ---- |
| `build/bin/server` | 基础版服务端，只适合登录、心跳等基础功能测试 |
| `build/bin/server_full` | 完整版服务端，支持广播、点对点消息、踢人 |
| `build/bin/interactiveClient` | 交互式功能测试客户端 |
| `build/bin/benchmarkClient` | 压测客户端 |
