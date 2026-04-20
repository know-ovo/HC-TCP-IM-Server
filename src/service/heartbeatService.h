/**
 * @file heartbeatService.h
 * @brief 心跳服务类
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了心跳检测和在线状态管理服务。
 * HeartbeatService 负责监控客户端连接状态，检测超时连接。
 * 
 * 主要功能：
 * - 心跳超时检测
 * - 在线状态管理（本地 + Redis 分布式存储）
 * - 自动踢出超时连接
 * - 定时检查任务
 * 
 * Redis 集成：
 * - 在线状态存储在 Redis 中，支持分布式查询
 * - Key 格式：online:{userId}
 * - 使用 SETEX 设置过期时间
 * 
 * 线程安全：
 * - 所有公共方法都是线程安全的
 * - Redis 操作通过线程池异步执行
 * 
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include "net/eventLoop.h"
#include "net/tcpConnection.h"

class RedisClient;
class ThreadPool;

/**
 * @brief 心跳服务类
 * 
 * HeartbeatService 提供心跳检测和在线状态管理功能。
 * 
 * 特性：
 * - 可配置的超时时间和检查间隔
 * - 支持本地内存和 Redis 两种存储方式
 * - 异步更新 Redis 状态
 * - 自动清理超时连接
 * 
 * 使用场景：
 * - 监控客户端存活状态
 * - 查询用户在线状态
 * - 踢出长时间无响应的连接
 * 
 * @note 需要在 EventLoop 线程中创建和启动
 * 
 * @example
 * @code
 * HeartbeatService heartbeat(loop, 30, 10); // 30秒超时，10秒检查一次
 * heartbeat.setRedisClient(redisClient);
 * heartbeat.setKickCallback([](const auto& conn, const std::string& reason) {
 *     // 处理踢出逻辑
 * });
 * heartbeat.start();
 * @endcode
 */
class HeartbeatService
{
public:
    enum class ConnectionHealthState
    {
        New,
        AuthPending,
        Active,
        Idle,
        BackPressured,
        Closing
    };

    /**
     * @brief 踢出回调函数类型
     * @param conn 被踢出的连接
     * @param reason 踢出原因
     */
    using KickCallback = std::function<void(const std::shared_ptr<TcpConnection>& conn, const std::string& reason)>;

    /**
     * @brief 构造函数
     * @param loop 事件循环
     * @param timeoutSeconds 心跳超时时间（秒，默认 30）
     * @param checkIntervalSeconds 检查间隔（秒，默认 10）
     */
    HeartbeatService(EventLoop* loop, int timeoutSeconds = 30, int checkIntervalSeconds = 10);
    
    /**
     * @brief 析构函数
     */
    ~HeartbeatService();

    /**
     * @brief 设置 Redis 客户端
     * @param redisClient Redis 客户端智能指针
     * 
     * 设置后，在线状态将同步到 Redis。
     */
    void setRedisClient(std::shared_ptr<RedisClient> redisClient);
    
    /**
     * @brief 设置线程池
     * @param pool 线程池指针
     * 
     * 用于异步执行 Redis 操作，避免阻塞事件循环。
     */
    void setThreadPool(ThreadPool* pool);

    /**
     * @brief 启动心跳服务
     * 
     * 开始定时检查任务。
     */
    void start();
    
    /**
     * @brief 停止心跳服务
     */
    void stop();

    /**
     * @brief 处理心跳消息
     * @param conn TCP 连接
     * 
     * 更新连接的最后心跳时间。
     * 如果启用了 Redis，会异步刷新在线状态。
     */
    void onHeartbeat(const std::shared_ptr<TcpConnection>& conn);
    
    /**
     * @brief 处理连接状态变化
     * @param conn TCP 连接
     * @param connected true 表示连接建立，false 表示连接断开
     * 
     * 连接建立时记录连接信息，断开时清理。
     */
    void onConnection(const std::shared_ptr<TcpConnection>& conn, bool connected);

    /**
     * @brief 设置踢出回调
     * @param cb 踢出回调函数
     */
    void setKickCallback(KickCallback cb) { m_kickCallback = std::move(cb); }

    /**
     * @brief 注册用户ID与连接的映射
     * @param connId 连接ID
     * @param userId 用户ID
     * 
     * 用户登录成功后调用，建立连接与用户的关联。
     */
    void registerUserId(const std::string& connId, const std::string& userId);
    void onAuthenticated(const std::shared_ptr<TcpConnection>& conn, const std::string& userId);
    void onWriteBlocked(const std::shared_ptr<TcpConnection>& conn);
    void onBackPressureRecovered(const std::shared_ptr<TcpConnection>& conn);
    
    /**
     * @brief 取消用户ID与连接的映射
     * @param connId 连接ID
     */
    void unregisterUserId(const std::string& connId);
    void setNodeId(const std::string& nodeId);
    void setNodeAliveTtlSeconds(int ttlSeconds);
    void setConnectionGovernance(int authTimeoutSeconds, int idleTimeoutSeconds, int slowConsumerKickSeconds);
    void reportNodeAlive();
    
    /**
     * @brief 检查用户是否在线
     * @param userId 用户ID
     * @return true 用户在线
     * @return false 用户离线
     * 
     * 优先查询本地缓存，如果不存在则查询 Redis。
     */
    bool isUserOnline(const std::string& userId);

private:
    /**
     * @brief 检查心跳超时
     * 
     * 定时执行，检查所有连接的最后心跳时间，
     * 踢出超时的连接。
     */
    void checkHeartbeat();
    
    /**
     * @brief 更新用户在线状态
     * @param userId 用户ID
     * @param online true 表示在线，false 表示离线
     * 
     * 同步更新 Redis。
     */
    void updateOnlineStatus(const std::string& userId, bool online);
    
    /**
     * @brief 刷新用户在线状态
     * @param userId 用户ID
     * 
     * 重置 Redis 中的过期时间。
     */
    void refreshOnlineStatus(const std::string& userId);
    
    /**
     * @brief 异步更新用户在线状态
     * @param userId 用户ID
     * @param online true 表示在线，false 表示离线
     */
    void updateOnlineStatusAsync(const std::string& userId, bool online);
    
    /**
     * @brief 异步刷新用户在线状态
     * @param userId 用户ID
     */
    void refreshOnlineStatusAsync(const std::string& userId);

    struct ConnectionHealth
    {
        ConnectionHealthState state = ConnectionHealthState::New;
        int64_t createdAtMs = 0;
        int64_t lastHeartbeatAtMs = 0;
        int64_t lastBlockedAtMs = 0;
        bool authenticated = false;
    };

    EventLoop* m_loop;              ///< 事件循环
    int m_timeoutSeconds;           ///< 心跳超时时间（秒）
    int m_checkIntervalSeconds;     ///< 检查间隔（秒）

    mutable std::mutex m_mutex;     ///< 互斥锁
    std::unordered_map<std::string, int64_t> m_lastHeartbeatTime;  ///< 连接ID到最后心跳时间的映射
    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> m_connections; ///< 连接ID到连接的映射
    std::unordered_map<std::string, std::string> m_connToUser;     ///< 连接ID到用户ID的映射
    std::unordered_map<std::string, ConnectionHealth> m_connectionHealth; ///< 连接健康状态

    std::shared_ptr<RedisClient> m_redisClient;   ///< Redis 客户端
    ThreadPool* m_threadPool;       ///< 线程池
    bool m_useRedis;                ///< 是否使用 Redis
    std::string m_nodeId { "single-node" };
    int m_nodeAliveTtlSeconds { 15 };
    int m_authTimeoutSeconds { 10 };
    int m_idleTimeoutSeconds { 300 };
    int m_slowConsumerKickSeconds { 30 };

    std::atomic<bool> m_running;    ///< 服务运行状态
    KickCallback m_kickCallback;    ///< 踢出回调
};
