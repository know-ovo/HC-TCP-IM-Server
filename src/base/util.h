/**
 * @file util.h
 * @brief 通用工具函数集合
 * 
 * 提供网络字节序转换、时间戳操作、字符串处理等通用工具函数。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

/**
 * @brief 工具函数命名空间
 */
namespace util {

/**
 * @name 字节序转换函数
 * @{
 */

/**
 * @brief 16位主机字节序转网络字节序
 * @param host16 主机字节序值
 * @return uint16_t 网络字节序值
 */
uint16_t HostToNetwork16(uint16_t host16);

/**
 * @brief 16位网络字节序转主机字节序
 * @param net16 网络字节序值
 * @return uint16_t 主机字节序值
 */
uint16_t NetworkToHost16(uint16_t net16);

/**
 * @brief 32位主机字节序转网络字节序
 * @param host32 主机字节序值
 * @return uint32_t 网络字节序值
 */
uint32_t HostToNetwork32(uint32_t host32);

/**
 * @brief 32位网络字节序转主机字节序
 * @param net32 网络字节序值
 * @return uint32_t 主机字节序值
 */
uint32_t NetworkToHost32(uint32_t net32);

/**
 * @brief 64位主机字节序转网络字节序
 * @param host64 主机字节序值
 * @return uint64_t 网络字节序值
 */
uint64_t HostToNetwork64(uint64_t host64);

/**
 * @brief 64位网络字节序转主机字节序
 * @param net64 网络字节序值
 * @return uint64_t 主机字节序值
 */
uint64_t NetworkToHost64(uint64_t net64);

/** @} */

/**
 * @name 时间戳函数
 * @{
 */

/**
 * @brief 获取当前时间戳（毫秒）
 * @return int64_t 毫秒级时间戳
 */
int64_t GetTimestampMs();

/**
 * @brief 获取当前时间戳（微秒）
 * @return int64_t 微秒级时间戳
 */
int64_t GetTimestampUs();

/**
 * @brief 获取当前时间戳字符串
 * @return std::string 格式化的时间戳字符串
 */
std::string GetTimestampString();

/**
 * @brief 线程休眠
 * @param ms 休眠时间（毫秒）
 */
void SleepMs(int ms);

/** @} */

/**
 * @name 字符串处理函数
 * @{
 */

/**
 * @brief 分割字符串
 * @param str 要分割的字符串
 * @param delimiter 分隔符
 * @return std::vector<std::string> 分割后的字符串数组
 */
std::vector<std::string> SplitString(const std::string& str, char delimiter);

/**
 * @brief 去除字符串首尾空白
 * @param str 要处理的字符串
 * @return std::string 处理后的字符串
 */
std::string TrimString(const std::string& str);

/**
 * @brief 转换为小写
 * @param str 要转换的字符串
 * @return std::string 小写字符串
 */
std::string ToLower(const std::string& str);

/**
 * @brief 转换为大写
 * @param str 要转换的字符串
 * @return std::string 大写字符串
 */
std::string ToUpper(const std::string& str);

/**
 * @brief 检查字符串是否以指定前缀开头
 * @param str 要检查的字符串
 * @param prefix 前缀
 * @return true 以指定前缀开头
 * @return false 不以指定前缀开头
 */
bool StartWith(const std::string& str, const std::string& prefix);

/**
 * @brief 检查字符串是否以指定后缀结尾
 * @param str 要检查的字符串
 * @param suffix 后缀
 * @return true 以指定后缀结尾
 * @return false 不以指定后缀结尾
 */
bool EndWith(const std::string& str, const std::string& suffix);

/** @} */

} // namespace util
