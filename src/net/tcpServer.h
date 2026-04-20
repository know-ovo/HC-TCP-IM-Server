/**
 * @file tcpServer.h
 * @brief TCP 服务器
 * 
 * TcpServer 是服务器端的核心类，负责监听端口、接受新连接、管理连接生命周期。
 * 基于 Reactor 模式实现，支持单线程或多线程模式。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <atomic>
#include "net/eventLoop.h"
#include "net/channel.h"
#include "net/tcpConnection.h"
#include "net/inetAddress.h"
#include "codec/buffer.h"
#include "net/timestamp.h"

/**
 * @brief TCP 服务器类
 * 
 * TcpServer 提供完整的 TCP 服务器功能。
 * 
 * 特性：
 * - 监听指定端口
 * - 自动接受新连接
 * - 连接生命周期管理
 * - 回调机制处理业务逻辑
 * 
 * 使用示例：
 * @code
 * EventLoop loop;
 * InetAddress listenAddr(8888);
 * TcpServer server(&loop, listenAddr, "MyServer");
 * 
 * server.setConnectionCallback([](const TcpConnectionPtr& conn) {
 *     if (conn->connected()) {
 *         std::cout << "New connection" << std::endl;
 *     }
 * });
 * 
 * server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
 *     conn->send(buf);
 * });
 * 
 * server.start();
 * loop.loop();
 * @endcode
 */
class TcpServer : NonCopyable
{
public:
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*, Timestamp)>;

    /**
     * @brief 构造函数
     * @param loop 主事件循环
     * @param listenAddr 监听地址
     * @param name 服务器名称
     */
    TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name);

    /**
     * @brief 析构函数
     */
    ~TcpServer();

    /**
     * @brief 设置工作线程数量
     * @param numThreads 线程数量（0 表示单线程模式）
     */
    void setThreadNum(int numThreads);

    /**
     * @brief 启动服务器
     * @note 只能调用一次
     */
    void start();

    /**
     * @name 回调设置
     * @{
     */

    /**
     * @brief 设置连接状态变化回调
     * @param cb 回调函数
     */
    void setConnectionCallback(ConnectionCallback cb) { m_connectionCallback = std::move(cb); }

    /**
     * @brief 设置消息到达回调
     * @param cb 回调函数
     */
    void setMessageCallback(MessageCallback cb) { m_messageCallback = std::move(cb); }

    /** @} */

private:
    /**
     * @brief 处理新连接
     */
    void newConnection();

    /**
     * @brief 移除连接
     * @param conn 要移除的连接
     */
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
