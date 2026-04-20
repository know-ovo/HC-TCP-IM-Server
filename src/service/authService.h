/**
 * @file authService.h
 * @brief 认证服务类
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了用户认证和会话管理服务。
 * AuthService 负责处理用户登录、登出、会话验证等功能。
 * 
 * 主要功能：
 * - 用户登录认证
 * - 会话管理（本地内存 + Redis 分布式存储）
 * - Token 验证
 * - 会话 ID 生成
 * 
 * Redis 集成：
 * - 会话数据存储在 Redis 中，支持分布式部署
 * - Key 格式：session:{sessionId}
 * - 过期时间可配置
 * 
 * 线程安全：
 * - 所有公共方法都是线程安全的
 * - 内部使用互斥锁保护共享数据
 * 
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include "net/tcpConnection.h"
#include "storage/sessionRepository.h"

/**
 * @brief 用户上下文结构体
 * 
 * 存储已认证用户的会话信息。
 */
class UserContext
{
public:
    std::string m_userId;       ///< 用户ID
    std::string m_sessionId;    ///< 会话ID
    std::string m_deviceId;     ///< 设备ID
    std::string m_nodeId;       ///< 节点ID
    int64_t m_loginTime;        ///< 登录时间戳（毫秒）
    bool m_authenticated;       ///< 认证状态

    /**
     * @brief 默认构造函数
     * 
     * 初始化成员变量为默认值。
     */
    UserContext() : m_loginTime(0), m_authenticated(false) {}
};

class RedisClient;

/**
 * @brief 认证服务类
 * 
 * AuthService 提供用户认证和会话管理功能。
 * 
 * 特性：
 * - 支持本地内存和 Redis 两种存储方式
 * - 自动生成唯一的会话 ID
 * - 支持多设备登录
 * - 线程安全
 * 
 * 使用场景：
 * - 用户登录处理
 * - 会话验证
 * - 用户状态查询
 * 
 * @note 在分布式环境下，必须设置 RedisClient 以保证会话一致性
 * 
 * @example
 * @code
 * AuthService authService;
 * authService.setRedisClient(redisClient);
 * authService.setSessionExpire(3600); // 1小时过期
 * 
 * authService.login(conn, "user001", "token123", "device001", 
 *     [](bool success, const std::string& sessionId, const std::string& errorMsg) {
 *         if (success) {
 *             // 登录成功
 *         }
 *     });
 * @endcode
 */
class AuthService
{
public:
    /**
     * @brief 认证回调函数类型
     * @param success 是否认证成功
     * @param sessionId 会话ID（成功时有效）
     * @param errorMsg 错误信息（失败时有效）
     */
    using AuthCallback = std::function<void(bool success, const std::string& sessionId, const std::string& errorMsg)>;

    /**
     * @brief 构造函数
     */
    AuthService();
    
    /**
     * @brief 析构函数
     */
    ~AuthService();

    /**
     * @brief 设置 Redis 客户端
     * @param redisClient Redis 客户端智能指针
     * 
     * 设置后，会话数据将同时存储到 Redis。
     * 这对于分布式部署是必需的。
     */
    void setRedisClient(std::shared_ptr<RedisClient> redisClient);
    void setSessionRepository(std::shared_ptr<storage::SessionRepository> sessionRepository);
    void setNodeId(const std::string& nodeId);
    bool bindSessionToNode(const std::string& sessionId, const std::string& nodeId);
    std::optional<std::string> getSessionNode(const std::string& sessionId);
    
    /**
     * @brief 设置会话过期时间
     * @param expireSeconds 过期时间（秒）
     */
    void setSessionExpire(int expireSeconds) { m_sessionExpire = expireSeconds; }

    /**
     * @brief 处理用户登录
     * @param conn TCP 连接
     * @param userId 用户ID
     * @param token 登录令牌
     * @param deviceId 设备ID
     * @param callback 认证结果回调
     * 
     * 登录流程：
     * 1. 验证 token
     * 2. 生成会话 ID
     * 3. 创建用户上下文
     * 4. 存储会话到 Redis（如果启用）
     * 5. 调用回调通知结果
     */
    void login(const std::shared_ptr<TcpConnection>& conn,
               const std::string& userId,
               const std::string& token,
               const std::string& deviceId,
               AuthCallback callback);

    /**
     * @brief 处理用户登出
     * @param conn TCP 连接
     * 
     * 清除会话信息，从 Redis 删除会话数据。
     */
    void logout(const std::shared_ptr<TcpConnection>& conn);

    /**
     * @brief 检查连接是否已认证
     * @param conn TCP 连接
     * @return true 已认证
     * @return false 未认证
     */
    bool isAuthenticated(const std::shared_ptr<TcpConnection>& conn);

    /**
     * @brief 获取连接的用户上下文
     * @param conn TCP 连接
     * @return 用户上下文智能指针，如果未认证则返回 nullptr
     */
    std::shared_ptr<UserContext> getUserContext(const std::shared_ptr<TcpConnection>& conn);

    /**
     * @brief 根据用户ID获取用户上下文
     * @param userId 用户ID
     * @return 用户上下文智能指针，如果不存在则返回 nullptr
     */
    std::shared_ptr<UserContext> getUserById(const std::string& userId);

    /**
     * @brief 验证会话ID
     * @param sessionId 会话ID
     * @param outUserId 输出用户ID
     * @return true 会话有效
     * @return false 会话无效或已过期
     * 
     * 会从 Redis 中查询会话数据并验证。
     */
    bool validateSession(const std::string& sessionId, std::string& outUserId);

private:
    /**
     * @brief 验证用户令牌
     * @param userId 用户ID
     * @param token 令牌
     * @return true 验证通过
     * @return false 验证失败
     * 
     * @note 当前为简化实现，实际项目应接入真实的认证系统
     */
    bool validateToken(const std::string& userId, const std::string& token);
    
    /**
     * @brief 生成唯一的会话ID
     * @return 会话ID字符串
     * 
     * 格式：UUID 风格的唯一标识符
     */
    std::string generateSessionId();
    
    /**
     * @brief 序列化会话数据
     * @param context 用户上下文
     * @return JSON 格式的字符串
     */
    std::string serializeSession(const std::shared_ptr<UserContext>& context);
    
    /**
     * @brief 反序列化会话数据
     * @param data JSON 格式的字符串
     * @param context 输出的用户上下文
     * @return true 反序列化成功
     * @return false 反序列化失败
     */
    bool deserializeSession(const std::string& data, std::shared_ptr<UserContext>& context);

    mutable std::mutex m_mutex;     ///< 互斥锁，保护共享数据
    std::unordered_map<std::string, std::shared_ptr<UserContext>> m_connToUser;   ///< 连接ID到用户上下文的映射
    std::unordered_map<std::string, std::shared_ptr<UserContext>> m_userToContext;///< 用户ID到用户上下文的映射
    std::shared_ptr<RedisClient> m_redisClient;   ///< Redis 客户端
    std::shared_ptr<storage::SessionRepository> m_sessionRepository; ///< MySQL 会话仓储
    std::string m_nodeId { "single-node" };
    int m_sessionExpire;           ///< 会话过期时间（秒）
    bool m_useRedis;               ///< 是否使用 Redis
};
