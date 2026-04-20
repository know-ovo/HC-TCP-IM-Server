# 项目各业务运行流程（一问一答精确版）

---

## 一、连接建立流程

### Q1: 当客户端发起TCP连接时，服务器的完整处理流程是什么？

**A1:**

1. 首先调用 `TcpServer::start()` 启动服务器，`Acceptor` 开始监听端口。
2. 客户端调用 `connect()` 发起连接，服务器调用 `accept()` 接受新连接。
3. 创建 `TcpConnection` 对象，封装 socket 文件描述符。
4. 设置回调函数：
   - 调用 `m_channel->setReadCallback()` 设置读回调为 `handleRead()`。
   - 调用 `m_channel->setWriteCallback()` 设置写回调为 `handleWrite()`。
   - 调用 `m_channel->setCloseCallback()` 设置关闭回调为 `handleClose()`。
   - 调用 `m_channel->setErrorCallback()` 设置错误回调为 `handleError()`。
5. 调用 `TcpConnection::connectEstablished()` 完成连接建立。
6. 在 `connectEstablished()` 内部：
   - 调用 `setState(kConnected)` 将状态设为已连接。
   - 调用 `m_channel->enableReading()` 启用读事件监听。
   - 调用 `m_connectionCallback` 触发连接建立回调。
7. 在 `connectionCallback`（`server_full.cpp:241-269`）中：
   - 调用 `overloadService->canAcceptConnection()` 检查是否过载。
   - 如果过载，调用 `conn->forceClose()` 直接关闭连接。
   - 如果不过载，调用 `overloadService->onConnectionAccepted()` 增加连接计数。
   - 调用 `heartbeatService->onConnection(conn, true)` 注册到心跳服务。

---

## 二、登录流程

### Q2: 客户端发送登录请求的完整处理流程是什么？

**A2:**

1. 客户端发送 `CmdLoginReq` 消息，包含 userId、token、deviceId，格式为 JSON。
2. 服务器接收数据：
   - 调用 `TcpConnection::handleRead()` 处理可读事件。
   - 调用 `m_inputBuffer.readFd(m_sockfd)` 将数据读取到 Buffer。
   - 调用 `m_messageCallback` 触发消息回调。
3. 解析协议：
   - 调用 `Codec::onMessage()` 开始解析。
   - 调用 `parseHeader()` 读取 16 字节协议头。
   - 调用 `parsePayload()` 读取消息体并验证 CRC。
   - 调用 `m_messageCallback` 触发业务回调。
4. 业务处理（`server_full.cpp:136-168`）：
   - 调用 `serializer::Deserialize(message, loginReq)` 反序列化 JSON。
   - 调用 `authService->login()` 验证用户 token。
   - 如果验证成功：
     - 构造 `LoginResp` 对象，设置 resultCode=0、resultMsg="success"、sessionId。
     - 调用 `serializer::Serialize(loginResp)` 序列化为 JSON。
     - 调用 `Codec::pack()` 打包，添加 16 字节协议头。
     - 调用 `conn->send()` 发送响应。
     - 调用 `messageService->registerConnection(userId, conn)` 注册用户连接。
     - 在 Redis 模式下，调用 `heartbeatService->registerUserId()` 注册用户 ID。
   - 如果验证失败：
     - 构造 `LoginResp` 对象，设置 resultCode=1、resultMsg=errorMsg。
     - 调用 `conn->send()` 发送错误响应。

---

## 三、心跳流程

### Q3: 心跳机制的完整运行流程是什么？

**A3:**

【注册阶段】
1. 连接建立时，调用 `heartbeatService->onConnection(conn, true)` 注册连接。
2. 记录连接信息，设置 `lastHeartbeatTime = now`。

【心跳请求阶段】
1. 客户端定期发送 `CmdHeartbeatReq` 心跳请求。
2. 服务器接收：
   - 调用 `TcpConnection::handleRead()` 处理可读事件。
   - 调用 `Codec::onMessage()` 解析协议。
   - 调用业务回调（`server_full.cpp:170-180`）。
3. 更新心跳时间：
   - 调用 `heartbeatService->onHeartbeat(conn)` 更新 `lastHeartbeatTime = now`。
4. 构造心跳响应：
   - 构造 `HeartbeatResp` 对象，设置 `serverTime = util::GetTimestampMs()`。
   - 调用 `Codec::pack()` 打包。
   - 调用 `conn->send()` 发送响应。

【超时检查阶段】
1. `HeartbeatService` 启动定时器，默认间隔 10 秒。
2. 定时遍历所有连接：
   - 对每个连接，检查 `now - lastHeartbeatTime > heartbeatTimeout`（默认 30 秒）。
3. 如果超时：
   - 调用 `kickCallback(conn, reason)` 踢除连接。
   - 记录警告日志 `LOG_WARN()`。
   - 调用 `conn->forceClose()` 强制关闭连接。

---

## 四、P2P消息流程

### Q4: 用户A给用户B发送P2P消息的完整处理流程是什么？

**A4:**

1. 客户端A发送 `CmdP2pMsgReq` 消息，包含 fromUserId、toUserId、content。
2. 服务器接收并解析：
   - 调用 `TcpConnection::handleRead()` 处理可读事件。
   - 调用 `Codec::onMessage()` 解析协议。
   - 调用业务回调（`server_full.cpp:182-204`）。
3. 反序列化：
   - 调用 `serializer::Deserialize(message, p2pReq)` 反序列化 JSON。
4. 业务处理（`MessageService::sendP2PMessage`）：
   - 调用 `std::lock_guard<std::mutex> lock(m_mutex)` 加锁保护。
5. 查找目标用户：
   - 调用 `m_userToConn.find(toUserId)` 查找目标用户连接。
6. 如果用户B在线：
   - 调用 `outMsgId = m_nextMsgId++` 原子自增生成消息 ID。
   - 构造 `BroadcastMsgNotify` 对象，设置 fromUserId、content、timestamp。
   - 调用 `serializer::Serialize(notify)` 序列化为 JSON。
   - 调用 `Codec::pack(&buffer, CmdBroadcastMsgNotify, msgId, notifyMsg)` 打包。
   - 调用 `conn->send(buffer.peek(), buffer.readableBytes())` 通过B的连接发送。
7. 如果用户B不在线：
   - 设置 `errorMsg = "User not online"`。
8. 向用户A返回响应：
   - 构造 `P2PMsgResp` 对象，设置 resultCode、resultMsg、msgId。
   - 调用 `serializer::Serialize(p2pResp)` 序列化。
   - 调用 `Codec::pack()` 打包。
   - 调用 `conn->send()` 发送给用户A。

---

## 五、广播消息流程

### Q5: 广播消息的完整处理流程是什么？

**A5:**

1. 客户端发送 `CmdBroadcastMsgReq` 消息，包含 fromUserId、content。
2. 服务器接收并解析：
   - 调用 `TcpConnection::handleRead()` 处理可读事件。
   - 调用 `Codec::onMessage()` 解析协议。
   - 调用业务回调（`server_full.cpp:206-215`）。
3. 反序列化：
   - 调用 `serializer::Deserialize(message, broadcastReq)` 反序列化 JSON。
4. 业务处理（`MessageService::broadcastMessage`）：
   - 调用 `std::lock_guard<std::mutex> lock(m_mutex)` 加锁保护。
5. 构造广播消息：
   - 构造 `BroadcastMsgNotify` 对象，设置 fromUserId、content、timestamp。
6. 序列化打包：
   - 调用 `serializer::Serialize(notify)` 序列化为 JSON。
   - 调用 `uint32_t msgId = m_nextMsgId++` 生成消息 ID。
   - 调用 `Codec::pack(&buffer, CmdBroadcastMsgNotify, msgId, notifyMsg)` 打包。
7. 遍历所有在线用户发送：
   - 对 `m_userToConn` 中的每个连接进行遍历。
8. 逐个发送：
   - 如果连接存在且已连接，调用 `conn->send(buffer.peek(), buffer.readableBytes())` 发送。
9. 注意：广播消息不向发送方返回响应。

---

## 六、连接关闭流程

### Q6: 客户端主动断开连接时，服务器的完整处理流程是什么？

**A6:**

1. 检测到连接关闭：
   - 在 `TcpConnection::handleRead()` 中，调用 `readFd()` 返回 0，表示对端关闭。
   - 调用 `handleClose()` 处理连接关闭。
2. 或者检测到错误：
   - 在 `TcpConnection::handleError()` 中检测到错误。
   - 调用 `handleClose()` 处理连接关闭。
3. 在 `handleClose()` 内部：
   - 调用 `setState(kDisconnected)` 设置状态为已断开。
   - 调用 `m_channel->disableAll()` 禁用所有事件。
   - 调用 `m_connectionCallback` 触发连接回调。
   - 调用 `m_closeCallback` 触发关闭回调。
4. 在 `connectionCallback` 的连接关闭分支（`server_full.cpp:256-268`）中：
   - 调用 `overloadService->onConnectionClosed()` 减少连接计数。
   - 调用 `heartbeatService->onConnection(conn, false)` 从心跳服务注销。
   - 调用 `authService->getUserContext(conn)` 获取用户上下文。
   - 如果用户上下文存在，调用 `messageService->unregisterConnection(userContext->m_userId)` 从消息服务注销。
   - 调用 `authService->logout(conn)` 从认证服务注销。
5. `TcpConnection` 析构：
   - 调用 `closesocket(m_sockfd)` 或 `close(m_sockfd)` 关闭套接字。

---

## 七、超时踢人流程

### Q7: 心跳超时导致踢人的完整流程是什么？

**A7:**

1. `HeartbeatService` 定时器触发，默认每 10 秒触发一次。
2. 遍历所有连接：
   - 对 `m_connections` 中的每个连接进行遍历。
3. 获取连接最后心跳时间：
   - 读取 `conn->lastHeartbeatTime` 获取最后心跳时间。
4. 判断超时：
   - 检查 `now - lastHeartbeatTime > heartbeatTimeout`，默认超时时间 30 秒。
5. 如果超时：
   - 调用 `kickCallback(conn, "Heartbeat timeout")` 触发踢人回调。
6. 在 `kickCallback`（`server_full.cpp:277-280`）中：
   - 记录警告日志 `LOG_WARN("Kicking connection: {}, reason: {}", conn->name(), reason)`。
   - 调用 `conn->forceClose()` 强制关闭连接。
7. 在 `forceClose()` 内部：
   - 调用 `setState(kDisconnecting)` 设置状态为正在断开。
   - 调用 `m_loop->queueInLoop([this]() { forceCloseInLoop(); })` 投递关闭任务。
8. 在 `forceCloseInLoop()` 内部：
   - 调用 `handleClose()` 处理关闭。
9. 触发连接关闭流程（详见 Q6）。

---

## 八、过载保护流程

### Q8: 新连接建立时，过载保护的判断流程是什么？

**A8:**

1. 连接建立后，`connectionCallback` 被调用（`server_full.cpp:247-252`）。
2. 检查是否可以接受连接：
   - 调用 `overloadService->canAcceptConnection()` 判断。
3. 在 `canAcceptConnection()` 内部：
   - 返回 `currentConnCount_ < maxConnCount_` 的结果。
4. 如果过载：
   - 记录警告日志 `LOG_WARN("Connection rejected: overload")`。
   - 调用 `conn->forceClose()` 强制关闭连接。
   - 直接返回，不再继续处理。
5. 如果不过载：
   - 调用 `overloadService->onConnectionAccepted()`，原子自增 `currentConnCount_++`。
   - 继续后续处理，如心跳注册等。

---

## 九、消息ID生成流程

### Q9: 消息ID是如何保证唯一的？

**A9:**

1. `MessageService` 初始化：
   - 设置 `m_nextMsgId = 1`，类型为 `std::atomic<uint32_t>`。
2. 发送 P2P 消息时（`MessageService::sendP2PMessage`）：
   - 调用 `outMsgId = m_nextMsgId++`，即调用 `fetch_add(1)` 进行原子自增。
3. 发送广播消息时（`MessageService::broadcastMessage`）：
   - 调用 `uint32_t msgId = m_nextMsgId++` 生成消息 ID。
4. 原子操作保证：
   - 多线程安全。
   - 不会重复生成 ID。
   - ID 有序递增。

---

## 十、编解码流程

### Q10: 消息的编码和解码完整流程是什么？

**A10:**

【编码流程（发送端）】
1. 构造消息对象，如 `P2PMsgReq`、`LoginResp` 等。
2. 序列化：
   - 调用 `serializer::Serialize(obj)` 将对象转为 JSON 字符串。
3. 打包：
   - 调用 `Codec::pack(buffer, command, seqId, message)` 进行打包。
4. 在 `pack()` 内部：
   - 计算 `bodyLen = message.size()`。
   - 计算 `totalLen = 16 + bodyLen`。
   - 写入 16 字节协议头：
     - 偏移 0-3 字节：写入总长度，调用 `NetworkToHost32()` 转换字节序。
     - 偏移 4-5 字节：写入命令号，调用 `NetworkToHost16()` 转换字节序。
     - 偏移 6-9 字节：写入序列号，调用 `NetworkToHost32()` 转换字节序。
     - 偏移 10-11 字节：先填入 0 作为 CRC16 占位。
     - 偏移 12-15 字节：写入消息体长度，调用 `NetworkToHost32()` 转换字节序。
   - 调用 `buffer->append(message)` 追加消息体。
   - 调用 `CalcCRC16(buffer->peek(), totalLen)` 计算校验和。
   - 将计算得到的 CRC16 回填到协议头。
5. 发送：
   - 调用 `conn->send(buffer)` 发送数据。

【解码流程（接收端）】
1. 接收数据到 Buffer。
2. 调用 `Codec::onMessage(conn, buffer, receiveTime)` 开始解码。
3. 进入状态机循环：
   - `while (true)` 循环处理。
4. `kExpectHeader` 状态：
   - 如果 `buffer->readableBytes() < 16`，跳出循环。
   - 调用 `parseHeader(buffer)` 解析协议头。
   - 读取 16 字节头，调用 `NetworkToHost32()` 获取总长度。
   - 设置 `m_expectedLength = totalLen`。
   - 设置 `m_state = kExpectPayload`。
5. `kExpectPayload` 状态：
   - 如果 `buffer->readableBytes() < m_expectedLength`，跳出循环。
   - 调用 `parsePayload(conn, buffer)` 解析消息体。
   - 读取 `totalLen` 字节，验证 CRC16。
   - 提取 command、seqId、message（JSON 字符串）。
   - 调用 `buffer->retrieve(totalLen)` 消费数据。
   - 设置 `m_state = kExpectHeader`。
6. 调用业务回调：
   - 调用 `m_messageCallback(conn, command, seqId, message)`。
7. 业务层反序列化：
   - 调用 `serializer::Deserialize(message, obj)` 反序列化为对象。

---

## 十一、线程安全保证

### Q11: 服务器的线程安全是如何保证的？

**A11:**

【IO 线程内】
1. `EventLoop` 事件循环：
   - 所有回调在同一个线程串行执行。
   - 不需要锁，天然线程安全。

【跨线程通信】
2. 调用 `EventLoop::runInLoop()`：
   - 将要执行的函数投递到目标线程。
   - 在目标线程的 `EventLoop` 中执行。

【共享数据保护】
3. 简单计数器使用 `std::atomic`：
   - `OverloadProtectService::currentConnCount_`。
   - `MessageService::m_nextMsgId`。
   - 使用原子操作，无锁。
4. 复杂数据结构使用 `std::mutex`：
   - `MessageService::m_userToConn`。
   - `AuthService` 的用户映射。
   - 调用 `std::lock_guard<std::mutex> lock(m_mutex)` 加锁。

【对象生命周期】
5. 使用 `std::shared_ptr`：
   - `TcpConnection` 继承 `enable_shared_from_this`。
   - 在回调中调用 `shared_from_this()` 获取自身智能指针。
   - 引用计数保证对象存活。

---

## 十二、优雅退出流程

### Q12: 服务器的优雅退出流程是什么？

**A12:**

1. 收到信号：
   - 调用 `signal(SIGINT, SignalHandler)` 注册 SIGINT 信号处理。
   - 调用 `signal(SIGTERM, SignalHandler)` 注册 SIGTERM 信号处理。
2. `SignalHandler` 被调用：
   - 调用 `g_loop->quit()` 退出事件循环。
3. `loop.loop()` 返回：
   - 退出事件循环。
4. 停止服务：
   - 调用 `heartbeatService->stop()` 停止心跳服务。
   - 调用 `threadPool.stop()` 停止线程池。
5. Redis 断开（如果启用）：
   - 调用 `redisClient->disconnect()` 断开 Redis 连接。
6. 进程退出：
   - 返回 0，进程退出。

---

## 关键文件索引

| 功能模块 | 文件位置 |
|---------|---------|
| 连接管理 | `src/net/tcpConnection.h/cpp` |
| 事件循环 | `src/net/eventLoop.h/cpp` |
| 编解码器 | `src/codec/codec.h/cpp` |
| 消息服务 | `src/service/messageService.h/cpp` |
| 认证服务 | `src/service/authService.h/cpp` |
| 心跳服务 | `src/service/heartbeatService.h/cpp` |
| 过载保护 | `src/service/overloadProtectService.h/cpp` |
| 服务器入口 | `src/server_full.cpp` |

---

## 总结

整个服务器基于 **Reactor 模式** 和 **One Loop Per Thread** 模型，通过以下核心机制实现高并发：

1. **事件驱动**：调用 `epoll_wait` 监听 IO 事件。
2. **回调机制**：事件触发时调用对应回调函数。
3. **状态机**：`Codec` 使用状态机处理 TCP 粘包。
4. **线程池**：耗时业务交给线程池处理。
5. **原子操作**：简单计数器使用 `atomic`。
6. **互斥锁**：复杂数据结构使用 `mutex`。

所有业务流程都围绕 **TcpConnection**、**Codec**、**Service** 三个核心模块展开，代码结构清晰，职责分明。
