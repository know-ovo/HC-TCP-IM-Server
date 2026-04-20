#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "codec/protocol.h"

class MySqlClient;

namespace storage
{

struct DeliveryRecord
{
    uint64_t messageId = 0;
    std::string receiverUserId;
    uint64_t serverSeq = 0;
    protocol::DeliveryStatus status = protocol::DeliveryPersisted;
    uint32_t retryCount = 0;
    std::string lastError;
    int64_t deliveredAtMs = 0;
    int64_t ackedAtMs = 0;
    int64_t createdAtMs = 0;
    int64_t updatedAtMs = 0;
};

class DeliveryRepository
{
public:
    explicit DeliveryRepository(std::shared_ptr<MySqlClient> client);

    bool createDelivery(const DeliveryRecord& record, std::string& errorMsg);
    bool updateDeliveryStatus(uint64_t messageId,
                              const std::string& receiverUserId,
                              protocol::DeliveryStatus status,
                              uint32_t retryCount,
                              int64_t deliveredAtMs,
                              int64_t ackedAtMs,
                              std::string& errorMsg);
    std::optional<DeliveryRecord> findByMessageAndReceiver(uint64_t messageId,
                                                           const std::string& receiverUserId,
                                                           std::string& errorMsg);
    std::vector<DeliveryRecord> listPendingByReceiver(const std::string& receiverUserId,
                                                      uint64_t afterSeq,
                                                      size_t limit,
                                                      std::string& errorMsg);
    uint64_t getMaxServerSeq(const std::string& receiverUserId, std::string& errorMsg);
    uint64_t getLastAckedSeq(const std::string& receiverUserId, std::string& errorMsg);
    bool appendAckLog(uint64_t messageId,
                      const std::string& receiverUserId,
                      uint64_t serverSeq,
                      uint32_t ackCode,
                      int64_t ackedAtMs,
                      std::string& errorMsg);

private:
    std::shared_ptr<MySqlClient> m_client;
};

} // namespace storage
