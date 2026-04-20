/**
 * @file currentThread.h
 * @brief 当前线程信息
 * 
 * 提供获取当前线程 ID 的功能，用于线程标识和调试。
 * 使用线程局部存储缓存线程 ID，避免频繁系统调用。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <unistd.h>
#include <sys/syscall.h>

/**
 * @brief 当前线程信息命名空间
 */
namespace CurrentThread
{

extern __thread int t_cached_tid;

/**
 * @brief 缓存当前线程 ID
 */
void cacheTid();

/**
 * @brief 获取当前线程 ID
 * @return int 线程 ID
 * 
 * 使用 __builtin_expect 优化，假设线程 ID 已缓存。
 * 首次调用时会调用 cacheTid() 进行缓存。
 */
inline int tid()
{
    if (__builtin_expect(t_cached_tid == 0, 0))
    {
        cacheTid();
    }
    return t_cached_tid;
}

} // namespace CurrentThread
