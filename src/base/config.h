/**
 * @file config.h
 * @brief 配置文件解析器
 * 
 * 提供 INI 格式配置文件的解析和访问功能。
 * 支持分节读取配置项，支持字符串、整数、布尔值和浮点数类型。
 * 采用单例模式，全局唯一实例。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

/**
 * @brief 配置文件管理类
 * 
 * 单例模式的配置管理器，支持 INI 格式配置文件。
 * 
 * 配置文件格式示例：
 * @code
 * [server]
 * port = 8888
 * worker_threads = 4
 * 
 * [redis]
 * host = 127.0.0.1
 * port = 6379
 * @endcode
 */
class Config
{
public:
    /**
     * @brief 获取单例实例
     * @return Config& 配置管理器实例引用
     */
    static Config& instance();

    /**
     * @brief 加载配置文件
     * @param filePath 配置文件路径
     * @return true 加载成功
     * @return false 加载失败
     */
    bool load(const std::string& filePath);

    /**
     * @brief 获取字符串配置项
     * @param section 节名
     * @param key 键名
     * @param defaultValue 默认值
     * @return std::string 配置值
     */
    std::string getString(const std::string& section, const std::string& key, const std::string& defaultValue = "");

    /**
     * @brief 获取整数配置项
     * @param section 节名
     * @param key 键名
     * @param defaultValue 默认值
     * @return int 配置值
     */
    int getInt(const std::string& section, const std::string& key, int defaultValue = 0);

    /**
     * @brief 获取布尔配置项
     * @param section 节名
     * @param key 键名
     * @param defaultValue 默认值
     * @return bool 配置值
     */
    bool getBool(const std::string& section, const std::string& key, bool defaultValue = false);

    /**
     * @brief 获取浮点数配置项
     * @param section 节名
     * @param key 键名
     * @param defaultValue 默认值
     * @return double 配置值
     */
    double getDouble(const std::string& section, const std::string& key, double defaultValue = 0.0);

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    /**
     * @brief 去除字符串首尾空白
     * @param s 要处理的字符串
     */
    void trim(std::string& s);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_data;
};
