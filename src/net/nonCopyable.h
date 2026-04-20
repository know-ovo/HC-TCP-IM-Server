/**
 * @file nonCopyable.h
 * @brief 不可拷贝基类
 * 
 * 提供一个基类，继承它的类将禁止拷贝构造和拷贝赋值。
 * 这是一种常用的 C++ 惯用法，用于防止意外的对象复制。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

/**
 * @brief 不可拷贝基类
 * 
 * 继承此类的对象将禁止拷贝构造和拷贝赋值操作。
 * 
 * 使用示例：
 * @code
 * class MyClass : public NonCopyable {
 *     // MyClass 现在不可拷贝
 * };
 * @endcode
 */
class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

private:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};
