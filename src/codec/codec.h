/**
 * @file codec.h
 * @brief 消息编解码器
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了 Codec 类，负责网络消息的编解码工作。
 * Codec 实现了基于长度字段的协议解析，支持 TCP 粘包处理。
 * 
 * 协议格式：
 * +--------+--------+--------+--------+--------+----------------+
 * | Header | Length | Cmd    | SeqId  | CRC16  | Payload        |
 * | 4Bytes | 4Bytes | 2Bytes | 4Bytes | 2Bytes | Variable       |
 * +--------+--------+--------+--------+--------+----------------+
 * 
 * 编解码流程：
 * 1. 发送：pack() 将消息打包成字节流
 * 2. 接收：onMessage() 解析字节流，处理粘包问题
 * 
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include "codec/protocol.h"
#include "codec/buffer.h"
#include "net/nonCopyable.h"
#include "net/timestamp.h"

class TcpConnection;

/**
 * @brief 消息编解码器类
 * 
 * Codec 类负责网络消息的序列化和反序列化。
 * 
 * 特性：
 * - 状态机解析：使用状态机处理 TCP 粘包问题
 * - 协议封装：自动添加协议头和校验码
 * - 回调机制：解析完成后通过回调通知上层
 * - 错误处理：支持错误回调通知
 * 
 * 解析状态：
 * - kExpectHeader：等待接收消息头
 * - kExpectPayload：等待接收消息体
 * 
 * 使用场景：
 * - TCP 连接的消息解析
 * - 消息的打包发送
 * 
 * @note Codec 不是线程安全的，每个 TcpConnection 应该有独立的 Codec
 * 
 * @example
 * @code
 * Codec codec;
 * codec.setMessageCallback([](const auto& conn, uint16_t cmd, uint32_t seq, const std::string& msg) {
 *     // 处理消息
 * });
 * 
 * // 发送消息
 * Buffer buf;
 * Codec::pack(&buf, CmdLoginReq, 1, loginData);
 * conn->send(&buf);
 * @endcode
 */
class Codec : NonCopyable
{
public:
    struct DecodedPacket
    {
        protocol::PacketHeader header;
        std::string payload;
    };

    /**
     * @brief 消息回调函数类型
     * @param conn TCP 连接
     * @param command 消息命令字
     * @param seqId 序列号
     * @param message 消息内容（JSON 格式）
     */
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                                uint16_t command,
                                                uint32_t seqId,
                                                const std::string& message)>;

    /**
     * @brief 完整包回调
     */
    using PacketCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                              const DecodedPacket&)>;
    
    /**
     * @brief 错误回调函数类型
     * @param conn TCP 连接
     * @param errorCode 错误码
     */
    using ErrorCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                              int errorCode)>;

    /**
     * @brief 构造函数
     * 
     * 初始化解析状态为等待消息头。
     */
    Codec();
    
    /**
     * @brief 析构函数
     */
    ~Codec() = default;

    /**
     * @brief 设置消息回调
     * @param cb 消息回调函数
     */
    void setMessageCallback(MessageCallback cb) { m_messageCallback = std::move(cb); }

    /**
     * @brief 设置完整数据包回调
     * @param cb 回调函数
     */
    void setPacketCallback(PacketCallback cb) { m_packetCallback = std::move(cb); }
    
    /**
     * @brief 设置错误回调
     * @param cb 错误回调函数
     */
    void setErrorCallback(ErrorCallback cb) { m_errorCallback = std::move(cb); }

    /**
     * @brief 打包消息
     * @param buffer 输出缓冲区
     * @param command 命令字
     * @param seqId 序列号
     * @param message 消息内容
     * 
     * 将消息打包成协议格式，包括：
     * - 协议头标识
     * - 消息长度
     * - 命令字
     * - 序列号
     * - CRC16 校验码
     * - 消息体
     * 
     * @note 此函数是静态的，可以在任何地方调用
     */
    static void pack(Buffer* buffer,
                     uint16_t command,
                     uint32_t seqId,
                     const std::string& message);

    /**
     * @brief 按完整头部打包消息
     */
    static void pack(Buffer* buffer,
                     const protocol::PacketHeader& header,
                     const std::string& message);

    /**
     * @brief 处理接收到的数据
     * @param conn TCP 连接
     * @param buffer 输入缓冲区
     * @param receiveTime 接收时间戳
     * 
     * 解析输入缓冲区中的数据，处理 TCP 粘包问题。
     * 当完整消息解析完成后，调用消息回调。
     * 
     * 解析流程：
     * 1. 检查是否有足够的数据读取消息头
     * 2. 解析消息头，获取消息长度
     * 3. 检查是否有足够的数据读取消息体
     * 4. 解析消息体，验证 CRC 校验码
     * 5. 调用消息回调
     */
    void onMessage(const std::shared_ptr<TcpConnection>& conn,
                   Buffer* buffer,
                   Timestamp receiveTime);

private:
    /**
     * @brief 解析状态枚举
     */
    enum ParseState
    {
        kExpectHeader,   ///< 等待接收消息头
        kExpectPayload,  ///< 等待接收消息体
    };

    /**
     * @brief 解析消息头
     * @param buffer 输入缓冲区
     * @return true 解析成功，false 数据不足
     * 
     * 从缓冲区中读取消息头，提取消息长度。
     * 成功后状态转换为 kExpectPayload。
     */
    bool parseHeader(Buffer* buffer);
    
    /**
     * @brief 解析消息体
     * @param conn TCP 连接
     * @param buffer 输入缓冲区
     * @return true 解析成功，false 数据不足
     * 
     * 从缓冲区中读取消息体，验证 CRC 校验码。
     * 成功后调用消息回调，状态转换为 kExpectHeader。
     */
    bool parsePayload(const std::shared_ptr<TcpConnection>& conn, Buffer* buffer);

    ParseState m_state;           ///< 当前解析状态
    uint32_t m_expectedLength;    ///< 期望的整个包长度
    protocol::PacketHeader m_currentHeader; ///< 当前解析中的消息头

    MessageCallback m_messageCallback;  ///< 消息回调
    PacketCallback m_packetCallback;    ///< 完整包回调
    ErrorCallback m_errorCallback;      ///< 错误回调
};
