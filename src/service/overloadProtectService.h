/**
 * @file overloadProtectService.h
 * @brief 过载保护服务类
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了服务器过载保护服务。
 * OverloadProtectService 通过限制连接数和 QPS 来防止服务器过载。
 * 
 * 主要功能：
 * - 连接数限制：限制最大并发连接数
 * - QPS 限制：限制每秒处理的消息数
 * - 实时统计：提供当前连接数和 QPS 查询
 * 
 * 保护策略：
 * - 当连接数达到上限时，拒绝新连接
 * - 当 QPS 达到上限时，丢弃部分消息
 * 
 * 设计特点：
 * - 无锁设计：使用原子操作，性能高效
 * - 可配置：支持动态调整限制阈值
 * - 低开销：统计操作 O(1) 时间复杂度
 * 
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "infra/rateLimiter.h"

/**
 * @brief 过载保护服务类
 * 
 * OverloadProtectService 提供服务器过载保护功能。
 * 
 * 特性：
 * - 连接数限制：防止过多连接耗尽服务器资源
 * - QPS 限制：防止消息洪泛导致处理延迟
 * - 无锁实现：使用原子操作，线程安全
 * - 动态配置：支持运行时调整限制参数
 * 
 * 使用场景：
 * - 新连接接入前的检查
 * - 消息处理前的检查
 * - 服务器负载监控
 * 
 * @example
 * @code
 * OverloadProtectService protector(10000, 100000); // 最大1万连接，10万QPS
 * 
 * // 新连接接入时
 * if (protector.canAcceptConnection()) {
 *     protector.onConnectionAccepted();
 *     // 处理连接
 * } else {
 *     // 拒绝连接
 * }
 * 
 * // 消息处理时
 * if (protector.canProcessMessage()) {
 *     protector.onMessageProcessed();
 *     // 处理消息
 * } else {
 *     // 丢弃消息
 * }
 * @endcode
 */
class OverloadProtectService
{
public:
    /**
     * @brief 构造函数
     * @param maxConnections 最大连接数（默认 10000）
     * @param maxQps 最大 QPS（默认 100000）
     */
    OverloadProtectService(size_t maxConnections = 10000, size_t maxQps = 100000);
    
    /**
     * @brief 析构函数
     */
    ~OverloadProtectService() = default;

    /**
     * @brief 检查是否可以接受新连接
     * @return true 可以接受新连接
     * @return false 已达到连接数上限
     */
    bool canAcceptConnection();
    bool canAcceptConnection(const std::string& ip);
    
    /**
     * @brief 连接已接受通知
     * 
     * 连接成功建立后调用，增加当前连接计数。
     */
    void onConnectionAccepted();
    
    /**
     * @brief 连接已关闭通知
     * 
     * 连接关闭后调用，减少当前连接计数。
     */
    void onConnectionClosed();

    /**
     * @brief 检查是否可以处理消息
     * @return true 可以处理消息
     * @return false 已达到 QPS 上限
     */
    bool canProcessMessage();
    bool canProcessCommand(uint16_t command);
    bool canSendMessage(const std::string& userId);
    
    /**
     * @brief 消息已处理通知
     * 
     * 消息处理完成后调用，增加消息计数。
     */
    void onMessageProcessed();

    /**
     * @brief 设置最大连接数
     * @param max 最大连接数
     */
    void setMaxConnections(size_t max) { m_maxConnections = max; }
    
    /**
     * @brief 设置最大 QPS
     * @param max 最大 QPS
     */
    void setMaxQps(size_t max) { m_maxQps = max; }
    void setRateLimitEnabled(bool enabled) { m_rateLimitEnabled = enabled; }
    void configureIpConnectRate(double perSecond);
    void configureCommandRates(double loginQps, double p2pQps, double broadcastQps);
    void configureUserSendRate(double perSecond);

    /**
     * @brief 获取当前连接数
     * @return 当前连接数
     */
    size_t getCurrentConnections() const { return m_currentConnections.load(); }
    
    /**
     * @brief 获取当前 QPS
     * @return 当前 QPS（每秒消息数）
     * 
     * 计算方式：统计最近一秒内处理的消息数。
     */
    size_t getCurrentQps() const;

private:
    std::shared_ptr<infra::TokenBucketRateLimiter> getOrCreateLimiter(
        std::unordered_map<std::string, std::shared_ptr<infra::TokenBucketRateLimiter>>& pool,
        const std::string& key,
        double rate,
        double burst);

    std::atomic<size_t> m_maxConnections;     ///< 最大连接数
    std::atomic<size_t> m_maxQps;             ///< 最大 QPS
    std::atomic<size_t> m_currentConnections; ///< 当前连接数
    
    std::atomic<size_t> m_messageCount;       ///< 消息计数（用于 QPS 计算）
    std::atomic<int64_t> m_lastResetTime;     ///< 上次重置时间

    std::atomic<bool> m_rateLimitEnabled { false };
    std::mutex m_mutex;
    double m_ipConnectRatePerSecond { 0.0 };
    double m_userSendRatePerSecond { 0.0 };
    std::shared_ptr<infra::TokenBucketRateLimiter> m_loginLimiter;
    std::shared_ptr<infra::TokenBucketRateLimiter> m_p2pLimiter;
    std::shared_ptr<infra::TokenBucketRateLimiter> m_broadcastLimiter;
    std::unordered_map<std::string, std::shared_ptr<infra::TokenBucketRateLimiter>> m_ipLimiters;
    std::unordered_map<std::string, std::shared_ptr<infra::TokenBucketRateLimiter>> m_userLimiters;
};
