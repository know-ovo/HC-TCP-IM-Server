#include "storage/sessionRepository.h"

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

int64_t ToInt64(const std::unordered_map<std::string, std::string>& row, const std::string& key)
{
    auto it = row.find(key);
    if (it == row.end() || it->second.empty())
    {
        return 0;
    }
    return static_cast<int64_t>(std::stoll(it->second));
}

SessionRecord BuildSessionRecord(const std::unordered_map<std::string, std::string>& row)
{
    SessionRecord record;
    record.sessionId = row.at("session_id");
    record.userId = row.at("user_id");
    record.deviceId = row.at("device_id");
    record.serverNodeId = row.at("server_node_id");
    record.lastAckedSeq = ToUint64(row, "last_acked_seq");
    record.createdAtMs = ToInt64(row, "created_at_ms");
    record.expiresAtMs = ToInt64(row, "expires_at_ms");
    return record;
}

} // namespace

SessionRepository::SessionRepository(std::shared_ptr<MySqlClient> client)
    : m_client(std::move(client))
{
}

bool SessionRepository::createSession(const SessionRecord& record, std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "INSERT INTO im_session "
        << "(session_id, user_id, device_id, server_node_id, last_acked_seq, created_at_ms, expires_at_ms) VALUES ('"
        << m_client->escape(record.sessionId) << "', '"
        << m_client->escape(record.userId) << "', '"
        << m_client->escape(record.deviceId) << "', '"
        << m_client->escape(record.serverNodeId) << "', "
        << record.lastAckedSeq << ", "
        << record.createdAtMs << ", "
        << record.expiresAtMs << ") "
        << "ON DUPLICATE KEY UPDATE "
        << "user_id=VALUES(user_id), "
        << "device_id=VALUES(device_id), "
        << "server_node_id=VALUES(server_node_id), "
        << "last_acked_seq=VALUES(last_acked_seq), "
        << "created_at_ms=VALUES(created_at_ms), "
        << "expires_at_ms=VALUES(expires_at_ms)";

    if (!m_client->execute(sql.str()))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    errorMsg.clear();
    return true;
}

bool SessionRepository::deleteSession(const std::string& sessionId, std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "DELETE FROM im_session WHERE session_id='" << m_client->escape(sessionId) << "'";
    if (!m_client->execute(sql.str()))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    errorMsg.clear();
    return true;
}

std::optional<SessionRecord> SessionRepository::findBySessionId(const std::string& sessionId, std::string& errorMsg)
{
    std::vector<MySqlClient::Row> rows;
    std::ostringstream sql;
    sql << "SELECT session_id, user_id, device_id, server_node_id, last_acked_seq, created_at_ms, expires_at_ms "
        << "FROM im_session WHERE session_id='" << m_client->escape(sessionId) << "' LIMIT 1";

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
    return BuildSessionRecord(rows.front());
}

bool SessionRepository::updateLastAckedSeq(const std::string& sessionId, uint64_t seq, std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "UPDATE im_session SET last_acked_seq=GREATEST(last_acked_seq, " << seq
        << ") WHERE session_id='" << m_client->escape(sessionId) << "'";

    if (!m_client->execute(sql.str()))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    errorMsg.clear();
    return true;
}

bool SessionRepository::updateLastAckedSeqByUser(const std::string& userId, uint64_t seq, std::string& errorMsg)
{
    std::ostringstream sql;
    sql << "UPDATE im_session SET last_acked_seq=GREATEST(last_acked_seq, " << seq
        << ") WHERE user_id='" << m_client->escape(userId) << "'";

    if (!m_client->execute(sql.str()))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    errorMsg.clear();
    return true;
}

} // namespace storage
