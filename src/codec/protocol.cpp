// Protocol.cpp
#include "codec/protocol.h"

namespace protocol
{

bool IsResponseCommand(uint16_t command)
{
    switch (command)
    {
    case CmdLoginResp:
    case CmdHeartbeatResp:
    case CmdP2pMsgResp:
    case CmdKickUserResp:
    case CmdMessageAckResp:
    case CmdPullOfflineResp:
        return true;
    default:
        return false;
    }
}

uint16_t GuessFlagsFromCommand(uint16_t command)
{
    switch (command)
    {
    case CmdP2pMsgNotify:
    case CmdBroadcastMsgNotify:
        return kFlagNone;
    case CmdMessageAckReq:
        return kFlagRequest | kFlagAck;
    case CmdMessageAckResp:
        return kFlagResponse | kFlagAck;
    default:
        return IsResponseCommand(command) ? kFlagResponse : kFlagRequest;
    }
}

uint16_t CalcCRC16(const void* data, size_t len)
{
    const uint8_t* buf = static_cast<const uint8_t*>(data);
    uint16_t crc = 0xFFFF;
    const uint16_t polynomial = 0x1021;

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint16_t>(buf[i]) << 8;
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ polynomial;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

}
