#include "storage/deliveryRepository.h"

#include <sstream>
#include <unordered_map>

#include "base/util.h"
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

int64_t ToInt64(const std::unordered_map<std::string, std::string>& row, const std::string& key)
{
    auto it = row.find(key);
    if (it == row.end() || it->second.empty())
    {
        return 0;
    }
    return static_cast<int64_t>(std::stoll(it->second));
}

DeliveryRecord BuildDeliveryRecord(const std::unordered_map<std::string, std::string>& row)
{
    DeliveryRecord record;
    record.messageId = ToUint64(row, "message_id");
    record.receiverUserId = row.at("receiver_user_id");
    record.serverSeq = ToUint64(row, "server_seq");
    record.status = static_cast<protocol::DeliveryStatus>(std::stoul(row.at("status")));
    record.retryCount = static_cast<uint32_t>(std::stoul(row.at("retry_count")));
    record.lastError = row.at("last_error");
    record.deliveredAtMs = ToInt64(row, "delivered_at_ms");
    record.ackedAtMs = ToInt64(row, "acked_at_ms");
    record.createdAtMs = ToInt64(row, "created_at_ms");
    record.updatedAtMs = ToInt64(row, "updated_at_ms");
    return record;
}

} // namespace

DeliveryRepository::DeliveryRepository(std::shared_ptr<MySqlClient> client)
    : m_client(std::move(client))
{
}

bool DeliveryRepository::createDelivery(const DeliveryRecord& record, std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "INSERT INTO im_message_delivery "
        << "(message_id, receiver_user_id, server_seq, status, retry_count, last_error, "
        << "delivered_at_ms, acked_at_ms, created_at_ms, updated_at_ms) VALUES ("
        << record.messageId << ", '"
        << m_client->escape(record.receiverUserId) << "', "
        << record.serverSeq << ", "
        << static_cast<uint32_t>(record.status) << ", "
        << record.retryCount << ", '"
        << m_client->escape(record.lastError) << "', "
        << record.deliveredAtMs << ", "
        << record.ackedAtMs << ", "
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

bool DeliveryRepository::updateDeliveryStatus(uint64_t messageId,
                                              const std::string& receiverUserId,
                                              protocol::DeliveryStatus status,
                                              uint32_t retryCount,
                                              int64_t deliveredAtMs,
                                              int64_t ackedAtMs,
                                              std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "UPDATE im_message_delivery SET "
        << "status=" << static_cast<uint32_t>(status) << ", "
        << "retry_count=" << retryCount << ", "
        << "delivered_at_ms=" << deliveredAtMs << ", "
        << "acked_at_ms=" << ackedAtMs << ", "
        << "updated_at_ms=" << util::GetTimestampMs() << " "
        << "WHERE message_id=" << messageId
        << " AND receiver_user_id='" << m_client->escape(receiverUserId) << "'";

    if (!m_client->execute(sql.str()))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    errorMsg.clear();
    return true;
}

std::optional<DeliveryRecord> DeliveryRepository::findByMessageAndReceiver(uint64_t messageId,
                                                                           const std::string& receiverUserId,
                                                                           std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    std::ostringstream sql;
    sql << "SELECT message_id, receiver_user_id, server_seq, status, retry_count, last_error, "
        << "delivered_at_ms, acked_at_ms, created_at_ms, updated_at_ms "
        << "FROM im_message_delivery WHERE message_id=" << messageId
        << " AND receiver_user_id='" << m_client->escape(receiverUserId) << "' LIMIT 1";

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
    return BuildDeliveryRecord(rows.front());
}

std::vector<DeliveryRecord> DeliveryRepository::listPendingByReceiver(const std::string& receiverUserId,
                                                                      uint64_t afterSeq,
                                                                      size_t limit,
                                                                      std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    std::ostringstream sql;
    sql << "SELECT message_id, receiver_user_id, server_seq, status, retry_count, last_error, "
        << "delivered_at_ms, acked_at_ms, created_at_ms, updated_at_ms "
        << "FROM im_message_delivery WHERE receiver_user_id='" << m_client->escape(receiverUserId) << "' "
        << "AND server_seq > " << afterSeq << " "
        << "AND status IN (" << static_cast<uint32_t>(protocol::DeliveryPersisted) << ", "
        << static_cast<uint32_t>(protocol::DeliveryDelivering) << ") "
        << "ORDER BY server_seq ASC LIMIT " << limit;

    if (!m_client->query(sql.str(), rows))
    {
        errorMsg = m_client->lastError();
        return {};
    }

    std::vector<DeliveryRecord> result;
    result.reserve(rows.size());
    for (const auto& row : rows)
    {
        result.push_back(BuildDeliveryRecord(row));
    }

    errorMsg.clear();
    return result;
}

uint64_t DeliveryRepository::getMaxServerSeq(const std::string& receiverUserId, std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    std::ostringstream sql;
    sql << "SELECT COALESCE(MAX(server_seq), 0) AS max_server_seq "
        << "FROM im_message_delivery WHERE receiver_user_id='" << m_client->escape(receiverUserId) << "'";

    if (!m_client->query(sql.str(), rows))
    {
        errorMsg = m_client->lastError();
        return 0;
    }

    errorMsg.clear();
    return rows.empty() ? 0 : ToUint64(rows.front(), "max_server_seq");
}

uint64_t DeliveryRepository::getLastAckedSeq(const std::string& receiverUserId, std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    std::ostringstream sql;
    sql << "SELECT COALESCE(MAX(server_seq), 0) AS last_acked_seq "
        << "FROM im_message_delivery WHERE receiver_user_id='" << m_client->escape(receiverUserId) << "' "
        << "AND status IN (" << static_cast<uint32_t>(protocol::DeliveryDelivered) << ", "
        << static_cast<uint32_t>(protocol::DeliveryRead) << ")";

    if (!m_client->query(sql.str(), rows))
    {
        errorMsg = m_client->lastError();
        return 0;
    }

    errorMsg.clear();
    return rows.empty() ? 0 : ToUint64(rows.front(), "last_acked_seq");
}

bool DeliveryRepository::appendAckLog(uint64_t messageId,
                                      const std::string& receiverUserId,
                                      uint64_t serverSeq,
                                      uint32_t ackCode,
                                      int64_t ackedAtMs,
                                      std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "INSERT INTO im_message_ack_log "
        << "(message_id, receiver_user_id, server_seq, ack_code, acked_at_ms) VALUES ("
        << messageId << ", '"
        << m_client->escape(receiverUserId) << "', "
        << serverSeq << ", "
        << ackCode << ", "
        << ackedAtMs << ")";

    if (!m_client->execute(sql.str()))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    errorMsg.clear();
    return true;
}

} // namespace storage
