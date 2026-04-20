/**
 * @file inetAddress.h
 * @brief 网络地址封装
 * 
 * 封装 IPv4 网络地址（IP + 端口），提供地址创建、解析等功能。
 * 基于 struct sockaddr_in 实现。
 * 
 * @author HighConcurrencyServer Team
 * @date 2026-01-01
 * @version 1.0.0
 * @copyright Copyright (c) 2026 HighConcurrencyServer. All rights reserved.
 */

#pragma once

#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

/**
 * @brief IPv4 网络地址类
 * 
 * InetAddress 封装了 sockaddr_in 结构，提供便捷的地址操作接口。
 * 
 * 使用示例：
 * @code
 * InetAddress addr(8888);                    // 监听任意地址的 8888 端口
 * InetAddress addr("192.168.1.1", 8888);     // 指定 IP 和端口
 * std::string ip = addr.toIp();              // 获取 IP 字符串
 * uint16_t port = addr.port();               // 获取端口号
 * @endcode
 */
class InetAddress
{
public:
    /**
     * @brief 构造函数（监听任意地址）
     * @param port 端口号
     */
    InetAddress(uint16_t port)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_addr.s_addr = INADDR_ANY;
        m_addr.sin_port = htons(port);
    }

    /**
     * @brief 构造函数（指定 IP 和端口）
     * @param ip IP 地址字符串
     * @param port 端口号
     */
    InetAddress(const std::string& ip, uint16_t port)
    {
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &m_addr.sin_addr);
    }

    /**
     * @brief 构造函数（从 sockaddr_in 结构）
     * @param addr sockaddr_in 结构
     */
    InetAddress(const struct sockaddr_in& addr)
        : m_addr(addr)
    {
    }

    /**
     * @brief 获取 sockaddr_in 结构
     * @return const struct sockaddr_in& sockaddr_in 结构引用
     */
    const struct sockaddr_in& getSockAddrInet() const { return m_addr; }

    /**
     * @brief 设置 sockaddr_in 结构
     * @param addr sockaddr_in 结构
     */
    void setSockAddrInet(const struct sockaddr_in& addr) { m_addr = addr; }

    /**
     * @brief 获取 IP 地址字符串
     * @return std::string IP 地址
     */
    std::string toIp() const
    {
        char buf[64];
        inet_ntop(AF_INET, &m_addr.sin_addr, buf, sizeof(buf));
        return std::string(buf);
    }

    /**
     * @brief 获取 IP:Port 字符串
     * @return std::string IP:Port 格式的地址
     */
    std::string toIpPort() const
    {
        char buf[64];
        inet_ntop(AF_INET, &m_addr.sin_addr, buf, sizeof(buf));
        uint16_t port = ntohs(m_addr.sin_port);
        char fullBuf[128];
        snprintf(fullBuf, sizeof(fullBuf), "%s:%u", buf, port);
        return std::string(fullBuf);
    }

    /**
     * @brief 获取端口号
     * @return uint16_t 端口号
     */
    uint16_t port() const { return ntohs(m_addr.sin_port); }

private:
    struct sockaddr_in m_addr;
};
