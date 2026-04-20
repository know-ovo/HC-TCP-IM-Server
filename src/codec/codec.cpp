// Codec.cpp
#include "codec/codec.h"
#include "net/tcpConnection.h"
#include "base/log.h"
#include "base/util.h"
#include <cstring>

namespace
{

constexpr uint32_t kMagicFieldOffset = 0;
constexpr uint32_t kVersionFieldOffset = 4;
constexpr uint32_t kHeaderLenFieldOffset = 6;
constexpr uint32_t kTotalLenFieldOffset = 8;
constexpr uint32_t kCommandFieldOffset = 12;
constexpr uint32_t kFlagsFieldOffset = 14;
constexpr uint32_t kRequestIdFieldOffset = 16;
constexpr uint32_t kClientSeqFieldOffset = 20;
constexpr uint32_t kServerSeqFieldOffset = 28;
constexpr uint32_t kErrorCodeFieldOffset = 36;
constexpr uint32_t kChecksumFieldOffset = 40;

void WriteHeader(char* out, const protocol::PacketHeader& header)
{
    uint32_t magic = util::HostToNetwork32(header.magic);
    uint16_t version = util::HostToNetwork16(header.version);
    uint16_t headerLen = util::HostToNetwork16(header.headerLen);
    uint32_t totalLen = util::HostToNetwork32(header.totalLen);
    uint16_t command = util::HostToNetwork16(header.command);
    uint16_t flags = util::HostToNetwork16(header.flags);
    uint32_t requestId = util::HostToNetwork32(header.requestId);
    uint64_t clientSeq = util::HostToNetwork64(header.clientSeq);
    uint64_t serverSeq = util::HostToNetwork64(header.serverSeq);
    uint32_t errorCode = util::HostToNetwork32(header.errorCode);
    uint32_t checksum = util::HostToNetwork32(header.checksum);

    memcpy(out + kMagicFieldOffset, &magic, sizeof(magic));
    memcpy(out + kVersionFieldOffset, &version, sizeof(version));
    memcpy(out + kHeaderLenFieldOffset, &headerLen, sizeof(headerLen));
    memcpy(out + kTotalLenFieldOffset, &totalLen, sizeof(totalLen));
    memcpy(out + kCommandFieldOffset, &command, sizeof(command));
    memcpy(out + kFlagsFieldOffset, &flags, sizeof(flags));
    memcpy(out + kRequestIdFieldOffset, &requestId, sizeof(requestId));
    memcpy(out + kClientSeqFieldOffset, &clientSeq, sizeof(clientSeq));
    memcpy(out + kServerSeqFieldOffset, &serverSeq, sizeof(serverSeq));
    memcpy(out + kErrorCodeFieldOffset, &errorCode, sizeof(errorCode));
    memcpy(out + kChecksumFieldOffset, &checksum, sizeof(checksum));
}

protocol::PacketHeader ReadHeader(const char* data)
{
    protocol::PacketHeader header;

    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t headerLen = 0;
    uint32_t totalLen = 0;
    uint16_t command = 0;
    uint16_t flags = 0;
    uint32_t requestId = 0;
    uint64_t clientSeq = 0;
    uint64_t serverSeq = 0;
    uint32_t errorCode = 0;
    uint32_t checksum = 0;

    memcpy(&magic, data + kMagicFieldOffset, sizeof(magic));
    memcpy(&version, data + kVersionFieldOffset, sizeof(version));
    memcpy(&headerLen, data + kHeaderLenFieldOffset, sizeof(headerLen));
    memcpy(&totalLen, data + kTotalLenFieldOffset, sizeof(totalLen));
    memcpy(&command, data + kCommandFieldOffset, sizeof(command));
    memcpy(&flags, data + kFlagsFieldOffset, sizeof(flags));
    memcpy(&requestId, data + kRequestIdFieldOffset, sizeof(requestId));
    memcpy(&clientSeq, data + kClientSeqFieldOffset, sizeof(clientSeq));
    memcpy(&serverSeq, data + kServerSeqFieldOffset, sizeof(serverSeq));
    memcpy(&errorCode, data + kErrorCodeFieldOffset, sizeof(errorCode));
    memcpy(&checksum, data + kChecksumFieldOffset, sizeof(checksum));

    header.magic = util::NetworkToHost32(magic);
    header.version = util::NetworkToHost16(version);
    header.headerLen = util::NetworkToHost16(headerLen);
    header.totalLen = util::NetworkToHost32(totalLen);
    header.command = util::NetworkToHost16(command);
    header.flags = util::NetworkToHost16(flags);
    header.requestId = util::NetworkToHost32(requestId);
    header.clientSeq = util::NetworkToHost64(clientSeq);
    header.serverSeq = util::NetworkToHost64(serverSeq);
    header.errorCode = util::NetworkToHost32(errorCode);
    header.checksum = util::NetworkToHost32(checksum);
    return header;
}

}

Codec::Codec()
    : m_state(kExpectHeader)
    , m_expectedLength(protocol::kHeaderSize)
{
}

void Codec::pack(Buffer* buffer,
                 uint16_t command,
                 uint32_t seqId,
                 const std::string& message)
{
    protocol::PacketHeader header;
    header.command = command;
    header.flags = protocol::GuessFlagsFromCommand(command);
    header.requestId = seqId;
    header.clientSeq = seqId;
    header.totalLen = protocol::kHeaderSize + static_cast<uint32_t>(message.size());
    header.checksum = protocol::CalcCRC16(message.data(), message.size());
    pack(buffer, header, message);
}

void Codec::pack(Buffer* buffer,
                 const protocol::PacketHeader& rawHeader,
                 const std::string& message)
{
    protocol::PacketHeader header = rawHeader;
    header.magic = protocol::kMagicNumber;
    header.version = protocol::kProtocolVersionV1;
    header.headerLen = protocol::kHeaderSize;
    header.totalLen = protocol::kHeaderSize + static_cast<uint32_t>(message.size());
    header.checksum = protocol::CalcCRC16(message.data(), message.size());

    buffer->ensureWritableBytes(protocol::kHeaderSize);
    char* out = buffer->beginWrite();
    WriteHeader(out, header);
    buffer->hasWritten(protocol::kHeaderSize);
    buffer->append(message.data(), message.size());
}

void Codec::onMessage(const std::shared_ptr<TcpConnection>& conn,
                      Buffer* buffer,
                      Timestamp receiveTime)
{
    (void)receiveTime;
    while (true)
    {
        if (m_state == kExpectHeader)
        {
            if (buffer->readableBytes() < protocol::kHeaderSize)
            {
                break;
            }
            if (!parseHeader(buffer))
            {
                break;
            }
        }

        if (m_state == kExpectPayload)
        {
            if (buffer->readableBytes() < m_expectedLength)
            {
                break;
            }
            if (!parsePayload(conn, buffer))
            {
                break;
            }
        }
    }
}

bool Codec::parseHeader(Buffer* buffer)
{
    m_currentHeader = ReadHeader(buffer->peek());

    if (m_currentHeader.magic != protocol::kMagicNumber)
    {
        LOG_ERROR("Codec::parseHeader invalid magic: 0x{:08X}", m_currentHeader.magic);
        if (m_errorCallback)
        {
            m_errorCallback(nullptr, protocol::ErrInvalidPacket);
        }
        buffer->retrieveAll();
        m_state = kExpectHeader;
        m_expectedLength = protocol::kHeaderSize;
        return false;
    }

    if (m_currentHeader.version != protocol::kProtocolVersionV1)
    {
        LOG_ERROR("Codec::parseHeader unsupported version: {}", m_currentHeader.version);
        if (m_errorCallback)
        {
            m_errorCallback(nullptr, protocol::ErrUnsupportedVersion);
        }
        buffer->retrieveAll();
        m_state = kExpectHeader;
        m_expectedLength = protocol::kHeaderSize;
        return false;
    }

    if (m_currentHeader.headerLen != protocol::kHeaderSize ||
        m_currentHeader.totalLen < protocol::kHeaderSize ||
        m_currentHeader.totalLen > protocol::kMaxMessageSize)
    {
        LOG_ERROR("Codec::parseHeader invalid lengths: headerLen={}, totalLen={}",
                  m_currentHeader.headerLen, m_currentHeader.totalLen);
        if (m_errorCallback)
        {
            m_errorCallback(nullptr, protocol::ErrPayloadTooLarge);
        }
        buffer->retrieveAll();
        m_state = kExpectHeader;
        m_expectedLength = protocol::kHeaderSize;
        return false;
    }

    m_expectedLength = m_currentHeader.totalLen;
    m_state = kExpectPayload;
    return true;
}

bool Codec::parsePayload(const std::shared_ptr<TcpConnection>& conn, Buffer* buffer)
{
    const char* data = buffer->peek();
    const uint32_t payloadLen = m_currentHeader.totalLen - m_currentHeader.headerLen;
    const char* payload = data + m_currentHeader.headerLen;

    const uint32_t calcChecksum = protocol::CalcCRC16(payload, payloadLen);
    if (m_currentHeader.checksum != calcChecksum)
    {
        LOG_ERROR("Codec::parsePayload checksum mismatch: received={}, calculated={}",
                  m_currentHeader.checksum, calcChecksum);
        if (m_errorCallback)
        {
            m_errorCallback(conn, protocol::ErrChecksumMismatch);
        }
        buffer->retrieve(m_expectedLength);
        m_state = kExpectHeader;
        m_expectedLength = protocol::kHeaderSize;
        return false;
    }

    DecodedPacket packet;
    packet.header = m_currentHeader;
    packet.payload.assign(payload, payloadLen);

    buffer->retrieve(m_expectedLength);
    m_state = kExpectHeader;
    m_expectedLength = protocol::kHeaderSize;

    if (m_packetCallback)
    {
        m_packetCallback(conn, packet);
    }

    if (m_messageCallback)
    {
        m_messageCallback(conn,
                          packet.header.command,
                          packet.header.requestId,
                          packet.payload);
    }

    return true;
}
