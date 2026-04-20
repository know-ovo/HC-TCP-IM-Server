#pragma once

#include <cstdint>
#include <string>

namespace infra
{

struct TraceContext
{
    std::string traceId;
    uint64_t requestId = 0;
    std::string sessionId;
    std::string userId;
    std::string nodeId;
    uint64_t messageId = 0;
    uint64_t serverSeq = 0;
};

class ScopedTraceContext
{
public:
    explicit ScopedTraceContext(const TraceContext& context);
    ~ScopedTraceContext();

private:
    bool m_hadPrevious = false;
    TraceContext m_previous;
};

std::string GenerateTraceId();

void SetCurrentTraceContext(const TraceContext& context);
void ClearCurrentTraceContext();
bool HasCurrentTraceContext();
TraceContext CurrentTraceContext();

std::string FormatTraceContextForLog();

} // namespace infra
