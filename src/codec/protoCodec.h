/**
 * @file protoCodec.h
 * @brief Protobuf 协议编解码工具类
 * 
 * 提供协议负载格式解析、格式字符串转换、Protobuf 运行环境检测等通用工具函数。
 * 属于编解码模块的辅助工具，为上层协议编解码提供基础支撑能力。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

 /*
 ProtoCodec 是协议编解码模块的静态工具类，
 负责处理协议负载格式的解析与字符串互转，
 并提供 Protobuf 库运行环境检测功能，支撑编解码核心逻辑。
 */

#pragma once

#include <string>
#include "codec/protocol.h"

/**
 * @brief Protobuf 协议工具类
 * 
 * 提供静态工具方法，用于协议负载格式处理、Protobuf 环境检测等通用功能。
 * 静态类无需实例化，直接通过类名调用方法。
 * 
 * 特性：
 * - 纯静态工具类，无成员变量
 * - 支持协议负载格式与字符串互转
 * - 检测系统 Protobuf 运行环境是否可用
 * 
 * 使用示例：
 * @code
 * // 字符串解析为负载格式
 * protocol::PayloadFormat format = ProtoCodec::parsePayloadFormat("protobuf");
 * // 负载格式转换为字符串
 * const char* fmtStr = ProtoCodec::toString(format);
 * // 检测 Protobuf 库是否可用
 * bool available = ProtoCodec::protobufAvailable();
 * @endcode
 */
class ProtoCodec
{
public:
    /**
     * @brief 解析字符串为协议负载格式枚举
     * @param value 负载格式字符串（如 protobuf/json）
     * @return protocol::PayloadFormat 对应的协议负载格式枚举值
     */
    static protocol::PayloadFormat parsePayloadFormat(const std::string& value);

    /**
     * @brief 将协议负载格式枚举转换为字符串
     * @param format 协议负载格式枚举
     * @return const char* 格式对应的字符串常量
     */
    static const char* toString(protocol::PayloadFormat format);

    /**
     * @brief 检测 Protobuf 库是否可用
     * @return bool 可用返回 true，不可用返回 false
     */
    static bool protobufAvailable();
};