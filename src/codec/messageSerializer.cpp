// MessageSerializer.cpp
#include "codec/messageSerializer.h"

namespace serializer
{

std::string Serialize(const protocol::LoginReq& msg)
{
    json j;
    j["user_id"] = msg.userId;
    j["token"] = msg.token;
    j["device_id"] = msg.deviceId;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::LoginReq& msg)
{
    try
    {
        json j = json::parse(data);
        msg.userId = j["user_id"].get<std::string>();
        msg.token = j["token"].get<std::string>();
        if (j.contains("device_id"))
        {
            msg.deviceId = j["device_id"].get<std::string>();
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::LoginResp& msg)
{
    json j;
    j["result_code"] = msg.resultCode;
    j["result_msg"] = msg.resultMsg;
    j["session_id"] = msg.sessionId;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::LoginResp& msg)
{
    try
    {
        json j = json::parse(data);
        msg.resultCode = j["result_code"].get<uint32_t>();
        msg.resultMsg = j["result_msg"].get<std::string>();
        if (j.contains("session_id"))
        {
            msg.sessionId = j["session_id"].get<std::string>();
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::HeartbeatReq& msg)
{
    (void)msg;
	return "{}";
}

bool Deserialize(const std::string& data, protocol::HeartbeatReq& msg)
{
    (void)data;
    (void)msg;
	return true;
}

std::string Serialize(const protocol::HeartbeatResp& msg)
{
	json j;
	j["server_time"] = msg.serverTime;
	return j.dump();
}

bool Deserialize(const std::string& data, protocol::HeartbeatResp& msg)
{
    try
    {
        json j = json::parse(data);
        msg.serverTime = j["server_time"].get<int64_t>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::P2PMsgReq& msg)
{
    json j;
    j["from_user_id"] = msg.fromUserId;
    j["to_user_id"] = msg.toUserId;
    j["client_msg_id"] = msg.clientMsgId;
    j["content"] = msg.content;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::P2PMsgReq& msg)
{
    try
    {
        json j = json::parse(data);
        msg.fromUserId = j["from_user_id"].get<std::string>();
        msg.toUserId = j["to_user_id"].get<std::string>();
        msg.clientMsgId = j.value("client_msg_id", "");
        msg.content = j["content"].get<std::string>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::P2PMsgResp& msg)
{
    json j;
    j["result_code"] = msg.resultCode;
    j["result_msg"] = msg.resultMsg;
    j["msg_id"] = msg.msgId;
    j["server_seq"] = msg.serverSeq;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::P2PMsgResp& msg)
{
    try
    {
        json j = json::parse(data);
        msg.resultCode = j["result_code"].get<uint32_t>();
        msg.resultMsg = j["result_msg"].get<std::string>();
        msg.msgId = j["msg_id"].get<uint64_t>();
        msg.serverSeq = j.value("server_seq", static_cast<uint64_t>(0));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::P2PMsgNotify& msg)
{
    json j;
    j["msg_id"] = msg.msgId;
    j["server_seq"] = msg.serverSeq;
    j["from_user_id"] = msg.fromUserId;
    j["to_user_id"] = msg.toUserId;
    j["client_msg_id"] = msg.clientMsgId;
    j["content"] = msg.content;
    j["timestamp"] = msg.timestamp;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::P2PMsgNotify& msg)
{
    try
    {
        json j = json::parse(data);
        msg.msgId = j["msg_id"].get<uint64_t>();
        msg.serverSeq = j["server_seq"].get<uint64_t>();
        msg.fromUserId = j["from_user_id"].get<std::string>();
        msg.toUserId = j["to_user_id"].get<std::string>();
        msg.clientMsgId = j.value("client_msg_id", "");
        msg.content = j["content"].get<std::string>();
        msg.timestamp = j["timestamp"].get<int64_t>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::BroadcastMsgReq& msg)
{
    json j;
    j["from_user_id"] = msg.fromUserId;
    j["content"] = msg.content;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::BroadcastMsgReq& msg)
{
    try
    {
        json j = json::parse(data);
        msg.fromUserId = j["from_user_id"].get<std::string>();
        msg.content = j["content"].get<std::string>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::BroadcastMsgNotify& msg)
{
    json j;
    j["from_user_id"] = msg.fromUserId;
    j["content"] = msg.content;
    j["timestamp"] = msg.timestamp;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::BroadcastMsgNotify& msg)
{
    try
    {
        json j = json::parse(data);
        msg.fromUserId = j["from_user_id"].get<std::string>();
        msg.content = j["content"].get<std::string>();
        msg.timestamp = j["timestamp"].get<int64_t>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::KickUserReq& msg)
{
    json j;
    j["target_user_id"] = msg.targetUserId;
    j["reason"] = msg.reason;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::KickUserReq& msg)
{
    try
    {
        json j = json::parse(data);
        msg.targetUserId = j["target_user_id"].get<std::string>();
        msg.reason = j["reason"].get<std::string>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::KickUserResp& msg)
{
    json j;
    j["result_code"] = msg.resultCode;
    j["result_msg"] = msg.resultMsg;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::KickUserResp& msg)
{
    try
    {
        json j = json::parse(data);
        msg.resultCode = j["result_code"].get<uint32_t>();
        msg.resultMsg = j["result_msg"].get<std::string>();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::MessageAckReq& msg)
{
    json j;
    j["msg_id"] = msg.msgId;
    j["server_seq"] = msg.serverSeq;
    j["ack_code"] = msg.ackCode;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::MessageAckReq& msg)
{
    try
    {
        json j = json::parse(data);
        msg.msgId = j["msg_id"].get<uint64_t>();
        msg.serverSeq = j["server_seq"].get<uint64_t>();
        msg.ackCode = j.value("ack_code", static_cast<uint32_t>(protocol::AckReceived));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::MessageAckResp& msg)
{
    json j;
    j["result_code"] = msg.resultCode;
    j["result_msg"] = msg.resultMsg;
    j["server_seq"] = msg.serverSeq;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::MessageAckResp& msg)
{
    try
    {
        json j = json::parse(data);
        msg.resultCode = j["result_code"].get<uint32_t>();
        msg.resultMsg = j["result_msg"].get<std::string>();
        msg.serverSeq = j.value("server_seq", static_cast<uint64_t>(0));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::PullOfflineReq& msg)
{
    json j;
    j["user_id"] = msg.userId;
    j["last_acked_seq"] = msg.lastAckedSeq;
    j["limit"] = msg.limit;
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::PullOfflineReq& msg)
{
    try
    {
        json j = json::parse(data);
        msg.userId = j["user_id"].get<std::string>();
        msg.lastAckedSeq = j.value("last_acked_seq", static_cast<uint64_t>(0));
        msg.limit = j.value("limit", static_cast<uint32_t>(100));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string Serialize(const protocol::PullOfflineResp& msg)
{
    json j;
    j["result_code"] = msg.resultCode;
    j["result_msg"] = msg.resultMsg;
    j["next_begin_seq"] = msg.nextBeginSeq;
    j["messages"] = json::array();
    for (const auto& item : msg.messages)
    {
        j["messages"].push_back({
            {"msg_id", item.msgId},
            {"server_seq", item.serverSeq},
            {"from_user_id", item.fromUserId},
            {"to_user_id", item.toUserId},
            {"client_msg_id", item.clientMsgId},
            {"content", item.content},
            {"timestamp", item.timestamp},
        });
    }
    return j.dump();
}

bool Deserialize(const std::string& data, protocol::PullOfflineResp& msg)
{
    try
    {
        json j = json::parse(data);
        msg.resultCode = j["result_code"].get<uint32_t>();
        msg.resultMsg = j["result_msg"].get<std::string>();
        msg.nextBeginSeq = j.value("next_begin_seq", static_cast<uint64_t>(0));
        msg.messages.clear();
        if (j.contains("messages"))
        {
            for (const auto& item : j["messages"])
            {
                protocol::PullOfflineResp::OfflineItem offline;
                offline.msgId = item["msg_id"].get<uint64_t>();
                offline.serverSeq = item["server_seq"].get<uint64_t>();
                offline.fromUserId = item["from_user_id"].get<std::string>();
                offline.toUserId = item["to_user_id"].get<std::string>();
                offline.clientMsgId = item.value("client_msg_id", "");
                offline.content = item["content"].get<std::string>();
                offline.timestamp = item["timestamp"].get<int64_t>();
                msg.messages.push_back(std::move(offline));
            }
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

}
