/**
 * @file messageSerializer.h
 * @brief 消息序列化器
 * @author IM Server Team
 * @version 1.0
 * @date 2026-01-01
 * 
 * @details
 * 本文件定义了消息的序列化和反序列化函数。
 * 使用 nlohmann/json 库实现 JSON 格式的序列化。
 * 
 * 序列化流程：
 * 1. 将协议结构体转换为 JSON 对象
 * 2. 将 JSON 对象序列化为字符串
 * 
 * 反序列化流程：
 * 1. 将 JSON 字符串解析为 JSON 对象
 * 2. 将 JSON 对象转换为协议结构体
 * 
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <string>
#include "codec/protocol.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @brief 消息序列化器命名空间
 * 
 * 提供所有协议消息的序列化和反序列化功能。
 * 
 * 使用场景：
 * - 发送消息前将结构体序列化为 JSON 字符串
 * - 接收消息后将 JSON 字符串反序列化为结构体
 * 
 * @example
 * @code
 * // 序列化
 * protocol::LoginReq req;
 * req.userId = "user001";
 * req.token = "abc123";
 * std::string data = serializer::Serialize(req);
 * 
 * // 反序列化
 * protocol::LoginReq parsed;
 * if (serializer::Deserialize(data, parsed)) {
 *     // 使用 parsed
 * }
 * @endcode
 */
namespace serializer
{

/**
 * @brief 序列化登录请求
 * @param msg 登录请求结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::LoginReq& msg);

/**
 * @brief 反序列化登录请求
 * @param data JSON 格式的字符串
 * @param msg 输出的登录请求结构体
 * @return true 反序列化成功
 * @return false 反序列化失败（格式错误）
 */
bool Deserialize(const std::string& data, protocol::LoginReq& msg);

/**
 * @brief 序列化登录响应
 * @param msg 登录响应结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::LoginResp& msg);

/**
 * @brief 反序列化登录响应
 * @param data JSON 格式的字符串
 * @param msg 输出的登录响应结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::LoginResp& msg);

/**
 * @brief 序列化心跳请求
 * @param msg 心跳请求结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::HeartbeatReq& msg);

/**
 * @brief 反序列化心跳请求
 * @param data JSON 格式的字符串
 * @param msg 输出的心跳请求结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::HeartbeatReq& msg);

/**
 * @brief 序列化心跳响应
 * @param msg 心跳响应结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::HeartbeatResp& msg);

/**
 * @brief 反序列化心跳响应
 * @param data JSON 格式的字符串
 * @param msg 输出的心跳响应结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::HeartbeatResp& msg);

/**
 * @brief 序列化点对点消息请求
 * @param msg 点对点消息请求结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::P2PMsgReq& msg);

/**
 * @brief 反序列化点对点消息请求
 * @param data JSON 格式的字符串
 * @param msg 输出的点对点消息请求结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::P2PMsgReq& msg);

/**
 * @brief 序列化点对点消息响应
 * @param msg 点对点消息响应结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::P2PMsgResp& msg);

/**
 * @brief 反序列化点对点消息响应
 * @param data JSON 格式的字符串
 * @param msg 输出的点对点消息响应结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::P2PMsgResp& msg);

std::string Serialize(const protocol::P2PMsgNotify& msg);
bool Deserialize(const std::string& data, protocol::P2PMsgNotify& msg);

/**
 * @brief 序列化广播消息请求
 * @param msg 广播消息请求结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::BroadcastMsgReq& msg);

/**
 * @brief 反序列化广播消息请求
 * @param data JSON 格式的字符串
 * @param msg 输出的广播消息请求结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::BroadcastMsgReq& msg);

/**
 * @brief 序列化广播消息通知
 * @param msg 广播消息通知结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::BroadcastMsgNotify& msg);

/**
 * @brief 反序列化广播消息通知
 * @param data JSON 格式的字符串
 * @param msg 输出的广播消息通知结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::BroadcastMsgNotify& msg);

/**
 * @brief 序列化踢人请求
 * @param msg 踢人请求结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::KickUserReq& msg);

/**
 * @brief 反序列化踢人请求
 * @param data JSON 格式的字符串
 * @param msg 输出的踢人请求结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::KickUserReq& msg);

/**
 * @brief 序列化踢人响应
 * @param msg 踢人响应结构体
 * @return JSON 格式的字符串
 */
std::string Serialize(const protocol::KickUserResp& msg);

/**
 * @brief 反序列化踢人响应
 * @param data JSON 格式的字符串
 * @param msg 输出的踢人响应结构体
 * @return true 反序列化成功
 * @return false 反序列化失败
 */
bool Deserialize(const std::string& data, protocol::KickUserResp& msg);

std::string Serialize(const protocol::MessageAckReq& msg);
bool Deserialize(const std::string& data, protocol::MessageAckReq& msg);

std::string Serialize(const protocol::MessageAckResp& msg);
bool Deserialize(const std::string& data, protocol::MessageAckResp& msg);

std::string Serialize(const protocol::PullOfflineReq& msg);
bool Deserialize(const std::string& data, protocol::PullOfflineReq& msg);

std::string Serialize(const protocol::PullOfflineResp& msg);
bool Deserialize(const std::string& data, protocol::PullOfflineResp& msg);

}
