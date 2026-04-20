#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "codec/protocol.h"

class MySqlClient;

namespace storage
{

struct MessageRecord
{
    uint64_t messageId = 0;
    std::string clientMsgId;
    std::string fromUserId;
    std::string toUserId;
    std::string conversationId;
    uint32_t payloadFormat = static_cast<uint32_t>(protocol::PayloadFormat::Json);
    std::string content;
    std::string messageType = "text";
    protocol::DeliveryStatus status = protocol::DeliveryPersisted;
    int64_t createdAtMs = 0;
    int64_t updatedAtMs = 0;
};

class MessageRepository
{
public:
    explicit MessageRepository(std::shared_ptr<MySqlClient> client);

    bool createMessage(const MessageRecord& record, std::string& errorMsg);
    std::optional<MessageRecord> findByMessageId(uint64_t messageId, std::string& errorMsg);
    std::optional<MessageRecord> findBySenderClientMsgId(const std::string& senderId,
                                                         const std::string& clientMsgId,
                                                         std::string& errorMsg);
    uint64_t getMaxMessageId(std::string& errorMsg);

private:
    std::shared_ptr<MySqlClient> m_client;
};

} // namespace storage
