/**
 * @file redisClient.h
 * @brief Redis 客户端封装
 * 
 * 基于 hiredis 库封装的 Redis 客户端，提供简洁的 Redis 操作接口。
 * 支持字符串、哈希、列表、集合等常用数据结构的操作。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <hiredis/hiredis.h>

/**
 * @brief Redis 客户端类
 * 
 * 封装 hiredis 库，提供同步方式的 Redis 操作接口。
 * 
 * 特性：
 * - 支持连接超时设置
 * - 支持字符串操作（SET, GET, SETEX 等）
 * - 支持哈希操作（HSET, HGET, HGETALL 等）
 * - 支持列表操作（LPUSH, RPUSH, LRANGE 等）
 * - 支持集合操作（SADD, SREM, SMEMBERS 等）
 * - 支持原生命令执行
 * 
 * 使用示例：
 * @code
 * RedisClient client;
 * if (client.connect("127.0.0.1", 6379)) {
 *     client.set("key", "value");
 *     std::string value = client.get("key");
 * }
 * @endcode
 */
class RedisClient
{
public:
    using ReplyCallback = std::function<void(redisReply*)>;

    RedisClient();
    ~RedisClient();

    /**
     * @brief 连接 Redis 服务器
     * @param host Redis 服务器地址
     * @param port Redis 服务器端口
     * @param timeoutMs 连接超时时间（毫秒，默认 2000）
     * @return true 连接成功
     * @return false 连接失败
     */
    bool connect(const std::string& host, int port, int timeoutMs = 2000);

    /**
     * @brief 断开连接
     */
    void disconnect();

    /**
     * @brief 检查是否已连接
     * @return true 已连接
     * @return false 未连接
     */
    bool isConnected() const;

    /**
     * @name 字符串操作
     * @{
     */

    /**
     * @brief 设置键值对
     * @param key 键
     * @param value 值
     * @return true 操作成功
     * @return false 操作失败
     */
    bool set(const std::string& key, const std::string& value);

    /**
     * @brief 设置键值对（带过期时间）
     * @param key 键
     * @param value 值
     * @param expireSeconds 过期时间（秒）
     * @return true 操作成功
     * @return false 操作失败
     */
    bool setex(const std::string& key, const std::string& value, int expireSeconds);

    /**
     * @brief 获取键对应的值
     * @param key 键
     * @return std::string 值（不存在返回空字符串）
     */
    std::string get(const std::string& key);

    /**
     * @brief 删除键
     * @param key 键
     * @return true 操作成功
     * @return false 操作失败
     */
    bool del(const std::string& key);

    /**
     * @brief 检查键是否存在
     * @param key 键
     * @return true 存在
     * @return false 不存在
     */
    bool exists(const std::string& key);

    /**
     * @brief 设置键的过期时间
     * @param key 键
     * @param seconds 过期时间（秒）
     * @return true 操作成功
     * @return false 操作失败
     */
    bool expire(const std::string& key, int seconds);

    /**
     * @brief 获取键的剩余过期时间
     * @param key 键
     * @return int 剩余秒数（-1 表示永不过期，-2 表示不存在）
     */
    int ttl(const std::string& key);

    /** @} */

    /**
     * @name 计数器操作
     * @{
     */

    /**
     * @brief 键值自增 1
     * @param key 键
     * @return long long 自增后的值
     */
    long long incr(const std::string& key);

    /**
     * @brief 键值自减 1
     * @param key 键
     * @return long long 自减后的值
     */
    long long decr(const std::string& key);

    /**
     * @brief 键值自增指定值
     * @param key 键
     * @param increment 增量
     * @return long long 自增后的值
     */
    long long incrBy(const std::string& key, long long increment);

    /** @} */

    /**
     * @name 哈希操作
     * @{
     */

    /**
     * @brief 设置哈希字段值
     * @param key 键
     * @param field 字段
     * @param value 值
     * @return true 操作成功
     * @return false 操作失败
     */
    bool hset(const std::string& key, const std::string& field, const std::string& value);

    /**
     * @brief 获取哈希字段值
     * @param key 键
     * @param field 字段
     * @return std::string 值（不存在返回空字符串）
     */
    std::string hget(const std::string& key, const std::string& field);

    /**
     * @brief 删除哈希字段
     * @param key 键
     * @param field 字段
     * @return true 操作成功
     * @return false 操作失败
     */
    bool hdel(const std::string& key, const std::string& field);

    /**
     * @brief 获取哈希所有字段和值
     * @param key 键
     * @return std::unordered_map<std::string, std::string> 字段-值映射
     */
    std::unordered_map<std::string, std::string> hgetall(const std::string& key);

    /** @} */

    /**
     * @name 列表操作
     * @{
     */

    /**
     * @brief 从列表左侧插入元素
     * @param key 键
     * @param value 值
     * @return long long 列表长度
     */
    long long lpush(const std::string& key, const std::string& value);

    /**
     * @brief 从列表右侧插入元素
     * @param key 键
     * @param value 值
     * @return long long 列表长度
     */
    long long rpush(const std::string& key, const std::string& value);

    /**
     * @brief 从列表左侧弹出元素
     * @param key 键
     * @return std::string 弹出的元素
     */
    std::string lpop(const std::string& key);

    /**
     * @brief 从列表右侧弹出元素
     * @param key 键
     * @return std::string 弹出的元素
     */
    std::string rpop(const std::string& key);

    /**
     * @brief 获取列表指定范围的元素
     * @param key 键
     * @param start 起始索引
     * @param stop 结束索引（-1 表示最后一个元素）
     * @return std::vector<std::string> 元素列表
     */
    std::vector<std::string> lrange(const std::string& key, long long start, long long stop);

    /** @} */

    /**
     * @name 集合操作
     * @{
     */

    /**
     * @brief 添加集合成员
     * @param key 键
     * @param member 成员
     * @return true 添加成功
     * @return false 添加失败或成员已存在
     */
    bool sadd(const std::string& key, const std::string& member);

    /**
     * @brief 删除集合成员
     * @param key 键
     * @param member 成员
     * @return true 删除成功
     * @return false 删除失败或成员不存在
     */
    bool srem(const std::string& key, const std::string& member);

    /**
     * @brief 检查是否为集合成员
     * @param key 键
     * @param member 成员
     * @return true 是成员
     * @return false 不是成员
     */
    bool sismember(const std::string& key, const std::string& member);

    /**
     * @brief 获取集合所有成员
     * @param key 键
     * @return std::vector<std::string> 成员列表
     */
    std::vector<std::string> smembers(const std::string& key);

    /** @} */

    /**
     * @brief 执行原生 Redis 命令
     * @param format 格式化字符串
     * @param ... 可变参数
     * @return redisReply* Redis 回复对象（调用者负责释放）
     */
    redisReply* executeCommand(const char* format, ...);

    /**
     * @brief 获取最后一次错误信息
     * @return std::string 错误信息
     */
    std::string getLastError() const { return m_lastError; }

private:
    /**
     * @brief 释放 Redis 回复对象
     * @param reply Redis 回复对象
     */
    void freeReply(redisReply* reply);

    /**
     * @brief 检查 Redis 回复是否有效
     * @param reply Redis 回复对象
     * @return true 有效
     * @return false 无效
     */
    bool checkReply(redisReply* reply);

    redisContext* m_context;
    std::string m_lastError;
    mutable std::mutex m_mutex;
};

using RedisClientPtr = std::shared_ptr<RedisClient>;
