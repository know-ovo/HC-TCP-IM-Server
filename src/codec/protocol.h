/**
 * @file protocol.h
 * @brief 通信协议定义
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了即时通讯系统的通信协议，包括：
 * - 协议常量定义
 * - 命令字枚举
 * - 消息结构体定义
 * - CRC 校验函数
 * 
 * 协议格式（总长度 = Header + Payload 长度）：
 * +----------+----------+----------+----------+----------+----------+
 * | Magic    | Version  | HdrLen   | TotalLen | Cmd      | Flags    |
 * | 4Bytes   | 2Bytes   | 2Bytes   | 4Bytes   | 2Bytes   | 2Bytes   |
 * +----------+----------+----------+----------+----------+----------+
 * | ReqId    | ClientSeq          | ServerSeq          | ErrorCode  |
 * | 4Bytes   | 8Bytes             | 8Bytes             | 4Bytes     |
 * +----------+--------------------+--------------------+------------+
 * | Checksum | Payload ...                                          |
 * | 4Bytes   | Variable                                             |
 * +----------+------------------------------------------------------+
 * 
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 协议定义命名空间
 * 
 * 包含所有协议相关的常量、枚举和结构体定义。
 */
namespace protocol
{

/**
 * @brief 协议魔数
 */
constexpr uint32_t kMagicNumber = 0xABCD1234;

/**
 * @brief 协议版本
 */
constexpr uint16_t kProtocolVersionV1 = 1;

/**
 * @brief 协议头大小（字节）
 */
constexpr uint16_t kHeaderSize = 44;

/**
 * @brief 最大消息大小（字节）
 * 
 * 限制单个消息的最大大小为 64MB，防止恶意大消息攻击。
 */
constexpr uint32_t kMaxMessageSize = 64 * 1024 * 1024;

/**
 * @brief 数据负载格式
 */
enum class PayloadFormat : uint8_t
{
    Json = 0,
    Protobuf = 1
};

/**
 * @brief 协议标志位
 */
enum PacketFlags : uint16_t
{
    kFlagNone = 0x0000,
    kFlagRequest = 0x0001,
    kFlagResponse = 0x0002,
    kFlagAck = 0x0004,
    kFlagRetry = 0x0008,
    kFlagCompressed = 0x0010,
    kFlagEncrypted = 0x0020,
};

/**
 * @brief 统一错误码
 */
enum ErrorCode : uint32_t
{
    ErrOk = 0,
    ErrInvalidPacket = 1001,
    ErrUnsupportedVersion = 1002,
    ErrChecksumMismatch = 1003,
    ErrPayloadTooLarge = 1004,
    ErrAuthFailed = 2001,
    ErrUserOffline = 3001,
    ErrAckTimeout = 3002,
    ErrServerBusy = 5001
};

/**
 * @brief 统一协议头
 */
struct PacketHeader
{
    uint32_t magic = kMagicNumber;
    uint16_t version = kProtocolVersionV1;
    uint16_t headerLen = kHeaderSize;
    uint32_t totalLen = kHeaderSize;
    uint16_t command = 0;
    uint16_t flags = kFlagNone;
    uint32_t requestId = 0;
    uint64_t clientSeq = 0;
    uint64_t serverSeq = 0;
    uint32_t errorCode = ErrOk;
    uint32_t checksum = 0;
};

/**
 * @brief 命令字枚举
 * 
 * 定义所有消息类型的命令字。
 * 高字节表示消息大类，低字节表示具体消息。
 */
enum Command : uint16_t
{
    CmdLoginReq = 0x0001,          ///< 登录请求
    CmdLoginResp = 0x0002,         ///< 登录响应
    CmdHeartbeatReq = 0x0003,      ///< 心跳请求
    CmdHeartbeatResp = 0x0004,     ///< 心跳响应
    CmdP2pMsgReq = 0x0005,         ///< 点对点消息请求
    CmdP2pMsgResp = 0x0006,        ///< 点对点消息响应
    CmdP2pMsgNotify = 0x0007,      ///< 点对点消息通知
    CmdBroadcastMsgReq = 0x0008,   ///< 广播消息请求
    CmdBroadcastMsgNotify = 0x0009,///< 广播消息通知
    CmdKickUserReq = 0x000A,       ///< 踢人请求
    CmdKickUserResp = 0x000B,      ///< 踢人响应
    CmdMessageAckReq = 0x000C,     ///< 消息确认请求
    CmdMessageAckResp = 0x000D,    ///< 消息确认响应
    CmdPullOfflineReq = 0x000E,    ///< 拉取离线消息请求
    CmdPullOfflineResp = 0x000F,   ///< 拉取离线消息响应
};

enum DeliveryStatus : uint8_t
{
    DeliveryCreated = 0,
    DeliveryPersisted = 1,
    DeliveryDelivering = 2,
    DeliveryDelivered = 3,
    DeliveryFailed = 4,
    DeliveryRead = 5,
};

enum AckCode : uint32_t
{
    AckReceived = 0,
    AckRead = 1,
};

/**
 * @brief 登录请求结构体
 * 
 * 客户端发送登录请求时使用。
 */
struct LoginReq
{
    std::string userId;    ///< 用户ID
    std::string token;     ///< 登录令牌
    std::string deviceId;  ///< 设备ID，用于多设备管理
};

/**
 * @brief 登录响应结构体
 * 
 * 服务器响应登录请求时使用。
 */
struct LoginResp
{
    uint32_t resultCode;   ///< 结果码，0 表示成功
    std::string resultMsg; ///< 结果描述信息
    std::string sessionId; ///< 会话ID，登录成功后返回
};

/**
 * @brief 心跳请求结构体
 * 
 * 客户端发送心跳请求时使用。
 * 心跳请求不携带额外数据。
 */
struct HeartbeatReq
{
};

/**
 * @brief 心跳响应结构体
 * 
 * 服务器响应心跳请求时使用。
 */
struct HeartbeatResp
{
    int64_t serverTime;    ///< 服务器时间戳（毫秒）
};

/**
 * @brief 点对点消息请求结构体
 * 
 * 客户端发送点对点消息时使用。
 */
struct P2PMsgReq
{
    std::string fromUserId;///< 发送者用户ID
    std::string toUserId;  ///< 接收者用户ID
    std::string clientMsgId; ///< 客户端幂等消息ID
    std::string content;   ///< 消息内容
};

/**
 * @brief 点对点消息响应结构体
 * 
 * 服务器响应点对点消息请求时使用。
 */
struct P2PMsgResp
{
    uint32_t resultCode;   ///< 结果码，0 表示成功
    std::string resultMsg; ///< 结果描述信息
    uint64_t msgId;        ///< 消息ID，用于消息确认
    uint64_t serverSeq;    ///< 服务端序列号
};

/**
 * @brief 点对点消息通知
 */
struct P2PMsgNotify
{
    uint64_t msgId;          ///< 全局消息ID
    uint64_t serverSeq;      ///< 接收方序列号
    std::string fromUserId;  ///< 发送者用户ID
    std::string toUserId;    ///< 接收者用户ID
    std::string clientMsgId; ///< 客户端消息ID
    std::string content;     ///< 消息内容
    int64_t timestamp;       ///< 消息时间戳
};

/**
 * @brief 广播消息请求结构体
 * 
 * 客户端发送广播消息时使用。
 */
struct BroadcastMsgReq
{
    std::string fromUserId;///< 发送者用户ID
    std::string content;   ///< 消息内容
};

/**
 * @brief 广播消息通知结构体
 * 
 * 服务器向客户端推送广播消息时使用。
 */
struct BroadcastMsgNotify
{
    std::string fromUserId;///< 发送者用户ID
    std::string content;   ///< 消息内容
    int64_t timestamp;     ///< 消息时间戳（毫秒）
};

/**
 * @brief 踢人请求结构体
 * 
 * 管理员踢出用户时使用。
 */
struct KickUserReq
{
    std::string targetUserId;///< 目标用户ID
    std::string reason;      ///< 踢人原因
};

/**
 * @brief 踢人响应结构体
 * 
 * 服务器响应踢人请求时使用。
 */
struct KickUserResp
{
    uint32_t resultCode;   ///< 结果码，0 表示成功
    std::string resultMsg; ///< 结果描述信息
};

/**
 * @brief 消息确认请求
 */
struct MessageAckReq
{
    uint64_t msgId;       ///< 全局消息ID
    uint64_t serverSeq;   ///< 服务端序列号
    uint32_t ackCode;     ///< 确认类型
};

/**
 * @brief 消息确认响应
 */
struct MessageAckResp
{
    uint32_t resultCode;   ///< 结果码
    std::string resultMsg; ///< 结果描述
    uint64_t serverSeq;    ///< 当前已确认序列
};

/**
 * @brief 拉取离线消息请求
 */
struct PullOfflineReq
{
    std::string userId;    ///< 用户ID
    uint64_t lastAckedSeq; ///< 客户端已确认最大序列
    uint32_t limit;        ///< 拉取条数上限
};

/**
 * @brief 拉取离线消息响应
 */
struct PullOfflineResp
{
    struct OfflineItem
    {
        uint64_t msgId;
        uint64_t serverSeq;
        std::string fromUserId;
        std::string toUserId;
        std::string clientMsgId;
        std::string content;
        int64_t timestamp;
    };

    uint32_t resultCode;                ///< 结果码
    std::string resultMsg;              ///< 结果描述
    uint64_t nextBeginSeq;              ///< 下一次拉取起点
    std::vector<OfflineItem> messages;  ///< 离线消息列表
};

/**
 * @brief 判断命令是否是响应类命令
 */
bool IsResponseCommand(uint16_t command);

/**
 * @brief 根据命令猜测默认 flags
 */
uint16_t GuessFlagsFromCommand(uint16_t command);

/**
 * @brief 计算 CRC16 校验码
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC16 校验码
 * 
 * 使用 CRC16-CCITT 算法计算校验码。
 * 用于消息完整性校验。
 */
uint16_t CalcCRC16(const void* data, size_t len);

}
