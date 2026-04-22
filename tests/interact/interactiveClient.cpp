#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

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

atomic<bool> g_running(true);
atomic<bool> g_loggedIn(false);
atomic<uint32_t> g_requestId(1);
string g_userId;
string g_sessionId;
mutex g_consoleMutex;
protocol::PayloadFormat g_payloadFormat = protocol::PayloadFormat::Json;
bool g_compatibleJson = true;

#ifdef _WIN32
void InitWinsock()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed" << endl;
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
    if (flags < 0) return false;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool WaitForRead(int sockfd, int timeoutMs)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    const int ret = select(sockfd + 1, &readfds, nullptr, nullptr, &tv);
    return ret > 0;
}

bool WaitForWrite(int sockfd, int timeoutMs)
{
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sockfd, &writefds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    const int ret = select(sockfd + 1, nullptr, &writefds, nullptr, &tv);
    return ret > 0;
}

bool SendData(int sockfd, const char* data, size_t len, int timeoutMs = 5000)
{
    size_t totalSent = 0;
    while (totalSent < len)
    {
        if (!WaitForWrite(sockfd, timeoutMs))
        {
            return false;
        }

#ifdef _WIN32
        const ssize_t n = send(sockfd, data + totalSent, static_cast<int>(len - totalSent), 0);
#else
        const ssize_t n = write(sockfd, data + totalSent, len - totalSent);
#endif

        if (n > 0)
        {
            totalSent += static_cast<size_t>(n);
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

string EncodeBroadcastReq(const protocol::BroadcastMsgReq& req)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Serialize(req);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::BroadcastMessageReq pb;
    pb.set_from_user_id(req.fromUserId);
    pb.set_content(req.content);
    string out;
    if (pb.SerializeToString(&out))
    {
        return out;
    }
#endif
    return serializer::Serialize(req);
}

string EncodeKickReq(const protocol::KickUserReq& req)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Serialize(req);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::KickUserReq pb;
    pb.set_target_user_id(req.targetUserId);
    pb.set_reason(req.reason);
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

bool DecodeBroadcastNotify(const string& payload, protocol::BroadcastMsgNotify& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::BroadcastMessageNotify pb;
    if (pb.ParseFromString(payload))
    {
        out.fromUserId = pb.from_user_id();
        out.content = pb.content();
        out.timestamp = pb.timestamp();
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

bool DecodeKickResp(const string& payload, protocol::KickUserResp& out)
{
    if (g_payloadFormat == protocol::PayloadFormat::Json)
    {
        return serializer::Deserialize(payload, out);
    }
#ifdef HAVE_PROTOBUF
    im::protocol::KickUserResp pb;
    if (pb.ParseFromString(payload))
    {
        out.resultCode = pb.result_code();
        out.resultMsg = pb.result_msg();
        return true;
    }
#endif
    return g_compatibleJson ? serializer::Deserialize(payload, out) : false;
}

void PrintPrompt()
{
    cout << "> " << flush;
}

void HandlePacket(const Codec::DecodedPacket& packet)
{
    LOG_INFO("recv packet cmd=0x{:04x} req={} payload_bytes={}",
             packet.header.command,
             packet.header.requestId,
             packet.payload.size());
    lock_guard<mutex> lock(g_consoleMutex);
    switch (packet.header.command)
    {
    case protocol::CmdLoginResp:
    {
        protocol::LoginResp resp;
        if (!DecodeLoginResp(packet.payload, resp))
        {
            LOG_WARN("decode login response failed, req={}", packet.header.requestId);
            cout << "\n[系统] 登录响应解析失败" << endl;
            break;
        }
        if (resp.resultCode == 0)
        {
            g_loggedIn = true;
            g_sessionId = resp.sessionId;
            LOG_INFO("login success user={} session={}", g_userId, resp.sessionId);
            cout << "\n[系统] 登录成功！SessionId: " << resp.sessionId << endl;
        }
        else
        {
            LOG_WARN("login failed user={} code={} msg={}", g_userId, resp.resultCode, resp.resultMsg);
            cout << "\n[系统] 登录失败: " << resp.resultMsg << endl;
        }
        break;
    }
    case protocol::CmdHeartbeatResp:
    {
        protocol::HeartbeatResp resp;
        if (DecodeHeartbeatResp(packet.payload, resp))
        {
            LOG_INFO("heartbeat response server_time={}", resp.serverTime);
            cout << "\n[心跳] 服务器时间: " << resp.serverTime << endl;
        }
        else
        {
            LOG_WARN("decode heartbeat response failed, req={}", packet.header.requestId);
            cout << "\n[心跳] 响应解析失败" << endl;
        }
        break;
    }
    case protocol::CmdP2pMsgResp:
    {
        protocol::P2PMsgResp resp;
        if (!DecodeP2PResp(packet.payload, resp))
        {
            LOG_WARN("decode p2p response failed, req={}", packet.header.requestId);
            cout << "\n[消息] 点对点响应解析失败" << endl;
            break;
        }
        if (resp.resultCode == 0)
        {
            LOG_INFO("p2p send success msg_id={} seq={}", resp.msgId, resp.serverSeq);
            cout << "\n[消息] 点对点发送成功，MsgId: " << resp.msgId << ", Seq: " << resp.serverSeq << endl;
        }
        else
        {
            LOG_WARN("p2p send failed code={} msg={}", resp.resultCode, resp.resultMsg);
            cout << "\n[消息] 点对点发送失败: " << resp.resultMsg << endl;
        }
        break;
    }
    case protocol::CmdP2pMsgNotify:
    {
        protocol::P2PMsgNotify notify;
        if (!DecodeP2PNotify(packet.payload, notify))
        {
            LOG_WARN("decode p2p notify failed, req={}", packet.header.requestId);
            cout << "\n[消息] 点对点通知解析失败" << endl;
            break;
        }
        LOG_INFO("p2p notify from={} to={} msg_id={} seq={}",
                 notify.fromUserId,
                 notify.toUserId,
                 notify.msgId,
                 notify.serverSeq);
        const time_t ts = static_cast<time_t>(notify.timestamp / 1000);
        char timeStr[64] = {0};
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", localtime(&ts));
        cout << "\n[" << timeStr << "] " << notify.fromUserId << " -> " << notify.toUserId
             << ": " << notify.content << endl;
        break;
    }
    case protocol::CmdBroadcastMsgNotify:
    {
        protocol::BroadcastMsgNotify notify;
        if (!DecodeBroadcastNotify(packet.payload, notify))
        {
            LOG_WARN("decode broadcast notify failed, req={}", packet.header.requestId);
            cout << "\n[广播] 通知解析失败" << endl;
            break;
        }
        LOG_INFO("broadcast notify from={} content_bytes={}", notify.fromUserId, notify.content.size());
        const time_t ts = static_cast<time_t>(notify.timestamp / 1000);
        char timeStr[64] = {0};
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", localtime(&ts));
        cout << "\n[" << timeStr << "] " << notify.fromUserId << ": " << notify.content << endl;
        break;
    }
    case protocol::CmdKickUserResp:
    {
        protocol::KickUserResp resp;
        if (DecodeKickResp(packet.payload, resp))
        {
            LOG_INFO("kick response code={} msg={}", resp.resultCode, resp.resultMsg);
            cout << "\n[系统] 踢人结果: " << resp.resultMsg << " (code=" << resp.resultCode << ")" << endl;
        }
        else
        {
            LOG_WARN("decode kick response failed, req={}", packet.header.requestId);
            cout << "\n[系统] 踢人响应解析失败" << endl;
        }
        break;
    }
    default:
        LOG_WARN("unknown packet cmd=0x{:04x} req={}", packet.header.command, packet.header.requestId);
        cout << "\n[未知消息] command=0x" << hex << packet.header.command << dec
             << ", requestId=" << packet.header.requestId << endl;
        break;
    }

    PrintPrompt();
}

void ReceiveThread(int sockfd)
{
    Buffer buffer;
    Codec codec;
    codec.setPacketCallback([](const shared_ptr<TcpConnection>&, const Codec::DecodedPacket& packet) {
        HandlePacket(packet);
    });

    while (g_running)
    {
        if (!WaitForRead(sockfd, 100))
        {
            continue;
        }

        char readBuf[4096];
#ifdef _WIN32
        const ssize_t bytesRead = recv(sockfd, readBuf, sizeof(readBuf), 0);
#else
        const ssize_t bytesRead = read(sockfd, readBuf, sizeof(readBuf));
#endif
        if (bytesRead <= 0)
        {
            LOG_WARN("socket disconnected while receiving");
            lock_guard<mutex> lock(g_consoleMutex);
            cout << "\n[系统] 连接已断开" << endl;
            g_running = false;
            break;
        }

        buffer.append(readBuf, static_cast<size_t>(bytesRead));
        codec.onMessage(nullptr, &buffer, Timestamp::now());
    }
}

bool SendPacket(int sockfd, uint16_t command, const string& payload)
{
    Buffer buffer;
    Codec::pack(&buffer, command, g_requestId.fetch_add(1), payload);
    return SendData(sockfd, buffer.peek(), buffer.readableBytes());
}

void HeartbeatThread(int sockfd)
{
    while (g_running)
    {
        this_thread::sleep_for(chrono::seconds(10));
        if (!g_running || !g_loggedIn)
        {
            continue;
        }

        const protocol::HeartbeatReq req;
        const string msg = EncodeHeartbeatReq(req);
        if (!SendPacket(sockfd, protocol::CmdHeartbeatReq, msg))
        {
            LOG_WARN("heartbeat send failed");
            lock_guard<mutex> lock(g_consoleMutex);
            cout << "\n[系统] 心跳发送失败" << endl;
        }
    }
}

void PrintHelp()
{
    cout << "\n========== 命令帮助 ==========" << endl;
    cout << "login <userId> <token> [deviceId]  - 登录" << endl;
    cout << "p2p <toUserId> <message>           - 发送点对点消息" << endl;
    cout << "broadcast <message>                - 发送广播消息" << endl;
    cout << "kick <userId> [reason]             - 踢出用户" << endl;
    cout << "help                               - 显示帮助" << endl;
    cout << "quit                               - 退出" << endl;
    cout << "================================\n" << endl;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    InitWinsock();
#endif

    Log::init("client", "logs/client.log", spdlog::level::info);

    string serverIp = "127.0.0.1";
    int port = 8888;
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
        else if (arg == "--payload" && i + 1 < argc)
        {
            payloadFormatStr = argv[++i];
        }
        else if (arg == "--compatible-json" && i + 1 < argc)
        {
            g_compatibleJson = atoi(argv[++i]) != 0;
        }
    }

    g_payloadFormat = ProtoCodec::parsePayloadFormat(payloadFormatStr);
    if (g_payloadFormat == protocol::PayloadFormat::Protobuf && !ProtoCodec::protobufAvailable())
    {
        cout << "[系统] 当前构建未启用 Protobuf，已回退 JSON" << endl;
        g_payloadFormat = protocol::PayloadFormat::Json;
    }

    cout << "========================================" << endl;
    cout << "     IM 交互式客户端" << endl;
    cout << "========================================" << endl;
    cout << "服务器: " << serverIp << ":" << port << endl;
    cout << "Payload: " << ProtoCodec::toString(g_payloadFormat) << endl;
    cout << "Compatible JSON fallback: " << (g_compatibleJson ? "on" : "off") << endl;
    cout << "输入 'help' 查看可用命令" << endl;
    cout << "========================================\n" << endl;
    LOG_INFO("interactive client start target={}:{} payload={} compatible_json={}",
             serverIp,
             port,
             ProtoCodec::toString(g_payloadFormat),
             g_compatibleJson);

#ifdef _WIN32
    const int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    const int sockfd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sockfd < 0)
    {
        cerr << "创建 socket 失败" << endl;
        return -1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    cout << "正在连接服务器..." << endl;
    if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
    {
        LOG_ERROR("connect failed target={}:{}", serverIp, port);
        cerr << "连接服务器失败" << endl;
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        return -1;
    }

    SetNonBlocking(sockfd);
    LOG_INFO("connect success target={}:{}", serverIp, port);
    cout << "已连接到服务器\n" << endl;

    thread recvThread(ReceiveThread, sockfd);
    thread heartbeatThread(HeartbeatThread, sockfd);

    while (g_running)
    {
        {
            lock_guard<mutex> lock(g_consoleMutex);
            PrintPrompt();
        }

        string line;
        if (!getline(cin, line))
        {
            break;
        }
        if (line.empty())
        {
            continue;
        }

        istringstream iss(line);
        string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit")
        {
            g_running = false;
            break;
        }
        if (cmd == "help")
        {
            PrintHelp();
            continue;
        }

        if (cmd == "login")
        {
            string userId;
            string token;
            string deviceId;
            iss >> userId >> token >> deviceId;
            if (userId.empty() || token.empty())
            {
                cout << "用法: login <userId> <token> [deviceId]" << endl;
                continue;
            }
            if (deviceId.empty())
            {
                deviceId = "default";
            }

            g_userId = userId;
            protocol::LoginReq req;
            req.userId = userId;
            req.token = token;
            req.deviceId = deviceId;
            const string msg = EncodeLoginReq(req);
            LOG_INFO("send login user={} device={}", userId, deviceId);

            cout << (SendPacket(sockfd, protocol::CmdLoginReq, msg) ? "登录请求已发送..." : "登录请求发送失败") << endl;
            continue;
        }

        if (cmd == "p2p")
        {
            if (!g_loggedIn)
            {
                cout << "请先登录" << endl;
                continue;
            }
            string toUserId;
            string content;
            iss >> toUserId;
            getline(iss, content);
            if (!content.empty() && content[0] == ' ')
            {
                content = content.substr(1);
            }
            if (toUserId.empty() || content.empty())
            {
                cout << "用法: p2p <toUserId> <message>" << endl;
                continue;
            }

            protocol::P2PMsgReq req;
            req.fromUserId = g_userId;
            req.toUserId = toUserId;
            req.clientMsgId = to_string(g_requestId.load());
            req.content = content;
            const string msg = EncodeP2PReq(req);
            LOG_INFO("send p2p from={} to={} content_bytes={}", req.fromUserId, req.toUserId, req.content.size());
            cout << (SendPacket(sockfd, protocol::CmdP2pMsgReq, msg) ? "点对点消息已发送" : "点对点消息发送失败") << endl;
            continue;
        }

        if (cmd == "broadcast" || cmd == "bc")
        {
            if (!g_loggedIn)
            {
                cout << "请先登录" << endl;
                continue;
            }
            string content;
            getline(iss, content);
            if (!content.empty() && content[0] == ' ')
            {
                content = content.substr(1);
            }
            if (content.empty())
            {
                cout << "用法: broadcast <message>" << endl;
                continue;
            }

            protocol::BroadcastMsgReq req;
            req.fromUserId = g_userId;
            req.content = content;
            const string msg = EncodeBroadcastReq(req);
            LOG_INFO("send broadcast from={} content_bytes={}", req.fromUserId, req.content.size());
            cout << (SendPacket(sockfd, protocol::CmdBroadcastMsgReq, msg) ? "广播消息已发送" : "广播消息发送失败") << endl;
            continue;
        }

        if (cmd == "kick")
        {
            if (!g_loggedIn)
            {
                cout << "请先登录" << endl;
                continue;
            }
            string targetUserId;
            string reason;
            iss >> targetUserId;
            getline(iss, reason);
            if (!reason.empty() && reason[0] == ' ')
            {
                reason = reason.substr(1);
            }
            if (targetUserId.empty())
            {
                cout << "用法: kick <userId> [reason]" << endl;
                continue;
            }
            if (reason.empty())
            {
                reason = "管理员踢出";
            }

            protocol::KickUserReq req;
            req.targetUserId = targetUserId;
            req.reason = reason;
            const string msg = EncodeKickReq(req);
            LOG_INFO("send kick target={} reason={}", req.targetUserId, req.reason);
            cout << (SendPacket(sockfd, protocol::CmdKickUserReq, msg) ? "踢人请求已发送" : "踢人请求发送失败") << endl;
            continue;
        }

        cout << "未知命令: " << cmd << "，输入 'help' 查看帮助" << endl;
    }

    g_running = false;
    if (recvThread.joinable())
    {
        recvThread.join();
    }
    if (heartbeatThread.joinable())
    {
        heartbeatThread.join();
    }

#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif

#ifdef _WIN32
    CleanupWinsock();
#endif
    LOG_INFO("interactive client exit");
    cout << "\n客户端已退出" << endl;
    return 0;
}
