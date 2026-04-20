#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class MySqlClient;

namespace storage
{

struct SessionRecord
{
    std::string sessionId;
    std::string userId;
    std::string deviceId;
    std::string serverNodeId = "single-node";
    uint64_t lastAckedSeq = 0;
    int64_t createdAtMs = 0;
    int64_t expiresAtMs = 0;
};

class SessionRepository
{
public:
    explicit SessionRepository(std::shared_ptr<MySqlClient> client);

    bool createSession(const SessionRecord& record, std::string& errorMsg);
    bool deleteSession(const std::string& sessionId, std::string& errorMsg);
    std::optional<SessionRecord> findBySessionId(const std::string& sessionId, std::string& errorMsg);
    bool updateLastAckedSeq(const std::string& sessionId, uint64_t seq, std::string& errorMsg);
    bool updateLastAckedSeqByUser(const std::string& userId, uint64_t seq, std::string& errorMsg);

private:
    std::shared_ptr<MySqlClient> m_client;
};

} // namespace storage
