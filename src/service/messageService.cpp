// MessageService.cpp

#include "service/messageService.h"

#include <algorithm>
#include <chrono>

#include "base/log.h"
#include "base/util.h"
#include "codec/buffer.h"
#include "codec/codec.h"
#include "codec/messageSerializer.h"
#include "codec/protocol.h"
#include "infra/metricsRegistry.h"

namespace
{

bool ShouldRetryMessage(const storage::StoredMessage& message,
                        int64_t nowMs,
                        int ackTimeoutMs,
                        int retryBackoffMs)
{
    if (message.status == protocol::DeliveryPersisted)
    {
        return true;
    }

    if (message.status != protocol::DeliveryDelivering)
    {
        return false;
    }

    const int64_t lastDeliverAt = message.lastDeliverAt > 0 ? message.lastDeliverAt : message.createdAt;
    const int64_t nextRetryDelay = static_cast<int64_t>(ackTimeoutMs) +
                                   static_cast<int64_t>(std::max(0U, message.retryCount > 0 ? message.retryCount - 1 : 0U)) *
                                       retryBackoffMs;
    return nowMs - lastDeliverAt >= nextRetryDelay;
}

} // namespace

MessageService::~MessageService()
{
    stop();
}

void MessageService::start()
{
    if (m_retryRunning.exchange(true))
    {
        return;
    }

    m_retryThread = std::thread([this]() {
        while (m_retryRunning.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_retryIntervalMs));
            if (!m_retryRunning.load())
            {
                break;
            }

            std::vector<std::string> onlineUsers;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                onlineUsers.reserve(m_userToConn.size());
                for (const auto& pair : m_userToConn)
                {
                    if (pair.second && pair.second->connected())
                    {
                        onlineUsers.push_back(pair.first);
                    }
                }
            }

            for (const auto& userId : onlineUsers)
            {
                deliverPendingMessages(userId);
            }
        }
    });

    LOG_INFO("MessageService retry loop started, retry_interval_ms={}, retry_batch_size={}",
             m_retryIntervalMs,
             m_retryBatchSize);
}

void MessageService::stop()
{
    if (!m_retryRunning.exchange(false))
    {
        return;
    }

    if (m_retryThread.joinable())
    {
        m_retryThread.join();
    }

    LOG_INFO("MessageService retry loop stopped");
}

void MessageService::registerConnection(const std::string& userId, const std::shared_ptr<TcpConnection>& conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userToConn[userId] = conn;
    LOG_INFO("MessageService::registerConnection - userId: {}", userId);
}

void MessageService::unregisterConnection(const std::string& userId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userToConn.erase(userId);
    LOG_INFO("MessageService::unregisterConnection - userId: {}", userId);
}

bool MessageService::sendP2PMessage(const std::string& fromUserId,
                                    const std::string& toUserId,
                                    const std::string& content,
                                    uint32_t& outMsgId,
                                    std::string& errorMsg)
{
    uint64_t reliableMsgId = 0;
    uint64_t serverSeq = 0;
    const bool ok = sendReliableP2PMessage(fromUserId,
                                           toUserId,
                                           "",
                                           content,
                                           reliableMsgId,
                                           serverSeq,
                                           errorMsg);
    outMsgId = static_cast<uint32_t>(reliableMsgId);
    (void)serverSeq;
    return ok;
}

bool MessageService::sendReliableP2PMessage(const std::string& fromUserId,
                                            const std::string& toUserId,
                                            const std::string& clientMsgId,
                                            const std::string& content,
                                            uint64_t& outMsgId,
                                            uint64_t& outServerSeq,
                                            std::string& resultMsg)
{
    if (!m_messageStore)
    {
        resultMsg = "message store is not configured";
        return false;
    }

    storage::CreateMessageRequest request;
    request.fromUserId = fromUserId;
    request.toUserId = toUserId;
    request.clientMsgId = clientMsgId;
    request.content = content;
    request.timestamp = util::GetTimestampMs();

    storage::StoredMessage storedMessage;
    bool duplicate = false;
    if (!m_messageStore->saveMessageAndDelivery(request, storedMessage, duplicate, resultMsg))
    {
        return false;
    }

    outMsgId = storedMessage.notify.msgId;
    outServerSeq = storedMessage.notify.serverSeq;

    std::shared_ptr<TcpConnection> conn;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto connIt = m_userToConn.find(toUserId);
        if (connIt != m_userToConn.end())
        {
            conn = connIt->second;
        }
    }

    if (conn && conn->connected())
    {
        std::string deliverError;
        deliverLocked(conn, storedMessage, deliverError);
        resultMsg = deliverError.empty()
            ? (duplicate ? "duplicate request redelivered" : "persisted and delivered")
            : deliverError;
        if (!deliverError.empty())
        {
            return false;
        }
    }
    else
    {
        resultMsg = duplicate ? "duplicate request still pending offline" : "persisted and queued offline";
    }

    LOG_INFO("Reliable P2P persisted - from={}, to={}, msgId={}, serverSeq={}, duplicate={}",
             fromUserId,
             toUserId,
             outMsgId,
             outServerSeq,
             duplicate);
    return true;
}

bool MessageService::processAck(const std::string& userId,
                                uint64_t msgId,
                                uint64_t serverSeq,
                                uint32_t ackCode,
                                uint64_t& outLastAckedSeq,
                                std::string& errorMsg)
{
    if (!m_messageStore)
    {
        errorMsg = "message store is not configured";
        outLastAckedSeq = 0;
        return false;
    }

    const bool ok = m_messageStore->markAcked(userId,
                                              msgId,
                                              serverSeq,
                                              ackCode,
                                              util::GetTimestampMs(),
                                              outLastAckedSeq,
                                              errorMsg);
    if (ok)
    {
        if (m_sessionRepository)
        {
            std::string repoError;
            if (!m_sessionRepository->updateLastAckedSeqByUser(userId, outLastAckedSeq, repoError))
            {
                LOG_WARN("updateLastAckedSeqByUser failed - userId={}, seq={}, error={}",
                         userId,
                         outLastAckedSeq,
                         repoError);
            }
        }
        LOG_INFO("Reliable ACK processed - userId={}, msgId={}, serverSeq={}, ackCode={}, lastAckedSeq={}",
                 userId,
                 msgId,
                 serverSeq,
                 ackCode,
                 outLastAckedSeq);
    }
    return ok;
}

std::vector<protocol::PullOfflineResp::OfflineItem> MessageService::pullOfflineMessages(const std::string& userId,
                                                                                         uint64_t lastAckedSeq,
                                                                                         size_t limit,
                                                                                         uint64_t& nextBeginSeq)
{
    std::vector<protocol::PullOfflineResp::OfflineItem> result;
    nextBeginSeq = lastAckedSeq;

    if (!m_messageStore)
    {
        return result;
    }

    std::string errorMsg;
    const auto storedMessages = m_messageStore->loadPendingMessages(userId,
                                                                    lastAckedSeq,
                                                                    limit,
                                                                    nextBeginSeq,
                                                                    errorMsg);
    if (!errorMsg.empty())
    {
        LOG_WARN("loadPendingMessages failed - userId={}, error={}", userId, errorMsg);
        return result;
    }

    result.reserve(storedMessages.size());
    for (const auto& message : storedMessages)
    {
        protocol::PullOfflineResp::OfflineItem item;
        item.msgId = message.notify.msgId;
        item.serverSeq = message.notify.serverSeq;
        item.fromUserId = message.notify.fromUserId;
        item.toUserId = message.notify.toUserId;
        item.clientMsgId = message.notify.clientMsgId;
        item.content = message.notify.content;
        item.timestamp = message.notify.timestamp;
        result.push_back(std::move(item));
    }
    return result;
}

void MessageService::deliverPendingMessages(const std::string& userId)
{
    if (!m_messageStore)
    {
        return;
    }

    std::shared_ptr<TcpConnection> conn;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto connIt = m_userToConn.find(userId);
        if (connIt != m_userToConn.end())
        {
            conn = connIt->second;
        }
    }

    if (!conn || !conn->connected())
    {
        return;
    }

    std::string errorMsg;
    uint64_t nextBeginSeq = 0;
    const auto pendingMessages = m_messageStore->loadPendingMessages(userId,
                                                                     m_messageStore->getLastAckedSeq(userId),
                                                                     m_retryBatchSize,
                                                                     nextBeginSeq,
                                                                     errorMsg);
    if (!errorMsg.empty())
    {
        LOG_WARN("deliverPendingMessages failed - userId={}, error={}", userId, errorMsg);
        return;
    }

    const int64_t nowMs = util::GetTimestampMs();
    for (auto message : pendingMessages)
    {
        if (message.retryCount >= m_maxRetryCount)
        {
            std::string failError;
            if (!m_messageStore->markDeliveryAttempt(message.receiverId,
                                                     message.notify.msgId,
                                                     protocol::DeliveryFailed,
                                                     message.lastDeliverAt,
                                                     message.retryCount,
                                                     failError))
            {
                LOG_WARN("mark message failed status error - userId={}, msgId={}, error={}",
                         userId,
                         message.notify.msgId,
                         failError);
            }
            else
            {
                LOG_WARN("message marked failed after retry exhaustion - userId={}, msgId={}, serverSeq={}, retryCount={}",
                         userId,
                         message.notify.msgId,
                         message.notify.serverSeq,
                         message.retryCount);
            }
            continue;
        }

        if (!ShouldRetryMessage(message, nowMs, m_ackTimeoutMs, m_retryBackoffMs))
        {
            continue;
        }

        if (message.status == protocol::DeliveryDelivering)
        {
            infra::MetricsRegistry::instance().incCounter("im_message_ack_timeout_total",
                                                          1,
                                                          "Message delivery retries triggered by ACK timeout");
        }

        std::string deliverError;
        deliverLocked(conn, message, deliverError);
        if (!deliverError.empty())
        {
            LOG_WARN("pending message delivery failed - userId={}, msgId={}, error={}",
                     userId,
                     message.notify.msgId,
                     deliverError);
        }
    }
}

void MessageService::broadcastMessage(const std::string& fromUserId, const std::string& content)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    LOG_INFO("MessageService::broadcastMessage - from={}, receivers={}",
             fromUserId,
             m_userToConn.size());

    protocol::BroadcastMsgNotify notify;
    notify.fromUserId = fromUserId;
    notify.content = content;
    notify.timestamp = util::GetTimestampMs();

    const std::string notifyMsg = serializer::Serialize(notify);
    const uint64_t msgId = static_cast<uint64_t>(util::GetTimestampMs());

    for (const auto& pair : m_userToConn)
    {
        const auto& conn = pair.second;
        if (conn && conn->connected())
        {
            Buffer buffer;
            protocol::PacketHeader header;
            header.command = protocol::CmdBroadcastMsgNotify;
            header.flags = protocol::kFlagNone;
            header.requestId = 0;
            header.clientSeq = msgId;
            Codec::pack(&buffer, header, notifyMsg);
            conn->send(&buffer);
        }
    }
}

void MessageService::deliverLocked(const std::shared_ptr<TcpConnection>& conn, storage::StoredMessage& message, std::string& errorMsg)
{
    if (!conn || !conn->connected())
    {
        errorMsg = "connection is offline";
        return;
    }

    const std::string notifyMsg = serializer::Serialize(message.notify);
    Buffer buffer;
    protocol::PacketHeader header;
    header.command = protocol::CmdP2pMsgNotify;
    header.flags = protocol::kFlagNone;
    if (message.retryCount > 0)
    {
        header.flags |= protocol::kFlagRetry;
    }
    header.requestId = 0;
    header.clientSeq = message.notify.msgId;
    header.serverSeq = message.notify.serverSeq;
    Codec::pack(&buffer, header, notifyMsg);

    conn->send(&buffer);

    if (message.retryCount > 0)
    {
        infra::MetricsRegistry::instance().incCounter("im_message_retry_total",
                                                      1,
                                                      "Reliable message retry attempts");
    }
    infra::MetricsRegistry::instance().observe("im_message_latency_ms",
                                               static_cast<double>(util::GetTimestampMs() - message.createdAt),
                                               "Message latency from persist to delivery attempt in milliseconds");

    message.status = protocol::DeliveryDelivering;
    message.lastDeliverAt = util::GetTimestampMs();
    ++message.retryCount;

    if (m_messageStore &&
        !m_messageStore->markDeliveryAttempt(message.receiverId,
                                            message.notify.msgId,
                                            message.status,
                                            message.lastDeliverAt,
                                            message.retryCount,
                                            errorMsg))
    {
        return;
    }

    errorMsg.clear();

    LOG_INFO("Reliable P2P delivered - userId={}, msgId={}, serverSeq={}, retryCount={}",
             message.receiverId,
             message.notify.msgId,
             message.notify.serverSeq,
             message.retryCount);
}
