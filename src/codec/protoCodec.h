#pragma once

#include <string>
#include "codec/protocol.h"

class ProtoCodec
{
public:
    static protocol::PayloadFormat parsePayloadFormat(const std::string& value);
    static const char* toString(protocol::PayloadFormat format);
    static bool protobufAvailable();
};
