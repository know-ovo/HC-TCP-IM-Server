/**
 * @file log.h
 * @brief 日志系统封装
 * 
 * 基于 spdlog 库封装的日志系统，提供统一的日志接口。
 * 支持控制台输出和文件滚动输出，支持多日志级别。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

/**
 * @brief 日志管理类
 * 
 * 提供日志系统的初始化和访问接口。
 * 支持以下特性：
 * - 控制台彩色输出
 * - 文件滚动输出（按大小滚动）
 * - 多日志级别（DEBUG, INFO, WARN, ERROR, FATAL）
 * - 线程安全
 * 
 * 使用示例：
 * @code
 * Log::init("server", "logs/server.log");
 * LOG_INFO("Server started on port {}", port);
 * LOG_ERROR("Failed to bind port: {}", port);
 * @endcode
 */
class Log
{
public:
    /**
     * @brief 初始化日志系统
     * @param logName 日志名称
     * @param logPath 日志文件路径
     * @param level 日志级别（默认 INFO）
     * @param maxFileSize 单个日志文件最大大小（默认 100MB）
     * @param maxFiles 保留日志文件数量（默认 3 个）
     */
    static void init(const std::string& logName = "server",
                     const std::string& logPath = "logs/server.log",
                     int level = spdlog::level::info,
                     size_t maxFileSize = 100 * 1024 * 1024,
                     size_t maxFiles = 3);

    /**
     * @brief 设置日志级别
     * @param level 日志级别
     */
    static void setLevel(int level);

    /**
     * @brief 获取日志记录器
     * @return std::shared_ptr<spdlog::logger>& 日志记录器引用
     */
    static std::shared_ptr<spdlog::logger>& getLogger();

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

/** @brief 调试级别日志宏 */
#define LOG_DEBUG(...) Log::getLogger()->debug(__VA_ARGS__)

/** @brief 信息级别日志宏 */
#define LOG_INFO(...) Log::getLogger()->info(__VA_ARGS__)

/** @brief 警告级别日志宏 */
#define LOG_WARN(...) Log::getLogger()->warn(__VA_ARGS__)

/** @brief 错误级别日志宏 */
#define LOG_ERROR(...) Log::getLogger()->error(__VA_ARGS__)

/** @brief 致命错误级别日志宏 */
#define LOG_FATAL(...) Log::getLogger()->critical(__VA_ARGS__)
