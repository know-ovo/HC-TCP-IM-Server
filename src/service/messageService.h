/**
 * @file messageService.h
 * @brief 消息服务类
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了消息转发服务。
 * MessageService 负责处理点对点消息和广播消息的转发。
 * 
 * 主要功能：
 * - 点对点消息发送
 * - 广播消息发送
 * - 用户连接管理
 * - 消息ID生成
 * 
 * 设计特点：
 * - 线程安全：所有公共方法都是线程安全的
 * - 自动消息ID：使用原子计数器生成唯一消息ID
 * - 连接映射：维护用户ID到连接的映射
 * 
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "codec/protocol.h"
#include "net/tcpConnection.h"
#include "service/authService.h"
#include "storage/messageStore.h"
#include "storage/sessionRepository.h"

/**
 * @brief 消息服务类
 * 
 * MessageService 提供消息转发功能。
 * 
 * 特性：
 * - 点对点消息：支持向指定用户发送消息
 * - 广播消息：支持向所有在线用户发送消息
 * - 连接管理：维护用户ID与连接的映射关系
 * - 线程安全：内部使用互斥锁保护共享数据
 * 
 * 使用场景：
 * - 处理 P2P 消息请求
 * - 处理广播消息请求
 * - 管理用户连接状态
 * 
 * @note 需要依赖 AuthService 进行用户认证
 * 
 * @example
 * @code
 * MessageService msgService;
 * msgService.setAuthService(authService);
 * 
 * // 用户上线时注册连接
 * msgService.registerConnection("user001", conn);
 * 
 * // 发送点对点消息
 * uint32_t msgId;
 * std::string error;
 * msgService.sendP2PMessage("user001", "user002", "Hello", msgId, error);
 * 
 * // 用户下线时取消注册
 * msgService.unregisterConnection("user001");
 * @endcode
 */
class MessageService
{
public:
    /**
     * @brief 构造函数
     * 
     * 初始化消息ID计数器。
     */
    MessageService() = default;
    
    /**
     * @brief 析构函数
     */
    ~MessageService();

    /**
     * @brief 设置认证服务
     * @param authService 认证服务智能指针
     */
    void setAuthService(std::shared_ptr<AuthService> authService)
    {
        m_authService = authService;
    }

    void setMessageStore(std::shared_ptr<storage::IMessageStore> messageStore)
    {
        m_messageStore = std::move(messageStore);
    }

    void setSessionRepository(std::shared_ptr<storage::SessionRepository> sessionRepository)
    {
        m_sessionRepository = std::move(sessionRepository);
    }

    void setRetryConfig(int retryIntervalMs, size_t retryBatchSize)
    {
        m_retryIntervalMs = retryIntervalMs;
        m_retryBatchSize = retryBatchSize;
    }

    void setRetryPolicy(int ackTimeoutMs, int retryBackoffMs, uint32_t maxRetryCount)
    {
        m_ackTimeoutMs = ackTimeoutMs;
        m_retryBackoffMs = retryBackoffMs;
        m_maxRetryCount = maxRetryCount;
    }

    void start();
    void stop();

    /**
     * @brief 注册用户连接
     * @param userId 用户ID
     * @param conn TCP 连接
     * 
     * 用户登录成功后调用，建立用户ID与连接的映射。
     * 用于后续消息转发时查找目标连接。
     */
    void registerConnection(const std::string& userId, const std::shared_ptr<TcpConnection>& conn);
    
    /**
     * @brief 取消用户连接注册
     * @param userId 用户ID
     * 
     * 用户下线时调用，移除用户ID与连接的映射。
     */
    void unregisterConnection(const std::string& userId);

    /**
     * @brief 发送点对点消息
     * @param fromUserId 发送者用户ID
     * @param toUserId 接收者用户ID
     * @param content 消息内容
     * @param outMsgId 输出消息ID
     * @param errorMsg 输出错误信息
     * @return true 发送成功
     * @return false 发送失败（如目标用户不在线）
     * 
     * 发送流程：
     * 1. 生成消息ID
     * 2. 查找目标用户连接
     * 3. 构造消息包并发送
     */
    bool sendP2PMessage(const std::string& fromUserId,
                        const std::string& toUserId,
                        const std::string& content,
                        uint32_t& outMsgId,
                        std::string& errorMsg);

    /**
     * @brief 可靠发送点对点消息
     */
    bool sendReliableP2PMessage(const std::string& fromUserId,
                                const std::string& toUserId,
                                const std::string& clientMsgId,
                                const std::string& content,
                                uint64_t& outMsgId,
                                uint64_t& outServerSeq,
                                std::string& resultMsg);

    /**
     * @brief 处理客户端 ACK
     */
    bool processAck(const std::string& userId,
                    uint64_t msgId,
                    uint64_t serverSeq,
                    uint32_t ackCode,
                    uint64_t& outLastAckedSeq,
                    std::string& errorMsg);

    /**
     * @brief 拉取离线消息
     */
    std::vector<protocol::PullOfflineResp::OfflineItem> pullOfflineMessages(const std::string& userId,
                                                                            uint64_t lastAckedSeq,
                                                                            size_t limit,
                                                                            uint64_t& nextBeginSeq);

    /**
     * @brief 登录成功后补发未确认消息
     */
    void deliverPendingMessages(const std::string& userId);

    /**
     * @brief 广播消息
     * @param fromUserId 发送者用户ID
     * @param content 消息内容
     * 
     * 向所有在线用户发送消息。
     * 广播消息不返回响应，只通知接收方。
     */
    void broadcastMessage(const std::string& fromUserId, const std::string& content);

private:
    void deliverLocked(const std::shared_ptr<TcpConnection>& conn, storage::StoredMessage& message, std::string& errorMsg);

    std::shared_ptr<AuthService> m_authService;   ///< 认证服务
    std::shared_ptr<storage::IMessageStore> m_messageStore; ///< 可靠消息存储
    std::shared_ptr<storage::SessionRepository> m_sessionRepository; ///< 会话仓储

    mutable std::mutex m_mutex;   ///< 互斥锁，保护用户连接映射
    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> m_userToConn; ///< 用户ID到连接的映射
    std::atomic<bool> m_retryRunning { false };
    std::thread m_retryThread;
    int m_retryIntervalMs { 5000 };
    int m_ackTimeoutMs { 15000 };
    int m_retryBackoffMs { 3000 };
    uint32_t m_maxRetryCount { 5 };
    size_t m_retryBatchSize { 200 };
};
