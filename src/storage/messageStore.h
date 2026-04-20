#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "codec/protocol.h"
#include "storage/deliveryRepository.h"
#include "storage/messageRepository.h"

class MySqlClient;

namespace storage
{

struct CreateMessageRequest
{
    std::string fromUserId;
    std::string toUserId;
    std::string clientMsgId;
    std::string content;
    int64_t timestamp = 0;
};

struct StoredMessage
{
    protocol::P2PMsgNotify notify;
    protocol::DeliveryStatus status = protocol::DeliveryPersisted;
    std::string receiverId;
    int64_t createdAt = 0;
    int64_t lastDeliverAt = 0;
    int64_t ackedAt = 0;
    uint32_t retryCount = 0;
};

struct MySqlStoreOptions
{
    bool enabled = false;
    std::string host = "127.0.0.1";
    int port = 3306;
    std::string user;
    std::string password;
    std::string database = "im_server";
    int connectTimeoutMs = 2000;
};

class IMessageStore
{
public:
    virtual ~IMessageStore() = default;

    virtual bool init(std::string& errorMsg) = 0;
    virtual const char* name() const = 0;

    virtual bool saveMessageAndDelivery(const CreateMessageRequest& request,
                                        StoredMessage& outMessage,
                                        bool& outDuplicate,
                                        std::string& errorMsg) = 0;

    virtual bool markDeliveryAttempt(const std::string& receiverId,
                                     uint64_t msgId,
                                     protocol::DeliveryStatus status,
                                     int64_t deliverAt,
                                     uint32_t retryCount,
                                     std::string& errorMsg) = 0;

    virtual bool markAcked(const std::string& receiverId,
                           uint64_t msgId,
                           uint64_t serverSeq,
                           uint32_t ackCode,
                           int64_t ackedAt,
                           uint64_t& outLastAckedSeq,
                           std::string& errorMsg) = 0;

    virtual std::vector<StoredMessage> loadPendingMessages(const std::string& receiverId,
                                                           uint64_t afterServerSeq,
                                                           size_t limit,
                                                           uint64_t& nextBeginSeq,
                                                           std::string& errorMsg) = 0;

    virtual uint64_t getLastAckedSeq(const std::string& receiverId) = 0;
};

class InMemoryMessageStore : public IMessageStore
{
public:
    InMemoryMessageStore();

    bool init(std::string& errorMsg) override;
    const char* name() const override { return "memory"; }

    bool saveMessageAndDelivery(const CreateMessageRequest& request,
                                StoredMessage& outMessage,
                                bool& outDuplicate,
                                std::string& errorMsg) override;

    bool markDeliveryAttempt(const std::string& receiverId,
                             uint64_t msgId,
                             protocol::DeliveryStatus status,
                             int64_t deliverAt,
                             uint32_t retryCount,
                             std::string& errorMsg) override;

    bool markAcked(const std::string& receiverId,
                   uint64_t msgId,
                   uint64_t serverSeq,
                   uint32_t ackCode,
                   int64_t ackedAt,
                   uint64_t& outLastAckedSeq,
                   std::string& errorMsg) override;

    std::vector<StoredMessage> loadPendingMessages(const std::string& receiverId,
                                                   uint64_t afterServerSeq,
                                                   size_t limit,
                                                   uint64_t& nextBeginSeq,
                                                   std::string& errorMsg) override;

    uint64_t getLastAckedSeq(const std::string& receiverId) override;

private:
    std::string makeDedupKey(const std::string& fromUserId, const std::string& clientMsgId) const;

    std::mutex m_mutex;
    uint64_t m_nextMsgId;
    std::unordered_map<std::string, uint64_t> m_nextServerSeqByUser;
    std::unordered_map<std::string, uint64_t> m_lastAckedSeqByUser;
    std::unordered_map<std::string, uint64_t> m_clientMsgDedupIndex;
    std::unordered_map<uint64_t, StoredMessage> m_messages;
    std::unordered_map<std::string, std::vector<uint64_t>> m_pendingMsgIdsByUser;
};

class MySqlMessageStore : public IMessageStore
{
public:
    explicit MySqlMessageStore(MySqlStoreOptions options);
    ~MySqlMessageStore() override;

    bool init(std::string& errorMsg) override;
    const char* name() const override { return "mysql"; }

    bool saveMessageAndDelivery(const CreateMessageRequest& request,
                                StoredMessage& outMessage,
                                bool& outDuplicate,
                                std::string& errorMsg) override;

    bool markDeliveryAttempt(const std::string& receiverId,
                             uint64_t msgId,
                             protocol::DeliveryStatus status,
                             int64_t deliverAt,
                             uint32_t retryCount,
                             std::string& errorMsg) override;

    bool markAcked(const std::string& receiverId,
                   uint64_t msgId,
                   uint64_t serverSeq,
                   uint32_t ackCode,
                   int64_t ackedAt,
                   uint64_t& outLastAckedSeq,
                   std::string& errorMsg) override;

    std::vector<StoredMessage> loadPendingMessages(const std::string& receiverId,
                                                   uint64_t afterServerSeq,
                                                   size_t limit,
                                                   uint64_t& nextBeginSeq,
                                                   std::string& errorMsg) override;

    uint64_t getLastAckedSeq(const std::string& receiverId) override;

private:
    bool loadMaxMessageId(std::string& errorMsg);
    bool ensureServerSeqCursor(const std::string& receiverId, std::string& errorMsg);
    std::optional<StoredMessage> findDuplicateMessage(const CreateMessageRequest& request, std::string& errorMsg);
    bool executeInsertMessage(const StoredMessage& message, std::string& errorMsg);
    bool executeInsertDelivery(const StoredMessage& message, std::string& errorMsg);

    MySqlStoreOptions m_options;
    std::shared_ptr<MySqlClient> m_client;
    std::unique_ptr<MessageRepository> m_messageRepository;
    std::unique_ptr<DeliveryRepository> m_deliveryRepository;
    std::mutex m_mutex;
    uint64_t m_nextMsgId;
    std::unordered_map<std::string, uint64_t> m_nextServerSeqByUser;
    std::unordered_map<std::string, uint64_t> m_lastAckedSeqCache;
};

} // namespace storage
