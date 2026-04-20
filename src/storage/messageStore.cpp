#include "storage/messageStore.h"

#include <algorithm>
#include <sstream>

#include "base/util.h"
#include "storage/mysqlClient.h"

namespace storage
{

namespace
{

uint64_t ToUint64(const std::string& value)
{
    if (value.empty())
    {
        return 0;
    }
    return static_cast<uint64_t>(std::stoull(value));
}

uint32_t ToUint32(const std::string& value)
{
    if (value.empty())
    {
        return 0;
    }
    return static_cast<uint32_t>(std::stoul(value));
}

int64_t ToInt64(const std::string& value)
{
    if (value.empty())
    {
        return 0;
    }
    return static_cast<int64_t>(std::stoll(value));
}

StoredMessage BuildStoredMessageFromRow(const MySqlClient::Row& row)
{
    StoredMessage message;
    message.notify.msgId = ToUint64(row.at("msg_id"));
    message.notify.serverSeq = ToUint64(row.at("server_seq"));
    message.notify.fromUserId = row.at("from_user_id");
    message.notify.toUserId = row.at("to_user_id");
    message.notify.clientMsgId = row.at("client_msg_id");
    message.notify.content = row.at("content");
    message.notify.timestamp = ToInt64(row.at("created_at_ms"));
    message.receiverId = row.at("receiver_user_id");
    message.createdAt = ToInt64(row.at("created_at_ms"));
    message.lastDeliverAt = ToInt64(row.at("delivered_at_ms"));
    message.ackedAt = ToInt64(row.at("acked_at_ms"));
    message.retryCount = ToUint32(row.at("retry_count"));
    message.status = static_cast<protocol::DeliveryStatus>(ToUint32(row.at("status")));
    return message;
}

} // namespace

InMemoryMessageStore::InMemoryMessageStore()
    : m_nextMsgId(1)
{
}

bool InMemoryMessageStore::init(std::string& errorMsg)
{
    errorMsg.clear();
    return true;
}

bool InMemoryMessageStore::saveMessageAndDelivery(const CreateMessageRequest& request,
                                                  StoredMessage& outMessage,
                                                  bool& outDuplicate,
                                                  std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string dedupKey = makeDedupKey(request.fromUserId, request.clientMsgId);
    if (!dedupKey.empty())
    {
        const auto dedupIt = m_clientMsgDedupIndex.find(dedupKey);
        if (dedupIt != m_clientMsgDedupIndex.end())
        {
            const auto messageIt = m_messages.find(dedupIt->second);
            if (messageIt != m_messages.end())
            {
                outMessage = messageIt->second;
                outDuplicate = true;
                errorMsg.clear();
                return true;
            }
        }
    }

    StoredMessage message;
    message.createdAt = request.timestamp;
    message.receiverId = request.toUserId;
    message.notify.msgId = m_nextMsgId++;
    message.notify.serverSeq = ++m_nextServerSeqByUser[request.toUserId];
    message.notify.fromUserId = request.fromUserId;
    message.notify.toUserId = request.toUserId;
    message.notify.clientMsgId = request.clientMsgId;
    message.notify.content = request.content;
    message.notify.timestamp = request.timestamp;
    message.status = protocol::DeliveryPersisted;

    m_messages[message.notify.msgId] = message;
    m_pendingMsgIdsByUser[request.toUserId].push_back(message.notify.msgId);
    if (!dedupKey.empty())
    {
        m_clientMsgDedupIndex[dedupKey] = message.notify.msgId;
    }

    outMessage = message;
    outDuplicate = false;
    errorMsg.clear();
    return true;
}

bool InMemoryMessageStore::markDeliveryAttempt(const std::string& receiverId,
                                               uint64_t msgId,
                                               protocol::DeliveryStatus status,
                                               int64_t deliverAt,
                                               uint32_t retryCount,
                                               std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto messageIt = m_messages.find(msgId);
    if (messageIt == m_messages.end())
    {
        errorMsg = "message not found";
        return false;
    }

    if (messageIt->second.receiverId != receiverId)
    {
        errorMsg = "receiver mismatch";
        return false;
    }

    messageIt->second.status = status;
    messageIt->second.lastDeliverAt = deliverAt;
    messageIt->second.retryCount = retryCount;
    errorMsg.clear();
    return true;
}

bool InMemoryMessageStore::markAcked(const std::string& receiverId,
                                     uint64_t msgId,
                                     uint64_t serverSeq,
                                     uint32_t ackCode,
                                     int64_t ackedAt,
                                     uint64_t& outLastAckedSeq,
                                     std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto messageIt = m_messages.find(msgId);
    if (messageIt == m_messages.end())
    {
        errorMsg = "message not found";
        outLastAckedSeq = m_lastAckedSeqByUser[receiverId];
        return false;
    }

    StoredMessage& message = messageIt->second;
    if (message.receiverId != receiverId)
    {
        errorMsg = "ack user mismatch";
        outLastAckedSeq = m_lastAckedSeqByUser[receiverId];
        return false;
    }

    if (message.notify.serverSeq != serverSeq)
    {
        errorMsg = "server_seq mismatch";
        outLastAckedSeq = m_lastAckedSeqByUser[receiverId];
        return false;
    }

    message.ackedAt = ackedAt;
    message.status = (ackCode == protocol::AckRead) ? protocol::DeliveryRead : protocol::DeliveryDelivered;
    m_lastAckedSeqByUser[receiverId] = std::max(m_lastAckedSeqByUser[receiverId], serverSeq);
    outLastAckedSeq = m_lastAckedSeqByUser[receiverId];

    auto& pendingIds = m_pendingMsgIdsByUser[receiverId];
    pendingIds.erase(std::remove(pendingIds.begin(), pendingIds.end(), msgId), pendingIds.end());

    errorMsg.clear();
    return true;
}

std::vector<StoredMessage> InMemoryMessageStore::loadPendingMessages(const std::string& receiverId,
                                                                     uint64_t afterServerSeq,
                                                                     size_t limit,
                                                                     uint64_t& nextBeginSeq,
                                                                     std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<StoredMessage> result;
    nextBeginSeq = afterServerSeq;
    errorMsg.clear();

    const auto pendingIt = m_pendingMsgIdsByUser.find(receiverId);
    if (pendingIt == m_pendingMsgIdsByUser.end())
    {
        return result;
    }

    for (uint64_t msgId : pendingIt->second)
    {
        const auto messageIt = m_messages.find(msgId);
        if (messageIt == m_messages.end())
        {
            continue;
        }

        const StoredMessage& message = messageIt->second;
        if (message.notify.serverSeq <= afterServerSeq ||
            message.status == protocol::DeliveryDelivered ||
            message.status == protocol::DeliveryRead ||
            message.status == protocol::DeliveryFailed)
        {
            continue;
        }

        result.push_back(message);
        nextBeginSeq = std::max(nextBeginSeq, message.notify.serverSeq);
        if (result.size() >= limit)
        {
            break;
        }
    }

    if (!result.empty())
    {
        ++nextBeginSeq;
    }
    return result;
}

uint64_t InMemoryMessageStore::getLastAckedSeq(const std::string& receiverId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_lastAckedSeqByUser.find(receiverId);
    return it == m_lastAckedSeqByUser.end() ? 0 : it->second;
}

std::string InMemoryMessageStore::makeDedupKey(const std::string& fromUserId, const std::string& clientMsgId) const
{
    if (clientMsgId.empty())
    {
        return "";
    }
    return fromUserId + "#" + clientMsgId;
}

MySqlMessageStore::MySqlMessageStore(MySqlStoreOptions options)
    : m_options(std::move(options))
    , m_client(std::make_shared<MySqlClient>())
    , m_nextMsgId(1)
{
}

MySqlMessageStore::~MySqlMessageStore() = default;

bool MySqlMessageStore::init(std::string& errorMsg)
{
    if (!m_client->connect(m_options.host,
                           m_options.port,
                           m_options.user,
                           m_options.password,
                           m_options.database,
                           m_options.connectTimeoutMs))
    {
        errorMsg = m_client->lastError();
        return false;
    }

    m_messageRepository = std::make_unique<MessageRepository>(m_client);
    m_deliveryRepository = std::make_unique<DeliveryRepository>(m_client);
    return loadMaxMessageId(errorMsg);
}

bool MySqlMessageStore::saveMessageAndDelivery(const CreateMessageRequest& request,
                                               StoredMessage& outMessage,
                                               bool& outDuplicate,
                                               std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (auto existing = findDuplicateMessage(request, errorMsg))
    {
        outMessage = *existing;
        outDuplicate = true;
        return true;
    }
    if (!errorMsg.empty())
    {
        return false;
    }

    if (!ensureServerSeqCursor(request.toUserId, errorMsg))
    {
        return false;
    }

    StoredMessage message;
    message.createdAt = request.timestamp;
    message.receiverId = request.toUserId;
    message.notify.msgId = m_nextMsgId++;
    message.notify.serverSeq = ++m_nextServerSeqByUser[request.toUserId];
    message.notify.fromUserId = request.fromUserId;
    message.notify.toUserId = request.toUserId;
    message.notify.clientMsgId = request.clientMsgId;
    message.notify.content = request.content;
    message.notify.timestamp = request.timestamp;
    message.status = protocol::DeliveryPersisted;

    if (!m_client->begin())
    {
        errorMsg = m_client->lastError();
        return false;
    }

    if (!executeInsertMessage(message, errorMsg) || !executeInsertDelivery(message, errorMsg))
    {
        m_client->rollback();
        return false;
    }

    if (!m_client->commit())
    {
        errorMsg = m_client->lastError();
        m_client->rollback();
        return false;
    }

    outMessage = message;
    outDuplicate = false;
    errorMsg.clear();
    return true;
}

bool MySqlMessageStore::markDeliveryAttempt(const std::string& receiverId,
                                            uint64_t msgId,
                                            protocol::DeliveryStatus status,
                                            int64_t deliverAt,
                                            uint32_t retryCount,
                                            std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_deliveryRepository->updateDeliveryStatus(msgId,
                                                      receiverId,
                                                      status,
                                                      retryCount,
                                                      deliverAt,
                                                      0,
                                                      errorMsg);
}

bool MySqlMessageStore::markAcked(const std::string& receiverId,
                                  uint64_t msgId,
                                  uint64_t serverSeq,
                                  uint32_t ackCode,
                                  int64_t ackedAt,
                                  uint64_t& outLastAckedSeq,
                                  std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto delivery = m_deliveryRepository->findByMessageAndReceiver(msgId, receiverId, errorMsg);
    if (!delivery && errorMsg.empty())
    {
        errorMsg = "message not found";
        outLastAckedSeq = getLastAckedSeq(receiverId);
        return false;
    }
    if (!delivery)
    {
        outLastAckedSeq = getLastAckedSeq(receiverId);
        return false;
    }
    if (delivery->serverSeq != serverSeq)
    {
        errorMsg = "server_seq mismatch";
        outLastAckedSeq = getLastAckedSeq(receiverId);
        return false;
    }

    const protocol::DeliveryStatus ackStatus =
        (ackCode == protocol::AckRead) ? protocol::DeliveryRead : protocol::DeliveryDelivered;

    if (!m_client->begin())
    {
        errorMsg = m_client->lastError();
        outLastAckedSeq = getLastAckedSeq(receiverId);
        return false;
    }

    if (!m_deliveryRepository->updateDeliveryStatus(msgId,
                                                    receiverId,
                                                    ackStatus,
                                                    delivery->retryCount,
                                                    delivery->deliveredAtMs,
                                                    ackedAt,
                                                    errorMsg) ||
        !m_deliveryRepository->appendAckLog(msgId, receiverId, serverSeq, ackCode, ackedAt, errorMsg))
    {
        m_client->rollback();
        outLastAckedSeq = getLastAckedSeq(receiverId);
        return false;
    }

    if (!m_client->commit())
    {
        errorMsg = m_client->lastError();
        m_client->rollback();
        outLastAckedSeq = getLastAckedSeq(receiverId);
        return false;
    }

    m_lastAckedSeqCache[receiverId] = std::max(m_lastAckedSeqCache[receiverId], serverSeq);
    outLastAckedSeq = m_lastAckedSeqCache[receiverId];
    errorMsg.clear();
    return true;
}

std::vector<StoredMessage> MySqlMessageStore::loadPendingMessages(const std::string& receiverId,
                                                                  uint64_t afterServerSeq,
                                                                  size_t limit,
                                                                  uint64_t& nextBeginSeq,
                                                                  std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto deliveries = m_deliveryRepository->listPendingByReceiver(receiverId, afterServerSeq, limit, errorMsg);
    if (!errorMsg.empty())
    {
        nextBeginSeq = afterServerSeq;
        return {};
    }
    std::vector<StoredMessage> result;
    nextBeginSeq = afterServerSeq;
    for (const auto& delivery : deliveries)
    {
        auto message = m_messageRepository->findByMessageId(delivery.messageId, errorMsg);
        if (!message)
        {
            if (!errorMsg.empty())
            {
                nextBeginSeq = afterServerSeq;
                return {};
            }
            continue;
        }

        StoredMessage item;
        item.notify.msgId = message->messageId;
        item.notify.serverSeq = delivery.serverSeq;
        item.notify.fromUserId = message->fromUserId;
        item.notify.toUserId = message->toUserId;
        item.notify.clientMsgId = message->clientMsgId;
        item.notify.content = message->content;
        item.notify.timestamp = message->createdAtMs;
        item.receiverId = delivery.receiverUserId;
        item.createdAt = message->createdAtMs;
        item.lastDeliverAt = delivery.deliveredAtMs;
        item.ackedAt = delivery.ackedAtMs;
        item.retryCount = delivery.retryCount;
        item.status = delivery.status;
        result.push_back(std::move(item));
        nextBeginSeq = std::max(nextBeginSeq, delivery.serverSeq);
    }
    if (!result.empty())
    {
        ++nextBeginSeq;
    }

    errorMsg.clear();
    return result;
}

uint64_t MySqlMessageStore::getLastAckedSeq(const std::string& receiverId)
{
    const auto cacheIt = m_lastAckedSeqCache.find(receiverId);
    if (cacheIt != m_lastAckedSeqCache.end())
    {
        return cacheIt->second;
    }

    std::string errorMsg;
    const uint64_t seq = m_deliveryRepository ? m_deliveryRepository->getLastAckedSeq(receiverId, errorMsg) : 0;
    if (!errorMsg.empty())
    {
        return 0;
    }
    m_lastAckedSeqCache[receiverId] = seq;
    return seq;
}

bool MySqlMessageStore::loadMaxMessageId(std::string& errorMsg)
{
    const uint64_t maxId = m_messageRepository->getMaxMessageId(errorMsg);
    if (!errorMsg.empty())
    {
        return false;
    }
    m_nextMsgId = maxId + 1;
    errorMsg.clear();
    return true;
}

bool MySqlMessageStore::ensureServerSeqCursor(const std::string& receiverId, std::string& errorMsg)
{
    if (m_nextServerSeqByUser.find(receiverId) != m_nextServerSeqByUser.end())
    {
        return true;
    }

    m_nextServerSeqByUser[receiverId] = m_deliveryRepository->getMaxServerSeq(receiverId, errorMsg);
    if (!errorMsg.empty())
    {
        return false;
    }
    errorMsg.clear();
    return true;
}

std::optional<StoredMessage> MySqlMessageStore::findDuplicateMessage(const CreateMessageRequest& request, std::string& errorMsg)
{
    errorMsg.clear();
    if (request.clientMsgId.empty())
    {
        return std::nullopt;
    }

    const auto message = m_messageRepository->findBySenderClientMsgId(request.fromUserId,
                                                                      request.clientMsgId,
                                                                      errorMsg);
    if (!message)
    {
        if (!errorMsg.empty())
        {
            return std::nullopt;
        }
        return std::nullopt;
    }

    const auto delivery = m_deliveryRepository->findByMessageAndReceiver(message->messageId,
                                                                         request.toUserId,
                                                                         errorMsg);
    if (!delivery)
    {
        return std::nullopt;
    }

    StoredMessage storedMessage;
    storedMessage.notify.msgId = message->messageId;
    storedMessage.notify.serverSeq = delivery->serverSeq;
    storedMessage.notify.fromUserId = message->fromUserId;
    storedMessage.notify.toUserId = message->toUserId;
    storedMessage.notify.clientMsgId = message->clientMsgId;
    storedMessage.notify.content = message->content;
    storedMessage.notify.timestamp = message->createdAtMs;
    storedMessage.receiverId = delivery->receiverUserId;
    storedMessage.createdAt = message->createdAtMs;
    storedMessage.lastDeliverAt = delivery->deliveredAtMs;
    storedMessage.ackedAt = delivery->ackedAtMs;
    storedMessage.retryCount = delivery->retryCount;
    storedMessage.status = delivery->status;
    errorMsg.clear();
    return storedMessage;
}

bool MySqlMessageStore::executeInsertMessage(const StoredMessage& message, std::string& errorMsg)
{
    MessageRecord record;
    record.messageId = message.notify.msgId;
    record.clientMsgId = message.notify.clientMsgId;
    record.fromUserId = message.notify.fromUserId;
    record.toUserId = message.notify.toUserId;
    record.conversationId = message.notify.fromUserId + ":" + message.notify.toUserId;
    record.payloadFormat = static_cast<uint32_t>(protocol::PayloadFormat::Json);
    record.content = message.notify.content;
    record.messageType = "text";
    record.status = message.status;
    record.createdAtMs = message.createdAt;
    record.updatedAtMs = message.createdAt;
    return m_messageRepository->createMessage(record, errorMsg);
}

bool MySqlMessageStore::executeInsertDelivery(const StoredMessage& message, std::string& errorMsg)
{
    DeliveryRecord record;
    record.messageId = message.notify.msgId;
    record.receiverUserId = message.receiverId;
    record.serverSeq = message.notify.serverSeq;
    record.status = message.status;
    record.retryCount = message.retryCount;
    record.lastError.clear();
    record.deliveredAtMs = message.lastDeliverAt;
    record.ackedAtMs = message.ackedAt;
    record.createdAtMs = message.createdAt;
    record.updatedAtMs = message.createdAt;
    return m_deliveryRepository->createDelivery(record, errorMsg);
}

} // namespace storage
