#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "base/log.h"
#include "codec/buffer.h"
#include "codec/codec.h"
#include "codec/messageSerializer.h"
#include "codec/protoCodec.h"
#include "codec/protocol.h"
#include "net/timestamp.h"
#ifdef HAVE_PROTOBUF
#include "auth.pb.h"
#include "heartbeat.pb.h"
#include "message.pb.h"
#endif

using namespace std;
using namespace chrono;

atomic<long long> g_totalMessages(0);
atomic<long long> g_totalRounds(0);
atomic<long long> g_totalErrors(0);
atomic<long long> g_totalBytes(0);
atomic<long long> g_totalTimeouts(0);
atomic<bool> g_running(true);
atomic<uint32_t> g_requestId(1);
atomic<int> g_warmupReadyClients(0);
atomic<int> g_warmupFailedClients(0);
atomic<bool> g_startBenchmark(false);
atomic<long long> g_benchmarkStartNs(0);
atomic<long long> g_socketSetupErrors(0);
atomic<long long> g_connectErrors(0);
atomic<long long> g_sendErrors(0);
atomic<long long> g_recvErrors(0);
atomic<long long> g_protocolErrors(0);
atomic<long long> g_warmupLoginReqs(0);
atomic<long long> g_warmupLoginTimeouts(0);
atomic<long long> g_warmupLoginErrors(0);
atomic<long long> g_loginReqs(0);
atomic<long long> g_heartbeatReqs(0);
atomic<long long> g_p2pReqs(0);
atomic<long long> g_pullReqs(0);
atomic<long long> g_ackReqs(0);
atomic<long long> g_loginTimeouts(0);
atomic<long long> g_heartbeatTimeouts(0);
atomic<long long> g_p2pTimeouts(0);
atomic<long long> g_pullTimeouts(0);
atomic<long long> g_ackTimeouts(0);
atomic<long long> g_loginErrors(0);
atomic<long long> g_heartbeatErrors(0);
atomic<long long> g_p2pErrors(0);
atomic<long long> g_pullErrors(0);
atomic<long long> g_ackErrors(0);
atomic<long long> g_loginProtocolErrors(0);
atomic<long long> g_heartbeatProtocolErrors(0);
atomic<long long> g_p2pProtocolErrors(0);
atomic<long long> g_pullProtocolErrors(0);
atomic<long long> g_ackProtocolErrors(0);

protocol::PayloadFormat g_payloadFormat = protocol::PayloadFormat::Json;
bool g_compatibleJson = true;
string g_workload = "full";
constexpr size_t kAckBatchLimit = 64;
constexpr int kWarmupStartDelayMs = 100;
constexpr int kWarmupRetryDelayMs = 50;
constexpr int kWarmupLoginMinTimeoutMs = 3000;
constexpr int kWarmupDeadlineSec = 30;

struct LatencyStats
{
    vector<double> latencies;
    mutex mtx;

    void addLatency(double latencyMs)
    {
        lock_guard<mutex> lock(mtx);
        latencies.push_back(latencyMs);
    }

    double getPercentile(double percentile)
    {
        lock_guard<mutex> lock(mtx);
        if (latencies.empty())
        {
            return 0.0;
        }
        vector<double> sorted = latencies;
        sort(sorted.begin(), sorted.end());
        size_t index = static_cast<size_t>(sorted.size() * percentile / 100.0);
        if (index >= sorted.size())
        {
            index = sorted.size() - 1;
        }
        return sorted[index];
    }

    double getAverage()
    {
        lock_guard<mutex> lock(mtx);
        if (latencies.empty())
        {
            return 0.0;
        }
        double sum = 0.0;
        for (double l : latencies)
        {
            sum += l;
        }
        return sum / latencies.size();
    }

    double getMax()
    {
        lock_guard<mutex> lock(mtx);
        if (latencies.empty())
        {
            return 0.0;
        }
        return *max_element(latencies.begin(), latencies.end());
    }

    double getMin()
    {
        lock_guard<mutex> lock(mtx);
        if (latencies.empty())
        {
            return 0.0;
        }
        return *min_element(latencies.begin(), latencies.end());
    }
};

LatencyStats g_latencyStats;
mutex g_consoleMutex;
mutex g_protocolErrorMutex;
deque<string> g_recentProtocolErrors;

struct PendingAck
{
    uint64_t msgId = 0;
    uint64_t serverSeq = 0;
};

struct SessionState
{
    bool loggedIn = false;
    string userId;
    string peerUserId;
    uint64_t lastAckedSeq = 0;
    vector<PendingAck> pendingAcks;
};

struct ClientRxContext
{
    Buffer buffer;
    Codec codec;
    map<uint16_t, deque<Codec::DecodedPacket>> inbox;

    ClientRxContext()
    {
        codec.setPacketCallback([this](const shared_ptr<TcpConnection>&, const Codec::DecodedPacket& packet) {
            inbox[packet.header.command].push_back(packet);
        });
    }
};

enum class OpResult
{
    Ok,
    Timeout,
    Error
};

const char* OpName(uint16_t reqCmd)
{
    switch (reqCmd)
    {
    case protocol::CmdLoginReq:
        return "Login";
    case protocol::CmdHeartbeatReq:
        return "Heartbeat";
    case protocol::CmdP2pMsgReq:
        return "P2P";
    case protocol::CmdPullOfflineReq:
        return "PullOffline";
    case protocol::CmdMessageAckReq:
        return "ACK";
    default:
        return "Unknown";
    }
}

void RecordOpAttempt(uint16_t reqCmd)
{
    switch (reqCmd)
    {
    case protocol::CmdLoginReq:
        g_loginReqs++;
        break;
    case protocol::CmdHeartbeatReq:
        g_heartbeatReqs++;
        break;
    case protocol::CmdP2pMsgReq:
        g_p2pReqs++;
        break;
    case protocol::CmdPullOfflineReq:
        g_pullReqs++;
        break;
    case protocol::CmdMessageAckReq:
        g_ackReqs++;
        break;
    default:
        break;
    }
}

void RecordOpFailure(uint16_t reqCmd, OpResult result)
{
    if (result == OpResult::Ok)
    {
        return;
    }

    const bool isTimeout = (result == OpResult::Timeout);
    switch (reqCmd)
    {
    case protocol::CmdLoginReq:
        isTimeout ? g_loginTimeouts++ : g_loginErrors++;
        break;
    case protocol::CmdHeartbeatReq:
        isTimeout ? g_heartbeatTimeouts++ : g_heartbeatErrors++;
        break;
    case protocol::CmdP2pMsgReq:
        isTimeout ? g_p2pTimeouts++ : g_p2pErrors++;
        break;
    case protocol::CmdPullOfflineReq:
        isTimeout ? g_pullTimeouts++ : g_pullErrors++;
        break;
    case protocol::CmdMessageAckReq:
        isTimeout ? g_ackTimeouts++ : g_ackErrors++;
        break;
    default:
        break;
    }
}

void RecordProtocolFailure(uint16_t reqCmd, const string& detail)
{
    g_protocolErrors++;
    g_totalErrors++;
    RecordOpFailure(reqCmd, OpResult::Error);

    switch (reqCmd)
    {
    case protocol::CmdLoginReq:
        g_loginProtocolErrors++;
        break;
    case protocol::CmdHeartbeatReq:
        g_heartbeatProtocolErrors++;
        break;
    case protocol::CmdP2pMsgReq:
        g_p2pProtocolErrors++;
        break;
    case protocol::CmdPullOfflineReq:
        g_pullProtocolErrors++;
        break;
    case protocol::CmdMessageAckReq:
        g_ackProtocolErrors++;
        break;
    default:
        break;
    }

    lock_guard<mutex> lock(g_protocolErrorMutex);
    if (g_recentProtocolErrors.size() >= 10)
    {
        g_recentProtocolErrors.pop_front();
    }
    g_recentProtocolErrors.push_back(detail);
}

long long NowSteadyNs()
{
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

steady_clock::time_point SteadyTimePointFromNs(long long ns)
{
    return steady_clock::time_point(duration_cast<steady_clock::duration>(nanoseconds(ns)));
}

#ifdef _WIN32
void InitWinsock()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        LOG_ERROR("WSAStartup failed");
        exit(1);
    }
}

void CleanupWinsock()
{
    WSACleanup();
}
#endif

bool SetNonBlocking(int sockfd)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sockfd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0)
    {
        return false;
    }
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool WaitForRead(int sockfd, int timeoutMs)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return select(sockfd + 1, &readfds, nullptr, nullptr, &tv) > 0;
}

bool WaitForWrite(int sockfd, int timeoutMs)
{
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sockfd, &writefds);

    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return select(sockfd + 1, nullptr, &writefds, nullptr, &tv) > 0;
}

bool SendData(int sockfd, const char* data, size_t len, size_t& sentBytes, int timeoutMs = 5000)
{
    sentBytes = 0;
    while (sentBytes < len)
    {
        if (!WaitForWrite(sockfd, timeoutMs))
        {
            return false;
        }
#ifdef _WIN32
        const ssize_t n = send(sockfd, data + sentBytes, static_cast<int>(len - sentBytes), 0);
#else
        const ssize_t n = write(sockfd, data + sentBytes, len - sentBytes);
#endif
        if (n > 0)
        {
            sentBytes += static_cast<size_t>(n);
            continue;
        }
#ifdef _WIN32
        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINTR)
#else
        if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
#endif
        {
            return false;
        }
    }
    return true;
}

string EncodeLoginReq(const protocol::LoginReq& req)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Serialize(req);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::LoginReq pb;
    pb.set_user_id(req.userId);
    pb.set_token(req.token);
    pb.set_device_id(req.deviceId);
    string out;
    if (pb.SerializeToString(&out))
    {
        return out;
    }
#endif
    return serializer::Serialize(req);
}

string EncodeHeartbeatReq(const protocol::HeartbeatReq& req)
{
    (void)req;
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Serialize(req);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::HeartbeatReq pb;
    string out;
    if (pb.SerializeToString(&out))
    {
        return out;
    }
#endif
    return serializer::Serialize(req);
}

string EncodeP2PReq(const protocol::P2PMsgReq& req)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Serialize(req);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::P2PMessageReq pb;
    pb.set_from_user_id(req.fromUserId);
    pb.set_to_user_id(req.toUserId);
    pb.set_client_msg_id(req.clientMsgId);
    pb.set_content(req.content);
    string out;
    if (pb.SerializeToString(&out))
    {
        return out;
    }
#endif
    return serializer::Serialize(req);
}

string EncodeAckReq(const protocol::MessageAckReq& req)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Serialize(req);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::MessageAckReq pb;
    pb.set_msg_id(req.msgId);
    pb.set_server_seq(req.serverSeq);
    pb.set_ack_code(req.ackCode);
    string out;
    if (pb.SerializeToString(&out))
    {
        return out;
    }
#endif
    return serializer::Serialize(req);
}

string EncodePullReq(const protocol::PullOfflineReq& req)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Serialize(req);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::PullOfflineReq pb;
    pb.set_user_id(req.userId);
    pb.set_last_acked_seq(req.lastAckedSeq);
    pb.set_limit(req.limit);
    string out;
    if (pb.SerializeToString(&out))
    {
        return out;
    }
#endif
    return serializer::Serialize(req);
}

bool DecodeLoginResp(const string& payload, protocol::LoginResp& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::LoginResp pb;
    if (pb.ParseFromString(payload))
    {
        out.resultCode = pb.result_code();
        out.resultMsg = pb.result_msg();
        out.sessionId = pb.session_id();
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

bool DecodeHeartbeatResp(const string& payload, protocol::HeartbeatResp& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::HeartbeatResp pb;
    if (pb.ParseFromString(payload))
    {
        out.serverTime = pb.server_time();
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

bool DecodeP2PResp(const string& payload, protocol::P2PMsgResp& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::P2PMessageResp pb;
    if (pb.ParseFromString(payload))
    {
        out.resultCode = pb.result_code();
        out.resultMsg = pb.result_msg();
        out.msgId = pb.msg_id();
        out.serverSeq = pb.server_seq();
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

bool DecodeAckResp(const string& payload, protocol::MessageAckResp& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::MessageAckResp pb;
    if (pb.ParseFromString(payload))
    {
        out.resultCode = pb.result_code();
        out.resultMsg = pb.result_msg();
        out.serverSeq = pb.server_seq();
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

bool DecodeP2PNotify(const string& payload, protocol::P2PMsgNotify& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::P2PMessageNotify pb;
    if (pb.ParseFromString(payload))
    {
        out.msgId = pb.msg_id();
        out.serverSeq = pb.server_seq();
        out.fromUserId = pb.from_user_id();
        out.toUserId = pb.to_user_id();
        out.clientMsgId = pb.client_msg_id();
        out.content = pb.content();
        out.timestamp = pb.timestamp();
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

bool DecodePullResp(const string& payload, protocol::PullOfflineResp& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::PullOfflineResp pb;
    if (pb.ParseFromString(payload))
    {
        out.resultCode = pb.result_code();
        out.resultMsg = pb.result_msg();
        out.nextBeginSeq = pb.next_begin_seq();
        out.messages.clear();
        for (const auto& pbItem : pb.messages())
        {
            protocol::PullOfflineResp::OfflineItem item;
            item.msgId = pbItem.msg_id();
            item.serverSeq = pbItem.server_seq();
            item.fromUserId = pbItem.from_user_id();
            item.toUserId = pbItem.to_user_id();
            item.clientMsgId = pbItem.client_msg_id();
            item.content = pbItem.content();
            item.timestamp = pbItem.timestamp();
            out.messages.push_back(std::move(item));
        }
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

bool SendPacket(int sockfd, uint16_t command, uint32_t requestId, const string& payload, size_t& sentBytes)
{
    Buffer packet;
    Codec::pack(&packet, command, requestId, payload);
    return SendData(sockfd, packet.peek(), packet.readableBytes(), sentBytes);
}

void ProcessBackgroundPacket(const Codec::DecodedPacket& packet, SessionState& session)
{
    if (packet.header.command != protocol::CmdP2pMsgNotify)
    {
        return;
    }

    protocol::P2PMsgNotify notify;
    if (!DecodeP2PNotify(packet.payload, notify))
    {
        return;
    }
    session.pendingAcks.push_back({notify.msgId, notify.serverSeq});
}

OpResult WaitExpected(int sockfd,
                      ClientRxContext& rx,
                      SessionState& session,
                      uint16_t expectedCmd,
                      int readTimeoutMs,
                      Codec::DecodedPacket* matchedPacket)
{
    const auto deadline = steady_clock::now() + milliseconds(readTimeoutMs);

    while (g_running)
    {
        auto inboxIt = rx.inbox.find(expectedCmd);
        if (inboxIt != rx.inbox.end() && !inboxIt->second.empty())
        {
            const Codec::DecodedPacket packet = inboxIt->second.front();
            inboxIt->second.pop_front();
            if (inboxIt->second.empty())
            {
                rx.inbox.erase(inboxIt);
            }
            if (matchedPacket != nullptr)
            {
                *matchedPacket = packet;
            }
            return OpResult::Ok;
        }

        for (auto it = rx.inbox.begin(); it != rx.inbox.end(); )
        {
            auto& queue = it->second;
            while (!queue.empty())
            {
                ProcessBackgroundPacket(queue.front(), session);
                queue.pop_front();
            }
            if (queue.empty())
            {
                it = rx.inbox.erase(it);
            }
            else
            {
                ++it;
            }
        }

        const auto now = steady_clock::now();
        if (now >= deadline)
        {
            g_totalTimeouts++;
            return OpResult::Timeout;
        }

        const int sliceMs = static_cast<int>(duration_cast<milliseconds>(deadline - now).count());
        if (!WaitForRead(sockfd, std::max(1, std::min(100, sliceMs))))
        {
            continue;
        }

        char readBuf[4096];
#ifdef _WIN32
        const ssize_t n = recv(sockfd, readBuf, sizeof(readBuf), 0);
#else
        const ssize_t n = read(sockfd, readBuf, sizeof(readBuf));
#endif
        if (n <= 0)
        {
            g_recvErrors++;
            g_totalErrors++;
            return OpResult::Error;
        }
        rx.buffer.append(readBuf, static_cast<size_t>(n));
        rx.codec.onMessage(nullptr, &rx.buffer, Timestamp::now());
    }
    return OpResult::Error;
}

OpResult SendAndWait(int sockfd,
                     ClientRxContext& rx,
                     SessionState& session,
                     uint16_t reqCmd,
                     uint16_t respCmd,
                     const string& payload,
                     int readTimeoutMs,
                     Codec::DecodedPacket* matchedPacket,
                     bool countMetrics = true)
{
    const uint32_t requestId = g_requestId.fetch_add(1);
    size_t sentBytes = 0;
    if (!SendPacket(sockfd, reqCmd, requestId, payload, sentBytes))
    {
        g_sendErrors++;
        g_totalErrors++;
        if (countMetrics)
        {
            RecordOpFailure(reqCmd, OpResult::Error);
        }
        return OpResult::Error;
    }
    if (countMetrics)
    {
        RecordOpAttempt(reqCmd);
        g_totalMessages++;
        g_totalBytes += static_cast<long long>(sentBytes);
    }
    const OpResult waitResult = WaitExpected(sockfd, rx, session, respCmd, readTimeoutMs, matchedPacket);
    if (countMetrics)
    {
        RecordOpFailure(reqCmd, waitResult);
    }
    return waitResult;
}

OpResult DoLogin(int sockfd,
                 ClientRxContext& rx,
                 SessionState& session,
                 int readTimeoutMs,
                 bool countMetrics = true)
{
    protocol::LoginReq req;
    req.userId = session.userId;
    req.token = "valid_token_" + session.userId;
    req.deviceId = "bench-device";

    if (!countMetrics)
    {
        g_warmupLoginReqs++;
    }

    Codec::DecodedPacket responsePacket;
    const OpResult ret = SendAndWait(sockfd,
                                     rx,
                                     session,
                                     protocol::CmdLoginReq,
                                     protocol::CmdLoginResp,
                                     EncodeLoginReq(req),
                                     readTimeoutMs,
                                     &responsePacket,
                                     countMetrics);
    if (ret != OpResult::Ok)
    {
        if (!countMetrics)
        {
            if (ret == OpResult::Timeout)
            {
                g_warmupLoginTimeouts++;
            }
            else
            {
                g_warmupLoginErrors++;
            }
        }
        return ret;
    }

    protocol::LoginResp resp;
    const string& payload = responsePacket.payload;
    if (!DecodeLoginResp(payload, resp))
    {
        ostringstream oss;
        oss << OpName(protocol::CmdLoginReq) << " decode failed"
            << " user=" << session.userId
            << " payload_bytes=" << payload.size();
        if (countMetrics)
        {
            RecordProtocolFailure(protocol::CmdLoginReq, oss.str());
        }
        else
        {
            g_warmupLoginErrors++;
            LOG_WARN("Warmup {}", oss.str());
        }
        return OpResult::Error;
    }
    if (resp.resultCode != 0)
    {
        ostringstream oss;
        oss << OpName(protocol::CmdLoginReq) << " rejected"
            << " user=" << session.userId
            << " code=" << resp.resultCode
            << " msg=" << resp.resultMsg;
        if (countMetrics)
        {
            RecordProtocolFailure(protocol::CmdLoginReq, oss.str());
        }
        else
        {
            g_warmupLoginErrors++;
            LOG_WARN("Warmup {}", oss.str());
        }
        return OpResult::Error;
    }
    session.loggedIn = true;
    return OpResult::Ok;
}

OpResult DoHeartbeat(int sockfd, ClientRxContext& rx, SessionState& session, int readTimeoutMs)
{
    protocol::HeartbeatReq req;
    Codec::DecodedPacket responsePacket;
    const OpResult ret = SendAndWait(sockfd,
                                     rx,
                                     session,
                                     protocol::CmdHeartbeatReq,
                                     protocol::CmdHeartbeatResp,
                                     EncodeHeartbeatReq(req),
                                     readTimeoutMs,
                                     &responsePacket);
    if (ret != OpResult::Ok)
    {
        return ret;
    }

    protocol::HeartbeatResp resp;
    const string& payload = responsePacket.payload;
    if (!DecodeHeartbeatResp(payload, resp))
    {
        ostringstream oss;
        oss << OpName(protocol::CmdHeartbeatReq) << " decode failed"
            << " user=" << session.userId
            << " payload_bytes=" << payload.size();
        RecordProtocolFailure(protocol::CmdHeartbeatReq, oss.str());
        return OpResult::Error;
    }
    return OpResult::Ok;
}

OpResult DoP2P(int sockfd, ClientRxContext& rx, SessionState& session, int readTimeoutMs)
{
    protocol::P2PMsgReq req;
    req.fromUserId = session.userId;
    req.toUserId = session.peerUserId;
    req.clientMsgId = to_string(g_requestId.load()) + "_" + session.userId;
    req.content = "bench-msg-" + req.clientMsgId;

    Codec::DecodedPacket responsePacket;
    const OpResult ret = SendAndWait(sockfd,
                                     rx,
                                     session,
                                     protocol::CmdP2pMsgReq,
                                     protocol::CmdP2pMsgResp,
                                     EncodeP2PReq(req),
                                     readTimeoutMs,
                                     &responsePacket);
    if (ret != OpResult::Ok)
    {
        return ret;
    }

    protocol::P2PMsgResp resp;
    const string& payload = responsePacket.payload;
    if (!DecodeP2PResp(payload, resp))
    {
        ostringstream oss;
        oss << OpName(protocol::CmdP2pMsgReq) << " decode failed"
            << " from=" << session.userId
            << " to=" << session.peerUserId
            << " payload_bytes=" << payload.size();
        RecordProtocolFailure(protocol::CmdP2pMsgReq, oss.str());
        return OpResult::Error;
    }
    if (resp.resultCode != 0)
    {
        ostringstream oss;
        oss << OpName(protocol::CmdP2pMsgReq) << " rejected"
            << " from=" << session.userId
            << " to=" << session.peerUserId
            << " code=" << resp.resultCode
            << " msg=" << resp.resultMsg;
        RecordProtocolFailure(protocol::CmdP2pMsgReq, oss.str());
        return OpResult::Error;
    }
    return OpResult::Ok;
}

OpResult DoPullOffline(int sockfd, ClientRxContext& rx, SessionState& session, int readTimeoutMs)
{
    protocol::PullOfflineReq req;
    req.userId = session.userId;
    req.lastAckedSeq = session.lastAckedSeq;
    req.limit = 20;

    Codec::DecodedPacket responsePacket;
    const OpResult ret = SendAndWait(sockfd,
                                     rx,
                                     session,
                                     protocol::CmdPullOfflineReq,
                                     protocol::CmdPullOfflineResp,
                                     EncodePullReq(req),
                                     readTimeoutMs,
                                     &responsePacket);
    if (ret != OpResult::Ok)
    {
        return ret;
    }

    protocol::PullOfflineResp resp;
    const string& payload = responsePacket.payload;
    if (!DecodePullResp(payload, resp))
    {
        ostringstream oss;
        oss << OpName(protocol::CmdPullOfflineReq) << " decode failed"
            << " user=" << session.userId
            << " last_acked_seq=" << session.lastAckedSeq
            << " payload_bytes=" << payload.size();
        RecordProtocolFailure(protocol::CmdPullOfflineReq, oss.str());
        return OpResult::Error;
    }
    if (resp.resultCode != 0)
    {
        ostringstream oss;
        oss << OpName(protocol::CmdPullOfflineReq) << " rejected"
            << " user=" << session.userId
            << " last_acked_seq=" << session.lastAckedSeq
            << " code=" << resp.resultCode
            << " msg=" << resp.resultMsg;
        RecordProtocolFailure(protocol::CmdPullOfflineReq, oss.str());
        return OpResult::Error;
    }
    for (const auto& item : resp.messages)
    {
        session.pendingAcks.push_back({item.msgId, item.serverSeq});
    }
    return OpResult::Ok;
}

OpResult FlushPendingAcks(int sockfd, ClientRxContext& rx, SessionState& session, int readTimeoutMs)
{
    vector<PendingAck> currentAcks;
    currentAcks.swap(session.pendingAcks);
    const size_t limit = min(currentAcks.size(), kAckBatchLimit);

    for (size_t i = 0; i < limit; ++i)
    {
        const PendingAck pending = currentAcks[i];
        protocol::MessageAckReq req;
        req.msgId = pending.msgId;
        req.serverSeq = pending.serverSeq;
        req.ackCode = protocol::AckReceived;

        Codec::DecodedPacket responsePacket;
        const OpResult ret = SendAndWait(sockfd,
                                         rx,
                                         session,
                                         protocol::CmdMessageAckReq,
                                         protocol::CmdMessageAckResp,
                                         EncodeAckReq(req),
                                         readTimeoutMs,
                                         &responsePacket);
        if (ret != OpResult::Ok)
        {
            session.pendingAcks.insert(session.pendingAcks.begin(),
                                       currentAcks.begin() + static_cast<ptrdiff_t>(i),
                                       currentAcks.end());
            return ret;
        }
        protocol::MessageAckResp ackResp;
        const string& payload = responsePacket.payload;
        if (!DecodeAckResp(payload, ackResp))
        {
            ostringstream oss;
            oss << OpName(protocol::CmdMessageAckReq) << " decode failed"
                << " user=" << session.userId
                << " msg_id=" << pending.msgId
                << " server_seq=" << pending.serverSeq
                << " payload_bytes=" << payload.size();
            RecordProtocolFailure(protocol::CmdMessageAckReq, oss.str());
            return OpResult::Error;
        }
        if (ackResp.resultCode != 0)
        {
            ostringstream oss;
            oss << OpName(protocol::CmdMessageAckReq) << " rejected"
                << " user=" << session.userId
                << " msg_id=" << pending.msgId
                << " server_seq=" << pending.serverSeq
                << " code=" << ackResp.resultCode
                << " msg=" << ackResp.resultMsg;
            RecordProtocolFailure(protocol::CmdMessageAckReq, oss.str());
            session.pendingAcks.insert(session.pendingAcks.begin(),
                                       currentAcks.begin() + static_cast<ptrdiff_t>(i + 1),
                                       currentAcks.end());
            return OpResult::Error;
        }
        session.lastAckedSeq = max(session.lastAckedSeq, ackResp.serverSeq);
    }

    if (currentAcks.size() > limit)
    {
        session.pendingAcks.insert(session.pendingAcks.begin(),
                                   currentAcks.begin() + static_cast<ptrdiff_t>(limit),
                                   currentAcks.end());
    }
    return OpResult::Ok;
}

void CloseSocket(int sockfd)
{
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
}

void ClientThread(int clientId,
                  int numClients,
                  const string& serverIp,
                  int port,
                  int durationSec,
                  int intervalUs,
                  int readTimeoutMs)
{
#ifdef _WIN32
    const int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    const int sockfd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sockfd < 0)
    {
        g_socketSetupErrors++;
        g_totalErrors++;
        return;
    }

    if (!SetNonBlocking(sockfd))
    {
        g_socketSetupErrors++;
        g_totalErrors++;
        CloseSocket(sockfd);
        return;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    const int connectResult = connect(sockfd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
#ifdef _WIN32
    if (connectResult == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
#else
    if (connectResult < 0 && errno != EINPROGRESS)
#endif
    {
        g_connectErrors++;
        g_totalErrors++;
        CloseSocket(sockfd);
        return;
    }

    if (!WaitForWrite(sockfd, 5000))
    {
        g_connectErrors++;
        g_totalErrors++;
        CloseSocket(sockfd);
        return;
    }

    SessionState session;
    session.userId = "bench_user_" + to_string(clientId);
    const int peerId = (clientId + 1) % max(1, numClients);
    session.peerUserId = "bench_user_" + to_string(peerId);
    ClientRxContext rx;

    const int warmupLoginTimeoutMs = max(readTimeoutMs, kWarmupLoginMinTimeoutMs);
    const auto warmupDeadline = steady_clock::now() + seconds(kWarmupDeadlineSec);
    while (g_running && !session.loggedIn && steady_clock::now() < warmupDeadline)
    {
        const OpResult warmupResult = DoLogin(sockfd, rx, session, warmupLoginTimeoutMs, false);
        if (warmupResult == OpResult::Ok)
        {
            break;
        }
        this_thread::sleep_for(milliseconds(kWarmupRetryDelayMs));
    }

    if (!session.loggedIn)
    {
        g_warmupFailedClients++;
        CloseSocket(sockfd);
        return;
    }

    g_warmupReadyClients++;
    while (g_running && !g_startBenchmark.load())
    {
        this_thread::sleep_for(milliseconds(1));
    }
    if (!g_running)
    {
        CloseSocket(sockfd);
        return;
    }

    const long long startNs = g_benchmarkStartNs.load();
    while (g_running && NowSteadyNs() < startNs)
    {
        this_thread::sleep_for(milliseconds(1));
    }
    if (!g_running)
    {
        CloseSocket(sockfd);
        return;
    }

    const auto endTime = SteadyTimePointFromNs(startNs) + seconds(durationSec);
    while (g_running && steady_clock::now() < endTime)
    {
        const auto opStart = steady_clock::now();

        OpResult result = OpResult::Ok;
        bool completedRound = false;
        if (g_workload == "heartbeat")
        {
            result = DoHeartbeat(sockfd, rx, session, readTimeoutMs);
            completedRound = (result == OpResult::Ok);
        }
        else
        {
            result = DoHeartbeat(sockfd, rx, session, readTimeoutMs);
            if (result == OpResult::Ok)
            {
                result = DoP2P(sockfd, rx, session, readTimeoutMs);
            }
            if (result == OpResult::Ok)
            {
                result = DoPullOffline(sockfd, rx, session, readTimeoutMs);
            }
            if (result == OpResult::Ok)
            {
                result = FlushPendingAcks(sockfd, rx, session, readTimeoutMs);
            }
            completedRound = (result == OpResult::Ok);
        }

        const auto opEnd = steady_clock::now();
        const double latencyMs = duration_cast<microseconds>(opEnd - opStart).count() / 1000.0;
        g_latencyStats.addLatency(latencyMs);

        if (result == OpResult::Error)
        {
            break;
        }
        if (completedRound)
        {
            g_totalRounds++;
        }

        if (intervalUs > 0)
        {
#ifdef _WIN32
            Sleep(intervalUs / 1000);
#else
            usleep(intervalUs);
#endif
        }
    }

    CloseSocket(sockfd);
}

void ProgressMonitor(int durationSec, long long startNs)
{
    const auto startTime = SteadyTimePointFromNs(startNs);
    const auto endTime = startTime + seconds(durationSec);
    while (g_running && steady_clock::now() < endTime)
    {
        const int elapsed = static_cast<int>(duration_cast<seconds>(steady_clock::now() - startTime).count());
        const long long totalRequests = g_totalMessages.load();
        const long long totalRounds = g_totalRounds.load();
        const long long errors = g_totalErrors.load();
        const long long timeouts = g_totalTimeouts.load();
        const double requestQps = elapsed > 0 ? static_cast<double>(totalRequests) / elapsed : 0.0;
        const double roundQps = elapsed > 0 ? static_cast<double>(totalRounds) / elapsed : 0.0;

        {
            lock_guard<mutex> lock(g_consoleMutex);
            cout << "\r[" << elapsed << "s/" << durationSec << "s] "
                 << "Reqs: " << totalRequests
                 << " | Rounds: " << totalRounds
                 << " | Errs: " << errors
                 << " | Timeouts: " << timeouts
                 << " | ReqQPS: " << fixed << setprecision(0) << requestQps
                 << " | RoundQPS: " << fixed << setprecision(0) << roundQps
                 << "  " << flush;
        }
        this_thread::sleep_for(seconds(1));
    }
    cout << endl;
}

void GenerateReport(const string& reportFile,
                    int numClients,
                    int durationSec,
                    double elapsed,
                    int intervalUs,
                    int readTimeoutMs)
{
    ofstream report(reportFile);
    if (!report.is_open())
    {
        LOG_ERROR("Failed to open report file: {}", reportFile);
        return;
    }

    const long long totalRequests = g_totalMessages.load();
    const long long totalRounds = g_totalRounds.load();
    const long long errors = g_totalErrors.load();
    const long long timeouts = g_totalTimeouts.load();
    const long long bytes = g_totalBytes.load();
    const double requestQps = elapsed > 0.0 ? totalRequests / elapsed : 0.0;
    const double roundQps = elapsed > 0.0 ? totalRounds / elapsed : 0.0;

    report << "========================================\n";
    report << "       TCP Server Performance Report\n";
    report << "========================================\n\n";
    report << "## Test Configuration\n";
    report << "| Parameter | Value |\n";
    report << "|-----------|-------|\n";
    report << "| Workload | " << g_workload << " |\n";
    report << "| Concurrent Clients | " << numClients << " |\n";
    report << "| Test Duration | " << durationSec << " seconds |\n";
    report << "| Request Interval | " << intervalUs << " us |\n";
    report << "| Read Timeout | " << readTimeoutMs << " ms |\n\n";

    report << "## Warmup Metrics\n";
    report << "| Metric | Value |\n";
    report << "|--------|-------|\n";
    report << "| Warmup Ready Clients | " << g_warmupReadyClients.load() << " |\n";
    report << "| Warmup Failed Clients | " << g_warmupFailedClients.load() << " |\n";
    report << "| Warmup Login Requests | " << g_warmupLoginReqs.load() << " |\n";
    report << "| Warmup Login Timeouts | " << g_warmupLoginTimeouts.load() << " |\n";
    report << "| Warmup Login Errors | " << g_warmupLoginErrors.load() << " |\n\n";

    report << "## Throughput Metrics\n";
    report << "| Metric | Value |\n";
    report << "|--------|-------|\n";
    report << "| Total Requests | " << totalRequests << " |\n";
    report << "| Total Business Rounds | " << totalRounds << " |\n";
    report << "| Total Errors | " << errors << " |\n";
    report << "| Total Timeouts | " << timeouts << " |\n";
    report << "| Error Rate | " << fixed << setprecision(2) << (totalRequests > 0 ? static_cast<double>(errors) / totalRequests * 100.0 : 0.0) << "% |\n";
    report << "| Timeout Rate | " << fixed << setprecision(2) << (totalRequests > 0 ? static_cast<double>(timeouts) / totalRequests * 100.0 : 0.0) << "% |\n";
    report << "| Elapsed Time | " << fixed << setprecision(2) << elapsed << " s |\n";
    report << "| Request QPS | " << fixed << setprecision(2) << requestQps << " |\n";
    report << "| Business Round QPS | " << fixed << setprecision(2) << roundQps << " |\n";
    report << "| Avg Requests Per Round | " << fixed << setprecision(2) << (totalRounds > 0 ? static_cast<double>(totalRequests) / totalRounds : 0.0) << " |\n";
    report << "| Throughput | " << fixed << setprecision(2) << (elapsed > 0.0 ? bytes / 1024.0 / elapsed : 0.0) << " KB/s |\n\n";

    report << "## Error Breakdown\n";
    report << "| Type | Count |\n";
    report << "|------|-------|\n";
    report << "| SocketSetup | " << g_socketSetupErrors.load() << " |\n";
    report << "| Connect | " << g_connectErrors.load() << " |\n";
    report << "| Send | " << g_sendErrors.load() << " |\n";
    report << "| Receive | " << g_recvErrors.load() << " |\n";
    report << "| Protocol | " << g_protocolErrors.load() << " |\n\n";

    report << "## Protocol Error Breakdown\n";
    report << "| Operation | Protocol Errors |\n";
    report << "|-----------|-----------------|\n";
    report << "| Login | " << g_loginProtocolErrors.load() << " |\n";
    report << "| Heartbeat | " << g_heartbeatProtocolErrors.load() << " |\n";
    report << "| P2P | " << g_p2pProtocolErrors.load() << " |\n";
    report << "| PullOffline | " << g_pullProtocolErrors.load() << " |\n";
    report << "| ACK | " << g_ackProtocolErrors.load() << " |\n\n";

    {
        lock_guard<mutex> lock(g_protocolErrorMutex);
        report << "## Recent Protocol Errors\n";
        if (g_recentProtocolErrors.empty())
        {
            report << "None\n\n";
        }
        else
        {
            for (const string& item : g_recentProtocolErrors)
            {
                report << "- " << item << "\n";
            }
            report << "\n";
        }
    }

    report << "## Business Flow Metrics\n";
    report << "| Operation | Requests | Timeouts | Errors |\n";
    report << "|-----------|----------|----------|--------|\n";
    report << "| Login | " << g_loginReqs.load() << " | " << g_loginTimeouts.load() << " | " << g_loginErrors.load() << " |\n";
    report << "| Heartbeat | " << g_heartbeatReqs.load() << " | " << g_heartbeatTimeouts.load() << " | " << g_heartbeatErrors.load() << " |\n";
    report << "| P2P | " << g_p2pReqs.load() << " | " << g_p2pTimeouts.load() << " | " << g_p2pErrors.load() << " |\n";
    report << "| PullOffline | " << g_pullReqs.load() << " | " << g_pullTimeouts.load() << " | " << g_pullErrors.load() << " |\n";
    report << "| ACK | " << g_ackReqs.load() << " | " << g_ackTimeouts.load() << " | " << g_ackErrors.load() << " |\n\n";

    report << "## Latency Metrics (ms)\n";
    report << "| Metric | Value |\n";
    report << "|--------|-------|\n";
    report << "| Min | " << fixed << setprecision(3) << g_latencyStats.getMin() << " |\n";
    report << "| Max | " << fixed << setprecision(3) << g_latencyStats.getMax() << " |\n";
    report << "| Avg | " << fixed << setprecision(3) << g_latencyStats.getAverage() << " |\n";
    report << "| P50 | " << fixed << setprecision(3) << g_latencyStats.getPercentile(50) << " |\n";
    report << "| P90 | " << fixed << setprecision(3) << g_latencyStats.getPercentile(90) << " |\n";
    report << "| P95 | " << fixed << setprecision(3) << g_latencyStats.getPercentile(95) << " |\n";
    report << "| P99 | " << fixed << setprecision(3) << g_latencyStats.getPercentile(99) << " |\n";

    report.close();
    LOG_INFO("Report saved to: {}", reportFile);
}

void PrintUsage(const char* progName)
{
    cout << "Usage: " << progName << " [options]\n";
    cout << "Options:\n";
    cout << "  -h <host>                 Server IP address (default: 127.0.0.1)\n";
    cout << "  -p <port>                 Server port (default: 8888)\n";
    cout << "  -c <clients>              Number of concurrent clients (default: 100)\n";
    cout << "  -d <duration>             Test duration in seconds (default: 60)\n";
    cout << "  -i <interval>             Request interval in microseconds (default: 1000)\n";
    cout << "  -t <timeout>              Read timeout in milliseconds (default: 1000)\n";
    cout << "  -o <file>                 Output report file (default: report.md)\n";
    cout << "  --payload <fmt>           Payload format: json|protobuf (default: json)\n";
    cout << "  --compatible-json <0|1>   Decode json as fallback in protobuf mode (default: 1)\n";
    cout << "  --workload <mode>         heartbeat|full (default: full)\n";
    cout << "  --help                    Show this help message\n";
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    InitWinsock();
#endif

    Log::init("benchmark", "logs/benchmark.log", spdlog::level::info);

    string serverIp = "127.0.0.1";
    int port = 8888;
    int numClients = 100;
    int durationSec = 60;
    int intervalUs = 1000;
    int readTimeoutMs = 1000;
    string reportFile = "report.md";
    string payloadFormatStr = "json";

    for (int i = 1; i < argc; ++i)
    {
        const string arg = argv[i];
        if (arg == "-h" && i + 1 < argc)
        {
            serverIp = argv[++i];
        }
        else if (arg == "-p" && i + 1 < argc)
        {
            port = atoi(argv[++i]);
        }
        else if (arg == "-c" && i + 1 < argc)
        {
            numClients = atoi(argv[++i]);
        }
        else if (arg == "-d" && i + 1 < argc)
        {
            durationSec = atoi(argv[++i]);
        }
        else if (arg == "-i" && i + 1 < argc)
        {
            intervalUs = atoi(argv[++i]);
        }
        else if (arg == "-t" && i + 1 < argc)
        {
            readTimeoutMs = atoi(argv[++i]);
        }
        else if (arg == "-o" && i + 1 < argc)
        {
            reportFile = argv[++i];
        }
        else if (arg == "--payload" && i + 1 < argc)
        {
            payloadFormatStr = argv[++i];
        }
        else if (arg == "--compatible-json" && i + 1 < argc)
        {
            g_compatibleJson = atoi(argv[++i]) != 0;
        }
        else if (arg == "--workload" && i + 1 < argc)
        {
            g_workload = argv[++i];
        }
        else if (arg == "--help")
        {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    if (g_workload != "heartbeat" && g_workload != "full")
    {
        LOG_ERROR("Invalid workload mode: {} (expected heartbeat|full)", g_workload);
        return 1;
    }

    g_payloadFormat = ProtoCodec::parsePayloadFormat(payloadFormatStr);
    if (g_payloadFormat == protocol::PayloadFormat::Protobuf && !ProtoCodec::protobufAvailable())
    {
        LOG_WARN("Protobuf not available, fallback to JSON payload");
        g_payloadFormat = protocol::PayloadFormat::Json;
    }

    LOG_INFO("========================================");
    LOG_INFO("Starting benchmark: {} clients, {} seconds", numClients, durationSec);
    LOG_INFO("Server: {}:{}", serverIp, port);
    LOG_INFO("Read timeout: {} ms", readTimeoutMs);
    LOG_INFO("Payload format: {}", ProtoCodec::toString(g_payloadFormat));
    LOG_INFO("Compatible JSON fallback: {}", g_compatibleJson ? "on" : "off");
    LOG_INFO("Workload: {}", g_workload);
    LOG_INFO("========================================");

    vector<thread> threads;
    threads.reserve(static_cast<size_t>(numClients));

    for (int i = 0; i < numClients; ++i)
    {
        threads.emplace_back(ClientThread,
                             i,
                             numClients,
                             serverIp,
                             port,
                             durationSec,
                             intervalUs,
                             readTimeoutMs);
    }

    const auto warmupWaitDeadline = steady_clock::now() + seconds(kWarmupDeadlineSec + 5);
    while (steady_clock::now() < warmupWaitDeadline)
    {
        if (g_warmupReadyClients.load() + g_warmupFailedClients.load() >= numClients)
        {
            break;
        }
        this_thread::sleep_for(milliseconds(10));
    }

    if (g_warmupReadyClients.load() != numClients)
    {
        g_running = false;
        for (thread& t : threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
#ifdef _WIN32
        CleanupWinsock();
#endif
        LOG_ERROR("Warmup phase failed: ready_clients={}, failed_clients={}, expected_clients={}",
                  g_warmupReadyClients.load(),
                  g_warmupFailedClients.load(),
                  numClients);
        return 1;
    }

    const long long startNs = NowSteadyNs() + static_cast<long long>(kWarmupStartDelayMs) * 1000000LL;
    g_benchmarkStartNs = startNs;
    g_startBenchmark = true;
    thread monitorThread(ProgressMonitor, durationSec, startNs);

    for (thread& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    g_running = false;
    if (monitorThread.joinable())
    {
        monitorThread.join();
    }

    const double elapsed = duration_cast<duration<double>>(steady_clock::now() - SteadyTimePointFromNs(startNs)).count();
    LOG_INFO("========================================");
    LOG_INFO("Benchmark completed");
    LOG_INFO("Total requests: {}", g_totalMessages.load());
    LOG_INFO("Total business rounds: {}", g_totalRounds.load());
    LOG_INFO("Total errors: {}", g_totalErrors.load());
    LOG_INFO("Total timeouts: {}", g_totalTimeouts.load());
    LOG_INFO("Elapsed time: {:.2f} seconds", elapsed);
    LOG_INFO("Request QPS: {:.2f}", elapsed > 0.0 ? g_totalMessages.load() / elapsed : 0.0);
    LOG_INFO("Business Round QPS: {:.2f}", elapsed > 0.0 ? g_totalRounds.load() / elapsed : 0.0);
    LOG_INFO("========================================");

    GenerateReport(reportFile, numClients, durationSec, elapsed, intervalUs, readTimeoutMs);

#ifdef _WIN32
    CleanupWinsock();
#endif
    return 0;
}
