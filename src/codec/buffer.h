/**
 * @file buffer.h
 * @brief 应用层缓冲区类
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了 Buffer 类，用于管理网络通信中的应用层数据缓冲区。
 * Buffer 采用连续内存布局，支持高效的读写操作和内存管理。
 * 
 * 缓冲区布局：
 * +-------------------+------------------+------------------+
 * | prependable bytes |  readable bytes  | writable bytes   |
 * |                   |     (CONTENT)    |                  |
 * +-------------------+------------------+------------------+
 * |                   |                  |                  |
 * 0      <=      readerIndex   <=   writerIndex    <=     size
 * 
 * 设计特点：
 * - 自动扩容：当写入空间不足时自动扩展缓冲区
 * - 前置空间：支持在数据前添加头部信息
 * - 高效读取：支持从 socket 直接读取到缓冲区
 * - 零拷贝优化：通过 peek() 直接访问数据
 * 
 * @copyright Copyright (c) 2026
 */

#ifndef _CODEC_BUFFER_H_
#define _CODEC_BUFFER_H_

#include <string>
#include <vector>
#include <cassert>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int ssize_t;
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#endif

/**
 * @brief 应用层缓冲区类
 * 
 * Buffer 类实现了高效的网络数据缓冲区管理。
 * 
 * 特性：
 * - 连续内存：使用 std::vector<char> 作为底层存储
 * - 双指针设计：readerIndex 和 writerIndex 分别指向读位置和写位置
 * - 自动扩容：写入空间不足时自动扩展
 * - 前置空间：支持在数据前添加头部信息
 * 
 * 使用场景：
 * - TCP 连接的输入输出缓冲区
 * - 消息编解码的临时缓冲
 * - 数据包的组装和解析
 * 
 * @note Buffer 不是线程安全的，需要在外部进行同步
 * 
 * @example
 * @code
 * Buffer buf;
 * buf.append("hello", 5);           // 写入数据
 * std::string data = buf.retrieveAllAsString(); // 读取所有数据
 * @endcode
 */
class Buffer
{
public:
    /**
     * @brief 前置空间默认大小
     * 用于在数据前添加头部信息，避免频繁的内存移动
     */
    static const size_t kCheapPrepend = 8;
    
    /**
     * @brief 缓冲区初始大小
     */
    static const size_t kInitialSize = 1024;

    /**
     * @brief 构造函数
     * 
     * 初始化缓冲区，分配 kCheapPrepend + kInitialSize 大小的空间，
     * 读写指针都指向 kCheapPrepend 位置。
     */
    Buffer()
        : m_buffer(kCheapPrepend + kInitialSize)
        , m_readerIndex(kCheapPrepend)
        , m_writerIndex(kCheapPrepend)
    {
    }

    /**
     * @brief 获取可读字节数
     * @return 可读数据的字节数
     */
    size_t readableBytes() const
    {
        return m_writerIndex - m_readerIndex;
    }

    /**
     * @brief 获取可写字节数
     * @return 剩余可写空间的字节数
     */
    size_t writableBytes() const
    {
        return m_buffer.size() - m_writerIndex;
    }

    /**
     * @brief 获取前置空间大小
     * @return 前置空间的字节数
     * @note 前置空间是已读取数据占用的空间，可以被回收利用
     */
    size_t prependableBytes() const
    {
        return m_readerIndex;
    }

    /**
     * @brief 获取可读数据的起始地址
     * @return 指向可读数据起始位置的指针
     * @note 返回的指针在缓冲区扩容后可能失效
     */
    const char* peek() const
    {
        return begin() + m_readerIndex;
    }

    /**
     * @brief 获取可写区域的起始地址（可修改）
     * @return 指向可写区域起始位置的指针
     */
    char* beginWrite()
    {
        return begin() + m_writerIndex;
    }

    /**
     * @brief 获取可写区域的起始地址（只读）
     * @return 指向可写区域起始位置的常量指针
     */
    const char* beginWrite() const
    {
        return begin() + m_writerIndex;
    }

    /**
     * @brief 回收指定长度的已读数据
     * @param len 要回收的长度
     * @note 如果 len 等于可读数据长度，则重置缓冲区
     */
    void retrieve(size_t len)
    {
        assert(len <= readableBytes());
        if (len < readableBytes())
        {
            m_readerIndex += len;
        }
        else
        {
            retrieveAll();
        }
    }

    /**
     * @brief 回收所有可读数据
     * 
     * 重置读写指针到初始位置，但不清除数据。
     * 后续写入操作会覆盖原有数据。
     */
    void retrieveAll()
    {
        m_readerIndex = kCheapPrepend;
        m_writerIndex = kCheapPrepend;
    }

    /**
     * @brief 读取所有数据并转换为字符串
     * @return 包含所有可读数据的字符串
     */
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());
    }

    /**
     * @brief 读取指定长度的数据并转换为字符串
     * @param len 要读取的长度
     * @return 包含指定长度数据的字符串
     * @pre len <= readableBytes()
     */
    std::string retrieveAsString(size_t len)
    {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    /**
     * @brief 追加数据到缓冲区
     * @param data 数据指针
     * @param len 数据长度
     * @note 如果空间不足，会自动扩容
     */
    void append(const char* data, size_t len)
    {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        hasWritten(len);
    }

    /**
     * @brief 追加字符串到缓冲区
     * @param str 要追加的字符串
     */
    void append(const std::string& str)
    {
        append(str.data(), str.size());
    }

    /**
     * @brief 确保有足够的可写空间
     * @param len 需要的空间大小
     * 
     * 如果当前可写空间不足，会调用 makeSpace() 进行扩容或空间整理。
     */
    void ensureWritableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }

    /**
     * @brief 标记已写入的数据长度
     * @param len 已写入的长度
     * @pre len <= writableBytes()
     */
    void hasWritten(size_t len)
    {
        assert(len <= writableBytes());
        m_writerIndex += len;
    }

    /**
     * @brief 从文件描述符读取数据
     * @param fd 文件描述符（socket）
     * @param savedErrno 错误码输出参数
     * @return 读取的字节数，-1 表示错误
     * 
     * 使用系统调用从 socket 读取数据到缓冲区。
     * 在 Linux 下使用 readv 实现分散读取，可以同时利用前置空间。
     * 
     * @note 此函数会自动扩容缓冲区
     */
    ssize_t readFd(int fd, int* savedErrno);

private:
    /**
     * @brief 获取缓冲区起始地址（可修改）
     * @return 缓冲区起始地址
     */
    char* begin()
    {
        return &*m_buffer.begin();
    }

    /**
     * @brief 获取缓冲区起始地址（只读）
     * @return 缓冲区起始地址
     */
    const char* begin() const
    {
        return &*m_buffer.begin();
    }

    /**
     * @brief 扩展或整理缓冲区空间
     * @param len 需要的空间大小
     * 
     * 空间分配策略：
     * 1. 如果前置空间 + 可写空间 >= len，则移动数据到前面，复用前置空间
     * 2. 否则，扩展缓冲区大小
     */
    void makeSpace(size_t len);

    std::vector<char> m_buffer;     ///< 底层存储
    size_t m_readerIndex;           ///< 读指针位置
    size_t m_writerIndex;           ///< 写指针位置
};

#endif // _CODEC_BUFFER_H_
