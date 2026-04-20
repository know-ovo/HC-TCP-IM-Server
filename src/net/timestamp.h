/**
 * @file timestamp.h
 * @brief 时间戳类
 * 
 * 提供微秒精度的时间戳表示和操作，用于事件时间记录和定时器实现。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <string>
#include <cstdint>
#include <cinttypes>

/**
 * @brief 时间戳类
 * 
 * Timestamp 封装了自纪元以来的微秒数，提供时间比较和格式化功能。
 * 
 * 特性：
 * - 微秒精度
 * - 支持比较操作
 * - 支持格式化输出
 * 
 * 使用示例：
 * @code
 * Timestamp now = Timestamp::now();
 * std::string str = now.toFormattedString();
 * int64_t seconds = now.secondsSinceEpoch();
 * @endcode
 */
class Timestamp
{
public:
    /**
     * @brief 默认构造函数，创建无效时间戳
     */
    Timestamp() : m_microSecondsSinceEpoch(0) {}

    /**
     * @brief 构造函数
     * @param microSeconds 自纪元以来的微秒数
     */
    explicit Timestamp(int64_t microSeconds)
        : m_microSecondsSinceEpoch(microSeconds) {}

    /**
     * @brief 获取当前时间戳
     * @return Timestamp 当前时间戳
     */
    static Timestamp now();

    /**
     * @brief 获取无效时间戳
     * @return Timestamp 无效时间戳
     */
    static Timestamp invalid() { return Timestamp(); }

    /**
     * @brief 转换为字符串
     * @return std::string 时间戳字符串
     */
    std::string toString() const;

    /**
     * @brief 转换为格式化字符串
     * @param showMicroseconds 是否显示微秒
     * @return std::string 格式化的时间字符串
     */
    std::string toFormattedString(bool showMicroseconds = true) const;

    /**
     * @brief 检查时间戳是否有效
     * @return true 有效
     * @return false 无效
     */
    bool valid() const { return m_microSecondsSinceEpoch > 0; }

    /**
     * @brief 获取自纪元以来的微秒数
     * @return int64_t 微秒数
     */
    int64_t microSecondsSinceEpoch() const { return m_microSecondsSinceEpoch; }

    /**
     * @brief 获取自纪元以来的秒数
     * @return int64_t 秒数
     */
    int64_t secondsSinceEpoch() const
    {
        return static_cast<int64_t>(m_microSecondsSinceEpoch / kMicroSecondsPerSecond);
    }

    static const int64_t kMicroSecondsPerSecond = 1000 * 1000;

private:
    int64_t m_microSecondsSinceEpoch;
};

/**
 * @brief 小于比较运算符
 * @param lhs 左操作数
 * @param rhs 右操作数
 * @return true lhs < rhs
 * @return false lhs >= rhs
 */
inline bool operator<(Timestamp lhs, Timestamp rhs)
{
    return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

/**
 * @brief 相等比较运算符
 * @param lhs 左操作数
 * @param rhs 右操作数
 * @return true lhs == rhs
 * @return false lhs != rhs
 */
inline bool operator==(Timestamp lhs, Timestamp rhs)
{
    return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}
