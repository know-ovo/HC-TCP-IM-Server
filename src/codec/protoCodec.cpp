#include "codec/protoCodec.h"
#include "base/util.h"

protocol::PayloadFormat ProtoCodec::parsePayloadFormat(const std::string& value)
{
    const std::string lowered = util::ToLower(value);
    if (lowered == "protobuf" || lowered == "proto")
    {
        return protocol::PayloadFormat::Protobuf;
    }
    return protocol::PayloadFormat::Json;
}

const char* ProtoCodec::toString(protocol::PayloadFormat format)
{
    switch (format)
    {
    case protocol::PayloadFormat::Protobuf:
        return "protobuf";
    case protocol::PayloadFormat::Json:
    default:
        return "json";
    }
}

bool ProtoCodec::protobufAvailable()
{
#ifdef HAVE_PROTOBUF
    return true;
#else
    return false;
#endif
}
