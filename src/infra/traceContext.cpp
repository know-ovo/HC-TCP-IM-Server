#include "infra/traceContext.h"

#include <atomic>
#include <sstream>
#include <thread>

#include "base/util.h"

namespace infra
{

namespace
{

thread_local TraceContext g_currentTraceContext;
thread_local bool g_hasCurrentTraceContext = false;
std::atomic<uint64_t> g_traceCounter { 1 };

std::string NormalizeField(const std::string& value)
{
    return value.empty() ? "-" : value;
}

} // namespace

ScopedTraceContext::ScopedTraceContext(const TraceContext& context)
    : m_hadPrevious(g_hasCurrentTraceContext)
    , m_previous(g_currentTraceContext)
{
    SetCurrentTraceContext(context);
}

ScopedTraceContext::~ScopedTraceContext()
{
    if (m_hadPrevious)
    {
        SetCurrentTraceContext(m_previous);
        return;
    }

    ClearCurrentTraceContext();
}

std::string GenerateTraceId()
{
    std::ostringstream oss;
    oss << std::hex
        << util::GetTimestampUs()
        << "-"
        << g_traceCounter.fetch_add(1, std::memory_order_relaxed)
        << "-"
        << std::hash<std::thread::id> {}(std::this_thread::get_id());
    return oss.str();
}

void SetCurrentTraceContext(const TraceContext& context)
{
    g_currentTraceContext = context;
    g_hasCurrentTraceContext = true;
}

void ClearCurrentTraceContext()
{
    g_currentTraceContext = TraceContext {};
    g_hasCurrentTraceContext = false;
}

bool HasCurrentTraceContext()
{
    return g_hasCurrentTraceContext;
}

TraceContext CurrentTraceContext()
{
    return g_currentTraceContext;
}

std::string FormatTraceContextForLog()
{
    const TraceContext context = g_currentTraceContext;

    std::ostringstream oss;
    oss << "trace_id=" << NormalizeField(context.traceId)
        << " request_id=" << context.requestId
        << " session_id=" << NormalizeField(context.sessionId)
        << " user_id=" << NormalizeField(context.userId)
        << " node_id=" << NormalizeField(context.nodeId);

    if (context.messageId > 0)
    {
        oss << " message_id=" << context.messageId;
    }
    if (context.serverSeq > 0)
    {
        oss << " server_seq=" << context.serverSeq;
    }

    return oss.str();
}

} // namespace infra
