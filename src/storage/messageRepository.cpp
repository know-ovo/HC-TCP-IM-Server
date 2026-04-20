#include "storage/messageRepository.h"

#include <sstream>
#include <unordered_map>

#include "storage/mysqlClient.h"

namespace storage
{

namespace
{

uint64_t ToUint64(const std::unordered_map<std::string, std::string>& row, const std::string& key)
{
    auto it = row.find(key);
    if (it == row.end() || it->second.empty())
    {
        return 0;
    }
    return static_cast<uint64_t>(std::stoull(it->second));
}

MessageRecord BuildMessageRecord(const std::unordered_map<std::string, std::string>& row)
{
    MessageRecord record;
    record.messageId = ToUint64(row, "id");
    record.clientMsgId = row.at("client_msg_id");
    record.fromUserId = row.at("from_user_id");
    record.toUserId = row.at("to_user_id");
    record.conversationId = row.at("conversation_id");
    record.payloadFormat = static_cast<uint32_t>(std::stoul(row.at("payload_format")));
    record.content = row.at("content");
    record.messageType = row.at("message_type");
    record.status = static_cast<protocol::DeliveryStatus>(std::stoul(row.at("status")));
    record.createdAtMs = static_cast<int64_t>(std::stoll(row.at("created_at_ms")));
    record.updatedAtMs = static_cast<int64_t>(std::stoll(row.at("updated_at_ms")));
    return record;
}

} // namespace

MessageRepository::MessageRepository(std::shared_ptr<MySqlClient> client)
    : m_client(std::move(client))
{
}

bool MessageRepository::createMessage(const MessageRecord& record, std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "INSERT INTO im_message "
        << "(id, client_msg_id, from_user_id, to_user_id, conversation_id, payload_format, content, "
        << "message_type, status, created_at_ms, updated_at_ms) VALUES ("
        << record.messageId << ", '"
        << m_client->escape(record.clientMsgId) << "', '"
        << m_client->escape(record.fromUserId) << "', '"
        << m_client->escape(record.toUserId) << "', '"
        << m_client->escape(record.conversationId) << "', "
        << record.payloadFormat << ", '"
        << m_client->escape(record.content) << "', '"
        << m_client->escape(record.messageType) << "', "
        << static_cast<uint32_t>(record.status) << ", "
        << record.createdAtMs << ", "
        << record.updatedAtMs << ")";

    if (!m_client->execute(sql.str()))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    errorMsg.clear();
    return true;
}

std::optional<MessageRecord> MessageRepository::findByMessageId(uint64_t messageId, std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    std::ostringstream sql;
    sql << "SELECT id, client_msg_id, from_user_id, to_user_id, conversation_id, payload_format, "
        << "content, message_type, status, created_at_ms, updated_at_ms "
        << "FROM im_message WHERE id=" << messageId << " LIMIT 1";

    if (!m_client->query(sql.str(), rows))
    {
        errorMsg = m_client->lastError();
        return std::nullopt;
    }
    if (rows.empty())
    {
        errorMsg.clear();
        return std::nullopt;
    }

    errorMsg.clear();
    return BuildMessageRecord(rows.front());
}

std::optional<MessageRecord> MessageRepository::findBySenderClientMsgId(const std::string& senderId,
                                                                        const std::string& clientMsgId,
                                                                        std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    std::ostringstream sql;
    sql << "SELECT id, client_msg_id, from_user_id, to_user_id, conversation_id, payload_format, "
        << "content, message_type, status, created_at_ms, updated_at_ms "
        << "FROM im_message WHERE from_user_id='" << m_client->escape(senderId)
        << "' AND client_msg_id='" << m_client->escape(clientMsgId) << "' LIMIT 1";

    if (!m_client->query(sql.str(), rows))
    {
        errorMsg = m_client->lastError();
        return std::nullopt;
    }
    if (rows.empty())
    {
        errorMsg.clear();
        return std::nullopt;
    }

    errorMsg.clear();
    return BuildMessageRecord(rows.front());
}

uint64_t MessageRepository::getMaxMessageId(std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    if (!m_client->query("SELECT COALESCE(MAX(id), 0) AS max_id FROM im_message", rows))
    {
        errorMsg = m_client->lastError();
        return 0;
    }
    errorMsg.clear();
    return rows.empty() ? 0 : ToUint64(rows.front(), "max_id");
}

} // namespace storage
