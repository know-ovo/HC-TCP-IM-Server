/**
 * @file tcpConnection.h
 * @brief TCP 连接管理
 * 
 * TcpConnection 管理一个 TCP 连接的生命周期，包括数据收发、连接状态管理、
 * 回调处理等。使用 shared_ptr 管理，支持跨线程安全访问。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <memory>
#include <string>
#include <functional>

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

#include "codec/buffer.h"
#include "net/timestamp.h"
#include "net/nonCopyable.h"
#include "net/inetAddress.h"

class EventLoop;
class Channel;

/**
 * @brief TCP 连接类
 * 
 * TcpConnection 封装了一个 TCP 连接的所有操作。
 * 
 * 特性：
 * - 自动管理输入输出缓冲区
 * - 支持半关闭（shutdown）
 * - 支持强制关闭（forceClose）
 * - 线程安全的数据发送
 * - 丰富的回调机制
 * 
 * 连接状态：
 * - kConnecting：正在连接
 * - kConnected：已连接
 * - kDisconnecting：正在断开
 * - kDisconnected：已断开
 * 
 * 使用示例：
 * @code
 * void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
 *     std::string msg = buf->retrieveAllAsString();
 *     conn->send(msg);
 * }
 * @endcode
 */
class TcpConnection : public std::enable_shared_from_this<TcpConnection>, private NonCopyable
{
public:
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*, Timestamp)>;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    using HighWaterMarkCallback = std::function<void(const std::shared_ptr<TcpConnection>&, size_t)>;

    /**
     * @brief 构造函数
     * @param loop 所属的事件循环
     * @param name 连接名称
     * @param sockfd 套接字文件描述符
     * @param localAddr 本地地址
     * @param peerAddr 对端地址
     */
    TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                  const InetAddress& localAddr, const InetAddress& peerAddr);

    ~TcpConnection();

    /**
     * @name 基本信息
     * @{
     */

    /**
     * @brief 获取所属的事件循环
     * @return EventLoop* 事件循环指针
     */
    EventLoop* getLoop() const { return m_loop; }

    /**
     * @brief 获取连接名称
     * @return const std::string& 连接名称
     */
    const std::string& name() const { return m_name; }

    /**
     * @brief 获取本地地址
     * @return const InetAddress& 本地地址
     */
    const InetAddress& localAddress() const { return m_localAddr; }

    /**
     * @brief 获取对端地址
     * @return const InetAddress& 对端地址
     */
    const InetAddress& peerAddress() const { return m_peerAddr; }

    /**
     * @brief 检查是否已连接
     * @return true 已连接
     * @return false 未连接
     */
    bool connected() const { return m_state == State::kConnected; }

    /**
     * @brief 获取套接字文件描述符
     * @return int 文件描述符
     */
    int fd() const { return m_sockfd; }

    /** @} */

    /**
     * @name 数据发送
     * @{
     */

    /**
     * @brief 发送数据
     * @param data 数据指针
     * @param len 数据长度
     */
    void send(const void* data, size_t len);

    /**
     * @brief 发送字符串
     * @param data 字符串数据
     */
    void send(const std::string& data);

    /**
     * @brief 发送缓冲区数据
     * @param buffer 缓冲区
     */
    void send(Buffer* buffer);

    /** @} */

    /**
     * @name 连接关闭
     * @{
     */

    /**
     * @brief 半关闭连接（不再发送数据）
     */
    void shutdown();

    /**
     * @brief 强制关闭连接
     */
    void forceClose();

    /**
     * @brief 延迟强制关闭连接
     * @param seconds 延迟秒数
     */
    void forceCloseWithDelay(double seconds);

    /** @} */

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

    /**
     * @brief 设置连接关闭回调
     * @param cb 回调函数
     */
    void setCloseCallback(CloseCallback cb) { m_closeCallback = std::move(cb); }

    /**
     * @brief 设置数据发送完成回调
     * @param cb 回调函数
     */
    void setWriteCompleteCallback(WriteCompleteCallback cb) { m_writeCompleteCallback = std::move(cb); }

    /**
     * @brief 设置高水位回调
     */
    void setHighWaterMarkCallback(HighWaterMarkCallback cb) { m_highWaterMarkCallback = std::move(cb); }

    /**
     * @brief 设置高水位阈值
     */
    void setHighWaterMark(size_t bytes) { m_highWaterMark = bytes; }

    /**
     * @brief 设置低水位阈值
     */
    void setLowWaterMark(size_t bytes) { m_lowWaterMark = bytes; }

    /**
     * @brief 设置最大输出缓冲阈值
     */
    void setMaxOutputBufferBytes(size_t bytes) { m_maxOutputBufferBytes = bytes; }

    /**
     * @brief 当前输出缓冲大小
     */
    size_t outputBufferBytes() const { return m_outputBuffer.readableBytes(); }

    /**
     * @brief 当前是否处于背压状态
     */
    bool isBackPressured() const { return m_backPressured; }

    /** @} */

    /**
     * @brief 连接建立（内部使用）
     */
    void connectEstablished();

    /**
     * @brief 连接销毁（内部使用）
     */
    void connectDestroyed();

private:
    enum class State
    {
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
    HighWaterMarkCallback m_highWaterMarkCallback;
    Buffer m_inputBuffer;
    Buffer m_outputBuffer;
    size_t m_highWaterMark;
    size_t m_lowWaterMark;
    size_t m_maxOutputBufferBytes;
    bool m_backPressured;
};
